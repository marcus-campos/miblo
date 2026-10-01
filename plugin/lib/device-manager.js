import { trimSnapshot, alertOnlySnapshot } from './snapshot-builder.js';
import { LEGACY_MAX_SESSIONS, LEGACY_SNAPSHOT_MAX_BYTES } from './constants.js';

export class DeviceManager {
  #health = new Map();

  constructor({ client, store, discover = null, now = () => Date.now() }) {
    this.client = client;
    this.store = store;
    this.discover = discover;
    this.now = now;
  }

  async pushAll(snapshot) {
    await Promise.all(this.store.list().map((d) => this.#pushOne(d, snapshot)));
  }

  status() {
    return this.store.list().map((d) => {
      const h = this.#health.get(d.id) ?? {};
      return { id: d.id, name: d.name, addr: d.addr, online: !!h.online, unauthorized: !!h.unauthorized, lastOk: h.lastOk ?? null };
    });
  }

  #h(id) {
    if (!this.#health.has(id)) this.#health.set(id, { fails: 0, nextTry: 0, online: false, unauthorized: false, lastOk: null, caps: null, capsAt: 0 });
    return this.#health.get(id);
  }

  // A gadget's own caps (from /api/info), cached and refreshed hourly. Unknown until first read:
  // the legacy 8 sessions / 3072 bytes, which every firmware can accept.
  async #caps(dev, h) {
    if (h.caps && this.now() - h.capsAt < 3600_000) return h.caps;
    try {
      const info = await this.client.info(dev.addr, dev.token);
      const n = Number(info?.maxSessions);
      const b = Number(info?.maxBytes);
      h.caps = { maxSessions: Number.isInteger(n) && n > 0 ? n : LEGACY_MAX_SESSIONS,
                 maxBytes: Number.isInteger(b) && b > 0 ? b : LEGACY_SNAPSHOT_MAX_BYTES };
      h.capsAt = this.now();
    } catch {
      h.caps = h.caps ?? { maxSessions: LEGACY_MAX_SESSIONS, maxBytes: LEGACY_SNAPSHOT_MAX_BYTES };
    }
    return h.caps;
  }

  async #pushOne(dev, snapshot) {
    const h = this.#h(dev.id);
    if (this.now() < h.nextTry) return;
    try {
      const caps = await this.#caps(dev, h);
      try {
        await this.client.pushState(dev.addr, dev.token, trimSnapshot(snapshot, caps.maxSessions, caps.maxBytes));
      } catch (e) {
        // Low on memory (503): the full snapshot was refused. Resend just the alerts, which is
        // tiny and gets through, so a session that needs the user is never lost to low memory.
        const alertOnly = e?.status === 503 ? alertOnlySnapshot(snapshot) : null;
        if (!alertOnly) throw e;
        await this.client.pushState(dev.addr, dev.token, alertOnly);
      }
      Object.assign(h, { fails: 0, nextTry: 0, online: true, unauthorized: false, lastOk: this.now() });
    } catch (e) {
      h.fails += 1;
      h.online = false;
      h.unauthorized = e?.status === 401;
      h.nextTry = this.now() + Math.min(60_000, 1000 * 2 ** (h.fails - 1));
      if (h.fails >= 3 && this.discover) await this.#relocate(dev, h);
    }
  }

  async #relocate(dev, h) {
    try {
      const hit = (await this.discover()).find((f) => f.id === dev.id);
      if (hit && hit.addr !== dev.addr) {
        this.store.update(dev.id, { addr: hit.addr });
        h.nextTry = 0;
      }
    } catch {
      // discovery failed: keep the backoff
    }
  }
}

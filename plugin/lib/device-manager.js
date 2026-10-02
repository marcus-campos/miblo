import { trimSnapshot, alertOnlySnapshot } from './snapshot-builder.js';
import { LEGACY_MAX_SESSIONS, LEGACY_SNAPSHOT_MAX_BYTES } from './constants.js';
import { ZoneOffsets } from './tz-offsets.js';
import { verifyNewAddr, isLanAddr } from './relocation.js';

export class DeviceManager {
  #health = new Map();

  // `zones` (tz-offsets.js ZoneOffsets): the live offsets of each gadget's time zones.
  // `addrOk`: which addresses a gadget may move to (relocation.js isLanAddr; tests relax it).
  constructor({ client, store, discover = null, now = () => Date.now(), zones = new ZoneOffsets(), addrOk = isLanAddr }) {
    this.client = client;
    this.store = store;
    this.discover = discover;
    this.now = now;
    this.zones = zones;
    this.addrOk = addrOk;
  }

  async pushAll(snapshot) {
    await Promise.all(this.store.list().map((d) => this.#pushOne(d, snapshot)));
  }

  status() {
    return this.store.list().map((d) => {
      const h = this.#health.get(d.id) ?? {};
      // needsPair: it answered at a new address that could not prove it is this gadget: the user
      // must run /miblo:pair again (nothing was sent there).
      return { id: d.id, name: d.name, addr: d.addr, online: !!h.online, unauthorized: !!h.unauthorized, needsPair: !!h.needsPair, lastOk: h.lastOk ?? null };
    });
  }

  #h(id) {
    if (!this.#health.has(id)) this.#health.set(id, { fails: 0, nextTry: 0, online: false, unauthorized: false, needsPair: false, lastOk: null, caps: null, capsAt: 0 });
    return this.#health.get(id);
  }

  // A gadget's own caps (from /api/info), cached and refreshed hourly. Unknown until first read:
  // the legacy 8 sessions / 3072 bytes, which every firmware can accept. `zones`: its time zone and
  // second clock (tz, tz2), for the live offsets; none from a firmware that does not report them.
  async #caps(dev, h) {
    if (h.caps && this.now() - h.capsAt < 3600_000) return h.caps;
    try {
      const info = await this.client.info(dev.addr, dev.token);
      const n = Number(info?.maxSessions);
      const b = Number(info?.maxBytes);
      h.caps = { maxSessions: Number.isInteger(n) && n > 0 ? n : LEGACY_MAX_SESSIONS,
                 maxBytes: Number.isInteger(b) && b > 0 ? b : LEGACY_SNAPSHOT_MAX_BYTES,
                 zones: [info?.tz, info?.tz2].filter((z) => typeof z === 'string' && z) };
      h.capsAt = this.now();
    } catch {
      h.caps = h.caps ?? { maxSessions: LEGACY_MAX_SESSIONS, maxBytes: LEGACY_SNAPSHOT_MAX_BYTES, zones: [] };
    }
    return h.caps;
  }

  async #pushOne(dev, snapshot) {
    const h = this.#h(dev.id);
    if (this.now() < h.nextTry) return;
    try {
      const caps = await this.#caps(dev, h);
      try {
        // The live offsets go in before trimming: they count toward the gadget's byte cap.
        let own = snapshot;
        try {
          own = this.zones.withZones(snapshot, caps.zones, this.now());
        } catch {
          // never let the time zones cost the push: the gadget falls back to its own table
        }
        await this.client.pushState(dev.addr, dev.token, trimSnapshot(own, caps.maxSessions, caps.maxBytes));
      } catch (e) {
        // Low on memory (503): the full snapshot was refused. Resend just the alerts, which is
        // tiny and gets through, so a session that needs the user is never lost to low memory.
        const alertOnly = e?.status === 503 ? alertOnlySnapshot(snapshot) : null;
        if (!alertOnly) throw e;
        await this.client.pushState(dev.addr, dev.token, alertOnly);
      }
      Object.assign(h, { fails: 0, nextTry: 0, online: true, unauthorized: false, needsPair: false, lastOk: this.now() });
    } catch (e) {
      h.fails += 1;
      h.online = false;
      h.unauthorized = e?.status === 401;
      h.nextTry = this.now() + Math.min(60_000, 1000 * 2 ** (h.fails - 1));
      if (h.fails >= 3 && this.discover) await this.#relocate(dev, h);
    }
  }

  // The token goes to a new address only once that address proved it holds it (relocation.js).
  async #relocate(dev, h) {
    try {
      const hit = (await this.discover()).find((f) => f.id === dev.id);
      if (!hit || hit.addr === dev.addr) return;
      const verdict = await verifyNewAddr({ client: this.client, dev, addr: hit.addr, addrOk: this.addrOk });
      if (verdict === 'ok') {
        this.store.update(dev.id, { addr: hit.addr });
        h.caps = null;  // another address may be another firmware
        h.needsPair = false;
        h.nextTry = 0;
      } else if (verdict === 'refused') {
        h.needsPair = true;
      }
    } catch {
      // discovery failed: keep the backoff
    }
  }
}

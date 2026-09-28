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
    if (!this.#health.has(id)) this.#health.set(id, { fails: 0, nextTry: 0, online: false, unauthorized: false, lastOk: null });
    return this.#health.get(id);
  }

  async #pushOne(dev, snapshot) {
    const h = this.#h(dev.id);
    if (this.now() < h.nextTry) return;
    try {
      await this.client.pushState(dev.addr, dev.token, snapshot);
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
      // descoberta falhou: mantém o backoff
    }
  }
}

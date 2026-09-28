export class DeviceClient {
  constructor({ fetchImpl = globalThis.fetch, timeoutMs = 2500 } = {}) {
    this.fetch = fetchImpl;
    this.timeoutMs = timeoutMs;
  }

  async #req(addr, path, { method = 'GET', token, body } = {}) {
    const headers = {};
    if (body !== undefined) headers['content-type'] = 'application/json';
    if (token) headers.authorization = `Bearer ${token}`;
    const res = await this.fetch(`http://${addr}${path}`, {
      method,
      headers,
      body: body === undefined ? undefined : JSON.stringify(body),
      signal: AbortSignal.timeout(this.timeoutMs),
    });
    const text = await res.text();
    let data = null;
    try { data = text ? JSON.parse(text) : null; } catch { data = null; }
    if (!res.ok) {
      const err = new Error(`device ${path} -> HTTP ${res.status}`);
      err.status = res.status;
      throw err;
    }
    return data;
  }

  info(addr) {
    return this.#req(addr, '/api/info');
  }

  async pair(addr, code, host) {
    const data = await this.#req(addr, '/api/pair', { method: 'POST', body: { code: String(code), host } });
    if (!data?.token) throw new Error('device did not return a token');
    return data.token;
  }

  pushState(addr, token, snapshot) {
    return this.#req(addr, '/api/state', { method: 'POST', token, body: snapshot });
  }

  setConfig(addr, token, cfg) {
    return this.#req(addr, '/api/config', { method: 'POST', token, body: cfg });
  }

  reset(addr, token) {
    return this.#req(addr, '/api/reset', { method: 'POST', token, body: {} });
  }
}

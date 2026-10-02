// The reduced /api/info of a paired gadget asked without one of its tokens: {id, paired, proto}
// and nothing else (no name, fw, board or settings).
export const isReducedInfo = (info) => info?.paired === true && info.fw === undefined && info.name === undefined;

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
      err.data = data;
      throw err;
    }
    return data;
  }

  // A paired gadget answers someone without one of its tokens with only {id, paired, proto};
  // with the token (or before pairing) it reports everything (name, fw, board, settings...).
  info(addr, token) {
    return this.#req(addr, '/api/info', { token });
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

  // Pet mode right away for `minutes` (0 stops it): firmware 1.4.0+.
  demo(addr, token, minutes) {
    return this.#req(addr, '/api/demo', { method: 'POST', token, body: { minutes } });
  }

  reset(addr, token) {
    return this.#req(addr, '/api/reset', { method: 'POST', token, body: {} });
  }

  // ---- Daily life (firmware 1.11.0+; an older one answers 404) ----
  // Each takes the JSON body the firmware's handler validates (src/api.cpp dailyRoute).
  focus(addr, token, body) { return this.#req(addr, '/api/focus', { method: 'POST', token, body }); }
  meeting(addr, token, body) { return this.#req(addr, '/api/meeting', { method: 'POST', token, body }); }
  say(addr, token, body) { return this.#req(addr, '/api/say', { method: 'POST', token, body }); }
  remind(addr, token, body) { return this.#req(addr, '/api/remind', { method: 'POST', token, body }); }
  reminders(addr, token) { return this.#req(addr, '/api/remind', { token }); }
  timer(addr, token, body) { return this.#req(addr, '/api/timer', { method: 'POST', token, body }); }
  countdown(addr, token, body) { return this.#req(addr, '/api/countdown', { method: 'POST', token, body }); }
  find(addr, token) { return this.#req(addr, '/api/find', { method: 'POST', token, body: {} }); }
}

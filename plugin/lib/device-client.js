// The reduced /api/info of a paired gadget asked without one of its tokens: {id, paired, proto}
// and nothing else (no name, fw, board or settings).
export const isReducedInfo = (info) => info?.paired === true && info.fw === undefined && info.name === undefined;

// 503 {"error":"busy"}: the gadget was momentarily low on memory (heapLowForRequest) and refused
// the request before doing anything with it, or (for a pairing) could not save it and took it
// back, keeping the code valid. Usually gone a moment later.
export const isBusy = (e) => e?.status === 503;
export const busyLine = (label) => `${label} is busy right now — try again in a moment.`;

// Waits before the 2nd and 3rd attempt of a request the gadget refused as busy.
export const BUSY_RETRY_MS = [400, 800];

const defaultSleep = (ms) => new Promise((r) => setTimeout(r, ms));

export class DeviceClient {
  constructor({ fetchImpl = globalThis.fetch, timeoutMs = 2500, busyRetryMs = BUSY_RETRY_MS, sleep = defaultSleep } = {}) {
    this.fetch = fetchImpl;
    this.timeoutMs = timeoutMs;
    this.busyRetryMs = busyRetryMs;
    this.sleep = sleep;
  }

  // Retries only a 503 (busy), which the firmware sends with nothing changed, so resending is
  // always safe; never a timeout or a dropped connection, whose request may have been carried out.
  // Used for reads and for pairing; other writes leave a 503 to the caller.
  async #retryBusy(addr, path, opts) {
    for (let i = 0; ; i++) {
      try {
        return await this.#req(addr, path, opts);
      } catch (e) {
        if (!isBusy(e) || i >= this.busyRetryMs.length) throw e;
        await this.sleep(this.busyRetryMs[i]);
      }
    }
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
    return this.#retryBusy(addr, '/api/info', { token });
  }

  // A busy (503) pair is resent: the firmware refused it before checking the code. A pair that got
  // no answer is not: it may have succeeded, and the code rotates on success, so a resend would
  // only come back "bad code" and count as a wrong guess towards the lockout.
  async pair(addr, code, host) {
    const data = await this.#retryBusy(addr, '/api/pair', { method: 'POST', body: { code: String(code), host } });
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
  reminders(addr, token) { return this.#retryBusy(addr, '/api/remind', { token }); }
  timer(addr, token, body) { return this.#req(addr, '/api/timer', { method: 'POST', token, body }); }
  countdown(addr, token, body) { return this.#req(addr, '/api/countdown', { method: 'POST', token, body }); }
  find(addr, token) { return this.#req(addr, '/api/find', { method: 'POST', token, body: {} }); }
}

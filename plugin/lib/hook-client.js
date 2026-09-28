// Pure delivery logic of bin/hook.js, with all I/O injected so it can be tested.

const TOP_FIELDS = ['session_id', 'hook_event_name', 'cwd', 'tool_name', 'notification_type', 'agent_id', 'agent_type', 'source', 'trigger'];
// Only what describeTool reads; everything else (file contents, diffs...) stays local.
const TOOL_FIELDS = ['command', 'file_path', 'notebook_path', 'pattern', 'url', 'query', 'description'];
const TOOL_FIELD_MAX = 200;
// Stop/SubagentStop list the in-flight background work; only its shape is needed.
const TASK_FIELDS = ['type', 'status'];
const TASKS_MAX = 64;
const PID_EVENTS = new Set(['SessionStart', 'UserPromptSubmit']);

export const POLL_EVERY_MS = 100;
export const POLL_MAX_MS = 1500;
const DOWN_MAX_MS = 500;

export function pickEvent(raw) {
  const evt = raw && typeof raw === 'object' ? raw : {};
  const out = {};
  for (const k of TOP_FIELDS) if (typeof evt[k] === 'string') out[k] = evt[k];
  if (evt.tool_input && typeof evt.tool_input === 'object') {
    const ti = {};
    for (const k of TOOL_FIELDS) {
      if (typeof evt.tool_input[k] === 'string') ti[k] = evt.tool_input[k].slice(0, TOOL_FIELD_MAX);
    }
    out.tool_input = ti;
  }
  if (Array.isArray(evt.background_tasks)) {
    out.background_tasks = evt.background_tasks.slice(0, TASKS_MAX).map((t) => {
      const o = {};
      for (const k of TASK_FIELDS) if (typeof t?.[k] === 'string') o[k] = t[k].slice(0, TOOL_FIELD_MAX);
      return o;
    });
  }
  return out;
}

export const wantsPid = (evt) => PID_EVENTS.has(evt?.hook_event_name);

// fetch rejects with a TypeError when nothing listens on the port; HTTP error
// statuses and timeouts are not connection errors.
export const isConnError = (e) =>
  e instanceof TypeError || e?.code === 'ECONNREFUSED' || e?.cause?.code === 'ECONNREFUSED';

async function poll(check, { sleep, maxMs, everyMs = POLL_EVERY_MS }) {
  for (let waited = 0; waited < maxMs; waited += everyMs) {
    await sleep(everyMs);
    if (await check()) return true;
  }
  return false;
}

/**
 * io: { post(body), health() -> object|null, shutdown(), startBridge(), sleep(ms) }
 * health() resolves null when nothing answers, and an object otherwise.
 */
export async function deliver(body, io, { allowSpawn = true, checkVersion = false, version = '' } = {}) {
  const ours = (h) => h?.app === 'miblo-bridge';
  const spawnAndPost = async () => {
    io.startBridge();
    const up = await poll(async () => ours(await io.health()), { sleep: io.sleep, maxMs: POLL_MAX_MS });
    if (!up) return 'timeout';
    await io.post(body).catch(() => {});
    return 'spawned';
  };

  if (allowSpawn && checkVersion && version) {
    const h = await io.health();
    if (ours(h) && h.version !== version) {
      await io.shutdown().catch(() => {});
      await poll(async () => (await io.health()) === null, { sleep: io.sleep, maxMs: DOWN_MAX_MS });
      return spawnAndPost();
    }
  }

  try {
    await io.post(body);
    return 'sent';
  } catch (e) {
    if (!allowSpawn || !isConnError(e)) return 'dropped';
    const h = await io.health();
    if (h && !ours(h)) return 'foreign';
    if (ours(h)) {
      await io.post(body).catch(() => {});
      return 'sent';
    }
    return spawnAndPost();
  }
}

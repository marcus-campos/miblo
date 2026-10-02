// Pure delivery logic of bin/hook.js, with all I/O injected so it can be tested.

const TOP_FIELDS = ['session_id', 'hook_event_name', 'cwd', 'tool_name', 'notification_type', 'agent_id', 'agent_type', 'source', 'trigger'];
// Only what describeTool reads; everything else (file contents, diffs...) stays local.
const TOOL_FIELDS = ['command', 'file_path', 'notebook_path', 'pattern', 'url', 'query', 'description'];
const TOOL_FIELD_MAX = 200;
// Stop/SubagentStop list the in-flight background work; only its shape is needed.
const TASK_FIELDS = ['type', 'status'];
const TASKS_MAX = 64;
const PID_EVENTS = new Set(['SessionStart', 'UserPromptSubmit']);
// A permission Notification has no tool_name, only a message ("Claude needs your permission to use
// Bash", "researcher needs permission for Bash"). Only a tool name read from it is forwarded, never
// the text itself.
const PERM_TOOL = {
  permission_prompt: /\bpermission to use ([A-Za-z][\w-]{0,63})$/,
  worker_permission_prompt: /\bneeds permission for ([A-Za-z][\w-]{0,63})$/,
};

export const POLL_EVERY_MS = 100;
export const POLL_MAX_MS = 1500;
const DOWN_MAX_MS = 500;

export function pickEvent(raw) {
  const evt = raw && typeof raw === 'object' ? raw : {};
  const out = {};
  for (const k of TOP_FIELDS) if (typeof evt[k] === 'string') out[k] = evt[k];
  const toolIn = out.hook_event_name === 'Notification' && Object.hasOwn(PERM_TOOL, out.notification_type)
    ? PERM_TOOL[out.notification_type] : null;
  if (toolIn && !out.tool_name && typeof evt.message === 'string') {
    const m = toolIn.exec(evt.message.trim());
    if (m) out.tool_name = m[1];
  }
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
 * io: { post(body) -> reply object, health() -> object|null, shutdown(), startBridge(), sleep(ms) }
 * health() resolves null when nothing answers, and an object otherwise.
 * A bridge of another version (after /reload-plugins the hooks are new, the running bridge is not;
 * a bridge from before replies carried a version counts too) is shut down and replaced by this
 * plugin's: checked on SessionStart before posting, and on every event from the reply.
 */
export async function deliver(body, io, { allowSpawn = true, checkVersion = false, version = '' } = {}) {
  const ours = (h) => h?.app === 'miblo-bridge';
  const replace = async () => {
    await io.shutdown().catch(() => {});
    await poll(async () => (await io.health()) === null, { sleep: io.sleep, maxMs: DOWN_MAX_MS });
    return spawnAndPost();
  };
  const spawnAndPost = async () => {
    io.startBridge();
    const up = await poll(async () => ours(await io.health()), { sleep: io.sleep, maxMs: POLL_MAX_MS });
    if (!up) return 'timeout';
    await io.post(body).catch(() => {});
    return 'spawned';
  };

  if (allowSpawn && checkVersion && version) {
    const h = await io.health();
    if (ours(h) && h.version !== version) return replace();
  }

  let reply;
  try {
    reply = await io.post(body);
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
  if (allowSpawn && version && reply?.version !== version) {
    // Confirm on /health before asking anything to shut down: the port could be another app's.
    const h = await io.health();
    if (ours(h) && h.version !== version) return replace();
  }
  return 'sent';
}

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

const NOTE_TYPE = /^[A-Za-z0-9_.:-]{1,64}$/;

export const POLL_EVERY_MS = 100;
export const POLL_MAX_MS = 1500;
const DOWN_MAX_MS = 500;
// A proven bridge that gave no challenge (its challenges-per-second cap): asked again after about
// this long (jittered), at most THROTTLE_TRIES more times.
export const THROTTLE_WAIT_MS = 150;
const THROTTLE_TRIES = 2;

export function pickEvent(raw) {
  const evt = raw && typeof raw === 'object' ? raw : {};
  const out = {};
  for (const k of TOP_FIELDS) if (typeof evt[k] === 'string') out[k] = evt[k];
  // Any notification type is forwarded (the bridge reads unknown ones by name), but only as a
  // short identifier, never free text.
  if (out.notification_type !== undefined && !NOTE_TYPE.test(out.notification_type)) delete out.notification_type;
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
 * io: { post(body) -> reply object, health() -> object|null, shutdown(signed), startBridge(),
 *       foreign(), recentConflict() -> bool, sleep(ms) }
 * health() resolves null when nothing answers, and otherwise an object with `proven`: whether the
 * answer proved knowledge of the bridge key (bridge-auth.js). Nothing is posted to a listener that
 * did not prove it: on a shared computer another user may hold the port.
 * A bridge of another version (after /reload-plugins the hooks are new, the running bridge is not)
 * is shut down and replaced by this plugin's. So is one that says it is the bridge but cannot
 * prove it (an older bridge, before the key), asked unsigned, once: if it refuses (a bridge of
 * another config dir, or another program) or stays, it is foreign, and foreign() remembers that
 * for a few minutes (recentConflict) so later events skip it at once instead of retrying.
 */
export async function deliver(body, io, { allowSpawn = true, version = '' } = {}) {
  const ours = (h) => h?.app === 'miblo-bridge' && h.proven === true;
  const claims = (h) => h?.app === 'miblo-bridge' && h.proven !== true;
  const post = async () => {
    try {
      await io.post(body);
      return true;
    } catch {
      return false;
    }
  };
  const spawnAndPost = async () => {
    io.startBridge();
    const up = await poll(async () => ours(await io.health()), { sleep: io.sleep, maxMs: POLL_MAX_MS });
    if (!up) return 'timeout';
    return (await post()) ? 'spawned' : 'dropped';
  };
  const waitDownAndSpawn = async () => {
    await poll(async () => (await io.health()) === null, { sleep: io.sleep, maxMs: DOWN_MAX_MS });
    return spawnAndPost();
  };
  const foreign = () => {
    io.foreign();
    return 'foreign';
  };

  let h = await io.health();
  // A bridge is spawned at most once per delivery, and only when nothing answers this first probe.
  if (h === null) return allowSpawn ? spawnAndPost() : 'dropped';
  // Proven but throttled (no challenge to answer this second): ask again shortly rather than
  // lose the event. Throttled is never foreign; whatever answers then (nothing, or something
  // else) gets no spawn and no shutdown from this delivery: only a challenge to answer counts.
  if (ours(h) && !h.challenge) {
    for (let i = 0; i < THROTTLE_TRIES; i++) {
      await io.sleep(THROTTLE_WAIT_MS + Math.floor(Math.random() * 100));
      h = await io.health();
      if (!ours(h) || h.challenge) break;
    }
    if (!ours(h) || !h.challenge) return 'dropped';
  }
  if (ours(h)) {
    if (allowSpawn && version && h.version !== version) {
      await io.shutdown(true).catch(() => {});
      return waitDownAndSpawn();
    }
    return (await post()) ? 'sent' : 'dropped';
  }
  if (!allowSpawn || !claims(h) || io.recentConflict?.()) return foreign();
  try {
    await io.shutdown(false);
  } catch (e) {
    // Refused (401, 403...): not a bridge that takes an unsigned shutdown. Leave it alone.
    if (e?.status) return foreign();
  }
  const r = await waitDownAndSpawn();
  return r === 'timeout' ? foreign() : r;
}

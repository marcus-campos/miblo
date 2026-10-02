// The daily-life commands of bin/miblo.js: focus, meeting, find, timer, say, remind, countdown
// (they talk to every paired gadget, or only `--id <id>`) and today/limits (they read the bridge).
// Every argument is validated here exactly as the firmware validates it, before any request.
import { cleanId, cleanName } from './mdns.js';

const ok = (out) => ({ code: 0, out: out + '\n' });
const fail = (code, out) => ({ code, out: out + '\n' });

// ---- Limits shared with the firmware (miblo_focus.h, miblo_meeting.h, miblo_desknotes.h) ----
export const FOCUS = { min: 5, max: 120, default: 25 };
export const BREAK = { min: 1, max: 60, default: 5 };
export const ROUNDS = { min: 1, max: 12, default: 4 };
export const MEETING = { min: 1, max: 480, default: 60 };
export const TIMER = { min: 1, max: 180 };
export const SAY_MIN = { min: 1, max: 480, default: 30 };
// Message/reminder texts: at most 40 characters and 47 UTF-8 bytes (char[48] on the gadget);
// countdown labels: 20 characters and 40 bytes.
export const NOTE = { chars: 40, bytes: 47 };
export const LABEL = { chars: 20, bytes: 40 };

// The gadget's default break for a focus length (miblo_focus.cpp defaultBreakFor).
export const defaultBreakFor = (focusMin) => Math.max(1, Math.floor(focusMin / 5));

const isWhole = (s) => /^\d{1,6}$/.test(String(s));
const inRange = (v, r) => Number.isInteger(v) && v >= r.min && v <= r.max;
const quoteArg = (a) => String(a).replace(/[\p{Cc}]/gu, '').slice(0, 40);

// Strings that came from the gadget or the bridge, printed to the terminal: no control or bidi
// characters, bounded length.
export const printable = (s, max = 48) =>
  [...String(s ?? '').replace(/[\p{Cc}‎‏‪-‮⁦-⁩]/gu, ' ').replace(/\s+/g, ' ').trim()]
    .slice(0, max).join('');

// Words typed by the user -> one text for the gadget, or { error }. Whitespace (spaces, tabs,
// line breaks) collapses to one space; any other control character is refused, as the gadget
// refuses it; the length is checked in characters and in UTF-8 bytes.
export function typedText(words, limits, what) {
  const text = words.join(' ').replace(/\s+/g, ' ').trim();
  if (!text) return { error: `The ${what} is empty.` };
  if (!text.isWellFormed() || /[\p{Cc}]/u.test(text)) return { error: `The ${what} has a character the gadget cannot show: type it again without it.` };
  const chars = [...text].length;
  if (chars > limits.chars) return { error: `The ${what} is too long: ${chars} characters, the maximum is ${limits.chars}.` };
  if (Buffer.byteLength(text, 'utf8') > limits.bytes) {
    return { error: `The ${what} is too long for the gadget (${limits.bytes} bytes at most): use fewer emoji, accented or CJK characters.` };
  }
  return { text };
}

// `--id <id>` anywhere in the arguments -> { id, rest } or { error }.
export function takeId(args) {
  const rest = [];
  let id;
  for (let i = 0; i < args.length; i++) {
    if (args[i] !== '--id') { rest.push(args[i]); continue; }
    if (id !== undefined || i + 1 >= args.length) return { error: 'Give --id once, followed by a gadget id (see /miblo:status).' };
    id = args[++i];
  }
  return { id, rest };
}

const labelOf = (d) => cleanName(d.name) || cleanId(d.id).slice(0, 20);
const joinNames = (names) => names.join(', ');

// -> { targets } or { error } (a result to return as is).
function targetsFor(store, id) {
  const devices = store.list();
  if (!devices.length) return { error: fail(2, 'No paired Miblo gadgets. Run /miblo:pair first.') };
  if (id === undefined) return { targets: devices };
  const d = devices.find((x) => x.id === id);
  return d ? { targets: [d] } : { error: fail(2, `No paired gadget with id ${cleanId(id)}.`) };
}

// What the gadget said no to, by field (the 400 {"field"} of src/api.cpp dailyRoute).
const FIELD_HINTS = {
  focusMin: `the focus length (${FOCUS.min}-${FOCUS.max} min)`,
  breakMin: `the break length (${BREAK.min}-${BREAK.max} min)`,
  rounds: `the rounds (${ROUNDS.min}-${ROUNDS.max})`,
  text: `the text (up to ${NOTE.chars} characters, one line)`,
  label: `the label (up to ${LABEL.chars} characters)`,
  min: 'the minutes',
  in: 'the minutes (1-1440)',
  at: 'the time (HH:MM)',
  days: 'the days',
  date: 'the date (it must be today or later)',
  md: 'the date',
};

// One failed request -> one line for the user.
export function problemLine(label, e, what = {}) {
  if (e?.status === 404) return `${label} does not support this yet: update it with /miblo:update.`;
  if (e?.status === 401) return `${label} no longer knows this computer (run /miblo:pair again).`;
  const field = String(e?.data?.field ?? '').replace(/[^A-Za-z]/g, '').slice(0, 20);
  if (e?.status === 409) {
    if (field === 'clock') return `${label} has not got the time yet (it needs Wi-Fi with internet or a running bridge); try again in a minute.`;
    if (field === 'full') {
      return what.recurring
        ? `${label} already has 4 recurring reminders: delete one with /miblo:remind off N.`
        : `${label} already has 4 reminders: delete one with /miblo:remind off N.`;
    }
    if (field === 'none') return `${label} has no reminder on the screen.`;
    return `${label} refused it (${field || 'conflict'}).`;
  }
  if (e?.status === 400) {
    if (field === 'delete') return `${label} has no reminder ${what.n ?? ''}`.trim() + '.';
    return FIELD_HINTS[field] ? `${label} rejected ${FIELD_HINTS[field]}.` : `${label} rejected the request${field ? ` (${field})` : ''}.`;
  }
  return `${label} is offline.`;
}

// Sends `call(d)` to every target at once -> [{ d, label, reply } | { d, label, err }] in order.
const each = (targets, call) =>
  Promise.all(targets.map(async (d) => {
    const label = labelOf(d);
    try {
      return { d, label, reply: (await call(d)) ?? {} };
    } catch (err) {
      return { d, label, err };
    }
  }));

// The usual answer: "<what> on A, B." for the gadgets that took it, then one line per problem.
// Exit code 1 only when none took it.
function summarize(results, what, ctx) {
  const done = results.filter((r) => !r.err).map((r) => r.label);
  const lines = done.length ? [what(joinNames(done))] : [];
  for (const r of results) if (r.err) lines.push(problemLine(r.label, r.err, ctx));
  return done.length ? ok(lines.join('\n')) : fail(1, lines.join('\n'));
}

// ---- focus ----
const FOCUS_USAGE = 'Usage: focus [focus-min [break-min [rounds]]] | focus stop | focus status  [--id <id>]';

// -> { body } (start), { stop: true }, { status: true } or { error }.
export function parseFocusArgs(args) {
  if (args.length === 1 && (args[0] === 'stop' || args[0] === 'off')) return { stop: true };
  if (args.length === 1 && args[0] === 'status') return { status: true };
  if (args.length > 3) return { error: `Too many arguments.\n${FOCUS_USAGE}` };
  const bad = args.find((a) => !isWhole(a));
  if (bad !== undefined) return { error: `Unexpected argument "${quoteArg(bad)}".\n${FOCUS_USAGE}` };
  const [focusMin, breakMin, rounds] = args.map(Number);
  const checks = [[focusMin, FOCUS, 'Focus'], [breakMin, BREAK, 'Break'], [rounds, ROUNDS, 'Rounds']];
  for (const [v, r, name] of checks) {
    if (v !== undefined && !inRange(v, r)) {
      return { error: `${name} must be a whole number from ${r.min} to ${r.max}${name === 'Rounds' ? '' : ' minutes'} (got ${v}).` };
    }
  }
  if (!args.length) return { body: { focusMin: FOCUS.default, breakMin: BREAK.default, rounds: ROUNDS.default }, empty: true };
  // Only what was given: the gadget derives the rest (the break from the focus length).
  const body = { focusMin };
  if (breakMin !== undefined) body.breakMin = breakMin;
  if (rounds !== undefined) body.rounds = rounds;
  return { body };
}

const minutesLeft = (sec) => Math.max(1, Math.ceil(Number(sec) / 60));

// One gadget's focus from /api/info -> a line, or null when no focus is on.
function focusLine(label, f) {
  const left = `${minutesLeft(f.left)} min left`;
  const round = `${Number(f.round) || 1}/${Number(f.rounds) || 1}`;
  switch (f.phase) {
    case 'focus': return `Focus: round ${round}, ${left} (${label})`;
    case 'break': return `Focus: break after round ${round}, ${left} (${label})`;
    case 'back': return `Focus: back to focus in a moment, round ${round} next (${label})`;
    case 'long': return `Focus: long break, ${left}, then done (${label})`;
    default: return null;
  }
}

// /api/info of each target -> [{ label, line | null, err? }].
async function focusStatuses(targets, client) {
  const results = await each(targets, (d) => client.info(d.addr, d.token));
  return results.map((r) => {
    if (r.err) return { label: r.label, err: r.err };
    const info = r.reply;
    // A gadget that dropped this computer answers only {id, paired, proto}.
    if (info.paired === true && info.fw === undefined && info.name === undefined) return { label: r.label, err: { status: 401 } };
    if (!info.focus || typeof info.focus !== 'object') return { label: r.label, err: { status: 404 } };
    return { label: r.label, line: focusLine(r.label, info.focus) };
  });
}

async function focus(args, { store, client }) {
  const { id, rest, error } = takeId(args);
  if (error) return fail(2, error);
  const parsed = parseFocusArgs(rest);
  if (parsed.error) return fail(2, parsed.error);
  const t = targetsFor(store, id);
  if (t.error) return t.error;
  const { targets } = t;

  // No arguments with a focus already on somewhere: say what's left instead of starting over.
  if (parsed.status || parsed.empty) {
    const st = await focusStatuses(targets, client);
    const on = st.filter((s) => s.line);
    if (parsed.status || on.length) {
      const lines = on.map((s) => s.line);
      const off = st.filter((s) => !s.err && !s.line).map((s) => s.label);
      if (off.length) lines.push(`No focus on ${joinNames(off)}.`);
      for (const s of st) if (s.err) lines.push(problemLine(s.label, s.err));
      return st.some((s) => !s.err) ? ok(lines.join('\n')) : fail(1, lines.join('\n'));
    }
  }

  if (parsed.stop) {
    return summarize(await each(targets, (d) => client.focus(d.addr, d.token, { stop: true })), (n) => `Focus off on ${n}.`);
  }
  const { body } = parsed;
  const breakMin = body.breakMin ?? defaultBreakFor(body.focusMin);
  const rounds = body.rounds ?? ROUNDS.default;
  const results = await each(targets, (d) => client.focus(d.addr, d.token, body));
  return summarize(results, (n) =>
    `Focus on: ${body.focusMin} min, breaks of ${breakMin}, ${rounds} round${rounds === 1 ? '' : 's'} on ${n}.`);
}

// ---- meeting ----
async function meeting(args, { store, client }) {
  const { id, rest, error } = takeId(args);
  if (error) return fail(2, error);
  const usage = 'Usage: meeting [minutes] | meeting off  [--id <id>]';
  let body;
  if (!rest.length) body = {};
  else if (rest.length === 1 && (rest[0] === 'off' || rest[0] === 'stop')) body = { off: true };
  else if (rest.length === 1 && isWhole(rest[0])) {
    const min = Number(rest[0]);
    if (!inRange(min, MEETING)) return fail(2, `Minutes must be a whole number from ${MEETING.min} to ${MEETING.max} (got ${min}).`);
    body = { min };
  } else return fail(2, usage);
  const t = targetsFor(store, id);
  if (t.error) return t.error;
  const results = await each(t.targets, (d) => client.meeting(d.addr, d.token, body));
  return summarize(results, (n) => (body.off ? `Meeting mode off on ${n}.` : `Meeting mode on for ${body.min ?? MEETING.default} min on ${n}.`));
}

// ---- find ----
async function find(args, { store, client }) {
  const { id, rest, error } = takeId(args);
  if (error) return fail(2, error);
  if (rest.length) return fail(2, 'Usage: find  [--id <id>]');
  const t = targetsFor(store, id);
  if (t.error) return t.error;
  const results = await each(t.targets, (d) => client.find(d.addr, d.token));
  return summarize(results, (n) => `Waving and blinking for 10 s on ${n}.`);
}

// ---- timer ----
async function timer(args, { store, client }) {
  const { id, rest, error } = takeId(args);
  if (error) return fail(2, error);
  const usage = 'Usage: timer <minutes> | timer stop  [--id <id>]';
  let body;
  if (rest.length === 1 && (rest[0] === 'stop' || rest[0] === 'off')) body = { stop: true };
  else if (rest.length === 1 && isWhole(rest[0])) {
    const min = Number(rest[0]);
    if (!inRange(min, TIMER)) return fail(2, `Minutes must be a whole number from ${TIMER.min} to ${TIMER.max} (got ${min}).`);
    body = { min };
  } else return fail(2, usage);
  const t = targetsFor(store, id);
  if (t.error) return t.error;
  const results = await each(t.targets, (d) => client.timer(d.addr, d.token, body));
  return summarize(results, (n) => (body.stop ? `Timer stopped on ${n}.` : `Timer: ${body.min} min on ${n}.`));
}

// ---- say ----
const SAY_USAGE = 'Usage: say <text...> [--min N] | say off  [--id <id>]';

// -> { body } or { error }.
export function parseSayArgs(args) {
  if (args.length === 1 && args[0] === 'off') return { body: { off: true } };
  const words = [];
  let min;
  for (let i = 0; i < args.length; i++) {
    if (args[i] !== '--min') { words.push(args[i]); continue; }
    if (min !== undefined || !isWhole(args[i + 1] ?? '')) return { error: `--min needs a number of minutes.\n${SAY_USAGE}` };
    min = Number(args[++i]);
    if (!inRange(min, SAY_MIN)) return { error: `--min must be a whole number from ${SAY_MIN.min} to ${SAY_MIN.max} (got ${min}).` };
  }
  if (!words.length) return { error: SAY_USAGE };
  const t = typedText(words, NOTE, 'message');
  if (t.error) return { error: t.error };
  return { body: min === undefined ? { text: t.text } : { text: t.text, min } };
}

async function say(args, { store, client }) {
  const { id, rest, error } = takeId(args);
  if (error) return fail(2, error);
  const parsed = parseSayArgs(rest);
  if (parsed.error) return fail(2, parsed.error);
  const t = targetsFor(store, id);
  if (t.error) return t.error;
  const { body } = parsed;
  const results = await each(t.targets, (d) => client.say(d.addr, d.token, body));
  // The text was typed by the user and already checked for control characters.
  return summarize(results, (n) => (body.off
    ? `Message off on ${n}.`
    : `Message on ${n} for ${body.min ?? SAY_MIN.default} min: "${body.text}".`));
}

export const DAILY_COMMANDS = { focus, meeting, find, timer, say };

// The daily-life lines of miblo.js USAGE.
export const DAILY_USAGE = [
  '  focus [focus-min [break-min [rounds]]] | focus stop | focus status  [--id <id>]',
  '  meeting [minutes] | meeting off  [--id <id>]',
  '  find  [--id <id>]',
  '  timer <minutes> | timer stop  [--id <id>]',
  '  say <text...> [--min N] | say off  [--id <id>]',
];

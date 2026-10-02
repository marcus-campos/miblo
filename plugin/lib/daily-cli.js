// The daily-life commands of bin/miblo.js: focus, meeting, find, timer, say, remind, countdown,
// blue (they talk to every paired gadget, or only `--id <id>`) and today/limits (they read the
// bridge).
// Every argument is validated here exactly as the firmware validates it, before any request.
import { cleanId, cleanName } from './mdns.js';
import { busyLine, isBusy, isReducedInfo } from './device-client.js';

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

// Strings that came from the gadget or the bridge, printed to the terminal: no ANSI escape
// sequences, control, bidi or zero-width characters, bounded length.
const ANSI = /\u001b\[[0-?]*[ -/]*[@-~]|\u001b\][^\u0007\u001b]*(?:\u0007|\u001b\\)?|\u009b[0-?]*[ -/]*[@-~]/g;
export const printable = (s, max = 48) =>
  [...String(s ?? '').replace(ANSI, '').replace(/[\p{Cc}\u061c\u200b-\u200f\u202a-\u202e\u2060-\u2069\ufeff]/gu, ' ').replace(/\s+/g, ' ').trim()]
    .slice(0, max).join('');

// Free text a gadget sent back (reminder texts, countdown labels), as Claude reads it: cleaned,
// capped and always one quoted string (inner quotes escaped), so it reads as data, never as
// instructions. The commands' .md say so.
export const quotedText = (s, max) => JSON.stringify(printable(s, max));

// Words typed by the user -> one text for the gadget, or { error }. Whitespace (spaces, tabs,
// line breaks) collapses to one space; any other control character is refused, as the gadget
// refuses it; the length is checked in characters and in UTF-8 bytes.
export function typedText(words, limits, what) {
  // NFC first: an accent typed as a combining mark counts (and is stored) as one character.
  const text = words.join(' ').normalize('NFC').replace(/\s+/g, ' ').trim();
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
  blueStrength: 'the strength (1-100%)',
  blueFilter: 'the blue light filter setting',
  blueFrom: 'the start time (it must differ from the end)',
  blueTo: 'the end time (it must differ from the start)',
};

// One failed request -> one line for the user.
export function problemLine(label, e, what = {}) {
  if (e?.status === 404) return `${label} does not support this yet: update it with /miblo:update.`;
  if (e?.status === 401) return `${label} no longer knows this computer (run /miblo:pair again).`;
  if (isBusy(e)) return busyLine(label);
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
    if (isReducedInfo(info)) return { label: r.label, err: { status: 401 } };
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
    // Starting: a gadget whose /api/info could not be reached (no HTTP status) gets no POST.
    // (st is in the same order as targets.)
    const down = st.map((x) => Boolean(x.err && !x.err.status));
    if (down.some(Boolean)) {
      const reachable = targets.filter((_, i) => !down[i]);
      const offline = st.filter((_, i) => down[i]).map((x) => `${x.label} is offline.`);
      if (!reachable.length) return fail(1, offline.join('\n'));
      const res = await startFocus(reachable, client, parsed.body);
      return { code: res.code, out: res.out + offline.join('\n') + '\n' };
    }
  }

  if (parsed.stop) {
    return summarize(await each(targets, (d) => client.focus(d.addr, d.token, { stop: true })), (n) => `Focus off on ${n}.`);
  }
  return startFocus(targets, client, parsed.body);
}

async function startFocus(targets, client, body) {
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

// ---- remind ----
const REMIND_USAGE = [
  'Usage: remind <minutes> <text...>          (in N minutes, 1-1440)',
  '       remind <HH:MM> <text...>            (today, or tomorrow if past)',
  '       remind every day|daily|todo dia <HH:MM> <text...>',
  '       remind weekdays|dias úteis <HH:MM> <text...>',
  '       remind                              (list)',
  '       remind off [N]                      (dismiss the one on screen, or delete reminder N)',
  '       [--id <id>] on any of them',
].join('\n');
export const REMIND_IN = { min: 1, max: 1440 };
export const DAYS = { everyDay: 127, weekdays: 62 };  // bit 0 = Sunday
// Recurring prefixes, lower case, accents as typed or not.
const RECURRING = [
  ['every day', DAYS.everyDay], ['everyday', DAYS.everyDay], ['daily', DAYS.everyDay],
  ['todo dia', DAYS.everyDay], ['todos os dias', DAYS.everyDay],
  ['weekdays', DAYS.weekdays], ['every weekday', DAYS.weekdays], ['dias úteis', DAYS.weekdays], ['dias uteis', DAYS.weekdays],
];
const WEEKDAY = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];

// "9:45", "09:45", "9h45" -> "09:45"; null when not a time; { error } when out of range.
export function parseHHMM(s) {
  const m = /^(\d{1,2})[:h](\d{2})$/i.exec(String(s));
  if (!m) return null;
  const [h, mi] = [Number(m[1]), Number(m[2])];
  if (h > 23 || mi > 59) return { error: `Invalid time "${quoteArg(s)}": use HH:MM from 00:00 to 23:59.` };
  return `${String(h).padStart(2, '0')}:${m[2]}`;
}

// -> { list: true } | { body, kind: 'in'|'at'|'every', n? } | { error }.
export function parseRemindArgs(args) {
  // Every word on its own (a quoted "dias úteis" is two words); the text is joined back with spaces.
  const w = args.flatMap((a) => String(a).split(/\s+/)).filter(Boolean);
  if (!w.length) return { list: true };
  if (w[0].toLowerCase() === 'off' && w.length <= 2) {
    if (w.length === 1) return { body: { dismiss: true }, kind: 'dismiss' };
    const n = isWhole(w[1]) ? Number(w[1]) : NaN;
    if (!(n >= 1 && n <= 8)) return { error: `Give the number of the reminder to delete (1 to 8, as listed by /miblo:remind).` };
    return { body: { delete: n }, kind: 'delete', n };
  }
  const withText = (body, kind, rest) => {
    if (!rest.length) return { error: `What should the reminder say?\n${REMIND_USAGE}` };
    const t = typedText(rest, NOTE, 'reminder text');
    return t.error ? { error: t.error } : { body: { ...body, text: t.text }, kind };
  };
  if (isWhole(w[0])) {
    // "0930" or "930" is a time typed without the colon, not 930 minutes: ask for HH:MM.
    // Below 600 (10 h) a number stays minutes ("120" = 2 h).
    const v = w[0];
    const hh = Math.floor(Number(v) / 100);
    const looksLikeTime = v.length >= 3 && v.length <= 4 && Number(v) >= 600 && hh <= 23 && Number(v) % 100 <= 59;
    if ((v.length > 1 && v.startsWith('0')) || looksLikeTime) {
      return { error: `"${quoteArg(v)}" looks like a time: write it as HH:MM (e.g. 09:30), or give the minutes from now (1-${REMIND_IN.max}).` };
    }
    const min = Number(v);
    if (!inRange(min, REMIND_IN)) return { error: `Minutes must be a whole number from ${REMIND_IN.min} to ${REMIND_IN.max} (got ${min}).` };
    return withText({ in: min }, 'in', w.slice(1));
  }
  const at = parseHHMM(w[0]);
  if (at?.error) return { error: at.error };
  if (at) return withText({ at }, 'at', w.slice(1));
  const lower = w.map((x) => x.toLowerCase().normalize('NFC'));
  for (const [phrase, days] of RECURRING) {
    const k = phrase.split(' ').length;
    if (lower.slice(0, k).join(' ') !== phrase) continue;
    const time = parseHHMM(w[k] ?? '');
    if (time?.error) return { error: time.error };
    if (!time) return { error: `Give the time after "${phrase}", e.g. remind ${phrase} 09:45 stand-up.` };
    return withText({ at: time, days }, 'every', w.slice(k + 1));
  }
  return { error: REMIND_USAGE };
}

const daysText = (days) => {
  if (days === DAYS.everyDay) return 'every day';
  if (days === DAYS.weekdays) return 'weekdays';
  const names = WEEKDAY.filter((_, i) => days & (1 << i));
  return names.length ? names.join(' ') : 'never';
};

// Seconds -> "14 min" / "1h05".
export function durationText(sec) {
  const min = Math.max(0, Math.round(Number(sec) / 60));
  if (min < 60) return `${min} min`;
  return `${Math.floor(min / 60)}h${String(min % 60).padStart(2, '0')}`;
}

// One item of GET /api/remind -> "1  in 14 min  ligar pro cliente" / "5  weekdays 09:45  daily".
function reminderLine(it) {
  const id = Number.isInteger(it?.id) ? it.id : '?';
  const text = quotedText(it?.text, NOTE.chars);
  if (typeof it?.at === 'string') return `${id}  ${daysText(Number(it.days) || 0)} ${printable(it.at, 5)}  ${text}`;
  return `${id}  in ${durationText(Math.ceil(Number(it?.in ?? 0) / 60) * 60)}  ${text}`;
}

// Groups the gadgets that took a request by `key(reply)` -> one line per group.
function summarizeBy(results, key, line, ctx) {
  const groups = new Map();
  for (const r of results) {
    if (r.err) continue;
    const k = key(r.reply);
    groups.set(k, [...(groups.get(k) ?? []), r.label]);
  }
  const lines = [...groups].map(([k, names]) => line(joinNames(names), k));
  for (const r of results) if (r.err) lines.push(problemLine(r.label, r.err, ctx));
  return groups.size ? ok(lines.join('\n')) : fail(1, lines.join('\n'));
}

async function remind(args, { store, client }) {
  const { id, rest, error } = takeId(args);
  if (error) return fail(2, error);
  const parsed = parseRemindArgs(rest);
  if (parsed.error) return fail(2, parsed.error);
  const t = targetsFor(store, id);
  if (t.error) return t.error;
  const { targets } = t;

  if (parsed.list) {
    const results = await each(targets, (d) => client.reminders(d.addr, d.token));
    const several = targets.length > 1;
    const lines = [];
    for (const r of results) {
      if (r.err) { lines.push(problemLine(r.label, r.err)); continue; }
      const items = Array.isArray(r.reply.items) ? r.reply.items.slice(0, 8) : [];
      if (!items.length) { lines.push(several ? `${r.label}: no reminders.` : 'No reminders.'); continue; }
      if (several) lines.push(`${r.label}:`);
      for (const it of items) lines.push(`${several ? '  ' : ''}${reminderLine(it)}`);
    }
    return results.some((r) => !r.err) ? ok(lines.join('\n')) : fail(1, lines.join('\n'));
  }

  const { body, kind } = parsed;
  const results = await each(targets, (d) => client.remind(d.addr, d.token, body));
  if (kind === 'dismiss') {
    // Nothing on a screen is not a failure: say so for that gadget.
    for (const r of results) if (r.err?.status === 409 && r.err.data?.field === 'none') { r.reply = { none: true }; delete r.err; }
    return summarizeBy(results, (rep) => (rep.none ? 'none' : 'done'),
      (n, k) => (k === 'none' ? `Nothing to dismiss on ${n}.` : `Reminder dismissed on ${n}.`));
  }
  if (kind === 'delete') return summarize(results, (n) => `Deleted reminder ${parsed.n} on ${n}.`, { n: parsed.n });
  const when = kind === 'in' ? `in ${body.in} min`
    : kind === 'at' ? `at ${body.at}`
    : `${body.days === DAYS.weekdays ? 'on weekdays' : 'every day'} at ${body.at}`;
  // The text was typed by the user and already checked for control characters.
  return summarizeBy(results, (rep) => (Number.isInteger(rep.id) ? rep.id : '?'),
    (n, rid) => `Reminder ${rid} on ${n} ${when}: "${body.text}".`, { recurring: kind === 'every' });
}

// ---- countdown ----
const COUNTDOWN_USAGE = 'Usage: countdown <label...> <DD/MM[/YYYY]> | countdown off | countdown  [--id <id>]';
export const COUNTDOWN_MAX_DAYS = 999;  // the gadget's "label in N days" goes up to 999
const pad2 = (n) => String(n).padStart(2, '0');
const realDay = (y, m, d) => m >= 1 && m <= 12 && d >= 1 && d <= new Date(y, m, 0).getDate();
const localYMD = (t) => { const d = new Date(t); return [d.getFullYear(), d.getMonth() + 1, d.getDate()]; };
// Whole calendar days from a to b (local dates as [y, m, d]).
const daysBetween = (a, b) => Math.round((Date.UTC(b[0], b[1] - 1, b[2]) - Date.UTC(a[0], a[1] - 1, a[2])) / 86_400_000);

// -> { body } (body.md or body.date) | { off: true } | { show: true } | { error }.
export function parseCountdownArgs(args, nowMs = Date.now()) {
  if (!args.length) return { show: true };
  if (args.length === 1 && args[0] === 'off') return { body: { off: true } };
  const m = /^(\d{1,2})\/(\d{1,2})(?:\/(\d{4}))?$/.exec(args[args.length - 1]);
  if (!m) return { error: `End with the date, day first: DD/MM or DD/MM/YYYY.\n${COUNTDOWN_USAGE}` };
  const [day, month] = [Number(m[1]), Number(m[2])];
  const year = m[3] === undefined ? null : Number(m[3]);
  const today = localYMD(nowMs);
  if (!realDay(year ?? 2000, month, day)) return { error: `Invalid date "${quoteArg(m[0])}": use a real calendar day, day first (DD/MM).` };
  // A 29/02 without a year has no clear next occurrence: ask for the year.
  if (year === null && month === 2 && day === 29) return { error: 'For 29/02 give the year too (DD/MM/YYYY).' };
  if (year !== null) {
    const ahead = daysBetween(today, [year, month, day]);
    if (ahead < 0) return { error: `${quoteArg(m[0])} is in the past.` };
    if (ahead > COUNTDOWN_MAX_DAYS) return { error: `${quoteArg(m[0])} is too far away (${COUNTDOWN_MAX_DAYS} days at most).` };
  }
  const words = args.slice(0, -1);
  if (!words.length) return { error: `Give a label before the date, e.g. countdown release 15/10.\n${COUNTDOWN_USAGE}` };
  const t = typedText(words, LABEL, 'label');
  if (t.error) return { error: t.error };
  const body = year === null
    ? { label: t.text, md: `${pad2(month)}-${pad2(day)}` }
    : { label: t.text, date: `${year}-${pad2(month)}-${pad2(day)}` };
  return { body, shown: `${pad2(day)}/${pad2(month)}${year === null ? '' : `/${year}`}` };
}

async function countdown(args, { store, client, now }) {
  const { id, rest, error } = takeId(args);
  if (error) return fail(2, error);
  const parsed = parseCountdownArgs(rest, now());
  if (parsed.error) return fail(2, parsed.error);
  const t = targetsFor(store, id);
  if (t.error) return t.error;

  if (parsed.show) {
    const results = await each(t.targets, (d) => client.info(d.addr, d.token));
    const lines = [];
    let any = false;
    for (const r of results) {
      const info = r.reply;
      if (r.err) { lines.push(problemLine(r.label, r.err)); continue; }
      if (isReducedInfo(info)) { lines.push(problemLine(r.label, { status: 401 })); continue; }
      if (typeof info.countdown !== 'string') { lines.push(problemLine(r.label, { status: 404 })); continue; }
      any = true;
      const label = printable(info.countdown, LABEL.chars);
      const dm = /^(\d{4})-(\d{2})-(\d{2})$/.exec(String(info.countdownDate ?? ''));
      if (!label || !dm) { lines.push(`${r.label}: no countdown.`); continue; }
      const left = daysBetween(localYMD(now()), [Number(dm[1]), Number(dm[2]), Number(dm[3])]);
      const when = left > 1 ? `in ${left} days` : left === 1 ? 'tomorrow' : left === 0 ? 'today' : 'passed';
      lines.push(`${r.label}: ${JSON.stringify(label)} on ${dm[3]}/${dm[2]}/${dm[1]} (${when}).`);
    }
    return any ? ok(lines.join('\n')) : fail(1, lines.join('\n'));
  }

  const { body } = parsed;
  const results = await each(t.targets, (d) => client.countdown(d.addr, d.token, body));
  // The label was typed by the user and already checked for control characters.
  return summarize(results, (n) => (body.off ? `Countdown off on ${n}.` : `Countdown on ${n}: "${body.label}" on ${parsed.shown}.`));
}

// ---- today / limits: read from the running bridge (GET /status) ----
const NO_BRIDGE = "The bridge isn't running (it starts with Claude Code activity): nothing to show yet.";
const NO_LIMITS = 'No limits yet: link the status line with /miblo:link-statusline.';

// An epoch (seconds) -> "16:42" when it falls today on this computer, else "Thu 09:00".
export function clockText(epochSec, nowMs) {
  const t = new Date(epochSec * 1000);
  const n = new Date(nowMs);
  const hhmm = `${pad2(t.getHours())}:${pad2(t.getMinutes())}`;
  const sameDay = t.getFullYear() === n.getFullYear() && t.getMonth() === n.getMonth() && t.getDate() === n.getDate();
  return sameDay ? hhmm : `${WEEKDAY[t.getDay()]} ${hhmm}`;
}

const validEpoch = (v) => Number.isFinite(v) && v > 0;

// The bridge's `usage` -> "5h 62% (resets 16:42, at this pace runs out at 15:40) · week 38% (resets Thu 09:00)",
// or null when there is none.
export function limitsLine(usage, nowMs) {
  const nowSec = nowMs / 1000;
  const part = (w, name, withEta) => {
    if (!w || !Number.isFinite(Number(w.pct))) return null;
    const notes = [];
    const reset = Number(w.reset);
    if (validEpoch(reset) && reset > nowSec) notes.push(`resets ${clockText(reset, nowMs)}`);
    const eta = Number(w.eta);
    if (withEta && validEpoch(eta) && eta > nowSec && !(validEpoch(reset) && eta >= reset)) {
      notes.push(`at this pace runs out at ${clockText(eta, nowMs)}`);
    }
    return `${name} ${Math.round(Number(w.pct))}%${notes.length ? ` (${notes.join(', ')})` : ''}`;
  };
  const parts = [part(usage?.h5, '5h', true), part(usage?.d7, 'week', false)].filter(Boolean);
  return parts.length ? parts.join(' · ') : null;
}

async function today(args, { fetchStatus, now }) {
  if (args.length) return fail(2, 'Usage: today');
  const st = await fetchStatus();
  if (!st) return ok(NO_BRIDGE);
  const t = st.today ?? {};
  const turns = Math.max(0, Math.floor(Number(t.turns) || 0));
  const parts = [`${turns} response${turns === 1 ? '' : 's'}`, `${durationText(Number(t.work) || 0)} with Claude working`];
  // The bridge sends 0 when it has no cost readings: leave the cost out then.
  if (Number.isFinite(Number(t.usd)) && Number(t.usd) > 0) parts.push(`US$ ${Number(t.usd).toFixed(2)}`);
  const lines = [`Today: ${parts.join(', ')}.`, limitsLine(st.usage, now()) ?? NO_LIMITS];
  const top = Array.isArray(t.top) ? t.top.filter((x) => printable(x?.name, 40) && Number(x?.work) > 0).slice(0, 3) : [];
  if (top.length >= 2) lines.push(`Most work: ${top.map((x) => `${printable(x.name, 40)} ${durationText(Number(x.work))}`).join(', ')}.`);
  return ok(lines.join('\n'));
}

async function limits(args, { fetchStatus, now }) {
  if (args.length) return fail(2, 'Usage: limits');
  const st = await fetchStatus();
  if (!st) return ok(NO_BRIDGE);
  return ok(limitsLine(st.usage, now()) ?? NO_LIMITS);
}

// ---- blue: the blue light filter (firmware miblo_config.h blueFilter, blueStrength, blueFrom, blueTo) ----
// blueFilter: 0 off, 1 always, 2 between blueFrom and blueTo (minutes of the gadget's local day;
// the window may cross midnight). The strength is 1..100 %; the words of the old three-level
// select are the strengths with exactly their colours (miblo_config.cpp blueStrengthForLevel).
export const BLUE_STRENGTH = { min: 1, max: 100 };
export const BLUE_LEVELS = { low: 31, medium: 63, high: 100 };
const BLUE_USAGE = 'Usage: blue [status] | blue off | blue [on] [HH:MM HH:MM] [1-100%|low|medium|high]  [--id <id>]';
const hhmm = (m) => `${pad2(Math.floor(m / 60))}:${pad2(m % 60)}`;

// -> { status: true }, { patch } or { error }. "on" alone is always on; two times are a schedule
// ("on" may come with them); a number (with or without %) or low/medium/high is the strength,
// alone (the filter's state is kept) or with the rest; "off" takes nothing else.
export function parseBlueArgs(args) {
  if (!args.length || (args.length === 1 && String(args[0]).toLowerCase() === 'status')) return { status: true };
  let state;
  let strength;
  const times = [];
  for (const a of args) {
    const t = /^(\d{1,2})[:h](\d{2})$/.exec(a);
    const word = String(a).toLowerCase();
    if (word === 'on' || word === 'off') {
      if (state !== undefined) return { error: `Give "on" or "off" once.\n${BLUE_USAGE}` };
      state = word;
    } else if (t) {
      const h = Number(t[1]);
      const m = Number(t[2]);
      if (h > 23 || m > 59) return { error: `Invalid time "${quoteArg(a).slice(0, 10)}": use HH:MM from 00:00 to 23:59.` };
      times.push(h * 60 + m);
    } else if (/^[+-]?\d+(\.\d+)?%?$/.test(a) || Object.hasOwn(BLUE_LEVELS, word)) {
      if (strength !== undefined) return { error: `Give one strength.\n${BLUE_USAGE}` };
      strength = Object.hasOwn(BLUE_LEVELS, word) ? BLUE_LEVELS[word] : Number(String(a).replace('%', ''));
      if (!inRange(strength, BLUE_STRENGTH)) {
        return { error: `Strength must be a whole number from ${BLUE_STRENGTH.min} to ${BLUE_STRENGTH.max} % (got ${quoteArg(a)}).` };
      }
    } else {
      return { error: `Unexpected argument "${quoteArg(a)}".\n${BLUE_USAGE}` };
    }
  }
  if (state === 'off' && (times.length || strength !== undefined)) return { error: `"off" takes nothing else.\n${BLUE_USAGE}` };
  if (times.length && times.length !== 2) return { error: `Give both times (start and end), e.g. 21:00 07:00.\n${BLUE_USAGE}` };
  if (times.length && times[0] === times[1]) return { error: 'Start and end times must differ.' };
  const patch = {};
  if (state === 'off') patch.blueFilter = 0;
  else if (times.length) [patch.blueFilter, patch.blueFrom, patch.blueTo] = [2, times[0], times[1]];
  else if (state === 'on') patch.blueFilter = 1;
  if (strength !== undefined) patch.blueStrength = strength;
  return { patch };
}

const blueWhen = (c) => (c.blueFilter === 1 ? 'always on'
  : c.blueFilter === 2 ? `on from ${hhmm(c.blueFrom)} to ${hhmm(c.blueTo)}` : 'off');

// One gadget's filter from /api/info -> "on from 21:00 to 07:00 (strength 60%)". A firmware from
// before the slider reports only the old level.
const blueLine = (info) => (Number.isInteger(info.blueStrength)
  ? `${blueWhen(info)} (strength ${info.blueStrength}%)`
  : `${blueWhen(info)} (to choose the strength, update it with /miblo:update)`);

async function blue(args, { store, client }) {
  const { id, rest, error } = takeId(args);
  if (error) return fail(2, error);
  const parsed = parseBlueArgs(rest);
  if (parsed.error) return fail(2, parsed.error);
  const t = targetsFor(store, id);
  if (t.error) return t.error;

  // /api/info first: unknown config fields are ignored, so a firmware from before the filter would
  // accept the patch and do nothing, and one from before the slider (it has on/off/schedule) would
  // silently drop blueStrength: those are told to update instead.
  const wantsStrength = parsed.patch?.blueStrength !== undefined;
  const infos = (await each(t.targets, (d) => client.info(d.addr, d.token))).map((r) => {
    if (r.err) return r;
    if (isReducedInfo(r.reply)) return { ...r, err: { status: 401 } };
    const n = (k) => Number.isInteger(r.reply[k]);
    if (!n('blueFilter') || !n('blueFrom') || !n('blueTo')) return { ...r, err: { status: 404 } };
    if (wantsStrength && !n('blueStrength')) return { ...r, err: { status: 404 } };
    return r;
  });

  if (parsed.status) {
    const lines = infos.map((r) => (r.err ? problemLine(r.label, r.err) : `Blue light filter on ${r.label}: ${blueLine(r.reply)}.`));
    return infos.some((r) => !r.err) ? ok(lines.join('\n')) : fail(1, lines.join('\n'));
  }

  const { patch } = parsed;
  const sendable = infos.filter((r) => !r.err);
  const sent = await each(sendable.map((r) => r.d), (d) => client.setConfig(d.addr, d.token, patch));
  const results = infos.map((r) => (r.err ? r : sent.find((x) => x.d === r.d)));
  const parts = [];
  if (patch.blueFilter !== undefined) parts.push(blueWhen(patch));
  if (patch.blueStrength !== undefined) parts.push(`strength ${patch.blueStrength}%`);
  // The strength alone, on gadgets whose filter is off: nothing changes on screen until it is on.
  const tookIt = sendable.filter((r) => !sent.find((x) => x.d === r.d).err);
  const offNote = patch.blueFilter === undefined && tookIt.length && tookIt.every((r) => r.reply.blueFilter === 0)
    ? ' (it is off: /miblo:blue on to turn it on)' : '';
  return summarize(results, (names) => `Blue light filter ${parts.join(', ')} for ${names}${offNote}.`);
}

export const DAILY_COMMANDS = { focus, meeting, find, timer, say, remind, countdown, blue, today, limits };

// The daily-life lines of miblo.js USAGE.
export const DAILY_USAGE = [
  '  focus [focus-min [break-min [rounds]]] | focus stop | focus status  [--id <id>]',
  '  meeting [minutes] | meeting off  [--id <id>]',
  '  find  [--id <id>]',
  '  timer <minutes> | timer stop  [--id <id>]',
  '  say <text...> [--min N] | say off  [--id <id>]',
  '  remind [<minutes>|<HH:MM>|every day <HH:MM>|weekdays <HH:MM> <text...>] | remind off [N]  [--id <id>]',
  '  countdown <label...> <DD/MM[/YYYY]> | countdown off | countdown  [--id <id>]',
  '  blue [status] | blue off | blue [on] [HH:MM HH:MM] [1-100%|low|medium|high]  [--id <id>]',
  '  today',
  '  limits',
];

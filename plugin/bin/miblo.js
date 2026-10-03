#!/usr/bin/env node
import os from 'node:os';
import { spawn } from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { PORT, HOST, claudeSettingsPath, parseDataArg, pluginVersion, isMain } from '../lib/constants.js';
import { DeviceClient, busyLine, isBusy, isReducedInfo } from '../lib/device-client.js';
import { DeviceStore } from '../lib/device-store.js';
import { discover, cleanId, cleanName } from '../lib/mdns.js';
import { link, unlink, isLinked } from '../lib/statusline-link.js';
import { FirmwareUpdater } from '../lib/firmware-update.js';
import { DAILY_COMMANDS, DAILY_USAGE } from '../lib/daily-cli.js';
import { tokenTag } from '../lib/relocation.js';

const MODES = ['overview', 'limits', 'sessions'];
const USAGE = [
  'Usage: miblo.js --data <dir> <command>',
  '  discover',
  '  pair <ip[:port]> <code>',
  '  status',
  '  mode <overview|limits|sessions> [id]',
  '  rotate <on|off> [every-seconds] [show-seconds] [id] | rotate --status [id]',
  '  night <on|off> [HH:MM HH:MM] [brightness%] [id] | night --status [id]',
  '  settings [id]',
  '  rename <id> <name...> | rename <id> --default',
  '  owner <id> [--name <name...>|--name clear] [--birthday <DD/MM|MM-DD|clear>]',
  '  demo [minutes|stop]',
  '  reset <id>',
  '  update [check|open|send] [id] [code] [--file path] [--check]',
  '  link-statusline | unlink-statusline',
  ...DAILY_USAGE,
].join('\n');

const cleanAddr = (s) => String(s ?? '').replace(/[^A-Za-z0-9.:[\]-]/g, '').slice(0, 64);
const safe = (d) => ({ ...d, id: cleanId(d.id), name: cleanName(d.name) || cleanId(d.id).slice(0, 20), ...(d.addr !== undefined ? { addr: cleanAddr(d.addr) } : {}) });

const withPort = (addr) => (String(addr).includes(':') ? String(addr) : `${addr}:80`);

// Host locale ("en-US", "pt-BR", "zh-Hans-CN", ...) -> one of the firmware's supported
// language codes (lib/miblo_core/src/miblo_i18n.cpp kCodes), or null if unsupported.
// Mirrors the firmware's own tag matching (miblo_i18n.cpp matchTag): a bare "pt" or
// region "BR" maps to pt-BR, any other pt region maps to pt-PT.
const SIMPLE_LANGS = new Set(['en', 'es', 'fr', 'it', 'de', 'ru', 'zh']);
function mapLocaleToLang(locale) {
  const parts = String(locale ?? '').split(/[-_]/).filter(Boolean);
  const primary = (parts[0] || '').toLowerCase();
  if (primary === 'pt') {
    const region = (parts.slice(1).find((p) => p.length === 2) || '').toUpperCase();
    return region === '' || region === 'BR' ? 'pt-BR' : 'pt-PT';
  }
  return SIMPLE_LANGS.has(primary) ? primary : null;
}

// Opens a URL in the default browser. Resolves once the opener exits cleanly (or is still
// running after a few seconds), rejects if it could not be run or failed.
export function openInBrowser(url, platform = process.platform) {
  const [cmd, args, opts] = platform === 'darwin' ? ['open', [url], {}]
    // `start` takes the first quoted argument as a window title, hence the empty "".
    : platform === 'win32' ? ['cmd', ['/c', 'start', '""', url], { windowsVerbatimArguments: true }]
    : ['xdg-open', [url], {}];
  return new Promise((resolve, reject) => {
    const child = spawn(cmd, args, { stdio: 'ignore', detached: true, windowsHide: true, ...opts });
    const timer = setTimeout(() => { child.unref(); resolve(); }, 3000);
    timer.unref();
    child.on('error', (e) => { clearTimeout(timer); reject(e); });
    child.on('exit', (code) => {
      clearTimeout(timer);
      if (code === 0) resolve();
      else reject(new Error(`${cmd} exited with ${code}`));
    });
  });
}

// ---- settings: the gadget's own web settings page ----
// The stored addr is "ip:port"; the default HTTP port is dropped for a clean URL.
export const settingsUrl = (addr) => `http://${cleanAddr(addr).replace(/^(\[[^\]]*\]|[^:]*):80$/, '$1')}/`;

// The settings page marks this computer "(this computer)" by its token's tag (relocation.js).
export { tokenTag };

async function settings(args, store, openUrl) {
  const ok = (out) => ({ code: 0, out: out + '\n' });
  const fail = (code, out) => ({ code, out: out + '\n' });
  const devices = store.list();
  if (!devices.length) return fail(2, 'No paired Miblo gadgets. Run /miblo:pair first.');
  const [id] = args;
  let d;
  if (id) {
    d = devices.find((x) => x.id === id);
    if (!d) return fail(2, `No paired gadget with id ${cleanId(id)}.`);
  } else if (devices.length > 1) {
    const list = devices.map((x) => `${cleanId(x.id)}\t${cleanName(x.name)}\t${cleanAddr(x.addr)}`);
    return fail(2, ['Several gadgets are paired; pass the id of one:', ...list].join('\n'));
  } else {
    [d] = devices;
  }
  const url = settingsUrl(d.addr);
  const lines = [
    `${cleanName(d.name)} settings: ${url}`,
    `(also at http://${cleanId(d.id)}.local if the IP changed and your network resolves .local names)`,
  ];
  try {
    // The page marks this computer in its paired computers' list by its token's tag (never the
    // token): it survives renaming the computer on the page.
    await openUrl(d.token ? `${url}#me=${tokenTag(d.token)}` : url);
    lines.push('Opened in your browser.');
  } catch {
    lines.push('Could not open a browser: open the URL by hand.');
  }
  return ok(lines.join('\n'));
}

async function defaultFetchStatus() {
  try {
    const res = await fetch(`http://${HOST}:${PORT}/status`, { signal: AbortSignal.timeout(800) });
    return res.ok ? await res.json() : null;
  } catch {
    return null;
  }
}

// ---- rotate: optional Overview/Limits alternation (firmware miblo_config.h) ----
// Limits replaces Overview for `show` seconds once every `every` seconds (Overview mode only;
// alerts and sessions waiting on the user always win).
export const ROTATE_EVERY = { min: 10, max: 3600 };
export const ROTATE_SHOW = { min: 3, max: 300 };
const ROTATE_USAGE = 'Usage: rotate <on|off> [every-seconds] [show-seconds] [id]  (or: rotate --status [id])';

// -> { patch } or { error }. Positional numbers are every/show; any other word is the gadget id.
export function parseRotateArgs(args) {
  const [state, ...rest] = args;
  if (state !== 'on' && state !== 'off') return { error: `First argument must be "on" or "off".\n${ROTATE_USAGE}` };
  const nums = [];
  let id;
  for (const a of rest) {
    if (/^[+-]?\d+(\.\d+)?$/.test(a)) nums.push(Number(a));
    else if (id === undefined) id = a;
    else return { error: `Unexpected argument "${String(a).slice(0, 40)}".\n${ROTATE_USAGE}` };
  }
  if (nums.length > 2) return { error: `Too many numbers: give at most every-seconds and show-seconds.\n${ROTATE_USAGE}` };
  const [every, show] = nums;
  const inRange = (v, r) => Number.isInteger(v) && v >= r.min && v <= r.max;
  if (every !== undefined && !inRange(every, ROTATE_EVERY)) {
    return { error: `every-seconds must be a whole number from ${ROTATE_EVERY.min} to ${ROTATE_EVERY.max} (got ${every}).` };
  }
  if (show !== undefined && !inRange(show, ROTATE_SHOW)) {
    return { error: `show-seconds must be a whole number from ${ROTATE_SHOW.min} to ${ROTATE_SHOW.max} (got ${show}).` };
  }
  if (every !== undefined && show !== undefined && show >= every) {
    return { error: `show-seconds (${show}) must be shorter than every-seconds (${every}).` };
  }
  const patch = { rotate: state === 'on' };
  if (every !== undefined) patch.rotateEverySec = every;
  if (show !== undefined) patch.rotateShowSec = show;
  return { patch, id };
}

const describeRotation = (r) =>
  r.rotate ? `on, Limits every ${r.rotateEverySec} s for ${r.rotateShowSec} s` : 'off';

async function rotate(args, store, client) {
  const ok = (out) => ({ code: 0, out: out + '\n' });
  const fail = (code, out) => ({ code, out: out + '\n' });
  const parsed = args[0] === '--status' ? null : parseRotateArgs(args);
  if (parsed?.error) return fail(2, parsed.error);
  const devices = store.list();
  if (!devices.length) return fail(2, 'No paired Miblo gadgets. Run /miblo:pair first.');

  if (!parsed) {
    const id = args[1];
    const targets = devices.filter((d) => !id || d.id === id);
    if (!targets.length) return fail(2, `No paired gadget with id ${cleanId(id)}.`);
    const lines = [];
    for (const d of targets) {
      const label = `${cleanName(d.name)} (${cleanId(d.id)})`;
      try {
        const info = await client.info(d.addr, d.token);
        if (isReducedInfo(info)) lines.push(`${label}: no longer accepts this pairing (run /miblo:pair again)`);
        else if (typeof info?.rotate !== 'boolean') lines.push(`${label}: rotation not supported by this firmware (update it)`);
        else lines.push(`${label}: rotation ${describeRotation(info)}`);
      } catch (e) {
        lines.push(isBusy(e) ? `${label}: busy right now — try again in a moment` : `${label}: offline`);
      }
    }
    return ok(lines.join('\n'));
  }

  const { patch, id } = parsed;
  const targets = devices.filter((d) => !id || d.id === id);
  if (!targets.length) return fail(2, `No paired gadget with id ${cleanId(id)}.`);
  let done = 0;
  const problems = [];
  for (const d of targets) {
    try {
      await client.setConfig(d.addr, d.token, patch);
      done++;
    } catch (e) {
      const label = cleanName(d.name);
      if (e.status === 400 && e.data?.field) {
        const field = String(e.data.field).replace(/[^A-Za-z]/g, '').slice(0, 20);
        problems.push(field.startsWith('rotate')
          ? `${label} rejected ${field}: show-seconds must be shorter than every-seconds (${ROTATE_SHOW.min}-${ROTATE_SHOW.max} and ${ROTATE_EVERY.min}-${ROTATE_EVERY.max}); pass both numbers.`
          : `${label} rejected ${field}.`);
      } else if (e.status === 400) {
        problems.push(`${label} does not support rotation yet (update its firmware).`);
      } else {
        problems.push(isBusy(e) ? busyLine(label) : `${label} is offline.`);
      }
    }
  }
  const parts = [];
  if (patch.rotateEverySec) parts.push(`Limits every ${patch.rotateEverySec} s`);
  if (patch.rotateShowSec) parts.push(`for ${patch.rotateShowSec} s`);
  const what = patch.rotate ? `Rotation on${parts.length ? ` (${parts.join(', ')})` : ''}` : 'Rotation off';
  const out = [`${what} on ${done} gadget(s).`, ...problems].join('\n');
  return done ? ok(out) : fail(1, out);
}

// ---- night: dim the screen between two local times (firmware miblo_config.h) ----
// Times travel as minutes of the day (0..1439); the window may cross midnight.
export const NIGHT_BRIGHTNESS = { min: 1, max: 100 };
const NIGHT_USAGE = 'Usage: night <on|off> [HH:MM HH:MM] [brightness%] [id]  (or: night --status [id])';
const hhmm = (m) => `${String(Math.floor(m / 60)).padStart(2, '0')}:${String(m % 60).padStart(2, '0')}`;

// -> { patch, id } or { error }. Two HH:MM are start and end; a number (optionally with %) is
// the night brightness; any other word is the gadget id.
export function parseNightArgs(args) {
  const [state, ...rest] = args;
  if (state !== 'on' && state !== 'off') return { error: `First argument must be "on" or "off".\n${NIGHT_USAGE}` };
  const times = [];
  let brightness;
  let id;
  for (const a of rest) {
    const t = /^(\d{1,2})[:h](\d{2})$/.exec(a);
    if (t) {
      const h = Number(t[1]);
      const m = Number(t[2]);
      if (h > 23 || m > 59) return { error: `Invalid time "${String(a).slice(0, 10)}": use HH:MM from 00:00 to 23:59.` };
      times.push(h * 60 + m);
    } else if (/^[+-]?\d+(\.\d+)?%?$/.test(a)) {
      if (brightness !== undefined) return { error: `Too many numbers: give one night brightness.\n${NIGHT_USAGE}` };
      brightness = Number(a.replace('%', ''));
    } else if (id === undefined) {
      id = a;
    } else {
      return { error: `Unexpected argument "${String(a).slice(0, 40)}".\n${NIGHT_USAGE}` };
    }
  }
  if (times.length === 1 || times.length > 2) return { error: `Give both times (start and end), e.g. 22:00 07:00.\n${NIGHT_USAGE}` };
  if (times.length === 2 && times[0] === times[1]) return { error: 'Start and end times must differ.' };
  if (brightness !== undefined &&
      (!Number.isInteger(brightness) || brightness < NIGHT_BRIGHTNESS.min || brightness > NIGHT_BRIGHTNESS.max)) {
    return { error: `Night brightness must be a whole number from ${NIGHT_BRIGHTNESS.min} to ${NIGHT_BRIGHTNESS.max} (got ${brightness}).` };
  }
  const patch = { night: state === 'on' };
  if (times.length === 2) [patch.nightFrom, patch.nightTo] = times;
  if (brightness !== undefined) patch.nightBrightness = brightness;
  return { patch, id };
}

const describeNight = (n) =>
  n.night ? `on, ${hhmm(n.nightFrom)}-${hhmm(n.nightTo)} at ${n.nightBrightness}%` : 'off';

async function night(args, store, client) {
  const ok = (out) => ({ code: 0, out: out + '\n' });
  const fail = (code, out) => ({ code, out: out + '\n' });
  const parsed = args[0] === '--status' ? null : parseNightArgs(args);
  if (parsed?.error) return fail(2, parsed.error);
  const devices = store.list();
  if (!devices.length) return fail(2, 'No paired Miblo gadgets. Run /miblo:pair first.');
  const id = parsed ? parsed.id : args[1];
  const targets = devices.filter((d) => !id || d.id === id);
  if (!targets.length) return fail(2, `No paired gadget with id ${cleanId(id)}.`);

  if (!parsed) {
    const lines = [];
    for (const d of targets) {
      const label = `${cleanName(d.name)} (${cleanId(d.id)})`;
      try {
        const info = await client.info(d.addr, d.token);
        if (isReducedInfo(info)) lines.push(`${label}: no longer accepts this pairing (run /miblo:pair again)`);
        else if (typeof info?.night !== 'boolean') lines.push(`${label}: night mode not supported by this firmware (update it)`);
        else lines.push(`${label}: night mode ${describeNight(info)}`);
      } catch (e) {
        lines.push(isBusy(e) ? `${label}: busy right now — try again in a moment` : `${label}: offline`);
      }
    }
    return ok(lines.join('\n'));
  }

  const { patch } = parsed;
  let done = 0;
  const problems = [];
  for (const d of targets) {
    try {
      await client.setConfig(d.addr, d.token, patch);
      done++;
    } catch (e) {
      const label = cleanName(d.name);
      if (e.status === 400 && e.data?.field) {
        const field = String(e.data.field).replace(/[^A-Za-z]/g, '').slice(0, 20);
        problems.push(field === 'nightFrom' || field === 'nightTo'
          ? `${label} rejected ${field}: start and end must differ; pass both times.`
          : field.startsWith('night') ? `${label} rejected ${field}.` : `${label} does not support night mode yet (update its firmware).`);
      } else if (e.status === 400) {
        problems.push(`${label} does not support night mode yet (update its firmware).`);
      } else {
        problems.push(isBusy(e) ? busyLine(label) : `${label} is offline.`);
      }
    }
  }
  const parts = [];
  if (patch.nightFrom !== undefined) parts.push(`${hhmm(patch.nightFrom)}-${hhmm(patch.nightTo)}`);
  if (patch.nightBrightness !== undefined) parts.push(`${patch.nightBrightness}%`);
  const what = patch.night ? `Night mode on${parts.length ? ` (${parts.join(', ')})` : ''}` : 'Night mode off';
  const out = [`${what} on ${done} gadget(s).`, ...problems].join('\n');
  return done ? ok(out) : fail(1, out);
}

// ---- rename: the gadget's name (screen greeting, /miblo commands, mDNS instance) ----
// Firmware contract (POST /api/config {name}): at most 20 characters and under 64 UTF-8 bytes;
// "" restores the default Miblo-XXXX; anything else answers 400 {"field":"name"}.
export const NAME_MAX_CHARS = 20;
export const NAME_MAX_BYTES = 63;
const RENAME_USAGE = 'Usage: rename <id> <name...>  (or: rename <id> --default)';

// -> { id, name } (name "" = back to the default) or { error }. The name words are joined with
// spaces; control characters (and bidi overrides) are dropped, whitespace is collapsed.
// Words typed by the user -> one name: joined with spaces, control characters (and bidi
// overrides) dropped, whitespace collapsed.
const typedName = (words) => words.join(' ')
  .replace(/[\p{Cc}‎‏‪-‮⁦-⁩]/gu, ' ')
  .replace(/\s+/g, ' ')
  .trim();

// -> an error message, or null when the firmware will take this (non-empty) name.
function nameTooLong(name, what) {
  const chars = [...name].length;
  if (chars > NAME_MAX_CHARS) return `The ${what} is too long: ${chars} characters, the maximum is ${NAME_MAX_CHARS}.`;
  if (Buffer.byteLength(name, 'utf8') > NAME_MAX_BYTES) {
    return `The ${what} is too long for the gadget (${NAME_MAX_BYTES} bytes at most): use fewer emoji or accented characters.`;
  }
  return null;
}

// -> { id, name } (name "" = back to the default) or { error }.
export function parseRenameArgs(args) {
  const [id, ...words] = args;
  if (!id || !words.length) return { error: RENAME_USAGE };
  if (words.length === 1 && words[0] === '--default') return { id, name: '' };
  const name = typedName(words);
  if (!name) return { error: `The name is empty.\n${RENAME_USAGE}` };
  const tooLong = nameTooLong(name, 'name');
  return tooLong ? { error: tooLong } : { id, name };
}

// "miblo-4f2a" -> "Miblo-4F2A", the firmware's default name (used only if the gadget cannot be
// asked after a reset to the default).
const defaultNameFor = (id) => {
  const m = /^miblo-([0-9a-f]{4})$/i.exec(String(id));
  return m ? `Miblo-${m[1].toUpperCase()}` : null;
};

async function rename(args, store, client) {
  const ok = (out) => ({ code: 0, out: out + '\n' });
  const fail = (code, out) => ({ code, out: out + '\n' });
  const parsed = parseRenameArgs(args);
  if (parsed.error) return fail(2, parsed.error);
  const d = store.list().find((x) => x.id === parsed.id);
  if (!d) return fail(2, `No paired gadget with id ${cleanId(parsed.id)}.`);
  const oldLabel = cleanName(d.name) || cleanId(d.id).slice(0, 20);
  try {
    await client.setConfig(d.addr, d.token, { name: parsed.name });
  } catch (e) {
    if (e.status === 400 && e.data?.field === 'name') {
      return fail(2, `${oldLabel} rejected the name: use at most ${NAME_MAX_CHARS} characters.`);
    }
    if (e.status === 400) return fail(1, `${oldLabel} does not support renaming yet (update its firmware).`);
    if (e.status === 401) return fail(1, `${oldLabel} no longer accepts this pairing: run /miblo:pair again.`);
    if (isBusy(e)) return fail(1, busyLine(oldLabel));
    return fail(1, `Could not reach ${oldLabel}.`);
  }
  let name = parsed.name;
  let shown = name;  // typed by the user, already stripped of control characters
  if (!name) {
    // Back to the default: store the name the gadget now reports.
    let reported = '';
    try { reported = cleanName((await client.info(d.addr, d.token))?.name); } catch { /* best-effort */ }
    name = reported || defaultNameFor(d.id) || d.name;
    shown = cleanName(name) || cleanId(d.id).slice(0, 20);
  }
  store.update(d.id, { name });
  return ok(`Renamed ${oldLabel} to ${shown}.`);
}

// ---- owner: the owner's first name and birthday (greetings on the gadget screen) ----
// Firmware contract (POST /api/config): owner = name like the device name (20 characters,
// under 64 bytes), birthday = "MM-DD" (02-29 allowed); "" clears either. Neither is reported by
// /api/info (not even with a token), and the plugin does not store them.
const OWNER_USAGE = 'Usage: owner <id> [--name <name...>|--name clear] [--birthday <DD/MM|MM-DD|clear>]';
const DAYS_IN_MONTH = [31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31];

// "DD/MM" (day first) or "MM-DD" -> "MM-DD", "clear" -> "", else null.
export function parseBirthday(s) {
  const v = String(s ?? '').trim();
  if (v.toLowerCase() === 'clear') return '';
  let m = /^(\d{1,2})\/(\d{1,2})$/.exec(v);
  let month;
  let day;
  if (m) [day, month] = [Number(m[1]), Number(m[2])];
  else if ((m = /^(\d{2})-(\d{2})$/.exec(v))) [month, day] = [Number(m[1]), Number(m[2])];
  else return null;
  if (month < 1 || month > 12 || day < 1 || day > DAYS_IN_MONTH[month - 1]) return null;
  return `${String(month).padStart(2, '0')}-${String(day).padStart(2, '0')}`;
}

// -> { id, patch } or { error }. --name takes every word up to the next flag.
export function parseOwnerArgs(args) {
  const [id, ...rest] = args;
  if (!id || id.startsWith('--')) return { error: OWNER_USAGE };
  const patch = {};
  for (let i = 0; i < rest.length;) {
    const flag = rest[i++];
    const words = [];
    while (i < rest.length && !rest[i].startsWith('--')) words.push(rest[i++]);
    if (flag === '--name') {
      if ('owner' in patch) return { error: `--name given twice.\n${OWNER_USAGE}` };
      const clear = words.length === 1 && words[0].toLowerCase() === 'clear';
      const name = clear ? '' : typedName(words);
      if (!clear && !name) return { error: `--name needs a name (or "clear").\n${OWNER_USAGE}` };
      const tooLong = name && nameTooLong(name, 'name');
      if (tooLong) return { error: tooLong };
      patch.owner = name;
    } else if (flag === '--birthday') {
      if ('birthday' in patch) return { error: `--birthday given twice.\n${OWNER_USAGE}` };
      if (words.length !== 1) return { error: `--birthday needs one date, like 14/03 (day/month) or 03-14 (month-day), or "clear".\n${OWNER_USAGE}` };
      const birthday = parseBirthday(words[0]);
      if (birthday === null) {
        return { error: `Invalid birthday "${String(words[0]).slice(0, 12)}": use DD/MM (day first, e.g. 14/03) or MM-DD (e.g. 03-14), a real calendar day.` };
      }
      patch.birthday = birthday;
    } else {
      return { error: `Unexpected argument "${String(flag).slice(0, 40)}".\n${OWNER_USAGE}` };
    }
  }
  if (!('owner' in patch) && !('birthday' in patch)) return { error: `Give --name, --birthday or both.\n${OWNER_USAGE}` };
  return { id, patch };
}

async function owner(args, store, client) {
  const ok = (out) => ({ code: 0, out: out + '\n' });
  const fail = (code, out) => ({ code, out: out + '\n' });
  const parsed = parseOwnerArgs(args);
  if (parsed.error) return fail(2, parsed.error);
  const d = store.list().find((x) => x.id === parsed.id);
  if (!d) return fail(2, `No paired gadget with id ${cleanId(parsed.id)}.`);
  const label = cleanName(d.name) || cleanId(d.id).slice(0, 20);
  const { patch } = parsed;
  try {
    await client.setConfig(d.addr, d.token, patch);
  } catch (e) {
    const field = e.status === 400 ? String(e.data?.field ?? '') : '';
    if (field === 'owner') return fail(2, `${label} rejected the name: use at most ${NAME_MAX_CHARS} characters.`);
    if (field === 'birthday') return fail(2, `${label} rejected the birthday: use a real day, DD/MM or MM-DD.`);
    if (e.status === 400) return fail(1, `${label} does not support names and birthdays yet (update its firmware).`);
    if (e.status === 401) return fail(1, `${label} no longer accepts this pairing: run /miblo:pair again.`);
    if (isBusy(e)) return fail(1, busyLine(label));
    return fail(1, `Could not reach ${label}.`);
  }
  const knows = [];
  const forgot = [];
  // The owner name was typed by the user and is already stripped of control characters.
  if ('owner' in patch) (patch.owner ? knows.push(`name (${patch.owner})`) : forgot.push('name'));
  if ('birthday' in patch) (patch.birthday ? knows.push(`birthday (${patch.birthday})`) : forgot.push('birthday'));
  const parts = [];
  if (knows.length) parts.push(`now knows your ${knows.join(' and ')}`);
  if (forgot.length) parts.push(`forgot your ${forgot.join(' and ')}`);
  return ok(`${label} ${parts.join(' and ')}.`);
}

// ---- demo: pet mode right away, to show it off or test visits between Miblos ----
export const DEMO_MINUTES = { min: 1, max: 30, default: 10 };

async function demo(args, store, client) {
  const ok = (out) => ({ code: 0, out: out + '\n' });
  const fail = (code, out) => ({ code, out: out + '\n' });
  let minutes = DEMO_MINUTES.default;
  for (const a of args) {
    if (a === 'stop' || a === 'off') minutes = 0;
    else if (/^\d+$/.test(a)) minutes = Number(a);
    else return fail(2, 'Usage: demo [minutes|stop]');
  }
  if (minutes !== 0 && (minutes < DEMO_MINUTES.min || minutes > DEMO_MINUTES.max)) {
    return fail(2, `Minutes must be from ${DEMO_MINUTES.min} to ${DEMO_MINUTES.max} (got ${minutes}).`);
  }
  // The demo is about Miblos visiting each other: it needs at least two, and runs on all of them.
  const targets = store.list();
  if (minutes && targets.length < 2) {
    return fail(2, `The demo needs at least two paired Miblos (${targets.length} paired). Pair another one with /miblo:pair.`);
  }
  const done = [];
  const problems = [];
  for (const d of targets) {
    const label = cleanName(d.name) || cleanId(d.id);
    try {
      await client.demo(d.addr, d.token, minutes);
      done.push(label);
    } catch (e) {
      if (e.status === 404) problems.push(`${label} does not support the demo yet (update its firmware with /miblo:update).`);
      else if (e.status === 401) problems.push(`${label} no longer knows this computer (run /miblo:pair again).`);
      else problems.push(isBusy(e) ? busyLine(label) : `${label} is offline.`);
    }
  }
  const what = minutes ? `Demo on for ${minutes} min` : 'Demo off';
  const lines = done.length ? [`${what} on ${done.join(', ')}.`] : [];
  const out = [...lines, ...problems].join('\n');
  return done.length ? ok(out) : fail(1, out);
}

export async function run(argv, deps) {
  const { dataDir, pluginRoot, settingsPath, client, discoverFn, hostname, fetchStatus, locale } = deps;
  const store = new DeviceStore(dataDir);
  const [cmd, ...args] = argv;
  const ok = (out) => ({ code: 0, out: out + '\n' });
  const fail = (code, out) => ({ code, out: out + '\n' });

  switch (cmd) {
    case 'discover': {
      const found = (await discoverFn()).map(safe).filter((d) => d.id);
      if (!found.length) return ok('No Miblo gadgets found on this network.');
      return ok(found.map((d) => `${d.id}\t${d.name}\t${d.addr}`).join('\n'));
    }
    case 'pair': {
      const [rawAddr, code] = args;
      if (!rawAddr || !code) return fail(2, USAGE);
      const addr = withPort(rawAddr);
      try {
        // A gadget already paired to another computer reports only {id, paired, proto} until we
        // hold a token: read it again once paired; if that fails, use its id-derived name.
        const first = (await client.info(addr)) ?? {};
        if (!cleanId(first.id)) return fail(1, `The device at ${cleanAddr(addr)} did not report a valid id.`);
        let token;
        try {
          token = await client.pair(addr, code, hostname);
        } catch (e) {
          // No answer at all: the gadget may have paired and rotated its code (the reply was
          // lost), so it is not resent; a fresh code tells which.
          if (!e.status) {
            return fail(1, `No answer from the Miblo gadget at ${cleanAddr(addr)} while pairing. If it now says it is paired, show a new pairing code on it and pair again.`);
          }
          throw e;
        }
        let full = first;
        if (isReducedInfo(first)) {
          try { full = { ...((await client.info(addr, token)) ?? {}), id: first.id }; } catch { /* best-effort */ }
        }
        const info = safe({ ...full, name: cleanName(full.name) || defaultNameFor(first.id) });
        store.upsert({ id: info.id, name: info.name, addr, token });
        if (!isReducedInfo(full) && full.langSet !== true) {
          // Best-effort: the language was never chosen explicitly (automatic mode, or a
          // firmware before 0.2.3 that does not report it), so seed it from the host's
          // locale. A language picked on the settings page is never overwritten. Never let
          // this fail the pair itself.
          const lang = mapLocaleToLang(locale);
          if (lang) {
            try { await client.setConfig(addr, token, { lang }); } catch { /* best-effort */ }
          }
        }
        return ok(`Paired with ${info.name} (${info.id}) at ${addr}.`);
      } catch (e) {
        if (e.status === 429) {
          const secs = Math.max(1, Math.ceil(Number(e.data?.retryAfter ?? 60)));
          return fail(2, `Too many wrong codes. Try again in ${secs} s.`);
        }
        if (e.status === 403) return fail(2, 'Wrong pairing code.');
        if (isBusy(e)) return fail(1, busyLine(`The Miblo gadget at ${cleanAddr(addr)}`));
        return fail(1, `Could not reach a Miblo gadget at ${addr}.`);
      }
    }
    case 'status': {
      const live = await fetchStatus();
      let statusline;
      try {
        statusline = isLinked({ settingsPath }) ? 'linked' : 'not linked';
      } catch {
        statusline = 'settings.json unreadable';
      }
      const devices = (live?.devices ?? store.list().map(({ id, name, addr }) => ({ id, name, addr, online: null }))).map(safe);
      return ok(JSON.stringify({
        bridge: live ? 'running' : 'stopped',
        statusline,
        statuslineSeen: live?.statuslineSeen ?? false,
        devices,
        sessions: live?.sessions ?? [],
        usage: live?.usage ?? null,
        today: live?.today ?? null,
      }, null, 2));
    }
    case 'mode': {
      const [mode, id] = args;
      if (!MODES.includes(mode)) return fail(2, `Mode must be one of: ${MODES.join(', ')}.`);
      const targets = store.list().filter((d) => !id || d.id === id);
      let done = 0;
      const busy = [];
      for (const d of targets) {
        try {
          await client.setConfig(d.addr, d.token, { mode });
          done++;
        } catch (e) {
          // gadget offline: reflected in the count; a busy one is named
          if (isBusy(e)) busy.push(busyLine(cleanName(d.name)));
        }
      }
      return ok([`Mode set to ${mode} on ${done} gadget(s).`, ...busy].join('\n'));
    }
    case 'rotate':
      return rotate(args, store, client);
    case 'night':
      return night(args, store, client);
    case 'settings':
      return settings(args, store, deps.openUrl ?? openInBrowser);
    case 'rename':
      return rename(args, store, client);
    case 'owner':
      return owner(args, store, client);
    case 'demo':
      return demo(args, store, client);
    case 'reset': {
      const d = store.list().find((x) => x.id === args[0]);
      if (!d) return fail(2, `No paired gadget with id ${cleanId(args[0])}.`);
      try {
        await client.reset(d.addr, d.token);
      } catch (e) {
        if (isBusy(e)) return fail(1, busyLine(cleanName(d.name)));
        return fail(1, `Could not reach ${cleanName(d.name)}.`);
      }
      store.remove(d.id);
      return ok(`Factory reset sent to ${cleanName(d.name)}.`);
    }
    case 'update':
      return new FirmwareUpdater({ store, dataDir, pluginVersion: pluginVersion(), ...deps.updater }).run(args);
    case 'link-statusline': {
      const r = link({ settingsPath, pluginRoot, dataDir });
      return ok(r.changed ? 'Statusline linked.' : 'Statusline already linked.');
    }
    case 'unlink-statusline': {
      const r = unlink({ settingsPath });
      return ok(r.changed ? 'Statusline unlinked.' : 'Statusline was not linked.');
    }
    default:
      if (Object.hasOwn(DAILY_COMMANDS, cmd)) {
        return DAILY_COMMANDS[cmd](args, { store, client, fetchStatus, now: deps.now ?? Date.now });
      }
      return fail(2, USAGE);
  }
}

// For tests that run the CLI as a separate process (the installer's): with MIBLO_TEST=1,
// MIBLO_DISCOVER_JSON holds the list `discover` reports instead of asking the network. Otherwise,
// unset or not a JSON array, mDNS is used. It only changes what is listed: pairing still needs
// the code shown on the gadget.
export function fixedDiscovery(env) {
  if (env.MIBLO_TEST !== '1' || !env.MIBLO_DISCOVER_JSON) return null;
  let list;
  try {
    list = JSON.parse(env.MIBLO_DISCOVER_JSON);
  } catch {
    return null;
  }
  return Array.isArray(list) ? async () => list : null;
}

async function main() {
  let parsed;
  try {
    parsed = parseDataArg(process.argv.slice(2));
  } catch (e) {
    process.stdout.write(`Error: ${e.message}\n`);
    process.exitCode = 2;
    return;
  }
  const { dataDir, rest: argv } = parsed;
  const here = path.dirname(fileURLToPath(import.meta.url));
  const r = await run(argv, {
    dataDir,
    pluginRoot: path.resolve(here, '..'),
    settingsPath: claudeSettingsPath(),
    client: new DeviceClient(),
    discoverFn: fixedDiscovery(process.env) ?? (() => discover()),
    hostname: os.hostname(),
    fetchStatus: defaultFetchStatus,
    locale: Intl.DateTimeFormat().resolvedOptions().locale,
    openUrl: (url) => openInBrowser(url),
  });
  process.stdout.write(r.out);
  process.exitCode = r.code;
}

if (isMain(import.meta.url)) {
  main().catch((e) => {
    process.stdout.write(`Error: ${e.message}\n`);
    process.exitCode = 1;
  });
}

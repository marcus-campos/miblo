#!/usr/bin/env node
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { PORT, HOST, claudeSettingsPath, parseDataArg, pluginVersion } from '../lib/constants.js';
import { DeviceClient } from '../lib/device-client.js';
import { DeviceStore } from '../lib/device-store.js';
import { discover, cleanId, cleanName } from '../lib/mdns.js';
import { link, unlink, isLinked } from '../lib/statusline-link.js';
import { FirmwareUpdater } from '../lib/firmware-update.js';

const MODES = ['overview', 'limits', 'sessions'];
const USAGE = [
  'Usage: miblo.js --data <dir> <command>',
  '  discover',
  '  pair <ip[:port]> <code>',
  '  status',
  '  mode <overview|limits|sessions> [id]',
  '  rotate <on|off> [every-seconds] [show-seconds] [id] | rotate --status [id]',
  '  reset <id>',
  '  update [check|open|send] [id] [code] [--file path] [--check]',
  '  link-statusline | unlink-statusline',
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
        const info = await client.info(d.addr);
        if (typeof info?.rotate !== 'boolean') lines.push(`${label}: rotation not supported by this firmware (update it)`);
        else lines.push(`${label}: rotation ${describeRotation(info)}`);
      } catch {
        lines.push(`${label}: offline`);
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
        problems.push(`${label} is offline.`);
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
        const info = safe(await client.info(addr));
        if (!info.id) return fail(1, `The device at ${cleanAddr(addr)} did not report a valid id.`);
        const token = await client.pair(addr, code, hostname);
        store.upsert({ id: info.id, name: info.name, addr, token });
        if (info.langSet !== true) {
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
      }, null, 2));
    }
    case 'mode': {
      const [mode, id] = args;
      if (!MODES.includes(mode)) return fail(2, `Mode must be one of: ${MODES.join(', ')}.`);
      const targets = store.list().filter((d) => !id || d.id === id);
      let done = 0;
      for (const d of targets) {
        try {
          await client.setConfig(d.addr, d.token, { mode });
          done++;
        } catch {
          // gadget offline: reflected in the count
        }
      }
      return ok(`Mode set to ${mode} on ${done} gadget(s).`);
    }
    case 'rotate':
      return rotate(args, store, client);
    case 'reset': {
      const d = store.list().find((x) => x.id === args[0]);
      if (!d) return fail(2, `No paired gadget with id ${cleanId(args[0])}.`);
      try {
        await client.reset(d.addr, d.token);
      } catch {
        return fail(1, `Could not reach ${cleanName(d.name)}.`);
      }
      store.remove(d.id);
      return ok(`Factory reset sent to ${cleanName(d.name)}.`);
    }
    case 'update':
      return new FirmwareUpdater({ store, dataDir, pluginVersion: pluginVersion(), ...deps.updater }).run(args);
    case 'link-statusline': {
      const r = link({ settingsPath, pluginRoot });
      return ok(r.changed ? 'Statusline linked.' : 'Statusline already linked.');
    }
    case 'unlink-statusline': {
      const r = unlink({ settingsPath });
      return ok(r.changed ? 'Statusline unlinked.' : 'Statusline was not linked.');
    }
    default:
      return fail(2, USAGE);
  }
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
    discoverFn: () => discover(),
    hostname: os.hostname(),
    fetchStatus: defaultFetchStatus,
    locale: Intl.DateTimeFormat().resolvedOptions().locale,
  });
  process.stdout.write(r.out);
  process.exitCode = r.code;
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  main().catch((e) => {
    process.stdout.write(`Error: ${e.message}\n`);
    process.exitCode = 1;
  });
}

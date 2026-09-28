#!/usr/bin/env node
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { PORT, HOST, claudeSettingsPath, parseDataArg } from '../lib/constants.js';
import { DeviceClient } from '../lib/device-client.js';
import { DeviceStore } from '../lib/device-store.js';
import { discover, cleanId, cleanName } from '../lib/mdns.js';
import { link, unlink, isLinked } from '../lib/statusline-link.js';

const MODES = ['overview', 'limits', 'sessions'];
const USAGE = [
  'Usage: miblo.js --data <dir> <command>',
  '  discover',
  '  pair <ip[:port]> <code>',
  '  status',
  '  mode <overview|limits|sessions> [id]',
  '  reset <id>',
  '  link-statusline | unlink-statusline',
].join('\n');

const cleanAddr = (s) => String(s ?? '').replace(/[^A-Za-z0-9.:[\]-]/g, '').slice(0, 64);
const safe = (d) => ({ ...d, id: cleanId(d.id), name: cleanName(d.name) || cleanId(d.id).slice(0, 20), ...(d.addr !== undefined ? { addr: cleanAddr(d.addr) } : {}) });

const withPort = (addr) => (String(addr).includes(':') ? String(addr) : `${addr}:80`);

async function defaultFetchStatus() {
  try {
    const res = await fetch(`http://${HOST}:${PORT}/status`, { signal: AbortSignal.timeout(800) });
    return res.ok ? await res.json() : null;
  } catch {
    return null;
  }
}

export async function run(argv, deps) {
  const { dataDir, pluginRoot, settingsPath, client, discoverFn, hostname, fetchStatus } = deps;
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
        return ok(`Paired with ${info.name} (${info.id}) at ${addr}.`);
      } catch (e) {
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

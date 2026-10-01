#!/usr/bin/env node
// Entry point of every hook. It must never delay or break Claude Code:
// runs with "async": true, catches everything, always exits 0 and never writes to stdout.
import { spawn } from 'node:child_process';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { PORT, HOST, defaultDataDir, pluginVersion } from '../lib/constants.js';
import { findClaudePid } from '../lib/proc.js';
import { pickEvent, wantsPid, deliver } from '../lib/hook-client.js';

// Hard cap: a hook must never hang Claude Code.
setTimeout(() => process.exit(0), 3000).unref();

const here = path.dirname(fileURLToPath(import.meta.url));
const base = `http://${HOST}:${PORT}`;

async function readStdin() {
  const chunks = [];
  for await (const c of process.stdin) chunks.push(c);
  return Buffer.concat(chunks).toString('utf8');
}

async function postJson(p, body, timeoutMs = 800) {
  const res = await fetch(base + p, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body,
    signal: AbortSignal.timeout(timeoutMs),
  });
  const text = await res.text().catch(() => '');
  if (!res.ok) {
    const err = new Error(`bridge ${res.status}`);
    err.status = res.status;
    throw err;
  }
  try {
    return JSON.parse(text) ?? {};
  } catch {
    return {};
  }
}

async function health() {
  let res;
  try {
    res = await fetch(`${base}/health`, { signal: AbortSignal.timeout(300) });
  } catch {
    return null;
  }
  try {
    return (await res.json()) ?? {};
  } catch {
    return {};
  }
}

function startBridge() {
  try {
    const child = spawn(process.execPath, [path.join(here, 'bridge.js'), '--data', defaultDataDir()], {
      cwd: os.homedir(),
      detached: true,
      stdio: 'ignore',
      windowsHide: true,
    });
    child.on('error', () => {});
    child.unref();
  } catch {}
}

async function main() {
  const evt = pickEvent(JSON.parse((await readStdin()) || '{}'));
  if (wantsPid(evt)) evt.pid = findClaudePid();
  await deliver(
    JSON.stringify(evt),
    {
      post: (body) => postJson('/event', body),
      health,
      shutdown: () => postJson('/shutdown', '{}', 300),
      startBridge,
      sleep: (ms) => new Promise((r) => setTimeout(r, ms)),
    },
    {
      allowSpawn: process.env.MIBLO_NO_SPAWN !== '1',
      checkVersion: evt.hook_event_name === 'SessionStart',  // spare the old bridge a session it would drop
      version: pluginVersion(),
    },
  );
}

main()
  .catch(() => {})
  .finally(() => process.exit(0));

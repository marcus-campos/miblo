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
import { ensureKey, checkedHealth, signedFetch, logForeignOnce } from '../lib/bridge-auth.js';

// Hard cap: a hook must never hang Claude Code.
setTimeout(() => process.exit(0), 3000).unref();

const here = path.dirname(fileURLToPath(import.meta.url));
const base = `http://${HOST}:${PORT}`;
const dataDir = defaultDataDir();
// The bridge key (bridge-auth.js): never sent; a request goes only to a bridge that proved it
// knows it, signed over the single-use challenge from that bridge's last /health answer.
const key = ensureKey(dataDir);
let challenge = null;

async function health() {
  const h = await checkedHealth(base, key);
  challenge = h?.challenge ?? null;
  return h;
}

async function readStdin() {
  const chunks = [];
  for await (const c of process.stdin) chunks.push(c);
  return Buffer.concat(chunks).toString('utf8');
}

// `signed`: answer the bridge's challenge (only to a bridge that proved it knows the key); an
// older bridge, before the key, is asked to shut down unsigned.
async function postJson(p, body, { timeoutMs = 800, signed = true } = {}) {
  const c = challenge;
  challenge = null;  // single use
  let res;
  if (signed) {
    if (!c) throw new Error('no proven bridge');
    res = await signedFetch(base, key, c, { method: 'POST', path: p, body, timeoutMs });
  } else {
    res = await fetch(base + p, { method: 'POST', headers: { 'content-type': 'application/json' }, body, signal: AbortSignal.timeout(timeoutMs) });
  }
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

function startBridge() {
  try {
    const child = spawn(process.execPath, [path.join(here, 'bridge.js'), '--data', dataDir], {
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
      // Signed only to our own bridge; an older one (before the key) shuts down unsigned.
      shutdown: (signed) => postJson('/shutdown', '{}', { timeoutMs: 300, signed }),
      startBridge,
      foreign: () => logForeignOnce(dataDir, PORT),
      sleep: (ms) => new Promise((r) => setTimeout(r, ms)),
    },
    {
      allowSpawn: process.env.MIBLO_NO_SPAWN !== '1',
      version: pluginVersion(),
    },
  );
}

main()
  .catch(() => {})
  .finally(() => process.exit(0));

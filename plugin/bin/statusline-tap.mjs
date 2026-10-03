#!/usr/bin/env node
// Self-contained: copied to <claudeConfigDir>/miblo/ and referenced from
// settings.json, so it keeps working after the plugin is uninstalled.
// Imports nothing from lib/.
import { spawn } from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const port = Number(process.env.MIBLO_PORT || 47821);
// The plugin's data dir: `--data <dir>` (as the launcher passes it), else as the plugin finds it.
const dataArg = process.argv.indexOf('--data');
const dataDir = (dataArg > 1 && process.argv[dataArg + 1]) || process.env.CLAUDE_PLUGIN_DATA || path.join(os.homedir(), '.miblo');

async function readStdin() {
  const chunks = [];
  for await (const c of process.stdin) chunks.push(c);
  return Buffer.concat(chunks);
}

function loadOriginal() {
  try {
    return JSON.parse(fs.readFileSync(path.join(here, 'statusline-original.json'), 'utf8'));
  } catch {
    return null;
  }
}

// The bridge key (lib/bridge-auth.js, repeated here: this file imports nothing from lib/): the
// status line goes only to a bridge that proves it knows it, so another user holding the port on
// a shared computer gets nothing; the key itself is never sent. Null when there is none or it is
// not plainly ours.
function readKey() {
  try {
    const file = path.join(dataDir, 'bridge.key');
    const st = fs.statSync(file);
    if (!st.isFile() || (typeof process.getuid === 'function' && st.uid !== process.getuid())) return null;
    const key = fs.readFileSync(file, 'utf8').trim();
    return /^[0-9a-f]{64}$/.test(key) ? key : null;
  } catch {
    return null;
  }
}

function proven(key, nonce, proof) {
  const want = crypto.createHmac('sha256', key).update(`miblo-bridge:${nonce}`).digest('hex');
  return typeof proof === 'string' && /^[0-9a-f]{64}$/.test(proof) && crypto.timingSafeEqual(Buffer.from(proof, 'hex'), Buffer.from(want, 'hex'));
}

// Notes once a day, in <data>/bridge.log, that something else answers on the port.
function noteForeign() {
  try {
    const mark = path.join(dataDir, 'foreign-bridge');
    let last = 0;
    try { last = fs.statSync(mark).mtimeMs; } catch { /* never noted */ }
    if (Date.now() - last < 24 * 3600_000) return;
    fs.writeFileSync(mark, '');
    fs.appendFileSync(path.join(dataDir, 'bridge.log'),
      `${new Date().toISOString()} port ${port} answers without the bridge key: another program or user holds it; nothing was sent to it\n`);
  } catch {
    // best-effort
  }
}

async function forward(input) {
  const key = readKey();
  if (!key) return;
  try {
    const nonce = crypto.randomBytes(16).toString('hex');
    const health = await fetch(`http://127.0.0.1:${port}/health`, { headers: { 'x-miblo-nonce': nonce }, signal: AbortSignal.timeout(150) });
    await health.arrayBuffer().catch(() => {});
    if (!proven(key, nonce, health.headers.get('x-miblo-proof'))) {
      noteForeign();
      return;
    }
    // Our bridge, but at its challenges-per-second cap: skip this refresh (the next one comes soon).
    const challenge = health.headers.get('x-miblo-challenge');
    if (!/^[0-9a-f]{32}$/.test(challenge ?? '')) return;
    // Signed over the bridge's single-use challenge; the key itself never travels.
    const digest = crypto.createHash('sha256').update(input).digest('hex');
    const mac = crypto.createHmac('sha256', key).update(`miblo-req:${challenge}:POST:/statusline:${digest}`).digest('hex');
    const res = await fetch(`http://127.0.0.1:${port}/statusline`, {
      method: 'POST',
      headers: { 'content-type': 'application/json', 'x-miblo-auth': `${challenge}:${mac}` },
      body: input,
      signal: AbortSignal.timeout(150),
    });
    await res.arrayBuffer().catch(() => {});
  } catch {
    // bridge down or slow: the status line must not care
  }
}

function preferredShell() {
  if (process.platform === 'win32') return process.env.CLAUDE_CODE_GIT_BASH_PATH || 'bash';
  return process.env.SHELL || true;
}

// Resolves with the exit status, or null when the command could not be spawned
// or was killed by a signal. `spawnFailed` tells the caller a fallback may help.
function runWith(command, input, shell) {
  return new Promise((resolve) => {
    let child;
    try {
      child = spawn(command, { shell, stdio: ['pipe', 'inherit', 'inherit'], windowsHide: true });
    } catch {
      resolve({ status: null, spawnFailed: true });
      return;
    }
    let spawned = false;
    child.on('spawn', () => { spawned = true; });
    child.on('error', () => resolve({ status: null, spawnFailed: !spawned }));
    child.on('close', (status) => resolve({ status, spawnFailed: false }));
    child.stdin.on('error', () => {});
    child.stdin.end(input);
  });
}

async function runOriginal(command, input) {
  const r = await runWith(command, input, preferredShell());
  if (r.spawnFailed && process.platform === 'win32') return (await runWith(command, input, true)).status;
  return r.status;
}

async function main() {
  const input = await readStdin();
  const sent = forward(input);
  const original = loadOriginal();
  const status = original?.command ? await runOriginal(original.command, input) : 0;
  await sent;
  process.exit(status ?? 1);
}

main().catch(() => process.exit(1));

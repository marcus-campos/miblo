#!/usr/bin/env node
// Self-contained: copied to <claudeConfigDir>/miblo/ and referenced from
// settings.json, so it keeps working after the plugin is uninstalled.
// Imports nothing from lib/.
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const port = Number(process.env.MIBLO_PORT || 47821);

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

async function forward(input) {
  try {
    const res = await fetch(`http://127.0.0.1:${port}/statusline`, {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
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

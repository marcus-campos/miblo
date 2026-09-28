#!/usr/bin/env node
// Entrada de todos os hooks. Nunca pode atrasar nem quebrar o Claude Code:
// roda com "async": true, captura tudo, sai sempre com 0 e não escreve em stdout.
import { spawn } from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { PORT, HOST } from '../lib/constants.js';
import { findClaudePid } from '../lib/proc.js';

// Hard cap: a hook must never hang Claude Code.
setTimeout(() => process.exit(0), 3000).unref();

const here = path.dirname(fileURLToPath(import.meta.url));

async function readStdin() {
  const chunks = [];
  for await (const c of process.stdin) chunks.push(c);
  return Buffer.concat(chunks).toString('utf8');
}

async function post(body) {
  const res = await fetch(`http://${HOST}:${PORT}/event`, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body,
    signal: AbortSignal.timeout(800),
  });
  if (!res.ok) throw new Error(`bridge ${res.status}`);
}

function startBridge() {
  try {
    const dataDir = process.env.CLAUDE_PLUGIN_DATA || path.join(here, '..', '.data');
    const child = spawn(process.execPath, [path.join(here, 'bridge.js'), '--data', dataDir], {
      detached: true,
      stdio: 'ignore',
      windowsHide: true,
    });
    child.on('error', () => {});
    child.unref();
  } catch {}
}

async function main() {
  const evt = JSON.parse((await readStdin()) || '{}');
  if (evt.hook_event_name === 'SessionStart') evt.pid = findClaudePid();
  const body = JSON.stringify(evt);
  try {
    await post(body);
  } catch {
    if (process.env.MIBLO_NO_SPAWN === '1') return;
    startBridge();
    await new Promise((r) => setTimeout(r, 400));
    await post(body).catch(() => {});
  }
}

main()
  .catch(() => {})
  .finally(() => process.exit(0));

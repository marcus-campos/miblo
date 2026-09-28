#!/usr/bin/env node
// Autossuficiente: é copiado para o diretório de dados do plugin e referenciado
// em ~/.claude/settings.json. Não importa nada de lib/.
import { spawnSync } from 'node:child_process';
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

async function main() {
  const input = await readStdin();
  const forward = fetch(`http://127.0.0.1:${port}/statusline`, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: input,
    signal: AbortSignal.timeout(200),
  }).catch(() => {});

  const original = loadOriginal();
  if (original?.command) {
    const r = spawnSync(original.command, { shell: true, input, maxBuffer: 1024 * 1024 });
    if (r.stdout?.length) process.stdout.write(r.stdout);
    if (r.stderr?.length) process.stderr.write(r.stderr);
    process.exitCode = r.status ?? 0;
  }
  await forward;
}

main().catch(() => {});

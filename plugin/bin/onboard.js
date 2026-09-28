#!/usr/bin/env node
// Synchronous SessionStart: reminds, at most once per 24 h, to pair a gadget.
import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { DeviceStore } from '../lib/device-store.js';

const DAY_MS = 24 * 3600_000;

export function onboardMessage({ store, stampFile, now = () => Date.now() }) {
  if (store.list().length > 0) return null;
  let last = -Infinity;
  try {
    last = JSON.parse(fs.readFileSync(stampFile, 'utf8')).last;
  } catch {
    last = -Infinity;
  }
  if (now() - last <= DAY_MS) return null;
  fs.mkdirSync(path.dirname(stampFile), { recursive: true });
  fs.writeFileSync(stampFile, JSON.stringify({ last: now() }));
  return JSON.stringify({ systemMessage: 'Miblo: no desk gadget paired yet. Run /miblo:pair to connect it.' });
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  try {
    const dataDir = process.env.CLAUDE_PLUGIN_DATA;
    if (dataDir) {
      const msg = onboardMessage({ store: new DeviceStore(dataDir), stampFile: path.join(dataDir, 'onboard.json') });
      if (msg) process.stdout.write(msg);
    }
  } catch {
    // never get in the way of the session start
  }
  process.exitCode = 0;
}

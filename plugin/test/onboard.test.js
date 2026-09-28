import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { onboardMessage } from '../bin/onboard.js';

function setup(devices = []) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'miblo-onb-'));
  return { store: { list: () => devices }, stampFile: path.join(dir, 'onboard.json') };
}

test('nags once per 24h while nothing is paired', () => {
  const s = setup();
  const first = onboardMessage({ ...s, now: () => 0 });
  assert.match(JSON.parse(first).systemMessage, /\/miblo:pair/);
  assert.equal(onboardMessage({ ...s, now: () => 3600_000 }), null);
  assert.ok(onboardMessage({ ...s, now: () => 24 * 3600_000 + 1 }));
});

test('silent when a gadget is paired', () => {
  assert.equal(onboardMessage({ ...setup([{ id: 'g' }]), now: () => 0 }), null);
});

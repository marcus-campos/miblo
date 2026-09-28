import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { createLogger, errText } from '../lib/logger.js';

const tmp = () => fs.mkdtempSync(path.join(os.tmpdir(), 'miblo-log-'));

test('appends timestamped single-line entries, creating the directory', () => {
  const file = path.join(tmp(), 'nested', 'bridge.log');
  const log = createLogger(file, { now: () => new Date(Date.UTC(2026, 8, 28)) });
  log('first');
  log('second\nline');
  assert.equal(fs.readFileSync(file, 'utf8'), '2026-09-28T00:00:00.000Z first\n2026-09-28T00:00:00.000Z second line\n');
});

test('rotates to .1 once the file exceeds the limit', () => {
  const file = path.join(tmp(), 'bridge.log');
  const log = createLogger(file, { maxBytes: 100 });
  for (let i = 0; i < 5; i++) log('x'.repeat(40));
  assert.ok(fs.existsSync(`${file}.1`));
  assert.ok(fs.statSync(file).size <= 200);
  const before = fs.readFileSync(`${file}.1`, 'utf8');
  for (let i = 0; i < 5; i++) log('y'.repeat(40));
  assert.notEqual(fs.readFileSync(`${file}.1`, 'utf8'), before); // .1 replaced, never a .2
  assert.ok(!fs.existsSync(`${file}.2`));
});

test('never throws, even when the path is unwritable', () => {
  const dir = tmp();
  fs.writeFileSync(path.join(dir, 'file'), '');
  const log = createLogger(path.join(dir, 'file', 'bridge.log'));
  assert.doesNotThrow(() => log('x'));
});

test('errText keeps the error short', () => {
  assert.match(errText(new Error('boom')), /^Error: boom/);
  assert.equal(errText('plain'), 'plain');
});

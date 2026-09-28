import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { link, unlink, isLinked } from '../lib/statusline-link.js';

const pluginRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');

function setup(settings) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'miblo-link-'));
  const settingsPath = path.join(root, 'claude', 'settings.json');
  const tapDir = path.join(root, 'claude', 'miblo');
  if (settings !== undefined) {
    fs.mkdirSync(path.dirname(settingsPath), { recursive: true });
    fs.writeFileSync(settingsPath, JSON.stringify(settings, null, 2));
  }
  const read = () => JSON.parse(fs.readFileSync(settingsPath, 'utf8'));
  return { settingsPath, tapDir, read, opts: { settingsPath, pluginRoot } };
}

test('link wraps an existing statusLine and keeps other settings', () => {
  const s = setup({ theme: 'dark', statusLine: { type: 'command', command: 'bash ~/sl.sh', padding: 2 } });
  const r = link(s.opts);
  assert.equal(r.changed, true);
  const after = s.read();
  assert.equal(after.theme, 'dark');
  assert.equal(after.statusLine.padding, 2);
  assert.equal(after.statusLine.command, `node "${path.join(s.tapDir, 'statusline-tap.mjs').replace(/\\/g, '/')}"`);
  assert.ok(fs.existsSync(path.join(s.tapDir, 'statusline-tap.mjs')));
  assert.deepEqual(JSON.parse(fs.readFileSync(path.join(s.tapDir, 'statusline-original.json'), 'utf8')).command, 'bash ~/sl.sh');
  assert.ok(fs.existsSync(s.settingsPath + '.miblo-backup'));
  assert.equal(isLinked(s), true);
});

test('link is idempotent', () => {
  const s = setup({ statusLine: { type: 'command', command: 'x' } });
  link(s.opts);
  assert.equal(link(s.opts).changed, false);
  assert.equal(JSON.parse(fs.readFileSync(path.join(s.tapDir, 'statusline-original.json'), 'utf8')).command, 'x');
});

test('link works without settings.json or statusLine', () => {
  const s = setup(undefined);
  link(s.opts);
  assert.equal(s.read().statusLine.type, 'command');
  assert.deepEqual(JSON.parse(fs.readFileSync(path.join(s.tapDir, 'statusline-original.json'), 'utf8')), {});
});

test('unlink restores the original, or removes statusLine when there was none', () => {
  const a = setup({ statusLine: { type: 'command', command: 'orig', refreshInterval: 5 } });
  link(a.opts);
  assert.equal(unlink(a.opts).changed, true);
  assert.deepEqual(a.read().statusLine, { type: 'command', command: 'orig', refreshInterval: 5 });

  const b = setup({ other: 1 });
  link(b.opts);
  unlink(b.opts);
  assert.deepEqual(b.read(), { other: 1 });
  assert.equal(unlink(b.opts).changed, false);
});

test('refuses to touch a corrupt settings.json', () => {
  const s = setup(undefined);
  fs.mkdirSync(path.dirname(s.settingsPath), { recursive: true });
  fs.writeFileSync(s.settingsPath, '{broken');
  assert.throws(() => link(s.opts));
  assert.equal(fs.readFileSync(s.settingsPath, 'utf8'), '{broken');
});

test('the tap lives next to settings.json, not in the plugin data dir', () => {
  const s = setup({});
  link(s.opts);
  assert.equal(s.tapDir, path.join(path.dirname(s.settingsPath), 'miblo'));
  assert.ok(fs.existsSync(path.join(s.tapDir, 'statusline-tap.mjs')));
  assert.ok(!s.read().statusLine.command.includes('\\'));
});

test('an existing miblo tap is never saved as the original (no chained taps)', () => {
  const s = setup({ statusLine: { type: 'command', command: 'node "/old/place/statusline-tap.mjs"' } });
  const r = link(s.opts);
  assert.equal(r.changed, true);
  assert.equal(r.original, null);
  assert.deepEqual(JSON.parse(fs.readFileSync(path.join(s.tapDir, 'statusline-original.json'), 'utf8')), {});
  assert.equal(isLinked(s), true);
  unlink(s.opts);
  assert.equal(s.read().statusLine, undefined);
});

test('relinking over a foreign tap keeps a previously saved original', () => {
  const s = setup({ statusLine: { type: 'command', command: 'orig' } });
  link(s.opts);
  const settings = s.read();
  settings.statusLine.command = 'node "/elsewhere/statusline-tap.mjs"';
  fs.writeFileSync(s.settingsPath, JSON.stringify(settings));
  link(s.opts);
  unlink(s.opts);
  assert.equal(s.read().statusLine.command, 'orig');
});

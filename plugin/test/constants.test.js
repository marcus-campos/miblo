import { test } from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { defaultDataDir, parseDataArg, pluginVersion, isMain } from '../lib/constants.js';

const bin = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../bin');

test('defaultDataDir prefers CLAUDE_PLUGIN_DATA, else ~/.miblo', () => {
  const saved = process.env.CLAUDE_PLUGIN_DATA;
  try {
    process.env.CLAUDE_PLUGIN_DATA = '/tmp/x-data';
    assert.equal(defaultDataDir(), '/tmp/x-data');
    delete process.env.CLAUDE_PLUGIN_DATA;
    assert.equal(defaultDataDir(), path.join(os.homedir(), '.miblo'));
  } finally {
    if (saved === undefined) delete process.env.CLAUDE_PLUGIN_DATA;
    else process.env.CLAUDE_PLUGIN_DATA = saved;
  }
});

test('parseDataArg extracts --data and rejects an empty value', () => {
  assert.deepEqual(parseDataArg(['--data', '/d', 'status']), { dataDir: '/d', rest: ['status'] });
  assert.deepEqual(parseDataArg(['status']).rest, ['status']);
  assert.throws(() => parseDataArg(['--data', '', 'status']));
  assert.throws(() => parseDataArg(['--data']));
});

test('miblo.js and bridge.js refuse --data ""', () => {
  const cli = spawnSync(process.execPath, [path.join(bin, 'miblo.js'), '--data', '', 'status'], { encoding: 'utf8' });
  assert.equal(cli.status, 2);
  assert.match(cli.stdout, /--data needs a non-empty directory/);
  const br = spawnSync(process.execPath, [path.join(bin, 'bridge.js'), '--data', ''], { encoding: 'utf8', env: { ...process.env, MIBLO_PORT: '1' }, timeout: 5000 });
  assert.equal(br.status, 2);
});

test('pluginVersion reads plugin.json', () => {
  assert.match(pluginVersion(), /^\d+\.\d+\.\d+/);
});

test('the CLIs run when started through a symlinked folder (a ~/.claude kept in a dotfiles repo)', { skip: process.platform === 'win32' && 'symlinks need privileges on Windows' }, () => {
  const fs = process.getBuiltinModule('node:fs');
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'miblo-link-'));
  const linked = path.join(dir, 'plugin');
  fs.symlinkSync(path.resolve(bin, '..'), linked, 'dir');
  const env = { ...process.env, MIBLO_TEST: '1', MIBLO_DISCOVER_JSON: '[{"id":"miblo-4f2a","name":"Desk","addr":"127.0.0.1:1"}]' };
  const r = spawnSync(process.execPath, [path.join(linked, 'bin/miblo.js'), '--data', path.join(dir, 'data'), 'discover'], { env, encoding: 'utf8' });
  assert.equal(r.stdout, 'miblo-4f2a\tDesk\t127.0.0.1:1\n', r.stderr);
  assert.equal(isMain(pathToFileURL(fs.realpathSync(path.join(bin, 'miblo.js'))).href, path.join(linked, 'bin/miblo.js')), true);
  assert.equal(isMain(pathToFileURL(path.join(bin, 'miblo.js')).href, path.join(bin, 'bridge.js')), false);
  assert.equal(isMain(pathToFileURL(path.join(bin, 'miblo.js')).href, undefined), false);
  assert.equal(isMain(pathToFileURL(path.join(bin, 'miblo.js')).href, path.join(dir, 'missing.js')), false);
});

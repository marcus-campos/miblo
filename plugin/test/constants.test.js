import { test } from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { defaultDataDir, parseDataArg, pluginVersion } from '../lib/constants.js';

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

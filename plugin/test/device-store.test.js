import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { DeviceStore } from '../lib/device-store.js';

const tmp = () => fs.mkdtempSync(path.join(os.tmpdir(), 'miblo-store-'));

test('empty when the file does not exist or is corrupt', () => {
  const dir = tmp();
  const store = new DeviceStore(dir);
  assert.deepEqual(store.list(), []);
  fs.writeFileSync(path.join(dir, 'devices.json'), '{nope');
  assert.deepEqual(store.list(), []);
});

test('upsert replaces by id, update patches, remove deletes', () => {
  const store = new DeviceStore(tmp());
  store.upsert({ id: 'a', name: 'A', addr: '1.1.1.1:80', token: 't1' });
  store.upsert({ id: 'b', name: 'B', addr: '2.2.2.2:80', token: 't2' });
  store.upsert({ id: 'a', name: 'A', addr: '1.1.1.9:80', token: 't3' });
  store.update('b', { addr: '2.2.2.9:80' });
  assert.deepEqual(store.list().map((d) => [d.id, d.addr]).sort(), [['a', '1.1.1.9:80'], ['b', '2.2.2.9:80']]);
  store.remove('a');
  assert.deepEqual(store.list().map((d) => d.id), ['b']);
});

test('file is private to the user', { skip: process.platform === 'win32' }, () => {
  const dir = tmp();
  new DeviceStore(dir).upsert({ id: 'a', name: 'A', addr: 'x', token: 't' });
  const mode = fs.statSync(path.join(dir, 'devices.json')).mode & 0o777;
  assert.equal(mode, 0o600);
});

import { test } from 'node:test';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { ensureKey, readKey, proofFor, proofOk, keyOk, logForeignOnce } from '../lib/bridge-auth.js';

const tmp = () => fs.mkdtempSync(path.join(os.tmpdir(), 'miblo-key-'));

test('ensureKey makes a 0600 random key once, and every caller then gets the same one', () => {
  const dir = path.join(tmp(), 'data');
  const key = ensureKey(dir);
  assert.match(key, /^[0-9a-f]{64}$/);
  if (process.platform !== 'win32') assert.equal(fs.statSync(path.join(dir, 'bridge.key')).mode & 0o777, 0o600);
  assert.equal(ensureKey(dir), key);
  assert.equal(readKey(dir), key);
  assert.notEqual(ensureKey(tmp()), key);
  assert.deepEqual(fs.readdirSync(dir), ['bridge.key']);  // no temporary file left behind
});

test('a bridge.key that is not a key is replaced; a loose one is tightened', () => {
  const dir = tmp();
  fs.writeFileSync(path.join(dir, 'bridge.key'), 'garbage');
  assert.equal(readKey(dir), null);
  const key = ensureKey(dir);
  assert.match(key, /^[0-9a-f]{64}$/);
  if (process.platform !== 'win32') {
    fs.chmodSync(path.join(dir, 'bridge.key'), 0o644);
    assert.equal(readKey(dir), key);
    assert.equal(fs.statSync(path.join(dir, 'bridge.key')).mode & 0o777, 0o600);
  }
});

test('proofOk and keyOk accept only the right HMAC / key', () => {
  const key = crypto.randomBytes(32).toString('hex');
  const nonce = crypto.randomBytes(16).toString('hex');
  assert.equal(proofFor(key, nonce), crypto.createHmac('sha256', key).update(`miblo-bridge:${nonce}`).digest('hex'));
  assert.ok(proofOk(key, nonce, proofFor(key, nonce)));
  for (const bad of [null, '', 'zz', proofFor(key, nonce).slice(1), proofFor('0'.repeat(64), nonce), proofFor(key, nonce.replace(/./, 'f') === nonce ? 'e' + nonce.slice(1) : 'f' + nonce.slice(1))]) {
    assert.ok(!proofOk(key, nonce, bad), String(bad));
  }
  assert.ok(!proofOk(null, nonce, proofFor('null', nonce)));
  assert.ok(keyOk(key, key));
  for (const bad of [undefined, '', key.slice(1), '0'.repeat(64)]) assert.ok(!keyOk(key, bad));
  assert.ok(!keyOk(null, 'null'));
});

test('a foreign listener is noted in bridge.log once a day', () => {
  const dir = tmp();
  logForeignOnce(dir, 47821, 1_000_000_000_000);
  logForeignOnce(dir, 47821, 1_000_000_000_000);
  const log = () => fs.readFileSync(path.join(dir, 'bridge.log'), 'utf8').trim().split('\n');
  assert.equal(log().length, 1);
  assert.match(log()[0], /port 47821 answers without the bridge key/);
});

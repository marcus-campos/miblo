import { test } from 'node:test';
import assert from 'node:assert/strict';
import crypto from 'node:crypto';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { ensureKey, readKey, proofFor, proofOk, logForeignOnce, Challenges, authHeader, requestMac, CHALLENGE_TTL_MS, CHALLENGES_PER_SECOND } from '../lib/bridge-auth.js';

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

test('proofOk accepts only the right HMAC', () => {
  const key = crypto.randomBytes(32).toString('hex');
  const nonce = crypto.randomBytes(16).toString('hex');
  assert.equal(proofFor(key, nonce), crypto.createHmac('sha256', key).update(`miblo-bridge:${nonce}`).digest('hex'));
  assert.ok(proofOk(key, nonce, proofFor(key, nonce)));
  for (const bad of [null, '', 'zz', proofFor(key, nonce).slice(1), proofFor('0'.repeat(64), nonce), proofFor(key, nonce.replace(/./, 'f') === nonce ? 'e' + nonce.slice(1) : 'f' + nonce.slice(1))]) {
    assert.ok(!proofOk(key, nonce, bad), String(bad));
  }
  assert.ok(!proofOk(null, nonce, proofFor('null', nonce)));
});

test('a foreign listener is noted in bridge.log once a day', () => {
  const dir = tmp();
  logForeignOnce(dir, 47821, 1_000_000_000_000);
  logForeignOnce(dir, 47821, 1_000_000_000_000);
  const log = () => fs.readFileSync(path.join(dir, 'bridge.log'), 'utf8').trim().split('\n');
  assert.equal(log().length, 1);
  assert.match(log()[0], /port 47821 answers without the bridge key/);
});

// F5 hardening: the key never travels; each request answers a single-use challenge of the bridge.
test('a request mac binds the challenge, method, path and body', () => {
  const key = 'ab'.repeat(32);
  const c = '0123456789abcdef0123456789abcdef';
  const digest = crypto.createHash('sha256').update('{"a":1}').digest('hex');
  assert.equal(requestMac(key, c, 'POST', '/event', '{"a":1}'),
    crypto.createHmac('sha256', key).update(`miblo-req:${c}:POST:/event:${digest}`).digest('hex'));
  assert.equal(authHeader(key, c, 'GET', '/status'), `${c}:${requestMac(key, c, 'GET', '/status', '')}`);
  assert.ok(!authHeader(key, c, 'POST', '/event', '{}').includes(key));
});

test('the bridge accepts each challenge once, for that very request, within its lifetime', () => {
  let t = 0;
  const key = 'ab'.repeat(32);
  const ch = new Challenges({ now: () => t });
  const c = ch.issue();
  assert.match(c, /^[0-9a-f]{32}$/);
  assert.ok(ch.verify(key, authHeader(key, c, 'POST', '/event', 'B'), 'POST', '/event', 'B'));
  assert.ok(!ch.verify(key, authHeader(key, c, 'POST', '/event', 'B'), 'POST', '/event', 'B'), 'replayed');
  // A wrong answer spends the challenge too.
  const d = ch.issue();
  assert.ok(!ch.verify(key, authHeader(key, d, 'POST', '/event', 'B'), 'POST', '/statusline', 'B'));
  assert.ok(!ch.verify(key, authHeader(key, d, 'POST', '/event', 'B'), 'POST', '/event', 'B'));
  for (const [what, hdr] of [
    ['another body', (x) => authHeader(key, x, 'POST', '/event', 'C')],
    ['another method', (x) => authHeader(key, x, 'GET', '/event', 'B')],
    ['another key', (x) => authHeader('cd'.repeat(32), x, 'POST', '/event', 'B')],
    ['a challenge never issued', () => authHeader(key, 'f'.repeat(32), 'POST', '/event', 'B')],
    ['garbage', () => 'x:y'],
    ['the key itself', () => key],
  ]) {
    const x = ch.issue();
    assert.ok(!ch.verify(key, hdr(x), 'POST', '/event', 'B'), what);
  }
  const old = ch.issue();
  t += CHALLENGE_TTL_MS;
  assert.ok(!ch.verify(key, authHeader(key, old, 'GET', '/status'), 'GET', '/status'), 'expired');
  assert.ok(!new Challenges().verify(null, 'x', 'GET', '/status'));
});

// A flood of /health cannot push out the challenges real clients are about to answer: at most
// 20 are made a second, and when 256 are open new ones are refused (expired ones go first).
test('challenges are capped per second', () => {
  let t = 1_000_000;
  const ch = new Challenges({ now: () => t });
  assert.ok(CHALLENGES_PER_SECOND >= 20 && CHALLENGES_PER_SECOND <= 100);
  for (let i = 0; i < CHALLENGES_PER_SECOND; i++) assert.ok(ch.issue(), `#${i}`);
  assert.equal(ch.issue(), null);
  t += 1000;
  assert.ok(ch.issue());
});

test('a full set of open challenges refuses new ones instead of evicting the oldest', () => {
  const key = 'ab'.repeat(32);
  let t = 1_000_000;
  const ch = new Challenges({ now: () => t });
  const first = ch.issue();
  for (let i = 1; i < 256; i++) { t += 50; assert.ok(ch.issue(), `#${i}`); }
  t += 50;
  assert.equal(ch.issue(), null, 'full');
  assert.ok(ch.verify(key, authHeader(key, first, 'GET', '/status'), 'GET', '/status'), 'the oldest still open');
  assert.ok(ch.issue(), 'room again');
  t = 1_000_000 + CHALLENGE_TTL_MS + 13_000;  // all expired
  for (let i = 0; i < 20; i++) assert.ok(ch.issue());
});

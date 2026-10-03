import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { startFakeDevice } from './fakes/fake-device.js';
import { startFakeGitHub, fakeImage } from './fakes/fake-github.js';
import { DeviceStore } from '../lib/device-store.js';
import { compareVersions, parseImageName, parseUpdateArgs, FirmwareUpdater } from '../lib/firmware-update.js';
import { run } from '../bin/miblo.js';

const BOARD = 'geekmagic_ultra';
const IMG = `miblo-${BOARD}-0.2.2.bin`;

async function setup({ device = {}, github = {}, devices = 1, pluginVersion = '0.2.2' } = {}) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'miblo-upd-'));
  const dataDir = path.join(root, 'data');
  const store = new DeviceStore(dataDir);
  const devs = [];
  for (let i = 0; i < devices; i++) {
    const dev = await startFakeDevice({ id: `miblo-000${i}`, name: `Miblo-000${i}`, fw: '0.2.1', ...device });
    store.upsert({ id: `miblo-000${i}`, name: `Miblo-000${i}`, addr: dev.addr, token: 't' });
    devs.push(dev);
  }
  const gh = await startFakeGitHub({ release: 'v0.2.2', files: { [IMG]: fakeImage('a') }, ...github });
  const updater = { githubApi: gh.githubApi, rawBase: gh.rawBase, pluginVersion, pollMs: 10, rebootTimeoutMs: 2000 };
  const cli = async (...args) => {
    const r = await run(['update', ...args], { dataDir, updater });
    return { ...r, json: (() => { try { return JSON.parse(r.out); } catch { return null; } })() };
  };
  return {
    root, dataDir, dev: devs[0], devs, gh, cli,
    close: async () => { await Promise.all(devs.map((d) => d.close())); await gh.close(); },
  };
}

test('compareVersions orders x.y.z and puts pre-releases first', () => {
  assert.equal(compareVersions('0.2.1', '0.2.2'), -1);
  assert.equal(compareVersions('0.10.0', '0.9.9'), 1);
  assert.equal(compareVersions('v1.0.0', '1.0.0'), 0);
  assert.equal(compareVersions('0.0.0-fake', '0.0.0'), -1);
  assert.equal(compareVersions('garbage', '0.0.1'), -1);
});

test('parseImageName and parseUpdateArgs', () => {
  assert.deepEqual(parseImageName('/x/miblo-geekmagic_ultra-0.2.2.bin'), { board: BOARD, version: '0.2.2' });
  assert.deepEqual(parseImageName('miblo-loader-geekmagic_ultra-0.2.2.bin'), { board: 'loader-geekmagic_ultra', version: '0.2.2' });
  assert.equal(parseImageName('firmware.bin'), null);
  assert.deepEqual(parseUpdateArgs(['send', 'miblo-1', '0042']), { step: 'send', id: 'miblo-1', code: '0042', file: null, check: false });
  assert.deepEqual(parseUpdateArgs(['miblo-1', '--file', 'a.bin', '--check']), { step: null, id: 'miblo-1', code: null, file: 'a.bin', check: true });
});

test('check reports device fw/board, latest release and plugin status', async () => {
  const t = await setup({ pluginVersion: '0.2.1' });
  try {
    const r = await t.cli('check');
    assert.equal(r.code, 0);
    assert.deepEqual(r.json.plugin, { current: '0.2.1', latest: '0.2.2', source: 'release', needsUpdate: true });
    assert.deepEqual(r.json.firmware, { source: 'github', latest: '0.2.2' });
    assert.deepEqual(r.json.devices, [{ id: 'miblo-0000', name: 'Miblo-0000', online: true, fw: '0.2.1', board: BOARD, latest: '0.2.2', boardMatch: true, needsUpdate: true }]);
    assert.equal((await t.cli('--check')).json.devices[0].needsUpdate, true);
  } finally { await t.close(); }
});

test('check says up to date when the gadget already runs the latest', async () => {
  const t = await setup({ device: { fw: '0.2.2' } });
  try {
    const r = await t.cli('check', 'miblo-0000');
    assert.equal(r.json.devices[0].needsUpdate, false);
    assert.equal(r.json.plugin.needsUpdate, false);
  } finally { await t.close(); }
});

test('no releases: check explains and falls back to plugin.json on main; open suggests --file', async () => {
  const t = await setup({ github: { release: null, rawPluginVersion: '0.3.0' } });
  try {
    const r = await t.cli('check');
    assert.equal(r.code, 0);
    assert.deepEqual(r.json.plugin, { current: '0.2.2', latest: '0.3.0', source: 'main', needsUpdate: true });
    assert.equal(r.json.firmware.latest, null);
    assert.match(r.json.firmware.error, /No firmware releases.*--file/);
    assert.equal(r.json.devices[0].needsUpdate, false);
    const o = await t.cli('open', 'miblo-0000');
    assert.equal(o.code, 1);
    assert.match(o.out, /No firmware releases are published yet\. Use --file/);
  } finally { await t.close(); }
});

test('plugin status uses an injectable fetch for the raw-URL fallback and tolerates being offline', async () => {
  const calls = [];
  const fetchImpl = async (url) => {
    calls.push(String(url));
    if (url.endsWith('/releases/latest')) return new Response('{}', { status: 404 });
    if (url === 'https://raw.example/marcus-campos/miblo/main/plugin/.claude-plugin/plugin.json') return Response.json({ version: '1.0.0' });
    return new Response('', { status: 404 });
  };
  const u = new FirmwareUpdater({ store: { list: () => [] }, dataDir: os.tmpdir(), fetchImpl, rawBase: 'https://raw.example', pluginVersion: '1.0.0' });
  const r = await u.check({});
  assert.deepEqual(r.plugin, { current: '1.0.0', latest: '1.0.0', source: 'main', needsUpdate: false });
  assert.ok(calls.includes('https://api.github.com/repos/marcus-campos/miblo/releases/latest'));

  const offline = new FirmwareUpdater({ store: { list: () => [] }, dataDir: os.tmpdir(), fetchImpl: async () => { throw new Error('ENOTFOUND'); }, pluginVersion: '1.0.0' });
  const o = await offline.check({});
  assert.deepEqual(o.plugin, { current: '1.0.0', latest: null, source: null, needsUpdate: null });
  assert.match(o.firmware.error, /Could not reach GitHub/);
});

test('happy path: open downloads + verifies, send uploads with the code and waits for the reboot', async () => {
  const t = await setup();
  try {
    const o = await t.cli('open', 'miblo-0000');
    assert.equal(o.code, 0, o.out);
    assert.deepEqual(o.json, { id: 'miblo-0000', name: 'Miblo-0000', from: '0.2.1', to: '0.2.2', codeRequired: true });
    assert.ok(fs.existsSync(path.join(t.dataDir, 'firmware', IMG)));
    assert.equal(t.dev.state.gateOpen, true);

    const s = await t.cli('send', 'miblo-0000', '1234');
    assert.equal(s.code, 0, s.out);
    assert.match(s.out, /Miblo-0000 updated from 0\.2\.1 to 0\.2\.2\./);
    assert.equal(t.dev.state.fw, '0.2.2');
    const [up] = t.dev.state.uploads;
    assert.equal(up.filename, IMG);
    assert.ok(up.bytes.equals(fakeImage('a')));
    assert.ok(up.contentLength > up.bytes.length);
    // a second send has nothing pending
    assert.equal((await t.cli('send', 'miblo-0000', '1234')).code, 2);
  } finally { await t.close(); }
});

test('the cached image is reused when its checksum still matches', async () => {
  const t = await setup();
  try {
    await t.cli('open', 'miblo-0000');
    await t.cli('open', 'miblo-0000');
    assert.equal(t.gh.hits.filter((h) => h === `/dl/${IMG}`).length, 1);
  } finally { await t.close(); }
});

test('codeless path: a never-configured unit takes the upload without a code', async () => {
  const t = await setup({ device: { otaCodeRequired: false } });
  try {
    const o = await t.cli('open');
    assert.equal(o.json.codeRequired, false);
    const s = await t.cli('send');
    assert.equal(s.code, 0, s.out);
    assert.equal(t.dev.state.fw, '0.2.2');
  } finally { await t.close(); }
});

test('the one-shot form opens and, with no code needed, sends right away', async () => {
  const t = await setup({ device: { otaCodeRequired: false } });
  try {
    const r = await t.cli('miblo-0000');
    assert.equal(r.code, 0, r.out);
    assert.match(r.out, /updated from 0\.2\.1 to 0\.2\.2/);
  } finally { await t.close(); }
});

test('wrong code returns 2 and keeps the update open for a retry', async () => {
  const t = await setup();
  try {
    await t.cli('open', 'miblo-0000');
    const bad = await t.cli('send', 'miblo-0000', '0000');
    assert.equal(bad.code, 2);
    assert.match(bad.out, /Wrong code\./);
    assert.equal(t.dev.state.fw, '0.2.1');
    const noCode = await t.cli('send', 'miblo-0000');
    assert.equal(noCode.code, 2);
    assert.match(noCode.out, /4-digit code/);
    assert.equal((await t.cli('send', 'miblo-0000', '1234')).code, 0);
  } finally { await t.close(); }
});

test('a send after the upload window closed (5 min) says to open it again; nothing is flashed', async () => {
  for (const otaCodeRequired of [true, false]) {
    let now = 0;
    const t = await setup({ device: { now: () => now, otaCodeRequired } });
    try {
      await t.cli('open', 'miblo-0000');
      now += 300_000;
      const late = await t.cli('send', 'miblo-0000', '1234');
      assert.equal(late.code, 2, late.out);
      assert.match(late.out, /update window on Miblo-0000 closed.*update open/);
      assert.equal(t.dev.state.uploads.length, 0);
      assert.equal(t.dev.state.otaBadCodes, 0);  // not a wrong code
      await t.cli('open', 'miblo-0000');
      assert.equal((await t.cli('send', 'miblo-0000', '1234')).code, 0);
    } finally { await t.close(); }
  }
});

test('a gadget that resets the upload right away (window closed, locked) says why and to run /miblo:update again', async () => {
  const t = await setup({ device: { resetUploads: 1 } });
  try {
    await t.cli('open', 'miblo-0000');
    const r = await t.cli('send', 'miblo-0000', '1234');
    assert.equal(r.code, 2, r.out);
    assert.match(r.out, /Miblo-0000 stopped the upload: the update window may have closed or the gadget is locked after wrong codes — run \/miblo:update again\./);
    assert.equal(t.dev.state.uploads.length, 0);
    assert.equal((await t.cli('send', 'miblo-0000', '1234')).code, 0);  // still pending: a retry works
  } finally { await t.close(); }
});

test('lockout after repeated wrong codes reports retryAfter (on send and on open)', async () => {
  let now = 0;
  const t = await setup({ device: { now: () => now } });
  try {
    await t.cli('open', 'miblo-0000');
    for (let i = 0; i < 4; i++) assert.match((await t.cli('send', 'miblo-0000', '0000')).out, /Wrong code/);
    const locked = await t.cli('send', 'miblo-0000', '0000');
    assert.equal(locked.code, 2);
    assert.match(locked.out, /Too many wrong codes\. Try again in 60 s\./);
    now += 30_000;
    const o = await t.cli('open', 'miblo-0000');
    assert.equal(o.code, 2);
    assert.match(o.out, /Try again in 30 s\./);
  } finally { await t.close(); }
});

test('board mismatch: a release without the board, or a --file for another board, is refused', async () => {
  const t = await setup({ device: { board: 'other_board' } });
  try {
    const c = await t.cli('check');
    assert.equal(c.json.devices[0].boardMatch, false);
    assert.equal(c.json.devices[0].needsUpdate, false);
    const o = await t.cli('open', 'miblo-0000');
    assert.equal(o.code, 2);
    assert.match(o.out, /no firmware for board other_board/);
    const file = path.join(t.root, IMG);
    fs.writeFileSync(file, fakeImage('f'));
    const f = await t.cli('open', 'miblo-0000', '--file', file);
    assert.equal(f.code, 2);
    assert.match(f.out, /Refusing: .* is for board geekmagic_ultra, but the gadget is other_board/);
    assert.equal(t.dev.state.gateOpen, false);
  } finally { await t.close(); }
});

test('checksum mismatch: the download is rejected and nothing is opened', async () => {
  const t = await setup({ github: { sums: `${'0'.repeat(64)}  ${IMG}\n` } });
  try {
    const o = await t.cli('open', 'miblo-0000');
    assert.equal(o.code, 1);
    assert.match(o.out, /Checksum mismatch/);
    assert.equal(t.dev.state.gateOpen, false);
    assert.ok(!fs.existsSync(path.join(t.dataDir, 'firmware', IMG)));
  } finally { await t.close(); }
});

test('--file: a local image updates without GitHub; version comes from the name', async () => {
  const t = await setup({ github: { release: null } });
  try {
    const file = path.join(t.root, `miblo-${BOARD}-0.3.0.bin`);
    fs.writeFileSync(file, fakeImage('l'));
    const c = await t.cli('check', '--file', file);
    assert.equal(c.json.firmware.latest, '0.3.0');
    assert.equal(c.json.devices[0].needsUpdate, true);
    assert.equal((await t.cli('open', '--file', file)).json.to, '0.3.0');
    const s = await t.cli('send', '1234');
    assert.equal(s.code, 0, s.out);
    assert.equal(t.dev.state.fw, '0.3.0');

    const junk = path.join(t.root, `miblo-${BOARD}-0.4.0.bin`);
    fs.writeFileSync(junk, Buffer.from('not firmware'));
    assert.match((await t.cli('open', '--file', junk)).out, /does not look like a Miblo firmware image/);
    assert.match((await t.cli('open', '--file', path.join(t.root, 'fw.bin'))).out, /miblo-<board>-<version>\.bin/);
  } finally { await t.close(); }
});

test('offline gadget and ambiguous targets are reported', async () => {
  const t = await setup({ devices: 2 });
  try {
    assert.equal((await t.cli('check')).json.devices.length, 2);
    const amb = await t.cli('open');
    assert.equal(amb.code, 2);
    assert.match(amb.out, /Several gadgets are paired; pass an id: miblo-0000, miblo-0001/);
    await t.devs[1].close();
    const c = await t.cli('check', 'miblo-0001');
    assert.deepEqual(c.json.devices, [{ id: 'miblo-0001', name: 'Miblo-0001', online: false, needsUpdate: null }]);
    const o = await t.cli('open', 'miblo-0001');
    assert.equal(o.code, 1);
    assert.match(o.out, /Could not reach Miblo-0001/);
    assert.match((await t.cli('open', 'nope')).out, /No paired gadget with id nope/);
  } finally { await t.devs[0].close(); await t.gh.close(); }
});

test('send times out when the gadget does not come back with the new version', async () => {
  const t = await setup({ device: { rebootMs: 60_000 } });
  try {
    await t.cli('open', 'miblo-0000');
    const r = await run(['update', 'send', 'miblo-0000', '1234'], {
      dataDir: t.dataDir, updater: { githubApi: t.gh.githubApi, pollMs: 10, rebootTimeoutMs: 300 },
    });
    assert.equal(r.code, 1);
    assert.match(r.out, /did not come back with 0\.2\.2/);
  } finally { t.dev.state.rebooting = false; await t.close(); }
});

test('a paired gadget: check, open and send read its version with the stored token', async () => {
  const t = await setup({ device: { tokens: ['t'] } });
  try {
    const c = await t.cli('check');
    assert.equal(c.json.devices[0].fw, '0.2.1');
    assert.equal(c.json.devices[0].needsUpdate, true);
    const o = await t.cli('open', 'miblo-0000');
    assert.equal(o.code, 0, o.out);
    const s = await t.cli('send', 'miblo-0000', '1234');
    assert.equal(s.code, 0, s.out);
    assert.match(s.out, /updated from 0\.2\.1 to 0\.2\.2/);
  } finally { await t.close(); }
});

test('a gadget that dropped this pairing: version unknown in check, open refuses and says to pair', async () => {
  const t = await setup({ device: { tokens: ['someone-else'] } });
  try {
    const c = await t.cli('check');
    assert.equal(c.code, 0);
    assert.equal(c.json.devices[0].online, true);
    assert.equal(c.json.devices[0].fw, '');
    assert.equal(c.json.devices[0].needsUpdate, null);
    const o = await t.cli('open', 'miblo-0000');
    assert.notEqual(o.code, 0);
    assert.match(o.out, /\/miblo:pair/);
  } finally { await t.close(); }
});

test('open while another code is on the gadget screen says so (busy), not "wrong codes"', async () => {
  const t = await setup({ device: { otherCodeSec: 240 } });
  try {
    const o = await t.cli('open', 'miblo-0000');
    assert.equal(o.code, 2);
    assert.match(o.out, /Another code is on the gadget screen\. Try again in 240 s\./);
    assert.doesNotMatch(o.out, /wrong codes/);
  } finally { await t.close(); }
});

// Firmware F4 follow-up: an anonymous LAN host could keep the owner out of OTA by re-opening a
// code before each expires. /update/open now carries the pairing token, and an authorised request
// replaces a code opened anonymously (never one an authorised session opened).
test('open sends the pairing token, so it replaces a code a stranger opened anonymously', async () => {
  const t = await setup({ device: { otherCodeSec: 240, tokens: ['t'] } });
  try {
    const o = await t.cli('open', 'miblo-0000');
    assert.equal(o.code, 0, o.out);
    assert.ok(t.dev.state.gateOpen);
    assert.ok(t.dev.state.authHeaders.includes('Bearer t'));
  } finally { await t.close(); }
  const u = await setup({ device: { otherCodeSec: 240, otherCodeAnon: false, tokens: ['t'] } });
  try {
    const o = await u.cli('open', 'miblo-0000');
    assert.equal(o.code, 2);
    assert.match(o.out, /Another code is on the gadget screen/);
  } finally { await u.close(); }
});

test('a busy gadget (503) is reported busy by update and check, not unreachable', async () => {
  const t = await setup({ device: { busy: Infinity } });
  try {
    const o = await run(['update', 'open', 'miblo-0000'], { dataDir: t.dataDir, updater: { githubApi: t.gh.githubApi, rawBase: t.gh.rawBase, sleep: async () => {} } });
    assert.equal(o.code, 1);
    assert.match(o.out, /Miblo-0000 is busy right now — try again in a moment\./);
    const c = await run(['update', 'check'], { dataDir: t.dataDir, updater: { githubApi: t.gh.githubApi, rawBase: t.gh.rawBase, sleep: async () => {} } });
    const row = JSON.parse(c.out).devices[0];
    assert.equal(row.online, true);
    assert.equal(row.busy, true);
    assert.equal(row.needsUpdate, null);
  } finally { await t.close(); }
});

test('update retries a gadget busy for a moment', async () => {
  const t = await setup({ device: { busy: 2, busyPath: '/api/info' } });
  try {
    const delays = [];
    const c = await run(['update', 'check'], { dataDir: t.dataDir, updater: { githubApi: t.gh.githubApi, rawBase: t.gh.rawBase, pluginVersion: '0.2.2', sleep: async (ms) => { delays.push(ms); } } });
    assert.equal(JSON.parse(c.out).devices[0].fw, '0.2.1');
    assert.deepEqual(delays, [400, 800]);
  } finally { await t.close(); }
});

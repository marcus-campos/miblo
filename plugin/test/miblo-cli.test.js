import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { startFakeDevice } from './fakes/fake-device.js';
import { DeviceClient } from '../lib/device-client.js';
import { DeviceStore } from '../lib/device-store.js';
import { run, tokenTag, fixedDiscovery } from '../bin/miblo.js';

const pluginRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');

function deps(extra = {}) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'miblo-cli-'));
  return {
    dataDir: path.join(root, 'data'),
    pluginRoot,
    settingsPath: path.join(root, 'settings.json'),
    client: new DeviceClient(),
    discoverFn: async () => [],
    hostname: 'test-host',
    fetchStatus: async () => null,
    openUrl: async () => {},  // never open a real browser from tests
    ...extra,
  };
}

test('discover lists gadgets or says none were found', async () => {
  assert.match((await run(['discover'], deps())).out, /No Miblo gadgets found/);
  const d = deps({ discoverFn: async () => [{ id: 'g1', name: 'Miblo-4F2A', addr: '10.0.0.5:80' }] });
  assert.equal((await run(['discover'], d)).out.trim(), 'g1\tMiblo-4F2A\t10.0.0.5:80');
});

test('pair stores the device; wrong code returns 2', async () => {
  const dev = await startFakeDevice();
  const d = deps();
  try {
    const bad = await run(['pair', dev.addr, '0000'], d);
    assert.equal(bad.code, 2);
    assert.match(bad.out, /Wrong pairing code/);

    const ok = await run(['pair', dev.addr, '4827'], d);
    assert.equal(ok.code, 0);
    assert.match(ok.out, /Paired with Miblo-4F2A \(miblo-4f2a\)/);
    assert.ok(!ok.out.includes(dev.state.token));
    const [saved] = new DeviceStore(d.dataDir).list();
    assert.equal(saved.token, dev.state.token);
  } finally {
    await dev.close();
  }
});

test('pair keeps the firmware version the gadget reports (relocation needs it)', async () => {
  const dev = await startFakeDevice({ fw: '1.14.0' });
  const d = deps();
  try {
    assert.equal((await run(['pair', dev.addr, '4827'], d)).code, 0);
    assert.equal(new DeviceStore(d.dataDir).list()[0].fw, '1.14.0');
  } finally {
    await dev.close();
  }
});

test('pair after 5 wrong codes reports the lockout with seconds remaining', async () => {
  let t = 0;
  const dev = await startFakeDevice({ now: () => t });
  const d = deps();
  try {
    for (let i = 0; i < 5; i++) assert.equal((await run(['pair', dev.addr, '0000'], d)).code, 2);
    const locked = await run(['pair', dev.addr, '4827'], d);
    assert.equal(locked.code, 2);
    assert.match(locked.out, /Too many wrong codes\. Try again in 60 s\./);
  } finally {
    await dev.close();
  }
});

test('pair seeds the device language from the host locale when the device reports none', async () => {
  const dev = await startFakeDevice();
  const d = deps({ locale: 'pt-BR' });
  try {
    await run(['pair', dev.addr, '4827'], d);
    assert.equal(dev.state.config.lang, 'pt-BR');
  } finally {
    await dev.close();
  }
});

test('pair maps host locales to the firmware\'s supported language codes', async () => {
  const cases = [
    ['en-US', 'en'],
    ['pt-PT', 'pt-PT'],
    ['pt', 'pt-BR'],
    ['es-ES', 'es'],
    ['zh-Hans-CN', 'zh'],
  ];
  for (const [locale, expected] of cases) {
    const dev = await startFakeDevice();
    try {
      await run(['pair', dev.addr, '4827'], deps({ locale }));
      assert.equal(dev.state.config.lang, expected, `${locale} -> ${expected}`);
    } finally {
      await dev.close();
    }
  }
});

test('pair skips the language sync for an unsupported locale, without failing the pair', async () => {
  const dev = await startFakeDevice();
  const d = deps({ locale: 'ja-JP' });
  try {
    const r = await run(['pair', dev.addr, '4827'], d);
    assert.equal(r.code, 0);
    assert.equal(dev.state.config.lang, undefined);
  } finally {
    await dev.close();
  }
});

test('pair does not overwrite a language the device already reports', async () => {
  const setConfigCalls = [];
  const client = {
    info: async () => ({ id: 'g', name: 'G', lang: 'fr', langSet: true }),
    pair: async () => 'tok',
    setConfig: async (...args) => setConfigCalls.push(args),
  };
  const r = await run(['pair', '10.0.0.9', '4827'], deps({ client, locale: 'en-US' }));
  assert.equal(r.code, 0);
  assert.equal(setConfigCalls.length, 0);
});

test('pair seeds the language when the device is in automatic mode (langSet false)', async () => {
  const setConfigCalls = [];
  const client = {
    info: async () => ({ id: 'g', name: 'G', lang: 'en', langSet: false }),
    pair: async () => 'tok',
    setConfig: async (...args) => setConfigCalls.push(args),
  };
  const r = await run(['pair', '10.0.0.9', '4827'], deps({ client, locale: 'pt-BR' }));
  assert.equal(r.code, 0);
  assert.deepEqual(setConfigCalls.map((c) => c[2]), [{ lang: 'pt-BR' }]);
});

test('pair still succeeds if the best-effort language setConfig fails', async () => {
  const client = {
    info: async () => ({ id: 'g', name: 'G' }),
    pair: async () => 'tok',
    setConfig: async () => { throw new Error('offline'); },
  };
  const r = await run(['pair', '10.0.0.9', '4827'], deps({ client, locale: 'en-US' }));
  assert.equal(r.code, 0);
  assert.match(r.out, /Paired with/);
});

test('pair adds :80 to a bare IP', async () => {
  const seen = [];
  const client = { info: async (addr) => { seen.push(addr); return { id: 'g', name: 'G' }; }, pair: async () => 'tok' };
  await run(['pair', '192.168.0.42', '1234'], deps({ client }));
  assert.equal(seen[0], '192.168.0.42:80');
});

test('mode sends config to all or to one gadget; invalid mode returns 2', async () => {
  const dev = await startFakeDevice();
  const d = deps();
  try {
    await run(['pair', dev.addr, '4827'], d);
    assert.equal((await run(['mode', 'banana'], d)).code, 2);
    const r = await run(['mode', 'limits'], d);
    assert.match(r.out, /Mode set to limits on 1 gadget/);
    assert.equal(dev.state.config.mode, 'limits');
  } finally {
    await dev.close();
  }
});

test('rotate turns the Overview/Limits rotation on and off with timings', async () => {
  const dev = await startFakeDevice();
  const d = deps();
  try {
    assert.equal((await run(['rotate', 'on'], d)).code, 2);  // nothing paired yet
    await run(['pair', dev.addr, '4827'], d);
    const st0 = await run(['rotate', '--status'], d);
    assert.equal(st0.code, 0);
    assert.match(st0.out, /Miblo-4F2A \(miblo-4f2a\): rotation off/);

    const on = await run(['rotate', 'on', '90', '12'], d);
    assert.equal(on.code, 0);
    assert.match(on.out, /Rotation on \(Limits every 90 s, for 12 s\) on 1 gadget/);
    assert.deepEqual(
      { rotate: dev.state.config.rotate, every: dev.state.config.rotateEverySec, show: dev.state.config.rotateShowSec },
      { rotate: true, every: 90, show: 12 });
    assert.match((await run(['rotate', '--status', 'miblo-4f2a'], d)).out, /rotation on, Limits every 90 s for 12 s/);

    const off = await run(['rotate', 'off', 'miblo-4f2a'], d);
    assert.match(off.out, /Rotation off on 1 gadget/);
    assert.equal(dev.state.config.rotate, false);
    assert.equal(dev.state.config.rotateEverySec, 90);  // timings kept

    assert.equal((await run(['rotate', 'on', 'nope'], d)).code, 2);  // unknown id
  } finally {
    await dev.close();
  }
});

test('rotate validates arguments client-side with clear messages', async () => {
  const d = deps();
  const cases = [
    [[], /"on" or "off"/],
    [['maybe'], /"on" or "off"/],
    [['on', '5'], /every-seconds must be a whole number from 10 to 3600 \(got 5\)/],
    [['on', '3601'], /from 10 to 3600/],
    [['on', '60', '2'], /show-seconds must be a whole number from 3 to 300 \(got 2\)/],
    [['on', '60', '301'], /from 3 to 300/],
    [['on', '60', '12.5'], /whole number/],
    [['on', '20', '20'], /show-seconds \(20\) must be shorter than every-seconds \(20\)/],
    [['on', '60', '10', '5'], /Too many numbers/],
    [['on', 'a', 'b'], /Unexpected argument "b"/],
  ];
  for (const [args, re] of cases) {
    const r = await run(['rotate', ...args], d);
    assert.equal(r.code, 2, args.join(' '));
    assert.match(r.out, re, args.join(' '));
  }
});

test('rotate reports a device-side rejection (show not shorter than the stored period)', async () => {
  const dev = await startFakeDevice();
  const d = deps();
  try {
    await run(['pair', dev.addr, '4827'], d);
    const r = await run(['rotate', 'on', '10'], d);  // stored show is 10 s: 10 >= 10
    assert.equal(r.code, 1);
    assert.match(r.out, /rejected rotateEverySec: show-seconds must be shorter than every-seconds/);
    assert.equal(dev.state.config.rotate, undefined);  // all-or-nothing
  } finally {
    await dev.close();
  }
});

test('night turns night mode on and off with its window and brightness', async () => {
  const dev = await startFakeDevice();
  const d = deps();
  try {
    assert.equal((await run(['night', 'on'], d)).code, 2);  // nothing paired yet
    await run(['pair', dev.addr, '4827'], d);
    assert.match((await run(['night', '--status'], d)).out, /Miblo-4F2A \(miblo-4f2a\): night mode off/);

    const on = await run(['night', 'on', '23:30', '06:15', '8%'], d);
    assert.equal(on.code, 0);
    assert.match(on.out, /Night mode on \(23:30-06:15, 8%\) on 1 gadget/);
    assert.deepEqual(
      { n: dev.state.config.night, f: dev.state.config.nightFrom, t: dev.state.config.nightTo, b: dev.state.config.nightBrightness },
      { n: true, f: 23 * 60 + 30, t: 6 * 60 + 15, b: 8 });
    assert.match((await run(['night', '--status', 'miblo-4f2a'], d)).out, /night mode on, 23:30-06:15 at 8%/);

    const off = await run(['night', 'off', 'miblo-4f2a'], d);
    assert.match(off.out, /Night mode off on 1 gadget/);
    assert.equal(dev.state.config.night, false);
    assert.equal(dev.state.config.nightFrom, 23 * 60 + 30);  // window kept
    assert.equal((await run(['night', 'on', 'nope'], d)).code, 2);  // unknown id
  } finally {
    await dev.close();
  }
});

test('night validates arguments client-side with clear messages', async () => {
  const d = deps();
  const cases = [
    [[], /"on" or "off"/],
    [['on', '22:00'], /Give both times/],
    [['on', '24:00', '07:00'], /Invalid time "24:00"/],
    [['on', '22:60', '07:00'], /Invalid time/],
    [['on', '22:00', '22:00'], /must differ/],
    [['on', '0'], /from 1 to 100 \(got 0\)/],
    [['on', '101%'], /from 1 to 100/],
    [['on', '10', '20'], /Too many numbers/],
    [['on', 'a', 'b'], /Unexpected argument "b"/],
  ];
  for (const [args, re] of cases) {
    const r = await run(['night', ...args], d);
    assert.equal(r.code, 2, args.join(' '));
    assert.match(r.out, re, args.join(' '));
  }
});

test('night reports a device-side rejection (window collapsing onto the stored end)', async () => {
  const dev = await startFakeDevice();
  const d = deps();
  try {
    await run(['pair', dev.addr, '4827'], d);
    // The CLI always sends both ends; a patch moving only the start onto the stored end (07:00)
    // is rejected whole, like the firmware does.
    const r = await fetch(`http://${dev.addr}/api/config`, {
      method: 'POST', headers: { authorization: `Bearer ${dev.state.token}`, 'content-type': 'application/json' },
      body: JSON.stringify({ night: true, nightFrom: 420 }),
    });
    assert.equal(r.status, 400);
    assert.equal((await r.json()).field, 'nightFrom');
    assert.equal(dev.state.config.night, undefined);
  } finally {
    await dev.close();
  }
});

test('reset sends the command and forgets the pairing', async () => {
  const dev = await startFakeDevice();
  const d = deps();
  try {
    await run(['pair', dev.addr, '4827'], d);
    const r = await run(['reset', 'miblo-4f2a'], d);
    assert.match(r.out, /Factory reset sent to Miblo-4F2A/);
    assert.equal(dev.state.resets, 1);
    assert.deepEqual(new DeviceStore(d.dataDir).list(), []);
  } finally {
    await dev.close();
  }
});

test('link and unlink the statusline', async () => {
  const d = deps();
  assert.equal((await run(['link-statusline'], d)).out.trim(), 'Statusline linked.');
  assert.equal((await run(['link-statusline'], d)).out.trim(), 'Statusline already linked.');
  assert.equal((await run(['unlink-statusline'], d)).out.trim(), 'Statusline unlinked.');
  assert.equal((await run(['unlink-statusline'], d)).out.trim(), 'Statusline was not linked.');
});

test('status reports bridge state, statusline and devices without tokens', async () => {
  const d = deps();
  new DeviceStore(d.dataDir).upsert({ id: 'g1', name: 'G1', addr: 'x:80', token: 'secret-token' });
  const r = await run(['status'], d);
  assert.ok(!r.out.includes('secret-token'));
  const s = JSON.parse(r.out);
  assert.equal(s.bridge, 'stopped');
  assert.equal(s.statusline, 'not linked');
  assert.equal(s.devices[0].id, 'g1');
});

test('unknown subcommand prints usage with code 2', async () => {
  const r = await run(['wat'], deps());
  assert.equal(r.code, 2);
  assert.match(r.out, /Usage/);
});

test('the usage lists the daily-life commands; an inherited object key is not a command', async () => {
  const r = await run(['wat'], deps());
  for (const cmd of ['focus', 'meeting', 'find', 'timer', 'say', 'remind', 'countdown', 'today', 'limits']) assert.match(r.out, new RegExp(`^  ${cmd}\\b`, 'm'), cmd);
  assert.equal((await run(['toString'], deps())).code, 2);
  assert.equal((await run(['constructor'], deps())).code, 2);
});

test('discover and pair sanitize gadget-provided strings', async () => {
  const d = deps({ discoverFn: async () => [{ id: 'g1`x`', name: 'Evil\n\u001b[31mname-that-is-way-too-long', addr: '10.0.0.5:80' }, { id: '!!!', name: 'x', addr: '1.2.3.4:80' }] });
  assert.equal((await run(['discover'], d)).out.trim(), 'g1x\tEvil31mname-that-is-\t10.0.0.5:80');

  const client = { info: async () => ({ id: 'id<script>', name: 'Na\u0000me; ls' }), pair: async () => 'tok' };
  const p = deps({ client });
  const r = await run(['pair', '10.0.0.7', '1234'], p);
  assert.equal(r.out.trim(), 'Paired with Name ls (idscript) at 10.0.0.7:80.');
  assert.deepEqual(new DeviceStore(p.dataDir).list().map(({ id, name }) => ({ id, name })), [{ id: 'idscript', name: 'Name ls' }]);

  const bad = await run(['pair', '10.0.0.7', '1234'], deps({ client: { info: async () => ({ id: '###' }), pair: async () => 'tok' } }));
  assert.equal(bad.code, 1);
});

test('status survives a corrupt settings.json', async () => {
  const d = deps();
  fs.writeFileSync(d.settingsPath, '{broken');
  const r = await run(['status'], d);
  assert.equal(r.code, 0);
  assert.equal(JSON.parse(r.out).statusline, 'settings.json unreadable');
});

test('status includes today\'s summary from the bridge', async () => {
  const today = { usd: 1.5, turns: 4, work: 900 };
  const r = await run(['status'], deps({ fetchStatus: async () => ({ devices: [], sessions: [], usage: null, today }) }));
  assert.deepEqual(JSON.parse(r.out).today, today);
});

test('settings prints the gadget URL and opens it in the browser', async () => {
  const opened = [];
  const d = deps({ openUrl: async (url) => { opened.push(url); } });
  new DeviceStore(d.dataDir).upsert({ id: 'miblo-4f2a', name: 'Miblo-4F2A', addr: '192.168.0.176:80', token: 'secret' });
  const r = await run(['settings'], d);
  assert.equal(r.code, 0);
  // The page it opens marks this computer in the paired computers' list by its token's tag
  // (#me=, the first 8 hex of FNV-1a-64 of the token: never the token); the printed URL stays plain.
  assert.deepEqual(opened, ['http://192.168.0.176/#me=ab23f0ee']);
  assert.ok(!opened[0].includes('secret'));
  assert.match(r.out, /Miblo-4F2A settings: http:\/\/192\.168\.0\.176\/\n/);
  assert.match(r.out, /http:\/\/miblo-4f2a\.local/);
  assert.match(r.out, /Opened in your browser/);
  assert.ok(!r.out.includes('secret'));
});

test('tokenTag is FNV-1a-64 of the token, first 8 hex (the gadget computes the same)', () => {
  assert.equal(tokenTag(''), 'cbf29ce4');
  assert.equal(tokenTag('a'), 'af63dc4c');
  assert.equal(tokenTag('00112233445566778899aabbccddeeff'), 'de18ad43');
  assert.equal(tokenTag('ffeeddccbbaa99887766554433221100'), '789a7dc7');
});

test('settings keeps a non-default port and picks a gadget by id', async () => {
  const opened = [];
  const d = deps({ openUrl: async (url) => { opened.push(url); } });
  const store = new DeviceStore(d.dataDir);
  store.upsert({ id: 'a', name: 'A', addr: '10.0.0.5:80', token: 't' });
  store.upsert({ id: 'b', name: 'B', addr: '10.0.0.6:8080', token: 't' });
  const r = await run(['settings', 'b'], d);
  assert.equal(r.code, 0);
  assert.deepEqual(opened, ['http://10.0.0.6:8080/#me=af63e94c']);
  assert.equal((await run(['settings', 'zzz'], d)).code, 2);
});

test('settings with several gadgets and no id lists them and asks for one', async () => {
  const opened = [];
  const d = deps({ openUrl: async (url) => { opened.push(url); } });
  const store = new DeviceStore(d.dataDir);
  store.upsert({ id: 'a', name: 'A', addr: '10.0.0.5:80', token: 't' });
  store.upsert({ id: 'b', name: 'B', addr: '10.0.0.6:80', token: 't' });
  const r = await run(['settings'], d);
  assert.equal(r.code, 2);
  assert.match(r.out, /pass the id/);
  assert.match(r.out, /^a\tA\t10\.0\.0\.5:80$/m);
  assert.match(r.out, /^b\tB\t10\.0\.0\.6:80$/m);
  assert.deepEqual(opened, []);
});

test('settings with no paired gadget exits 2', async () => {
  const r = await run(['settings'], deps());
  assert.equal(r.code, 2);
  assert.match(r.out, /No paired Miblo gadgets/);
});

test('settings still prints the URL when the browser cannot be opened', async () => {
  const d = deps({ openUrl: async () => { throw new Error('no xdg-open'); } });
  new DeviceStore(d.dataDir).upsert({ id: 'g', name: 'G', addr: '10.0.0.5:80', token: 't' });
  const r = await run(['settings'], d);
  assert.equal(r.code, 0);
  assert.match(r.out, /http:\/\/10\.0\.0\.5\//);
  assert.match(r.out, /open the URL by hand/);
});

test('rename renames the gadget and updates the stored name', async () => {
  const dev = await startFakeDevice();
  const d = deps();
  try {
    await run(['pair', dev.addr, '4827'], d);
    const r = await run(['rename', 'miblo-4f2a', '  Office', 'desk '], d);
    assert.equal(r.code, 0);
    assert.equal(r.out.trim(), 'Renamed Miblo-4F2A to Office desk.');
    assert.equal(dev.state.config.name, 'Office desk');
    assert.equal(new DeviceStore(d.dataDir).list()[0].name, 'Office desk');
    const info = await new DeviceClient().info(dev.addr, new DeviceStore(d.dataDir).list()[0].token);
    assert.equal(info.name, 'Office desk');
    assert.match((await run(['rotate', '--status'], d)).out, /^Office desk \(miblo-4f2a\)/);
  } finally {
    await dev.close();
  }
});

test('rename --default restores the name the gadget reports', async () => {
  const dev = await startFakeDevice();
  const d = deps();
  try {
    await run(['pair', dev.addr, '4827'], d);
    await run(['rename', 'miblo-4f2a', 'Kitchen'], d);
    const r = await run(['rename', 'miblo-4f2a', '--default'], d);
    assert.equal(r.code, 0);
    assert.equal(r.out.trim(), 'Renamed Kitchen to Miblo-4F2A.');
    assert.equal(dev.state.config.name, '');
    assert.equal(new DeviceStore(d.dataDir).list()[0].name, 'Miblo-4F2A');
  } finally {
    await dev.close();
  }
});

test('rename --default falls back to the id-derived name when the gadget cannot be read back', async () => {
  const calls = [];
  const client = {
    setConfig: async (...args) => calls.push(args[2]),
    info: async () => { throw new Error('offline'); },
  };
  const d = deps({ client });
  new DeviceStore(d.dataDir).upsert({ id: 'miblo-b452', name: 'Desk', addr: '10.0.0.5:80', token: 't' });
  const r = await run(['rename', 'miblo-b452', '--default'], d);
  assert.equal(r.code, 0);
  assert.deepEqual(calls, [{ name: '' }]);
  assert.equal(new DeviceStore(d.dataDir).list()[0].name, 'Miblo-B452');
});

test('rename validates the name client-side', async () => {
  const d = deps();
  new DeviceStore(d.dataDir).upsert({ id: 'g', name: 'G', addr: '10.0.0.5:80', token: 't' });
  const cases = [
    [[], /Usage: rename/],
    [['g'], /Usage: rename/],
    [['g', '   '], /name is empty/],
    [['g', '\u0007\u001b'], /name is empty/],
    [['g', 'a'.repeat(21)], /too long: 21 characters, the maximum is 20/],
    [['g', 'Twenty', 'one', 'chars', 'xxxx'], /too long: 21 characters/],
    [['g', '\u{1F600}'.repeat(16)], /63 bytes at most/],
  ];
  for (const [args, re] of cases) {
    const r = await run(['rename', ...args], d);
    assert.equal(r.code, 2, JSON.stringify(args));
    assert.match(r.out, re, JSON.stringify(args));
  }
  // 20 code points is fine, even when some take two UTF-16 units
  const client = { setConfig: async () => {} };
  const ok = await run(['rename', 'g', `${'x'.repeat(19)}\u{1F600}`], { ...d, client });
  assert.equal(ok.code, 0);
});

test('rename strips control characters from the name', async () => {
  const calls = [];
  const d = deps({ client: { setConfig: async (...args) => calls.push(args[2]) } });
  new DeviceStore(d.dataDir).upsert({ id: 'g', name: 'G', addr: '10.0.0.5:80', token: 't' });
  const r = await run(['rename', 'g', 'Of\u001b[31mfice\n‮desk'], d);
  assert.equal(r.code, 0);
  assert.deepEqual(calls, [{ name: 'Of [31mfice desk' }]);
  assert.equal(r.out, 'Renamed G to Of [31mfice desk.\n');
});

test('rename: unknown id, offline gadget and device-side rejection', async () => {
  const d = deps();
  new DeviceStore(d.dataDir).upsert({ id: 'g', name: 'Desk', addr: '127.0.0.1:1', token: 't' });

  const unknown = await run(['rename', 'nope`x`', 'Office'], d);
  assert.equal(unknown.code, 2);
  assert.equal(unknown.out.trim(), 'No paired gadget with id nopex.');

  const offline = await run(['rename', 'g', 'Office'], d);
  assert.equal(offline.code, 1);
  assert.equal(offline.out.trim(), 'Could not reach Desk.');
  assert.equal(new DeviceStore(d.dataDir).list()[0].name, 'Desk');

  const rejecting = {
    setConfig: async () => { const e = new Error('400'); e.status = 400; e.data = { error: 'invalid', field: 'name' }; throw e; },
  };
  const bad = await run(['rename', 'g', 'Office'], { ...d, client: rejecting });
  assert.equal(bad.code, 2);
  assert.match(bad.out, /Desk rejected the name/);
  assert.equal(new DeviceStore(d.dataDir).list()[0].name, 'Desk');
});

test('rename sanitizes the stored and gadget-reported names it prints', async () => {
  const client = { setConfig: async () => {}, info: async () => ({ name: 'Ev\u001bil\n<b>' }) };
  const d = deps({ client });
  new DeviceStore(d.dataDir).upsert({ id: 'g', name: 'Old\u001b[2Jname$(ls)', addr: '10.0.0.5:80', token: 't' });
  const r = await run(['rename', 'g', 'New'], d);
  assert.equal(r.out.trim(), 'Renamed Old2Jnamels to New.');
  const back = await run(['rename', 'g', '--default'], d);
  assert.equal(back.out.trim(), 'Renamed New to Evilb.');
  assert.equal(new DeviceStore(d.dataDir).list()[0].name, 'Evilb');
});

test('the fake device validates the name like the firmware', async () => {
  const dev = await startFakeDevice();
  try {
    const client = new DeviceClient();
    const token = await client.pair(dev.addr, '4827', 'h');
    for (const name of ['a'.repeat(21), 42, '\u{1F600}'.repeat(16)]) {
      await assert.rejects(client.setConfig(dev.addr, token, { name }), (e) => e.status === 400 && e.data.field === 'name');
    }
    assert.equal((await client.info(dev.addr, token)).name, 'Miblo-4F2A');
    await client.setConfig(dev.addr, token, { name: 'Café' });
    assert.equal((await client.info(dev.addr, token)).name, 'Café');
  } finally {
    await dev.close();
  }
});

test('owner sends the owner name and a normalized birthday, and stores neither', async () => {
  const dev = await startFakeDevice();
  const d = deps();
  try {
    await run(['pair', dev.addr, '4827'], d);
    const r = await run(['owner', 'miblo-4f2a', '--name', 'Ana', 'Maria', '--birthday', '14/03'], d);
    assert.equal(r.code, 0);
    assert.equal(r.out.trim(), 'Miblo-4F2A now knows your name (Ana Maria) and birthday (03-14).');
    assert.equal(dev.state.config.owner, 'Ana Maria');
    assert.equal(dev.state.config.birthday, '03-14');
    const info = await new DeviceClient().info(dev.addr, new DeviceStore(d.dataDir).list()[0].token);
    assert.ok(!('owner' in info) && !('birthday' in info));
    const stored = JSON.stringify(new DeviceStore(d.dataDir).list());
    assert.ok(!stored.includes('Ana') && !stored.includes('03-14'));

    assert.equal((await run(['owner', 'miblo-4f2a', '--birthday', '02-29'], d)).out.trim(),
      'Miblo-4F2A now knows your birthday (02-29).');
    assert.equal(dev.state.config.owner, 'Ana Maria');  // untouched

    const clear = await run(['owner', 'miblo-4f2a', '--birthday', 'clear', '--name', 'clear'], d);
    assert.equal(clear.out.trim(), 'Miblo-4F2A forgot your name and birthday.');
    assert.deepEqual([dev.state.config.owner, dev.state.config.birthday], ['', '']);

    const mixed = await run(['owner', 'miblo-4f2a', '--name', 'Bo', '--birthday', 'clear'], d);
    assert.equal(mixed.out.trim(), 'Miblo-4F2A now knows your name (Bo) and forgot your birthday.');
  } finally {
    await dev.close();
  }
});

test('owner validates its arguments client-side', async () => {
  const d = deps();
  new DeviceStore(d.dataDir).upsert({ id: 'g', name: 'G', addr: '10.0.0.5:80', token: 't' });
  const cases = [
    [[], /Usage: owner/],
    [['g'], /Give --name, --birthday or both/],
    [['g', '--name'], /--name needs a name/],
    [['g', '--name', '\u0007'], /--name needs a name/],
    [['g', '--name', 'a'.repeat(21)], /too long: 21 characters/],
    [['g', '--birthday'], /--birthday needs one date/],
    [['g', '--birthday', '14', '03'], /--birthday needs one date/],
    [['g', '--birthday', '30/02'], /Invalid birthday "30\/02"/],
    [['g', '--birthday', '31/04'], /real calendar day/],
    [['g', '--birthday', '13-01'], /Invalid birthday/],
    [['g', '--birthday', '2024-03-14'], /Invalid birthday/],
    [['g', '--name', 'A', '--name', 'B'], /given twice/],
    [['g', 'Ana'], /Unexpected argument "Ana"/],
    [['g', '--age', '3'], /Unexpected argument "--age"/],
  ];
  for (const [args, re] of cases) {
    const r = await run(['owner', ...args], d);
    assert.equal(r.code, 2, JSON.stringify(args));
    assert.match(r.out, re, JSON.stringify(args));
  }
});

test('owner: control characters stripped, unknown id, offline and device-side rejection', async () => {
  const calls = [];
  const d = deps({ client: { setConfig: async (...args) => calls.push(args[2]) } });
  new DeviceStore(d.dataDir).upsert({ id: 'g', name: 'De\u001bsk', addr: '127.0.0.1:1', token: 't' });
  const r = await run(['owner', 'g', '--name', 'A\u001b[2Jna', '--birthday', '1/2'], d);
  assert.equal(r.out, 'Desk now knows your name (A [2Jna) and birthday (02-01).\n');
  assert.deepEqual(calls, [{ owner: 'A [2Jna', birthday: '02-01' }]);

  assert.equal((await run(['owner', 'zz', '--name', 'Ana'], d)).code, 2);

  const offline = await run(['owner', 'g', '--name', 'Ana'], { ...d, client: new DeviceClient() });
  assert.equal(offline.code, 1);
  assert.equal(offline.out.trim(), 'Could not reach Desk.');

  const rejecting = (field) => ({
    setConfig: async () => { const e = new Error('400'); e.status = 400; e.data = { error: 'invalid', field }; throw e; },
  });
  const badBirthday = await run(['owner', 'g', '--birthday', '01/01'], { ...d, client: rejecting('birthday') });
  assert.equal(badBirthday.code, 2);
  assert.match(badBirthday.out, /Desk rejected the birthday/);
  const badName = await run(['owner', 'g', '--name', 'Ana'], { ...d, client: rejecting('owner') });
  assert.equal(badName.code, 2);
  assert.match(badName.out, /Desk rejected the name/);
});

test('the fake device validates owner and birthday like the firmware', async () => {
  const dev = await startFakeDevice();
  try {
    const client = new DeviceClient();
    const token = await client.pair(dev.addr, '4827', 'h');
    const bad = [[{ owner: 'a'.repeat(21) }, 'owner'], [{ owner: 1 }, 'owner'], [{ birthday: '02-30' }, 'birthday'],
      [{ birthday: '14/03' }, 'birthday'], [{ birthday: '00-10' }, 'birthday'], [{ birthday: 314 }, 'birthday']];
    for (const [patch, field] of bad) {
      await assert.rejects(client.setConfig(dev.addr, token, patch), (e) => e.status === 400 && e.data.field === field);
    }
    await client.setConfig(dev.addr, token, { owner: 'Zoë', birthday: '02-29' });
    assert.deepEqual([dev.state.config.owner, dev.state.config.birthday], ['Zoë', '02-29']);
    await client.setConfig(dev.addr, token, { owner: '', birthday: '' });
    assert.deepEqual([dev.state.config.owner, dev.state.config.birthday], ['', '']);
  } finally {
    await dev.close();
  }
});

test('demo puts every paired gadget in pet mode, and can stop it', async () => {
  const a = await startFakeDevice({ id: 'miblo-aaaa', name: 'Amon', code: '1111' });
  const b = await startFakeDevice({ id: 'miblo-bbbb', name: 'Shiru', code: '2222' });
  const d = deps();
  try {
    await run(['pair', a.addr, '1111'], d);
    await run(['pair', b.addr, '2222'], d);
    const on = await run(['demo'], d);
    assert.equal(on.code, 0);
    assert.match(on.out, /Demo on for 10 min on Amon, Shiru\./);
    assert.equal(a.state.demoMinutes, 10);
    assert.equal(b.state.demoMinutes, 10);
    assert.match((await run(['demo', '5'], d)).out, /Demo on for 5 min on Amon, Shiru\./);
    assert.equal(b.state.demoMinutes, 5);
    assert.match((await run(['demo', 'stop'], d)).out, /Demo off on Amon, Shiru\./);
    assert.equal(a.state.demoMinutes, 0);
    const bad = await run(['demo', '99'], d);
    assert.equal(bad.code, 2);
    assert.match(bad.out, /from 1 to 30/);
    assert.equal((await run(['demo', 'miblo-aaaa'], d)).code, 2);  // no per-gadget demo
  } finally {
    await a.close();
    await b.close();
  }
});

test('demo needs at least two paired Miblos', async () => {
  const a = await startFakeDevice({ id: 'miblo-aaaa', name: 'Amon', code: '1111' });
  const d = deps();
  try {
    assert.equal((await run(['demo'], d)).code, 2);
    await run(['pair', a.addr, '1111'], d);
    const one = await run(['demo'], d);
    assert.equal(one.code, 2);
    assert.match(one.out, /at least two paired Miblos \(1 paired\)/);
    assert.equal(a.state.demoMinutes, undefined);
  } finally {
    await a.close();
  }
});

test('demo explains old firmware and offline gadgets', async () => {
  const d = deps();
  const store = new DeviceStore(d.dataDir);
  store.upsert({ id: 'miblo-cccc', name: 'Old', addr: '127.0.0.1:9', token: 't' });
  store.upsert({ id: 'miblo-dddd', name: 'Gone', addr: '127.0.0.1:9', token: 't' });
  const off = await run(['demo'], d);
  assert.equal(off.code, 1);
  assert.match(off.out, /Old is offline/);
  const client = { demo: async () => { const e = new Error('404'); e.status = 404; throw e; } };
  const old = await run(['demo'], { ...d, client });
  assert.match(old.out, /does not support the demo yet/);
});

test('pair a gadget already paired to another computer: listed by id, named once paired', async () => {
  const dev = await startFakeDevice({ id: 'miblo-b452', name: 'Desk', tokens: ['someone-else'] });
  const d = deps();
  try {
    assert.deepEqual(Object.keys(await new DeviceClient().info(dev.addr)).sort(), ['id', 'paired', 'proto']);
    const r = await run(['pair', dev.addr, '4827'], d);
    assert.equal(r.code, 0, r.out);
    assert.equal(r.out.trim(), `Paired with Desk (miblo-b452) at ${dev.addr}.`);
    assert.equal(new DeviceStore(d.dataDir).list()[0].name, 'Desk');
  } finally {
    await dev.close();
  }
});

test('pair falls back to the id-derived name when the paired gadget cannot be read again', async () => {
  const seen = [];
  const client = {
    info: async (addr, token) => {
      seen.push(token);
      if (token) throw new Error('offline');
      return { id: 'miblo-b452', paired: true, proto: 1 };
    },
    pair: async () => 'tok',
    setConfig: async () => {},
  };
  const d = deps({ client });
  const r = await run(['pair', '10.0.0.9', '4827'], d);
  assert.equal(r.code, 0, r.out);
  assert.equal(r.out.trim(), 'Paired with Miblo-B452 (miblo-b452) at 10.0.0.9:80.');
  assert.deepEqual(seen, [undefined, 'tok']);
  assert.equal(new DeviceStore(d.dataDir).list()[0].name, 'Miblo-B452');
});

test('rotate, night and rename read a paired gadget with its token', async () => {
  const dev = await startFakeDevice();
  const d = deps();
  try {
    await run(['pair', dev.addr, '4827'], d);
    assert.match((await run(['rotate', '--status'], d)).out, /rotation off/);
    assert.match((await run(['night', '--status'], d)).out, /night mode off/);
    await run(['rename', 'miblo-4f2a', 'Kitchen'], d);
    await run(['rename', 'miblo-4f2a', '--default'], d);
    assert.equal(new DeviceStore(d.dataDir).list()[0].name, 'Miblo-4F2A');
  } finally {
    await dev.close();
  }
});

test('status commands survive the reduced /api/info of a gadget that dropped this pairing', async () => {
  const dev = await startFakeDevice({ tokens: ['someone-else'] });
  const d = deps();
  try {
    new DeviceStore(d.dataDir).upsert({ id: 'miblo-4f2a', name: 'Miblo-4F2A', addr: dev.addr, token: 'stale' });
    const rot = await run(['rotate', '--status'], d);
    assert.equal(rot.code, 0);
    assert.match(rot.out, /no longer accepts this pairing/);
    const night = await run(['night', '--status'], d);
    assert.match(night.out, /no longer accepts this pairing/);
  } finally {
    await dev.close();
  }
});

// ---- a busy gadget (503, low on memory) is called busy, never offline or unreachable ----
const BUSY = /Miblo-4F2A is busy right now — try again in a moment\./;

test('every gadget command says "busy" for a 503, not offline', async () => {
  const dev = await startFakeDevice();
  const d = deps({ client: new DeviceClient({ busyRetryMs: [1, 1] }) });
  try {
    await run(['pair', dev.addr, '4827'], d);
    dev.state.busyLeft = Infinity;
    const id = 'miblo-4f2a';
    for (const argv of [['rotate', '--status'], ['night', '--status']]) {
      const r = await run(argv, d);
      assert.match(r.out, /Miblo-4F2A \(miblo-4f2a\): busy right now — try again in a moment/, argv.join(' '));
      assert.doesNotMatch(r.out, /offline/);
    }
    for (const argv of [['rotate', 'on'], ['night', 'off'], ['rename', id, 'Desk'], ['owner', id, '--name', 'Ana'],
      ['mode', 'limits'], ['reset', id]]) {
      const r = await run(argv, d);
      assert.match(r.out, BUSY, argv.join(' '));
      assert.doesNotMatch(r.out, /offline|Could not reach/, argv.join(' '));
    }
    assert.equal(new DeviceStore(d.dataDir).list().length, 1);  // a busy reset keeps the pairing
  } finally {
    await dev.close();
  }
});

test('demo says busy for a 503', async () => {
  const a = await startFakeDevice();
  const b = await startFakeDevice({ id: 'miblo-4f2b', name: 'Miblo-4F2B' });
  const d = deps({ client: new DeviceClient({ busyRetryMs: [1, 1] }) });
  try {
    await run(['pair', a.addr, '4827'], d);
    await run(['pair', b.addr, '4827'], d);
    a.state.busyLeft = Infinity;
    const r = await run(['demo'], d);
    assert.match(r.out, BUSY);
    assert.match(r.out, /Demo on for 10 min on Miblo-4F2B\./);
  } finally {
    await a.close();
    await b.close();
  }
});

test('pair: a gadget busy for a moment still pairs; one that stays busy is called busy', async () => {
  const dev = await startFakeDevice({ busy: 2 });
  const d = deps({ client: new DeviceClient({ busyRetryMs: [1, 1] }) });
  try {
    assert.equal((await run(['pair', dev.addr, '4827'], d)).code, 0);
    dev.state.busyLeft = Infinity;
    const r = await run(['pair', dev.addr, '4827'], d);
    assert.equal(r.code, 1);
    assert.match(r.out, new RegExp(`The Miblo gadget at ${dev.addr} is busy right now — try again in a moment\\.`));
  } finally {
    await dev.close();
  }
});

test('pair with no answer to the pairing request itself says the code may be used up', async () => {
  const d = deps({
    client: { info: async () => ({ id: 'miblo-4f2a', paired: false }), pair: async () => { throw new TypeError('fetch failed'); } },
  });
  const r = await run(['pair', '10.0.0.7', '4827'], d);
  assert.equal(r.code, 1);
  assert.match(r.out, /No answer from the Miblo gadget at 10\.0\.0\.7:80 while pairing/);
});

test('MIBLO_DISCOVER_JSON replaces mDNS discovery (tests only); unset or invalid, mDNS is used', async () => {
  const list = [{ id: 'miblo-4f2a', name: 'Desk', addr: '127.0.0.1:8080' }];
  assert.deepEqual(await fixedDiscovery({ MIBLO_TEST: '1', MIBLO_DISCOVER_JSON: JSON.stringify(list) })(), list);
  // Outside tests (MIBLO_TEST=1) it is ignored: a stray variable never redirects pairing.
  assert.equal(fixedDiscovery({ MIBLO_DISCOVER_JSON: JSON.stringify(list) }), null);
  assert.equal(fixedDiscovery({ MIBLO_TEST: '0', MIBLO_DISCOVER_JSON: JSON.stringify(list) }), null);
  assert.equal(fixedDiscovery({ MIBLO_TEST: '1' }), null);
  assert.equal(fixedDiscovery({ MIBLO_TEST: '1', MIBLO_DISCOVER_JSON: 'not json' }), null);
  assert.equal(fixedDiscovery({ MIBLO_TEST: '1', MIBLO_DISCOVER_JSON: '{"id":"x"}' }), null);
});

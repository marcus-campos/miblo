import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { startFakeDevice } from './fakes/fake-device.js';
import { DeviceClient } from '../lib/device-client.js';
import { DeviceStore } from '../lib/device-store.js';
import { run } from '../bin/miblo.js';

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
  assert.deepEqual(opened, ['http://192.168.0.176/']);
  assert.match(r.out, /Miblo-4F2A settings: http:\/\/192\.168\.0\.176\//);
  assert.match(r.out, /http:\/\/miblo-4f2a\.local/);
  assert.match(r.out, /Opened in your browser/);
  assert.ok(!r.out.includes('secret'));
});

test('settings keeps a non-default port and picks a gadget by id', async () => {
  const opened = [];
  const d = deps({ openUrl: async (url) => { opened.push(url); } });
  const store = new DeviceStore(d.dataDir);
  store.upsert({ id: 'a', name: 'A', addr: '10.0.0.5:80', token: 't' });
  store.upsert({ id: 'b', name: 'B', addr: '10.0.0.6:8080', token: 't' });
  const r = await run(['settings', 'b'], d);
  assert.equal(r.code, 0);
  assert.deepEqual(opened, ['http://10.0.0.6:8080/']);
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

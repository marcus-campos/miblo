import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { updateNotice, CHECK_EVERY_MS, NOTIFY_EVERY_MS } from '../lib/update-notice.js';

const json = (body, status = 200) => ({ ok: status < 400, status, json: async () => body });

function setup({ tag = 'v0.3.0', fw = '0.2.4', devices = [{ id: 'miblo-b452', name: 'Miblo-B452', addr: '10.0.0.5' }] } = {}) {
  const dataDir = fs.mkdtempSync(path.join(os.tmpdir(), 'miblo-upd-'));
  const calls = [];
  const fetchImpl = async (url) => {
    calls.push(url);
    if (url.includes('api.github.com')) {
      if (tag instanceof Error) throw tag;
      return json({ tag_name: tag });
    }
    if (fw instanceof Error) throw fw;
    return json({ fw, board: 'geekmagic_ultra' });
  };
  return { dataDir, calls, store: { list: () => devices }, fetchImpl };
}

test('names the outdated plugin and gadgets', async () => {
  const s = setup();
  const msg = await updateNotice({ ...s, pluginVersion: '0.2.4', now: () => 0 });
  assert.match(msg, /Miblo 0\.3\.0 is available/);
  assert.match(msg, /plugin 0\.2\.4/);
  assert.match(msg, /Miblo-B452 0\.2\.4/);
  assert.match(msg, /\/miblo:update/);
});

test('silent when everything is up to date', async () => {
  const s = setup({ fw: '0.3.0' });
  assert.equal(await updateNotice({ ...s, pluginVersion: '0.3.0', now: () => 0 }), null);
});

test('an offline gadget is skipped, the plugin is still reported', async () => {
  const s = setup({ fw: new Error('timeout') });
  const msg = await updateNotice({ ...s, pluginVersion: '0.2.4', now: () => 0 });
  assert.match(msg, /plugin 0\.2\.4/);
  assert.doesNotMatch(msg, /Miblo-B452/);
});

test('GitHub is asked at most every CHECK_EVERY_MS, the notice once per NOTIFY_EVERY_MS', async () => {
  const s = setup();
  const gh = () => s.calls.filter((u) => u.includes('api.github.com')).length;
  assert.ok(await updateNotice({ ...s, pluginVersion: '0.2.4', now: () => 0 }));
  assert.equal(await updateNotice({ ...s, pluginVersion: '0.2.4', now: () => 1000 }), null);
  assert.equal(gh(), 1);
  assert.equal(await updateNotice({ ...s, pluginVersion: '0.2.4', now: () => CHECK_EVERY_MS + 1 }), null);
  assert.equal(gh(), 2);
  assert.ok(await updateNotice({ ...s, pluginVersion: '0.2.4', now: () => NOTIFY_EVERY_MS + 1 }));
});

test('offline or no release: no notice and no error', async () => {
  const s = setup({ tag: new Error('offline') });
  assert.equal(await updateNotice({ ...s, pluginVersion: '0.2.4', now: () => 0 }), null);
  const t = setup({ tag: 'nightly' });
  assert.equal(await updateNotice({ ...t, pluginVersion: '0.2.4', now: () => 0 }), null);
});

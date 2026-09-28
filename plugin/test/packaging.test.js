import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const read = (p) => JSON.parse(fs.readFileSync(path.join(root, p), 'utf8'));
const EVENTS = ['SessionStart', 'UserPromptSubmit', 'PreToolUse', 'PermissionRequest', 'PostToolUse', 'Notification', 'Stop', 'SessionEnd'];

test('manifest and marketplace agree on the plugin name', () => {
  assert.equal(read('.claude-plugin/plugin.json').name, 'miblo');
  const mk = read('../.claude-plugin/marketplace.json');
  assert.equal(mk.plugins[0].name, 'miblo');
  assert.equal(mk.plugins[0].source, './plugin');
});

test('every tracked event runs hook.js asynchronously', () => {
  const { hooks } = read('hooks/hooks.json');
  for (const ev of EVENTS) {
    const cmds = hooks[ev].flatMap((m) => m.hooks);
    const h = cmds.find((c) => c.command.includes('bin/hook.js'));
    assert.ok(h, ev);
    assert.equal(h.async, true, ev);
  }
  assert.equal(hooks.Notification[0].matcher, 'elicitation_dialog');
  const onboard = hooks.SessionStart.flatMap((m) => m.hooks).find((c) => c.command.includes('bin/onboard.js'));
  assert.ok(onboard);
  assert.notEqual(onboard.async, true);
});

test('the /miblo command references the CLI with the data dir', () => {
  const md = fs.readFileSync(path.join(root, 'commands/miblo.md'), 'utf8');
  assert.match(md, /\$\{CLAUDE_PLUGIN_ROOT\}\/bin\/miblo\.js/);
  assert.match(md, /\$\{CLAUDE_PLUGIN_DATA\}/);
  assert.match(md, /\$ARGUMENTS/);
});

import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const read = (p) => JSON.parse(fs.readFileSync(path.join(root, p), 'utf8'));
const EVENTS = ['SessionStart', 'UserPromptSubmit', 'PreToolUse', 'PermissionRequest', 'PermissionDenied', 'PostToolUse', 'PostToolUseFailure',
  'Notification', 'Elicitation', 'ElicitationResult', 'SubagentStart', 'SubagentStop', 'Stop', 'StopFailure', 'PreCompact', 'PostCompact', 'SessionEnd'];

test('manifest and marketplace agree on the plugin name', () => {
  assert.equal(read('.claude-plugin/plugin.json').name, 'miblo');
  const mk = read('../.claude-plugin/marketplace.json');
  assert.equal(mk.plugins[0].name, 'miblo');
  assert.equal(mk.plugins[0].source, './plugin');
  assert.equal(mk.description, 'Miblo desk gadget for Claude Code: session status, alerts and usage limits.');
  assert.equal(read('package.json').engines.node, '>=20');
});

test('every tracked event runs hook.js asynchronously', () => {
  const { hooks } = read('hooks/hooks.json');
  for (const ev of EVENTS) {
    const cmds = hooks[ev].flatMap((m) => m.hooks);
    const h = cmds.find((c) => c.command.includes('bin/hook.js'));
    assert.ok(h, ev);
    assert.equal(h.async, true, ev);
  }
  assert.equal(hooks.Notification[0].matcher,
    'permission_prompt|worker_permission_prompt|elicitation_dialog|elicitation_url_dialog|agent_needs_input');
  // Every registration spawns a hook process: nothing the tracker ignores is registered.
  assert.deepEqual(Object.keys(hooks).sort(), [...EVENTS].sort());
  const onboard = hooks.SessionStart.flatMap((m) => m.hooks).find((c) => c.command.includes('bin/onboard.js'));
  assert.ok(onboard);
  assert.notEqual(onboard.async, true);
});

const COMMANDS = ['pair', 'status', 'mode', 'rotate', 'night', 'settings', 'rename', 'owner', 'demo', 'link-statusline', 'unlink-statusline', 'reset',
  // daily life
  'focus', 'meeting', 'find', 'timer', 'say', 'remind', 'countdown', 'blue', 'today', 'limits'];

test('miblo.md was split into one command per action (plugins namespace commands as /miblo:<file>)', () => {
  assert.ok(!fs.existsSync(path.join(root, 'commands/miblo.md')));
  for (const name of COMMANDS) {
    assert.ok(fs.existsSync(path.join(root, `commands/${name}.md`)), name);
  }
});

for (const name of COMMANDS) {
  test(`the /miblo:${name} command references the CLI with the data dir`, () => {
    const md = fs.readFileSync(path.join(root, `commands/${name}.md`), 'utf8');
    assert.match(md, /\$\{CLAUDE_PLUGIN_ROOT\}\/bin\/miblo\.js/);
    assert.match(md, /\$\{CLAUDE_PLUGIN_DATA\}/);
    assert.match(md, /\$ARGUMENTS/);
  });

  // pair also offers the update, see below
  if (name !== 'pair') {
    test(`the /miblo:${name} command pre-approves only the miblo CLI`, () => {
      const md = fs.readFileSync(path.join(root, `commands/${name}.md`), 'utf8');
      const line = md.split(/\r?\n/).find((l) => l.startsWith('allowed-tools:'));
      assert.equal(line, 'allowed-tools: Bash(node "${CLAUDE_PLUGIN_ROOT}/bin/miblo.js":*), AskUserQuestion');
      assert.ok(!line.includes('Bash(node:*)'));
    });
  }

  test(`the /miblo:${name} command has its own description`, () => {
    const md = fs.readFileSync(path.join(root, `commands/${name}.md`), 'utf8');
    const line = md.split(/\r?\n/).find((l) => l.startsWith('description:'));
    assert.ok(line && line.length > 'description:'.length, name);
  });
}

// /miblo:pair offers the update right after pairing, so it allows the same commands as /miblo:update.
for (const name of ['update', 'pair']) {
  test(`the /miblo:${name} command references the CLI and pre-approves only miblo.js and the two plugin-update commands`, () => {
    const md = fs.readFileSync(path.join(root, `commands/${name}.md`), 'utf8');
    assert.match(md, /\$\{CLAUDE_PLUGIN_ROOT\}\/bin\/miblo\.js/);
    assert.match(md, /\$\{CLAUDE_PLUGIN_DATA\}/);
    assert.match(md, /\$ARGUMENTS/);
    assert.match(md, /^description: \S/m);
    const line = md.split(/\r?\n/).find((l) => l.startsWith('allowed-tools:'));
    assert.equal(line, 'allowed-tools: Bash(node "${CLAUDE_PLUGIN_ROOT}/bin/miblo.js":*), Bash(claude plugin marketplace update miblo), Bash(claude plugin update miblo@miblo), AskUserQuestion');
  });
}

// The daily-life commands take free text: the user's arguments must never be pasted into a shell
// line; Claude builds the command (free text only as one single-quoted argument).
for (const name of ['focus', 'meeting', 'find', 'timer', 'say', 'remind', 'countdown', 'blue', 'today', 'limits']) {
  test(`the /miblo:${name} command never pastes $ARGUMENTS into a command`, () => {
    const md = fs.readFileSync(path.join(root, `commands/${name}.md`), 'utf8');
    const uses = md.split(/\r?\n/).filter((l) => l.includes('$ARGUMENTS'));
    assert.deepEqual(uses, ['Arguments: `$ARGUMENTS`']);
    assert.match(md, /^Safety: never paste the arguments above/m);
  });
}

// The one-step installer (install.sh at the repository root) run against a fake `claude` that
// records its arguments: no network, nothing really installed.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
const script = path.join(repo, 'install.sh');
const skip = process.platform === 'win32' ? 'install.sh is for macOS and Linux' : false;

// A fake Claude Code CLI. Its state (marketplace added, plugin installed/enabled) lives in files
// next to the log, so a run that adds or installs is seen by the calls after it.
const FAKE = `#!/bin/sh
state='STATE'
printf '%s|%s\\n' "$0" "$*" >> "$state/log"
if read -r line; then printf 'stdin|%s\\n' "$line" >> "$state/log"; fi
case "$*" in
  --version) echo '9.9.9 (Claude Code)' ;;
  'plugin marketplace list --json')
    if [ -f "$state/market" ]; then printf '[\\n  {\\n    "name": "miblo",\\n    "source": "git"\\n  }\\n]\\n'; else echo '[]'; fi ;;
  'plugin marketplace add '*) [ -f "$state/fail-add" ] && exit 1; touch "$state/market" ;;
  'plugin marketplace update miblo') ;;
  'plugin list --json')
    if [ -f "$state/plugin" ]; then
      printf '[\\n  {\\n    "id": "other@x",\\n    "enabled": false\\n  },\\n  {\\n    "id": "miblo@miblo",\\n    "enabled": %s\\n  }\\n]\\n' "$(cat "$state/plugin")"
    else echo '[]'; fi ;;
  'plugin install miblo@miblo') echo true > "$state/plugin" ;;
  'plugin update miblo@miblo') ;;
  'plugin enable miblo@miblo') echo true > "$state/plugin" ;;
  *) exit 64 ;;
esac
`;

function setup() {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'miblo-installer-'));
  const home = path.join(dir, 'home');
  const bin = path.join(dir, 'bin');
  const state = path.join(dir, 'state');
  for (const d of [home, bin, state]) fs.mkdirSync(d);
  const placeFake = (where) => {
    fs.mkdirSync(path.dirname(where), { recursive: true });
    fs.writeFileSync(where, FAKE.replace('STATE', state), { mode: 0o755 });
    return where;
  };
  const run = ({ pipe = false } = {}) => {
    const env = { HOME: home, PATH: `${bin}:/usr/bin:/bin`, MIBLO_SYSTEM_BIN_DIRS: path.join(dir, 'none') };
    // pipe: the script arrives on standard input, as with `curl ... | sh`.
    const r = pipe
      ? spawnSync('sh', [], { env, input: fs.readFileSync(script), encoding: 'utf8' })
      : spawnSync('sh', [script], { env, input: '', encoding: 'utf8' });
    const logFile = path.join(state, 'log');
    const lines = fs.existsSync(logFile) ? fs.readFileSync(logFile, 'utf8').trim().split('\n') : [];
    return { ...r, lines, calls: lines.map((l) => l.split('|')[1]).filter((a) => a !== '--version') };
  };
  return { dir, home, bin, state, placeFake, run };
}

test('a Claude Code CLI on PATH: adds the marketplace over HTTPS, installs the plugin', { skip }, () => {
  const t = setup();
  const fake = t.placeFake(path.join(t.bin, 'claude'));
  const r = t.run({ pipe: true });
  assert.equal(r.status, 0, r.stdout + r.stderr);
  assert.deepEqual(r.calls, [
    'plugin marketplace list --json',
    'plugin marketplace add https://github.com/marcus-campos/miblo.git',
    'plugin list --json',
    'plugin install miblo@miblo',
  ]);
  assert.ok(r.lines.every((l) => l.startsWith(`${fake}|`)), r.lines.join('\n'));
  // Nothing the CLI ran could read the rest of the piped script.
  assert.ok(!r.lines.some((l) => l.startsWith('stdin|')), r.lines.join('\n'));
  assert.match(r.stdout, /Miblo is installed/);
  assert.match(r.stdout, /\/miblo:pair/);
});

test('only the Claude desktop app: uses its newest bundled CLI', { skip }, () => {
  const t = setup();
  const base = path.join(t.home, 'Library/Application Support/Claude/claude-code');
  const at = (ver, hash) => path.join(base, ver, hash, 'claude.app/Contents/MacOS/claude');
  t.placeFake(at('2.1.9', 'aaa'));
  const newest = t.placeFake(at('2.1.10', 'bbb'));
  t.placeFake(at('2.0.99', 'ccc'));
  const r = t.run();
  assert.equal(r.status, 0, r.stdout + r.stderr);
  assert.ok(r.lines.every((l) => l.startsWith(`${newest}|`)), r.lines.join('\n'));
  assert.ok(r.calls.includes('plugin install miblo@miblo'));
  assert.ok(r.stdout.includes(newest));
});

test('the usual install location when PATH lacks it (~/.local/bin)', { skip }, () => {
  const t = setup();
  const fake = t.placeFake(path.join(t.home, '.local/bin/claude'));
  const r = t.run();
  assert.equal(r.status, 0, r.stdout + r.stderr);
  assert.ok(r.lines.every((l) => l.startsWith(`${fake}|`)));
});

test('already installed: refreshes the marketplace and updates the plugin', { skip }, () => {
  const t = setup();
  t.placeFake(path.join(t.bin, 'claude'));
  fs.writeFileSync(path.join(t.state, 'market'), '');
  fs.writeFileSync(path.join(t.state, 'plugin'), 'true');
  const r = t.run();
  assert.equal(r.status, 0, r.stdout + r.stderr);
  assert.deepEqual(r.calls, [
    'plugin marketplace list --json',
    'plugin marketplace update miblo',
    'plugin list --json',
    'plugin update miblo@miblo',
  ]);
  assert.match(r.stdout, /Already installed; updating it/);
});

test('installed but turned off: updates it and turns it back on', { skip }, () => {
  const t = setup();
  t.placeFake(path.join(t.bin, 'claude'));
  fs.writeFileSync(path.join(t.state, 'market'), '');
  fs.writeFileSync(path.join(t.state, 'plugin'), 'false');
  const r = t.run();
  assert.equal(r.status, 0, r.stdout + r.stderr);
  assert.deepEqual(r.calls.slice(-2), ['plugin update miblo@miblo', 'plugin enable miblo@miblo']);
});

test('no Claude Code anywhere: explains how to get it and exits 1', { skip }, () => {
  const t = setup();
  const r = t.run();
  assert.equal(r.status, 1);
  assert.match(r.stdout, /Claude Code was not found/);
  assert.match(r.stdout, /claude\.ai\/download/);
  assert.deepEqual(r.lines, []);
});

test('the marketplace cannot be added: exits 2 and installs nothing', { skip }, () => {
  const t = setup();
  t.placeFake(path.join(t.bin, 'claude'));
  fs.writeFileSync(path.join(t.state, 'fail-add'), '');
  const r = t.run();
  assert.equal(r.status, 2);
  assert.ok(!r.calls.includes('plugin install miblo@miblo'));
});

test('"Install Miblo.command" is the same script, executable', () => {
  const command = path.join(repo, 'Install Miblo.command');
  assert.equal(fs.readFileSync(command, 'utf8'), fs.readFileSync(script, 'utf8'));
  if (process.platform !== 'win32') {
    assert.ok(fs.statSync(command).mode & 0o111);
    assert.ok(fs.statSync(script).mode & 0o111);
  }
});

test('install.ps1 installs the same marketplace and plugin', () => {
  const ps = fs.readFileSync(path.join(repo, 'install.ps1'), 'utf8');
  assert.ok(ps.includes("'https://github.com/marcus-campos/miblo.git'"));
  assert.ok(ps.includes("'miblo@miblo'"));
  assert.match(ps, /\/miblo:pair/);
});

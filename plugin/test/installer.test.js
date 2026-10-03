// The one-step installer (install.sh at the repository root) run against a fake `claude` that
// records its arguments: no network, nothing really installed.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawn, spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { startFakeDevice } from './fakes/fake-device.js';

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
      printf '[\\n  {\\n    "id": "other@x",\\n    "enabled": false,\\n    "installPath": "/elsewhere/other"\\n  },\\n  {\\n    "id": "miblo@miblo",\\n    "enabled": %s,\\n    "installPath": "%s"\\n  }\\n]\\n' "$(cat "$state/plugin")" "$(cat "$state/installpath" 2>/dev/null)"
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
  // MIBLO_TTY stands for /dev/tty: a missing file is a run with no terminal (pairing is skipped).
  const baseEnv = () => ({ HOME: home, PATH: `${bin}:/usr/bin:/bin`, MIBLO_SYSTEM_BIN_DIRS: path.join(dir, 'none'), MIBLO_TTY: path.join(dir, 'no-tty') });
  const result = (r) => {
    const logFile = path.join(state, 'log');
    const lines = fs.existsSync(logFile) ? fs.readFileSync(logFile, 'utf8').trim().split('\n') : [];
    return { ...r, lines, calls: lines.map((l) => l.split('|')[1]).filter((a) => a !== '--version') };
  };
  const run = ({ pipe = false } = {}) => {
    const env = baseEnv();
    // pipe: the script arrives on standard input, as with `curl ... | sh`.
    const r = pipe
      ? spawnSync('sh', [], { env, input: fs.readFileSync(script), encoding: 'utf8' })
      : spawnSync('sh', [script], { env, input: '', encoding: 'utf8' });
    return result(r);
  };
  // The installed plugin is this repository's, at the place Claude Code installs it. `answers`
  // are what the user types in the terminal (one per line); `devices` is what discovery finds.
  // Asynchronous, so the fake gadgets in this process can answer.
  const runPairing = ({ answers = null, devices = [], args = [], pipe = true } = {}) => {
    const installPath = path.join(home, '.claude/plugins/cache/miblo/miblo/1.14.0');
    fs.mkdirSync(path.dirname(installPath), { recursive: true });
    // A copy of this plugin, as Claude Code installs it.
    if (!fs.existsSync(installPath)) {
      const src = path.join(repo, 'plugin');
      fs.cpSync(src, installPath, { recursive: true, filter: (f) => !/^(test|node_modules)(\/|$)/.test(path.relative(src, f)) });
    }
    fs.writeFileSync(path.join(state, 'installpath'), installPath);
    const env = { ...baseEnv(), PATH: `${bin}:${path.dirname(process.execPath)}:/usr/bin:/bin`, MIBLO_DISCOVER_JSON: JSON.stringify(devices) };
    if (answers !== null) {
      env.MIBLO_TTY = path.join(dir, 'tty');
      fs.writeFileSync(env.MIBLO_TTY, answers.map((a) => `${a}\n`).join(''));
    }
    const child = pipe ? spawn('sh', ['-s', '--', ...args], { env }) : spawn('sh', [script, ...args], { env });
    let stdout = '';
    let stderr = '';
    child.stdout.on('data', (c) => { stdout += c; });
    child.stderr.on('data', (c) => { stderr += c; });
    child.stdin.end(pipe ? fs.readFileSync(script) : '');
    return new Promise((resolve) => child.on('close', (status) => resolve(result({ status, stdout, stderr }))));
  };
  const dataDir = path.join(home, '.claude/plugins/data/miblo-miblo');
  const paired = () => {
    const f = path.join(dataDir, 'devices.json');
    return fs.existsSync(f) ? JSON.parse(fs.readFileSync(f, 'utf8')) : null;
  };
  return { dir, home, bin, state, placeFake, run, runPairing, paired };
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

// --- Pairing right after the install ---

const pairable = (t) => t.placeFake(path.join(t.bin, 'claude'));
const deviceIds = (list) => JSON.stringify(list ?? null);

test('one gadget found: asks for the code in the terminal and pairs it', { skip }, async () => {
  const t = setup();
  pairable(t);
  const dev = await startFakeDevice({ name: 'Desk' });
  try {
    const r = await t.runPairing({ answers: ['4827'], devices: [{ id: 'miblo-4f2a', name: 'Desk', addr: dev.addr }] });
    assert.equal(r.status, 0, r.stdout + r.stderr);
    assert.match(r.stdout, /Miblo Desk paired\. Open Claude \(terminal, desktop app → Code, or your IDE\) and it will start showing your sessions\./);
    assert.equal(dev.state.tokens.length, 1);
    // Saved in the plugin's data folder, where the hooks look.
    assert.match(deviceIds(t.paired()), /miblo-4f2a/);
    assert.ok(!/type \/miblo:pair/.test(r.stdout), r.stdout);
  } finally {
    await dev.close();
  }
});

test('a wrong code, then the right one', { skip }, async () => {
  const t = setup();
  pairable(t);
  const dev = await startFakeDevice({ name: 'Desk' });
  try {
    const r = await t.runPairing({ answers: ['1111', '4827'], devices: [{ id: 'miblo-4f2a', name: 'Desk', addr: dev.addr }] });
    assert.equal(r.status, 0, r.stdout + r.stderr);
    assert.match(r.stdout, /Wrong code/);
    assert.match(r.stdout, /Miblo Desk paired/);
    assert.equal(dev.state.tokens.length, 1);
  } finally {
    await dev.close();
  }
});

test('three wrong codes: stops and points to /miblo:pair', { skip }, async () => {
  const t = setup();
  pairable(t);
  const dev = await startFakeDevice({ name: 'Desk' });
  try {
    const r = await t.runPairing({ answers: ['1111', '2222', '3333', '4827'], devices: [{ id: 'miblo-4f2a', name: 'Desk', addr: dev.addr }] });
    assert.equal(r.status, 0, r.stdout + r.stderr);
    assert.ok(!/paired\. Open Claude/.test(r.stdout), r.stdout);
    assert.match(r.stdout, /\/miblo:pair/);
    assert.equal(dev.state.tokens.length, 0);
  } finally {
    await dev.close();
  }
});

test('no gadget found: explains what to check, retries on Return, can be skipped', { skip }, async () => {
  const t = setup();
  pairable(t);
  const r = await t.runPairing({ answers: ['', 'q'], devices: [] });
  assert.equal(r.status, 0, r.stdout + r.stderr);
  assert.match(r.stdout, /same Wi-Fi/);
  assert.match(r.stdout, /pairing code/);
  assert.equal(r.stdout.match(/No Miblo found/g)?.length, 2, r.stdout);
  assert.match(r.stdout, /\/miblo:pair/);
});

test('several gadgets: the user picks number 2', { skip }, async () => {
  const t = setup();
  pairable(t);
  const a = await startFakeDevice({ id: 'miblo-aaaa', name: 'Kitchen', code: '1111' });
  const b = await startFakeDevice({ id: 'miblo-bbbb', name: 'Office', code: '2222' });
  try {
    const r = await t.runPairing({
      answers: ['2', '2222'],
      devices: [{ id: 'miblo-aaaa', name: 'Kitchen', addr: a.addr }, { id: 'miblo-bbbb', name: 'Office', addr: b.addr }],
    });
    assert.equal(r.status, 0, r.stdout + r.stderr);
    assert.match(r.stdout, /1\) Kitchen/);
    assert.match(r.stdout, /2\) Office/);
    assert.match(r.stdout, /Miblo Office paired/);
    assert.equal(a.state.tokens.length, 0);
    assert.equal(b.state.tokens.length, 1);
  } finally {
    await a.close();
    await b.close();
  }
});

test('--no-pair: installs only, and says how to pair later', { skip }, async () => {
  const t = setup();
  pairable(t);
  const dev = await startFakeDevice();
  try {
    const r = await t.runPairing({ answers: ['4827'], devices: [{ id: 'miblo-4f2a', name: 'Desk', addr: dev.addr }], args: ['--no-pair'] });
    assert.equal(r.status, 0, r.stdout + r.stderr);
    assert.match(r.stdout, /Miblo is installed/);
    assert.match(r.stdout, /\/miblo:pair/);
    assert.equal(dev.state.tokens.length, 0);
    assert.equal(t.paired(), null);
  } finally {
    await dev.close();
  }
});

test('no terminal to type in: skips pairing with a hint', { skip }, async () => {
  const t = setup();
  pairable(t);
  const r = await t.runPairing({ devices: [] });
  assert.equal(r.status, 0, r.stdout + r.stderr);
  assert.match(r.stdout, /Miblo is installed/);
  assert.match(r.stdout, /\/miblo:pair/);
  assert.ok(!/No Miblo found/.test(r.stdout), r.stdout);
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
  // It pairs too, unless told not to, and reads the code from the user.
  assert.match(ps, /NoPair/);
  assert.match(ps, /MIBLO_NO_PAIR/);
  assert.match(ps, /Read-Host/);
  assert.match(ps, /miblo-run/);
  assert.match(ps, /plugins[\\/]+data[\\/]+miblo-miblo|'data'[^\n]*'miblo-miblo'/);
});

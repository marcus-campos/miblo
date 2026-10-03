// bin/miblo-run: the POSIX sh launcher every hook, the status line and the slash commands go
// through. GUI apps (the Claude desktop app, IDEs) don't inherit the shell's PATH, and some users
// have no Node at all, so it finds a Node >= 20 on its own or downloads a pinned one once.
// Each test runs it under a bare environment: PATH holds only the tools it needs (no node),
// HOME and MIBLO_SYSROOT point at temp dirs holding fake `node` scripts that print a marker.
import { test as nodeTest } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawn, spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

// The launcher runs under Git Bash on Windows; these tests drive it with /bin/sh and fake node
// scripts, so they run on macOS and Linux.
const test = (name, opts, fn) => {
  if (typeof opts === 'function') return nodeTest(name, { skip: process.platform === 'win32' }, opts);
  return nodeTest(name, { ...opts, skip: opts.skip || process.platform === 'win32' }, fn);
};

const BIN = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../bin');
const LAUNCHER = path.join(BIN, 'miblo-run');
const src = fs.readFileSync(LAUNCHER, 'utf8').replace(/\r\n/g, '\n');
const NODE_VERSION = src.match(/^NODE_VERSION=(\S+)$/m)[1];

const TOOLS = ['sh', 'uname', 'mkdir', 'mv', 'rm', 'cat', 'find', 'tar', 'gzip', 'curl', 'shasum', 'sha256sum', 'openssl', 'sleep', 'date', 'wc',
  'chmod', 'unzip', 'sysctl', 'ln', 'ls', 'dirname', 'cut', 'tr', 'head'];

// A PATH with the system tools the launcher may call, and never a node.
function toolDir(root) {
  const dir = path.join(root, 'tools');
  fs.mkdirSync(dir, { recursive: true });
  for (const t of TOOLS) {
    const r = spawnSync('/bin/sh', ['-c', `command -v ${t}`], { encoding: 'utf8' });
    const p = r.stdout.trim();
    if (r.status === 0 && p.startsWith('/')) fs.symlinkSync(p, path.join(dir, t));
  }
  return dir;
}

function fakeNode(file, marker, version = 'v22.1.0') {
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, `#!/bin/sh\nif [ "$1" = "-v" ]; then echo ${version}; exit 0; fi\necho "${marker} $*"\n`);
  fs.chmodSync(file, 0o755);
  return file;
}

function sandbox() {
  const root = fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(), 'miblo-run-')));
  const home = path.join(root, 'home');
  const sys = path.join(root, 'sys');
  const data = path.join(root, 'data');
  fs.mkdirSync(home, { recursive: true });
  fs.mkdirSync(sys, { recursive: true });
  return { root, home, sys, data, tools: toolDir(root), runtime: path.join(data, 'runtime') };
}

function run(s, args, { env = {}, extraPath = [], cwd } = {}) {
  const r = spawnSync(process.env.MIBLO_TEST_SH || '/bin/sh', [LAUNCHER, ...args], {
    encoding: 'utf8',
    cwd,
    env: {
      MIBLO_TEST: '1',
      HOME: s.home,
      PATH: [...extraPath, s.tools].join(':'),
      MIBLO_SYSROOT: s.sys,
      SHELL: '/nonexistent',
      CLAUDE_PLUGIN_DATA: s.data,
      MIBLO_WAIT: '2',
      ...env,
    },
    timeout: 30_000,
  });
  return { code: r.status, out: r.stdout, err: r.stderr };
}

const cached = (s) => fs.readFileSync(path.join(s.runtime, 'node-path'), 'utf8').trim();

test('no node anywhere and --no-wait: exits 0 quietly and downloads nothing', () => {
  const s = sandbox();
  const r = run(s, ['--no-wait', 'onboard.js'], { env: { MIBLO_NODE_BASE_URL: 'file:///nonexistent' } });
  assert.equal(r.code, 0);
  assert.equal(r.out, '');
  assert.equal(r.err, '');
  assert.ok(!fs.existsSync(path.join(s.runtime, 'lock')));
  assert.deepEqual(fs.existsSync(s.runtime) ? fs.readdirSync(s.runtime).filter((f) => f.startsWith('node-v')) : [], []);
});

test('runs the script next to it with the node on PATH, passing the arguments through', () => {
  const s = sandbox();
  const bin = path.join(s.root, 'pathbin');
  fakeNode(path.join(bin, 'node'), 'PATH');
  const r = run(s, ['miblo.js', 'status', 'a b'], { extraPath: [bin] });
  assert.equal(r.code, 0);
  assert.equal(r.out, `PATH ${path.join(BIN, 'miblo.js')} status a b\n`);
  assert.equal(cached(s), path.join(bin, 'node'));
});

test('PATH comes before the well-known locations; Homebrew before nvm', () => {
  const s = sandbox();
  const bin = path.join(s.root, 'pathbin');
  fakeNode(path.join(bin, 'node'), 'PATH');
  fakeNode(path.join(s.sys, 'opt/homebrew/bin/node'), 'BREW');
  fakeNode(path.join(s.home, '.nvm/versions/node/v22.2.0/bin/node'), 'NVM');
  assert.match(run(s, ['hook.js'], { extraPath: [bin] }).out, /^PATH /);
  const s2 = sandbox();
  fakeNode(path.join(s2.sys, 'opt/homebrew/bin/node'), 'BREW');
  fakeNode(path.join(s2.home, '.nvm/versions/node/v22.2.0/bin/node'), 'NVM');
  assert.match(run(s2, ['hook.js']).out, /^BREW /);
});

test('a node older than 20 is skipped', () => {
  const s = sandbox();
  const bin = path.join(s.root, 'pathbin');
  fakeNode(path.join(bin, 'node'), 'OLD', 'v18.20.0');
  fakeNode(path.join(s.home, '.nvm/versions/node/v22.2.0/bin/node'), 'NVM');
  assert.match(run(s, ['hook.js'], { extraPath: [bin] }).out, /^NVM /);
});

test('nvm: the newest version wins, compared as numbers', () => {
  const s = sandbox();
  for (const v of ['v9.11.2', 'v20.9.0', 'v22.2.0', 'v22.10.0', 'v18.0.0']) {
    fakeNode(path.join(s.home, `.nvm/versions/node/${v}/bin/node`), `NVM-${v}`, v);
  }
  assert.match(run(s, ['hook.js']).out, /^NVM-v22\.10\.0 /);
});

for (const [name, rel, where] of [
  ['/usr/local', 'usr/local/bin/node', 'sys'],
  ['Volta', '.volta/bin/node', 'home'],
  ['fnm', '.local/share/fnm/node-versions/v22.3.0/installation/bin/node', 'home'],
  ['fnm (macOS)', 'Library/Application Support/fnm/node-versions/v22.3.0/installation/bin/node', 'home'],
  ['asdf', '.asdf/shims/node', 'home'],
  ['mise', '.local/share/mise/shims/node', 'home'],
  ['nodenv', '.nodenv/shims/node', 'home'],
  ['/usr/bin', 'usr/bin/node', 'sys'],
]) {
  test(`finds node installed by ${name}`, () => {
    const s = sandbox();
    fakeNode(path.join(s[where], rel), 'HIT');
    const r = run(s, ['hook.js']);
    assert.match(r.out, /^HIT /);
    assert.equal(cached(s), path.join(s[where], rel));
  });
}

test('the cached node comes first while it exists; a vanished one is found again', () => {
  const s = sandbox();
  const nvm = fakeNode(path.join(s.home, '.nvm/versions/node/v22.2.0/bin/node'), 'NVM');
  assert.match(run(s, ['hook.js']).out, /^NVM /);
  assert.equal(cached(s), nvm);
  // Cached: used even though a node is now on PATH.
  const bin = path.join(s.root, 'pathbin');
  fakeNode(path.join(bin, 'node'), 'PATH');
  assert.match(run(s, ['hook.js'], { extraPath: [bin] }).out, /^NVM /);
  // Gone (nvm uninstall): found again and the cache is replaced.
  fs.rmSync(path.join(s.home, '.nvm'), { recursive: true });
  assert.match(run(s, ['hook.js'], { extraPath: [bin] }).out, /^PATH /);
  assert.equal(cached(s), path.join(bin, 'node'));
});

test('the data dir is CLAUDE_PLUGIN_DATA, else the plugin data dir under the Claude config, else ~/.miblo', () => {
  const s = sandbox();
  fakeNode(path.join(s.home, '.volta/bin/node'), 'VOLTA');
  run(s, ['hook.js']);
  assert.ok(fs.existsSync(path.join(s.runtime, 'node-path')));
  run(s, ['hook.js'], { env: { CLAUDE_PLUGIN_DATA: '' } });
  assert.ok(fs.existsSync(path.join(s.home, '.miblo/runtime/node-path')));
  // The Bash tool that runs the slash commands has no CLAUDE_PLUGIN_DATA: the plugin's own dir.
  const plug = path.join(s.home, '.claude/plugins/data/miblo-miblo');
  fs.mkdirSync(plug, { recursive: true });
  run(s, ['miblo.js', '--data', plug, 'status'], { env: { CLAUDE_PLUGIN_DATA: '' } });
  assert.ok(fs.existsSync(path.join(plug, 'runtime/node-path')));
  const conf = path.join(s.root, 'conf');
  fs.mkdirSync(path.join(conf, 'plugins/data/miblo-miblo'), { recursive: true });
  run(s, ['hook.js'], { env: { CLAUDE_PLUGIN_DATA: '', CLAUDE_CONFIG_DIR: conf } });
  assert.ok(fs.existsSync(path.join(conf, 'plugins/data/miblo-miblo/runtime/node-path')));
});

// A prompt-injected `/miblo:say ... --data /tmp/x` must not choose which "node" runs.
test("--data in a script's arguments is ignored, except for the status line, right after it and absolute", () => {
  const s = sandbox();
  fakeNode(path.join(s.home, '.volta/bin/node'), 'VOLTA');
  const evil = path.join(s.root, 'evil');
  fakeNode(path.join(evil, 'bin/node'), 'EVIL');
  fs.mkdirSync(path.join(evil, 'runtime'), { recursive: true });
  fs.writeFileSync(path.join(evil, 'runtime/node-path'), path.join(evil, 'bin/node') + '\n');
  assert.match(run(s, ['miblo.js', 'say', '--data', evil]).out, /^VOLTA /);
  assert.match(run(s, ['miblo.js', '--data', evil, 'status']).out, /^VOLTA /);
  assert.match(run(s, ['--no-wait', 'statusline-tap.mjs', 'x', '--data', evil]).out, /^VOLTA /);
  assert.match(run(s, ['--no-wait', 'statusline-tap.mjs', '--data', 'evil'], { cwd: s.root }).out, /^VOLTA /);
  assert.match(run(s, ['--check', '--data', 'evil'], { cwd: s.root }).out, /node=.*\.volta/);
  // As the status line and --check pass it: honoured (the cache there is ours to trust).
  const mine = path.join(s.root, 'mine');
  run(s, ['--no-wait', 'statusline-tap.mjs', '--data', mine]);
  assert.ok(fs.existsSync(path.join(mine, 'runtime/node-path')));
});

// ---- PATH and cache hardening ----

test("PATH entries that are empty, relative or '.' are skipped (a repo's ./node never runs)", () => {
  const s = sandbox();
  const repo = path.join(s.root, 'repo');
  fakeNode(path.join(repo, 'node'), 'REPO');
  fakeNode(path.join(repo, 'bin/node'), 'REPOBIN');
  fakeNode(path.join(s.home, '.volta/bin/node'), 'VOLTA');
  for (const p of [`.:${s.tools}`, `:${s.tools}`, `${s.tools}:`, `bin:${s.tools}`, `./bin:${s.tools}`]) {
    const r = spawnSync('/bin/sh', [LAUNCHER, 'hook.js'], { cwd: repo, encoding: 'utf8',
      env: { MIBLO_TEST: '1', HOME: s.home, PATH: p, MIBLO_SYSROOT: s.sys, SHELL: '/nonexistent', CLAUDE_PLUGIN_DATA: s.data } });
    assert.match(r.out ?? r.stdout, /^VOLTA /, p);
    fs.rmSync(path.join(s.runtime, 'node-path'));
  }
});

test('a node on PATH inside the working directory runs but is never cached', () => {
  const s = sandbox();
  const repo = path.join(s.root, 'repo');
  fakeNode(path.join(repo, 'tools/node'), 'REPO');
  const r = run(s, ['hook.js'], { cwd: repo, extraPath: [path.join(repo, '.', 'tools').replace('/tools', '/./tools')] });
  assert.match(r.out, /^REPO /);
  assert.ok(!fs.existsSync(path.join(s.runtime, 'node-path')));
});

test('a cache that is not plainly ours is ignored', () => {
  for (const bad of ['relative/node', '/bin/sh', '']) {
    const s = sandbox();
    fakeNode(path.join(s.home, '.volta/bin/node'), 'VOLTA');
    const evil = fakeNode(path.join(s.root, 'evil/sh'), 'EVIL');
    fs.mkdirSync(s.runtime, { recursive: true });
    fs.writeFileSync(path.join(s.runtime, 'node-path'), (bad === '/bin/sh' ? evil : bad) + '\n');
    assert.match(run(s, ['hook.js']).out, /^VOLTA /, bad);
  }
  // A runtime dir others can write to.
  const s = sandbox();
  fakeNode(path.join(s.home, '.volta/bin/node'), 'VOLTA');
  const evil = fakeNode(path.join(s.root, 'evil/node'), 'EVIL');
  fs.mkdirSync(s.runtime, { recursive: true });
  fs.writeFileSync(path.join(s.runtime, 'node-path'), evil + '\n');
  fs.chmodSync(s.runtime, 0o777);
  assert.match(run(s, ['hook.js']).out, /^VOLTA /);
});

test('a cached node that is now too old is replaced', () => {
  const s = sandbox();
  const old = fakeNode(path.join(s.root, 'old/node'), 'OLD', 'v18.0.0');
  fakeNode(path.join(s.home, '.volta/bin/node'), 'VOLTA');
  fs.mkdirSync(s.runtime, { recursive: true });
  fs.writeFileSync(path.join(s.runtime, 'node-path'), old + '\n');
  assert.match(run(s, ['hook.js']).out, /^VOLTA /);
  assert.equal(cached(s), path.join(s.home, '.volta/bin/node'));
});

test('asks the login shell as a last resort and caches the answer', () => {
  const s = sandbox();
  const odd = fakeNode(path.join(s.root, 'odd place/bin/node'), 'LOGIN');
  const shell = path.join(s.root, 'fakeshell');
  fs.writeFileSync(shell, `#!/bin/sh\n[ "$1" = "-lc" ] || exit 1\necho "Welcome!"\necho "${odd}"\n`);
  fs.chmodSync(shell, 0o755);
  const r = run(s, ['hook.js'], { env: { SHELL: shell } });
  assert.match(r.out, /^LOGIN /);
  assert.equal(cached(s), odd);
});

test('a login shell that hangs is given up on quickly, and not asked again for a while', () => {
  const s = sandbox();
  const shell = path.join(s.root, 'fakeshell');
  const count = path.join(s.root, 'count');
  fs.writeFileSync(shell, `#!/bin/sh\necho x >> "${count}"\nexec sleep 30\n`);
  fs.chmodSync(shell, 0o755);
  const t0 = Date.now();
  const r = run(s, ['--no-wait', 'hook.js'], { env: { SHELL: shell } });
  assert.equal(r.code, 0);
  assert.equal(r.out, '');
  assert.ok(Date.now() - t0 < 4000, `took ${Date.now() - t0} ms`);
  run(s, ['--no-wait', 'hook.js'], { env: { SHELL: shell } });
  assert.equal(fs.readFileSync(count, 'utf8'), 'x\n');
});

test('refuses a script that is not a plain file name next to it', () => {
  const s = sandbox();
  fakeNode(path.join(s.home, '.volta/bin/node'), 'VOLTA');
  for (const bad of ['../x.js', '/etc/passwd', '.hidden.js', 'nope.js', '']) {
    const r = run(s, [bad]);
    assert.equal(r.code, 0, bad);
    assert.equal(r.out, '', bad);
  }
});

// ---- Download of the pinned Node when there is none ----

function dist(s, platform, { zip = false } = {}) {
  const name = `node-v${NODE_VERSION}-${platform}`;
  const stage = path.join(s.root, 'stage');
  const exe = zip ? path.join(stage, name, 'node.exe') : path.join(stage, name, 'bin/node');
  fakeNode(exe, 'DOWNLOADED', `v${NODE_VERSION}`);
  const out = path.join(s.root, 'dist', `v${NODE_VERSION}`);
  fs.mkdirSync(out, { recursive: true });
  const file = path.join(out, `${name}.${zip ? 'zip' : 'tar.gz'}`);
  const r = zip
    ? spawnSync('zip', ['-qr', file, name], { cwd: stage })
    : spawnSync('tar', ['-czf', file, name], { cwd: stage });
  assert.equal(r.status, 0);
  const sha = crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
  return { base: `file://${path.join(s.root, 'dist')}`, sha, name };
}

const hasCurl = spawnSync('/bin/sh', ['-c', 'command -v curl']).status === 0;

test('the pinned checksums cover every supported target', () => {
  for (const t of ['darwin-arm64', 'darwin-x64', 'linux-x64', 'linux-arm64', 'win-x64', 'win-arm64']) {
    assert.match(src, new RegExp(`^\\s+${t}\\) echo [0-9a-f]{64} ;;$`, 'm'), t);
  }
});

test('a download whose checksum does not match the pinned one is rejected', { skip: !hasCurl }, () => {
  const s = sandbox();
  const d = dist(s, 'linux-x64');
  const r = run(s, ['hook.js'], { env: { MIBLO_NODE_BASE_URL: d.base, MIBLO_NODE_PLATFORM: 'linux-x64' } });
  assert.equal(r.code, 0);
  assert.equal(r.out, '');
  assert.ok(!fs.existsSync(path.join(s.runtime, d.name)));
  assert.ok(!fs.existsSync(path.join(s.runtime, 'node-path')));
  assert.ok(!fs.existsSync(path.join(s.runtime, 'lock')));
  assert.match(fs.readFileSync(path.join(s.runtime, 'launcher.log'), 'utf8'), /checksum mismatch/);
  assert.deepEqual(fs.readdirSync(s.runtime).filter((f) => !['launcher.log', 'no-login-node', 'download-failed'].includes(f)), []);
});

test('downloads, verifies and installs the pinned Node once, then runs from it', { skip: !hasCurl }, () => {
  const s = sandbox();
  const d = dist(s, 'linux-x64');
  const env = { MIBLO_NODE_BASE_URL: d.base, MIBLO_NODE_PLATFORM: 'linux-x64', MIBLO_NODE_SHA256: d.sha };
  const r = run(s, ['hook.js'], { env });
  assert.equal(r.code, 0, r.err);
  assert.match(r.out, /^DOWNLOADED /);
  const exe = path.join(s.runtime, d.name, 'bin/node');
  assert.equal(cached(s), exe);
  // Later runs never download again, even with the mirror gone and the cache lost.
  fs.rmSync(path.join(s.root, 'dist'), { recursive: true });
  fs.rmSync(path.join(s.runtime, 'node-path'));
  assert.match(run(s, ['hook.js'], { env }).out, /^DOWNLOADED /);
});

test('Windows: the zip build is unpacked and node.exe is used', { skip: !hasCurl || spawnSync('/bin/sh', ['-c', 'command -v zip && command -v unzip']).status !== 0 }, () => {
  const s = sandbox();
  const d = dist(s, 'win-x64', { zip: true });
  const r = run(s, ['hook.js'], { env: { MIBLO_NODE_BASE_URL: d.base, MIBLO_NODE_PLATFORM: 'win-x64', MIBLO_NODE_SHA256: d.sha } });
  assert.match(r.out, /^DOWNLOADED /, r.err);
  assert.equal(cached(s), path.join(s.runtime, d.name, 'node.exe'));
});

test('--no-wait never downloads, even when a download is possible', { skip: !hasCurl }, () => {
  const s = sandbox();
  const d = dist(s, 'linux-x64');
  const r = run(s, ['--no-wait', 'onboard.js'], { env: { MIBLO_NODE_BASE_URL: d.base, MIBLO_NODE_PLATFORM: 'linux-x64', MIBLO_NODE_SHA256: d.sha } });
  assert.equal(r.out, '');
  assert.ok(!fs.existsSync(path.join(s.runtime, d.name)));
});

test('while another hook holds the download lock, it waits briefly and then exits quietly', { skip: !hasCurl }, async () => {
  const s = sandbox();
  const d = dist(s, 'linux-x64');
  const holder = spawn('sleep', ['30']);
  try {
    fs.mkdirSync(path.join(s.runtime, 'lock'), { recursive: true });
    fs.writeFileSync(path.join(s.runtime, 'lock/pid'), String(holder.pid));
    const t0 = Date.now();
    const r = run(s, ['hook.js'], { env: { MIBLO_NODE_BASE_URL: d.base, MIBLO_NODE_PLATFORM: 'linux-x64', MIBLO_NODE_SHA256: d.sha, MIBLO_WAIT: '1' } });
    assert.equal(r.code, 0);
    assert.equal(r.out, '');
    assert.ok(Date.now() - t0 < 5000);
    assert.ok(!fs.existsSync(path.join(s.runtime, d.name)));
  } finally {
    holder.kill();
  }
});

test('a lock left by a hook that died is taken over', { skip: !hasCurl }, () => {
  const s = sandbox();
  const d = dist(s, 'linux-x64');
  const dead = spawnSync('sh', ['-c', 'echo $$']).stdout.toString().trim();
  fs.mkdirSync(path.join(s.runtime, 'lock'), { recursive: true });
  fs.writeFileSync(path.join(s.runtime, 'lock/pid'), dead);
  const r = run(s, ['hook.js'], { env: { MIBLO_NODE_BASE_URL: d.base, MIBLO_NODE_PLATFORM: 'linux-x64', MIBLO_NODE_SHA256: d.sha } });
  assert.match(r.out, /^DOWNLOADED /, r.err);
});

test('a failed download (no network) exits 0 quietly and logs why', { skip: !hasCurl }, () => {
  const s = sandbox();
  const r = run(s, ['hook.js'], { env: { MIBLO_NODE_BASE_URL: `file://${s.root}/missing`, MIBLO_NODE_PLATFORM: 'linux-x64' } });
  assert.equal(r.code, 0);
  assert.equal(r.out, '');
  assert.match(fs.readFileSync(path.join(s.runtime, 'launcher.log'), 'utf8'), /download failed/);
});

test('an unsupported platform exits 0 quietly', () => {
  const s = sandbox();
  const r = run(s, ['hook.js'], { env: { MIBLO_NODE_PLATFORM: 'sunos-sparc' } });
  assert.equal(r.code, 0);
  assert.equal(r.out, '');
});

test('the launcher is plain POSIX sh', () => {
  for (const bashism of [/\[\[/, /^\s*function\s/m, /\$\{[A-Za-z_]+\/\//, /\blocal\s/, /<<</, /\$'/, /\bsource\s/, /==/]) {
    assert.doesNotMatch(src, bashism);
  }
  assert.ok(src.startsWith('#!/bin/sh\n'));
  const dash = spawnSync('/bin/sh', ['-n', LAUNCHER]);
  assert.equal(dash.status, 0, String(dash.stderr));
});

// /bin/sh is bash in POSIX mode on macOS; dash (Debian/Ubuntu's /bin/sh) is stricter. The whole
// suite runs under another shell with MIBLO_TEST_SH=/bin/dash; this keeps the main paths covered.
test('works under dash: discovery, cache and download', { skip: !fs.existsSync('/bin/dash') || !hasCurl }, () => {
  const s = sandbox();
  const d = dist(s, 'linux-x64');
  const dash = (args, env) => spawnSync('/bin/dash', [LAUNCHER, ...args], {
    encoding: 'utf8',
    env: { MIBLO_TEST: '1', HOME: s.home, PATH: s.tools, MIBLO_SYSROOT: s.sys, SHELL: '/nonexistent', CLAUDE_PLUGIN_DATA: s.data, ...env },
  });
  assert.match(dash(['hook.js'], { MIBLO_NODE_BASE_URL: d.base, MIBLO_NODE_PLATFORM: 'linux-x64', MIBLO_NODE_SHA256: d.sha }).stdout, /^DOWNLOADED /);
  fakeNode(path.join(s.home, '.nvm/versions/node/v22.2.0/bin/node'), 'NVM');
  fs.rmSync(path.join(s.runtime, 'node-path'));
  assert.match(dash(['hook.js', 'x']).stdout, /^NVM .*hook\.js x\n$/);
});

// ---- --check: one machine-readable line for the slash commands (pair, update, link-statusline) ----

test('--check prints the node it will use and exits 0', () => {
  const s = sandbox();
  const nvm = fakeNode(path.join(s.home, '.nvm/versions/node/v22.2.0/bin/node'), 'NVM', 'v22.2.0');
  const r = run(s, ['--check', '--data', path.join(s.root, 'd')], { env: { CLAUDE_PLUGIN_DATA: '' } });
  assert.equal(r.code, 0);
  assert.equal(r.out, `ok version=v22.2.0 node=${nvm}\n`);
  assert.equal(fs.readFileSync(path.join(s.root, 'd/runtime/node-path'), 'utf8').trim(), nvm);
  // From the cache, still checked.
  assert.equal(run(s, ['--check', '--data', path.join(s.root, 'd')]).out, `ok version=v22.2.0 node=${nvm}\n`);
});

test('--check downloads the pinned Node when there is none, with progress on stderr', { skip: !hasCurl }, () => {
  const s = sandbox();
  const d = dist(s, 'linux-x64');
  const r = run(s, ['--check'], { env: { MIBLO_NODE_BASE_URL: d.base, MIBLO_NODE_PLATFORM: 'linux-x64', MIBLO_NODE_SHA256: d.sha } });
  assert.equal(r.code, 0);
  assert.equal(r.out, `ok version=v${NODE_VERSION} node=${path.join(s.runtime, d.name, 'bin/node')}\n`);
  assert.match(r.err, /Downloading Node\.js/);
});

test('--check says why there is no node and exits non-zero', { skip: !hasCurl }, () => {
  const s = sandbox();
  const r = run(s, ['--check'], { env: { MIBLO_NODE_BASE_URL: `file://${s.root}/missing`, MIBLO_NODE_PLATFORM: 'linux-x64' } });
  assert.equal(r.code, 1);
  assert.match(r.out, /^missing reason=download failed: file:\/\/.*\n$/);
  const bad = dist(s, 'linux-x64');
  const r2 = run(s, ['--check'], { env: { MIBLO_NODE_BASE_URL: bad.base, MIBLO_NODE_PLATFORM: 'linux-x64' } });
  assert.equal(r2.code, 1);
  assert.match(r2.out, /^missing reason=checksum mismatch.*\n$/);
});

test('--check names a node that is too old', () => {
  const s = sandbox();
  const bin = path.join(s.root, 'pathbin');
  const old = fakeNode(path.join(bin, 'node'), 'OLD', 'v18.20.0');
  const r = run(s, ['--check'], { extraPath: [bin], env: { MIBLO_NODE_PLATFORM: 'sunos-sparc' } });
  assert.equal(r.code, 1);
  assert.equal(r.out, `missing reason=no official Node.js build for this platform; ${old} is v18.20.0, too old (20 or newer needed)\n`);
});

test('after a failed download, hooks wait 10 minutes before trying again; --check always tries', { skip: !hasCurl }, () => {
  const s = sandbox();
  const env = { MIBLO_NODE_BASE_URL: `file://${s.root}/missing`, MIBLO_NODE_PLATFORM: 'linux-x64' };
  run(s, ['hook.js'], { env });
  run(s, ['hook.js'], { env });
  const tries = () => fs.readFileSync(path.join(s.runtime, 'launcher.log'), 'utf8').split('\n').filter((l) => l.includes(' downloading ')).length;
  assert.equal(tries(), 1);
  assert.match(run(s, ['hook.js'], { env }).err, /could not be downloaded/);
  assert.equal(run(s, ['--check'], { env }).code, 1);
  assert.equal(tries(), 2);
});

test('the download mirror and hash overrides work only under MIBLO_TEST=1', { skip: !hasCurl }, () => {
  const s = sandbox();
  const d = dist(s, 'linux-x64');
  const r = run(s, ['--check'], { env: { MIBLO_TEST: '', MIBLO_NODE_BASE_URL: d.base, MIBLO_NODE_PLATFORM: 'linux-x64', MIBLO_NODE_SHA256: d.sha } });
  assert.doesNotMatch(r.out, /^ok /);
  assert.doesNotMatch(fs.readFileSync(path.join(s.runtime, 'launcher.log'), 'utf8'), /file:\/\//);
});

test('downloads only over https (curl --proto =https, wget --https-only)', () => {
  assert.match(src, /curl [^\n]*--proto "?=?\$?\{?[a-z_]*/);
  assert.ok(src.includes("--proto-redir"));
  assert.ok(src.includes('wget --https-only') || src.includes('--https-only'));
  assert.ok(src.includes('rm -rf "${rt:?}/${name:?}"'));
});

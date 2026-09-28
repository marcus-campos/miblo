import fs from 'node:fs';
import path from 'node:path';

const TAP = 'statusline-tap.mjs';
const ORIGINAL = 'statusline-original.json';

function readSettings(settingsPath) {
  try {
    return JSON.parse(fs.readFileSync(settingsPath, 'utf8'));
  } catch (e) {
    if (e.code === 'ENOENT') return {};
    throw new Error(`cannot parse ${settingsPath}: ${e.message}`);
  }
}

function writeJson(file, obj) {
  fs.mkdirSync(path.dirname(file), { recursive: true });
  const tmp = `${file}.miblo-tmp`;
  fs.writeFileSync(tmp, JSON.stringify(obj, null, 2) + '\n');
  fs.renameSync(tmp, file);
}

const slashes = (p) => p.replace(/\\/g, '/');

// Stable home of the tap and the saved original: <claudeConfigDir>/miblo/.
// It lives next to settings.json (not in the plugin data dir) so the user's
// status line survives an uninstall of the plugin.
export function tapDir(settingsPath) {
  return path.join(path.dirname(settingsPath), 'miblo');
}

export function tapCommand({ settingsPath }) {
  return `node "${slashes(path.join(tapDir(settingsPath), TAP))}"`;
}

export function installTap({ pluginRoot, settingsPath }) {
  const dir = tapDir(settingsPath);
  fs.mkdirSync(dir, { recursive: true });
  fs.copyFileSync(path.join(pluginRoot, 'bin', TAP), path.join(dir, TAP));
}

export function isLinked({ settingsPath }) {
  const cmd = readSettings(settingsPath).statusLine?.command;
  return typeof cmd === 'string' && slashes(cmd).includes(slashes(path.join(tapDir(settingsPath), TAP)));
}

export function link({ settingsPath, pluginRoot }) {
  const settings = readSettings(settingsPath);
  installTap({ pluginRoot, settingsPath });
  if (isLinked({ settingsPath })) return { changed: false, original: null };

  if (fs.existsSync(settingsPath) && !fs.existsSync(`${settingsPath}.miblo-backup`)) {
    fs.copyFileSync(settingsPath, `${settingsPath}.miblo-backup`);
  }
  const current = settings.statusLine ?? null;
  const originalFile = path.join(tapDir(settingsPath), ORIGINAL);
  // Never chain taps: a status line that is already a miblo tap is not the original.
  const isTap = typeof current?.command === 'string' && current.command.includes(TAP);
  const original = isTap ? null : current;
  if (!isTap || !fs.existsSync(originalFile)) writeJson(originalFile, original ?? {});
  settings.statusLine = { ...(current ?? {}), type: 'command', command: tapCommand({ settingsPath }) };
  writeJson(settingsPath, settings);
  return { changed: true, original };
}

export function unlink({ settingsPath }) {
  if (!isLinked({ settingsPath })) return { changed: false };
  const settings = readSettings(settingsPath);
  let original = null;
  try {
    original = JSON.parse(fs.readFileSync(path.join(tapDir(settingsPath), ORIGINAL), 'utf8'));
  } catch {
    original = null;
  }
  if (original?.command) settings.statusLine = original;
  else delete settings.statusLine;
  writeJson(settingsPath, settings);
  return { changed: true };
}

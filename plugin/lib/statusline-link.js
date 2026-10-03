import fs from 'node:fs';
import path from 'node:path';

const TAP = 'statusline-tap.mjs';
// The launcher finds Node where the shell's PATH is not inherited (the Claude desktop app, IDEs).
const LAUNCHER = 'miblo-run';
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
// One shell word, taken literally: $, `, " and \ need nothing; ' is written as '\''.
const shq = (p) => `'${p.replace(/'/g, `'\\''`)}'`;

// Stable home of the tap and the saved original: <claudeConfigDir>/miblo/.
// It lives next to settings.json (not in the plugin data dir) so the user's
// status line survives an uninstall of the plugin.
export function tapDir(settingsPath) {
  return path.join(path.dirname(settingsPath), 'miblo');
}

// --no-wait: the status line never waits for a Node download (the hooks make it). --data: the
// launcher shares the plugin's cached Node path and downloaded runtime.
export function tapCommand({ settingsPath, dataDir }) {
  const data = dataDir ? ` --data ${shq(slashes(dataDir))}` : '';
  return `sh ${shq(slashes(path.join(tapDir(settingsPath), LAUNCHER)))} --no-wait ${TAP}${data}`;
}

export function installTap({ pluginRoot, settingsPath }) {
  const dir = tapDir(settingsPath);
  fs.mkdirSync(dir, { recursive: true });
  for (const f of [TAP, LAUNCHER]) {
    const tmp = path.join(dir, `${f}.miblo-tmp`);
    fs.copyFileSync(path.join(pluginRoot, 'bin', f), tmp);
    fs.chmodSync(tmp, 0o755);
    fs.renameSync(tmp, path.join(dir, f));
  }
}

export function isLinked({ settingsPath }) {
  const cmd = readSettings(settingsPath).statusLine?.command;
  if (typeof cmd !== 'string') return false;
  const c = slashes(cmd);
  const dir = slashes(tapDir(settingsPath));
  // Through the launcher (quoted as now, or double-quoted as briefly before), or the older
  // `node "<dir>/statusline-tap.mjs"`.
  const launcher = `${dir}/${LAUNCHER}`;
  return c.includes(`${dir}/${TAP}`) || ((cmd.includes(shq(launcher)) || c.includes(`"${launcher}"`)) && c.includes(` ${TAP}`));
}

// Run by every new bridge: refreshes the copied tap and launcher, and moves a status line linked
// by an older Miblo (`node ".../statusline-tap.mjs"`) to the current command.
export function refreshLink({ settingsPath, pluginRoot, dataDir }) {
  if (!isLinked({ settingsPath })) return { changed: false };
  installTap({ pluginRoot, settingsPath });
  const settings = readSettings(settingsPath);
  const command = tapCommand({ settingsPath, dataDir });
  if (settings.statusLine.command === command) return { changed: false };
  settings.statusLine = { ...settings.statusLine, command };
  writeJson(settingsPath, settings);
  return { changed: true };
}

export function link({ settingsPath, pluginRoot, dataDir }) {
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
  settings.statusLine = { ...(current ?? {}), type: 'command', command: tapCommand({ settingsPath, dataDir }) };
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

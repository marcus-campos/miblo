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

export function tapCommand({ dataDir, nodePath = process.execPath }) {
  return `"${nodePath}" "${path.join(dataDir, TAP)}"`;
}

export function installTap({ pluginRoot, dataDir }) {
  fs.mkdirSync(dataDir, { recursive: true });
  fs.copyFileSync(path.join(pluginRoot, 'bin', TAP), path.join(dataDir, TAP));
}

export function isLinked({ settingsPath, dataDir }) {
  const cmd = readSettings(settingsPath).statusLine?.command;
  return typeof cmd === 'string' && cmd.includes(path.join(dataDir, TAP));
}

export function link({ settingsPath, dataDir, pluginRoot, nodePath = process.execPath }) {
  const settings = readSettings(settingsPath);
  installTap({ pluginRoot, dataDir });
  if (isLinked({ settingsPath, dataDir })) return { changed: false, original: null };

  if (fs.existsSync(settingsPath) && !fs.existsSync(`${settingsPath}.miblo-backup`)) {
    fs.copyFileSync(settingsPath, `${settingsPath}.miblo-backup`);
  }
  const original = settings.statusLine ?? null;
  writeJson(path.join(dataDir, ORIGINAL), original ?? {});
  settings.statusLine = { ...(original ?? {}), type: 'command', command: tapCommand({ dataDir, nodePath }) };
  writeJson(settingsPath, settings);
  return { changed: true, original };
}

export function unlink({ settingsPath, dataDir }) {
  if (!isLinked({ settingsPath, dataDir })) return { changed: false };
  const settings = readSettings(settingsPath);
  let original = null;
  try {
    original = JSON.parse(fs.readFileSync(path.join(dataDir, ORIGINAL), 'utf8'));
  } catch {
    original = null;
  }
  if (original?.command) settings.statusLine = original;
  else delete settings.statusLine;
  writeJson(settingsPath, settings);
  return { changed: true };
}

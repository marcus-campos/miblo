import { execFileSync } from 'node:child_process';

const CLAUDE_RE = /(^|[\\/])(claude|node)(\.exe)?$/i;

export function parsePs(text) {
  const map = new Map();
  for (const line of String(text).split('\n')) {
    const m = line.trim().match(/^(\d+)\s+(\d+)\s+(.+)$/);
    if (m) map.set(Number(m[1]), { ppid: Number(m[2]), comm: m[3].trim() });
  }
  return map;
}

export function findAncestor(map, startPid) {
  let pid = startPid;
  for (let i = 0; i < 32 && pid > 1; i++) {
    const p = map.get(pid);
    if (!p) return null;
    if (CLAUDE_RE.test(p.comm)) return pid;
    pid = p.ppid;
  }
  return null;
}

export function findClaudePid({ platform = process.platform, startPid = process.ppid, runPs } = {}) {
  if (platform === 'win32') return null;
  try {
    const read = runPs ?? (() => execFileSync('ps', ['-A', '-o', 'pid=,ppid=,comm='], { encoding: 'utf8', timeout: 500 }));
    return findAncestor(parsePs(read()), startPid);
  } catch {
    return null;
  }
}

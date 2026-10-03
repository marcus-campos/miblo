// SessionStart notice when a newer Miblo release exists: compares the latest GitHub release with
// the installed plugin and the firmware of each paired gadget. Runs inside the synchronous
// SessionStart hook, so every network call has a short timeout, the release is cached for
// CHECK_EVERY_MS and the notice for one version is shown at most once per NOTIFY_EVERY_MS.
// The bridge reads the same cache to send the latest version to the gadgets.
import fs from 'node:fs';
import path from 'node:path';
import { GITHUB_API, REPO, compareVersions } from './firmware-update.js';

export const CHECK_EVERY_MS = 6 * 3600_000;
export const NOTIFY_EVERY_MS = 24 * 3600_000;
const GITHUB_TIMEOUT_MS = 2000;
const DEVICE_TIMEOUT_MS = 1500;  // covers one SYN retransmit (~1 s) when the gadget delays a connection
const REREAD_MS = 5 * 60_000;
const VERSION_RE = /^\d+\.\d+\.\d+$/;

function readJson(file) {
  try {
    return JSON.parse(fs.readFileSync(file, 'utf8')) ?? {};
  } catch {
    return {};
  }
}

function writeJson(file, data) {
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, JSON.stringify(data));
}

// Latest release version from the cache, refreshed from GitHub when stale. null when unknown.
async function latestRelease({ fetchImpl, cacheFile, now, githubApi, repo }) {
  const cache = readJson(cacheFile);
  if (typeof cache.checkedAt === 'number' && now - cache.checkedAt < CHECK_EVERY_MS) {
    return VERSION_RE.test(cache.latest ?? '') ? cache.latest : null;
  }
  let latest = VERSION_RE.test(cache.latest ?? '') ? cache.latest : null;
  try {
    const res = await fetchImpl(`${githubApi}/repos/${repo}/releases/latest`, {
      headers: { accept: 'application/vnd.github+json', 'user-agent': 'miblo-plugin' },
      signal: AbortSignal.timeout(GITHUB_TIMEOUT_MS),
    });
    if (res.ok) {
      const v = String((await res.json())?.tag_name ?? '').replace(/^v/, '');
      if (VERSION_RE.test(v)) latest = v;
    }
  } catch {
    // offline or slow: keep what we had and try again after CHECK_EVERY_MS
  }
  writeJson(cacheFile, { ...cache, checkedAt: now, latest });
  return latest;
}

// The bridge's view of the cached release: get() is cheap (the file is re-read at most every
// REREAD_MS, since the SessionStart hook refreshes it), refreshIfStale() asks GitHub in the
// background when the cache is missing or older than CHECK_EVERY_MS. Never throws.
export function createReleaseCache({ dataDir, fetchImpl = globalThis.fetch, now = () => Date.now(),
  githubApi = GITHUB_API, repo = REPO }) {
  const cacheFile = path.join(dataDir, 'update-check.json');
  let latest = null;
  let readAt = -Infinity;
  const valid = (v) => (VERSION_RE.test(v ?? '') ? v : null);
  const reread = () => {
    latest = valid(readJson(cacheFile).latest);
    readAt = now();
  };
  return {
    get() {
      if (now() - readAt >= REREAD_MS) reread();
      return latest;
    },
    async refreshIfStale() {
      try {
        const { checkedAt } = readJson(cacheFile);
        if (typeof checkedAt === 'number' && now() - checkedAt < CHECK_EVERY_MS) return;
        latest = await latestRelease({ fetchImpl, cacheFile, now: now(), githubApi, repo });
        readAt = now();
      } catch {
        // unknown until the next check
      }
    },
  };
}

// Paired gadgets running firmware older than `latest` (offline ones are skipped).
async function outdatedGadgets({ store, fetchImpl, latest }) {
  const rows = await Promise.all(store.list().map(async (d) => {
    try {
      // With its pairing token: without one a paired gadget reports only its id (no fw).
      const headers = d.token ? { authorization: `Bearer ${d.token}` } : {};
      const res = await fetchImpl(`http://${d.addr}/api/info`, { headers, signal: AbortSignal.timeout(DEVICE_TIMEOUT_MS) });
      const fw = res.ok ? String((await res.json())?.fw ?? '') : '';
      return VERSION_RE.test(fw) && compareVersions(fw, latest) < 0 ? `${String(d.name ?? d.id).slice(0, 32)} ${fw}` : null;
    } catch {
      return null;
    }
  }));
  return rows.filter(Boolean);
}

export async function updateNotice({ store, dataDir, pluginVersion, fetchImpl = globalThis.fetch, now = () => Date.now(),
  githubApi = GITHUB_API, repo = REPO }) {
  const cacheFile = path.join(dataDir, 'update-check.json');
  const t = now();
  const latest = await latestRelease({ fetchImpl, cacheFile, now: t, githubApi, repo });
  if (!latest) return null;
  const cache = readJson(cacheFile);
  if (cache.notifiedVersion === latest && typeof cache.notifiedAt === 'number' && t - cache.notifiedAt < NOTIFY_EVERY_MS) {
    return null;
  }
  const parts = [];
  if (VERSION_RE.test(pluginVersion ?? '') && compareVersions(pluginVersion, latest) < 0) parts.push(`plugin ${pluginVersion}`);
  parts.push(...await outdatedGadgets({ store, fetchImpl, latest }));
  if (!parts.length) return null;
  writeJson(cacheFile, { ...cache, notifiedVersion: latest, notifiedAt: t });
  return `Miblo ${latest} is available (you have ${parts.join(', ')}). Run /miblo:update to update.`;
}

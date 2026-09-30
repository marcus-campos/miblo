#!/usr/bin/env node
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { PORT, HOST, DEBOUNCE_MS, HEARTBEAT_MS, PID_CHECK_MS, IDLE_EXIT_MS, claudeSettingsPath, pluginVersion, parseDataArg } from '../lib/constants.js';
import { SessionTracker } from '../lib/session-tracker.js';
import { MetricsStore } from '../lib/metrics-store.js';
import { DayStats } from '../lib/day-stats.js';
import { buildSnapshot } from '../lib/snapshot-builder.js';
import { DeviceClient } from '../lib/device-client.js';
import { DeviceStore } from '../lib/device-store.js';
import { DeviceManager } from '../lib/device-manager.js';
import { discover } from '../lib/mdns.js';
import { createBridgeServer } from '../lib/bridge-server.js';
import { createReleaseCache } from '../lib/update-notice.js';
import { isLinked, installTap } from '../lib/statusline-link.js';
import { createLogger, errText } from '../lib/logger.js';

export function createBridge({ dataDir, now = () => Date.now(), client = new DeviceClient(), discoverFn = discover, host = os.hostname(), version = '', onShutdown = () => {}, log = () => {},
  release = createReleaseCache({ dataDir, now }) }) {
  const tracker = new SessionTracker({ now });
  const metrics = new MetricsStore({ now });
  const day = new DayStats({ dataDir, now });
  const devices = new DeviceManager({ client, store: new DeviceStore(dataDir), discover: discoverFn, now });
  let seq = 0;
  let timer = null;
  let lastActive = now();

  const push = async () => {
    timer = null;
    if (tracker.hasActive()) lastActive = now();
    day.observe(tracker.sessions());
    const snapshot = buildSnapshot({ seq: ++seq, nowMs: now(), host, tracker, metrics, day, latest: release.get() });
    await devices.pushAll(snapshot);
  };
  const schedule = () => {
    if (!timer) timer = setTimeout(() => push().catch((e) => log(`push failed: ${errText(e)}`)), DEBOUNCE_MS);
  };

  const server = createBridgeServer({
    version,
    onShutdown,
    onEvent(evt) {
      if (evt?.hook_event_name === 'SessionEnd') metrics.forget(evt.session_id);
      if (tracker.handle(evt)) schedule();
    },
    onStatusline(sl) {
      if (metrics.ingest(sl, { fresh: tracker.sawStart(sl?.session_id) })) schedule();
    },
    getStatus: () => ({
      sessions: tracker.sessions(),
      usage: metrics.usage(),
      today: { ...metrics.today(), ...day.today() },
      devices: devices.status(),
      statuslineSeen: metrics.hasReadings(),
    }),
  });

  return { tracker, metrics, day, devices, release, server, push, schedule, idleFor: () => now() - lastActive };
}

function main() {
  const here = path.dirname(fileURLToPath(import.meta.url));
  let dataDir;
  try {
    ({ dataDir } = parseDataArg(process.argv.slice(2)));
  } catch {
    process.exit(2);
  }
  try {
    const settingsPath = claudeSettingsPath();
    if (isLinked({ settingsPath })) installTap({ pluginRoot: path.resolve(here, '..'), settingsPath });
  } catch {
    // the status line keeps working with the previous copy of the tap
  }
  const log = createLogger(path.join(dataDir, 'bridge.log'));
  process.on('unhandledRejection', (e) => log(`unhandledRejection: ${errText(e)}`));
  process.on('uncaughtException', (e) => {
    log(`uncaughtException: ${errText(e)}`);
    process.exit(1);
  });
  const bridge = createBridge({
    dataDir,
    log,
    version: pluginVersion(),
    onShutdown: () => {
      bridge.server.close(() => process.exit(0));
      bridge.server.closeIdleConnections?.();
      setTimeout(() => process.exit(0), 1000).unref();
    },
  });
  bridge.server.on('error', (e) => {
    if (e.code === 'EADDRINUSE') process.exit(0);
    log(`server error: ${errText(e)}`);
    process.exit(1);
  });
  bridge.server.listen(PORT, HOST);
  bridge.release.refreshIfStale();
  setInterval(() => bridge.push().catch((e) => log(`heartbeat push failed: ${errText(e)}`)), HEARTBEAT_MS);
  setInterval(() => {
    if (bridge.tracker.sweep()) bridge.schedule();
    if (bridge.idleFor() > IDLE_EXIT_MS) process.exit(0);
  }, PID_CHECK_MS);
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) main();

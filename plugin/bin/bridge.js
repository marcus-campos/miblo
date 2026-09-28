#!/usr/bin/env node
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { PORT, HOST, DEBOUNCE_MS, HEARTBEAT_MS, PID_CHECK_MS, IDLE_EXIT_MS, claudeSettingsPath } from '../lib/constants.js';
import { SessionTracker } from '../lib/session-tracker.js';
import { MetricsStore } from '../lib/metrics-store.js';
import { buildSnapshot } from '../lib/snapshot-builder.js';
import { DeviceClient } from '../lib/device-client.js';
import { DeviceStore } from '../lib/device-store.js';
import { DeviceManager } from '../lib/device-manager.js';
import { discover } from '../lib/mdns.js';
import { createBridgeServer } from '../lib/bridge-server.js';
import { isLinked, installTap } from '../lib/statusline-link.js';

export function createBridge({ dataDir, now = () => Date.now(), client = new DeviceClient(), discoverFn = discover, host = os.hostname() }) {
  const tracker = new SessionTracker({ now });
  const metrics = new MetricsStore({ now });
  const devices = new DeviceManager({ client, store: new DeviceStore(dataDir), discover: discoverFn, now });
  let seq = 0;
  let timer = null;
  let lastActive = now();

  const push = async () => {
    timer = null;
    if (tracker.hasActive()) lastActive = now();
    const snapshot = buildSnapshot({ seq: ++seq, nowMs: now(), host, tracker, metrics });
    await devices.pushAll(snapshot);
  };
  const schedule = () => {
    if (!timer) timer = setTimeout(() => push().catch(() => {}), DEBOUNCE_MS);
  };

  const server = createBridgeServer({
    onEvent(evt) {
      if (evt?.hook_event_name === 'SessionEnd') metrics.forget(evt.session_id);
      if (tracker.handle(evt)) schedule();
    },
    onStatusline(sl) {
      if (metrics.ingest(sl)) schedule();
    },
    getStatus: () => ({
      sessions: tracker.sessions(),
      usage: metrics.usage(),
      today: metrics.today(),
      devices: devices.status(),
      statuslineSeen: metrics.hasReadings(),
    }),
  });

  return { tracker, metrics, devices, server, push, schedule, idleFor: () => now() - lastActive };
}

function argValue(name) {
  const i = process.argv.indexOf(name);
  return i > 0 ? process.argv[i + 1] : undefined;
}

function main() {
  const here = path.dirname(fileURLToPath(import.meta.url));
  const dataDir = argValue('--data') || process.env.CLAUDE_PLUGIN_DATA || path.join(os.homedir(), '.miblo');
  try {
    if (isLinked({ settingsPath: claudeSettingsPath(), dataDir })) installTap({ pluginRoot: path.resolve(here, '..'), dataDir });
  } catch {
    // a status line continua funcionando com a cópia anterior do tap
  }
  const bridge = createBridge({ dataDir });
  bridge.server.on('error', (e) => process.exit(e.code === 'EADDRINUSE' ? 0 : 1));
  bridge.server.listen(PORT, HOST);
  setInterval(() => bridge.push().catch(() => {}), HEARTBEAT_MS);
  setInterval(() => {
    if (bridge.tracker.sweep()) bridge.schedule();
    if (bridge.idleFor() > IDLE_EXIT_MS) process.exit(0);
  }, PID_CHECK_MS);
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) main();

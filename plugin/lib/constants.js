import os from 'node:os';
import path from 'node:path';

export const PORT = Number(process.env.MIBLO_PORT || 47821);
export const HOST = '127.0.0.1';
export const PROTOCOL_VERSION = 1;
export const MAX_SESSIONS = 8;
export const NAME_LEN = 20;
export const DET_LEN = 32;
export const MODEL_LEN = 12;
export const SNAPSHOT_MAX_BYTES = 3072;
export const ALERT_TTL_MS = 30_000;
export const DEBOUNCE_MS = 150;
export const HEARTBEAT_MS = 10_000;
export const PID_CHECK_MS = 15_000;
export const IDLE_EXIT_MS = 30 * 60_000;
export const MDNS_SERVICE = '_miblo._tcp.local';

export function claudeSettingsPath() {
  const dir = process.env.CLAUDE_CONFIG_DIR || path.join(os.homedir(), '.claude');
  return path.join(dir, 'settings.json');
}

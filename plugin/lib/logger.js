import fs from 'node:fs';
import path from 'node:path';

export const LOG_MAX_BYTES = 64 * 1024;

// Append-only line logger with a single rotation (file -> file.1).
// Callers must never pass tokens or snapshot bodies. Logging never throws.
export function createLogger(file, { maxBytes = LOG_MAX_BYTES, now = () => new Date() } = {}) {
  return function log(msg) {
    try {
      fs.mkdirSync(path.dirname(file), { recursive: true });
      try {
        if (fs.statSync(file).size > maxBytes) fs.renameSync(file, `${file}.1`);
      } catch {
        // no log file yet
      }
      fs.appendFileSync(file, `${now().toISOString()} ${String(msg).replace(/\n/g, ' ')}\n`);
    } catch {
      // disk full or read-only: nothing useful to do
    }
  };
}

export const errText = (e) => (e && e.stack ? String(e.stack).split('\n').slice(0, 3).join(' | ') : String(e));

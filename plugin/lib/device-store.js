import fs from 'node:fs';
import path from 'node:path';

export class DeviceStore {
  constructor(dataDir) {
    this.file = path.join(dataDir, 'devices.json');
  }

  list() {
    try {
      const data = JSON.parse(fs.readFileSync(this.file, 'utf8'));
      return Array.isArray(data.devices) ? data.devices : [];
    } catch {
      return [];
    }
  }

  upsert(device) {
    this.#write([...this.list().filter((d) => d.id !== device.id), device]);
  }

  update(id, patch) {
    this.#write(this.list().map((d) => (d.id === id ? { ...d, ...patch } : d)));
  }

  remove(id) {
    this.#write(this.list().filter((d) => d.id !== id));
  }

  #write(devices) {
    fs.mkdirSync(path.dirname(this.file), { recursive: true });
    const tmp = `${this.file}.tmp`;
    fs.writeFileSync(tmp, JSON.stringify({ devices }, null, 2), { mode: 0o600 });
    fs.renameSync(tmp, this.file);
    fs.chmodSync(this.file, 0o600);
  }
}

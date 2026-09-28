import dgram from 'node:dgram';
import { MDNS_SERVICE } from './constants.js';

const T_A = 1;
const T_PTR = 12;
const T_TXT = 16;
const T_SRV = 33;

function encodeName(name) {
  const labels = name.split('.').filter(Boolean).map((l) => {
    const b = Buffer.from(l, 'utf8');
    return Buffer.concat([Buffer.from([b.length]), b]);
  });
  return Buffer.concat([...labels, Buffer.from([0])]);
}

export function buildQuery(service) {
  const header = Buffer.alloc(12);
  header.writeUInt16BE(1, 4);
  return Buffer.concat([header, encodeName(service), Buffer.from([0x00, T_PTR, 0x80, 0x01])]);
}

function readName(buf, offset) {
  const labels = [];
  let off = offset;
  let end = null;
  for (let guard = 0; guard < 128; guard++) {
    const len = buf[off];
    if (len === undefined) throw new Error('truncated name');
    if (len === 0) { off += 1; break; }
    if ((len & 0xc0) === 0xc0) {
      if (end === null) end = off + 2;
      off = ((len & 0x3f) << 8) | buf[off + 1];
      continue;
    }
    labels.push(buf.toString('utf8', off + 1, off + 1 + len));
    off += 1 + len;
  }
  return { name: labels.join('.'), next: end ?? off };
}

export function parseMessage(buf) {
  if (buf.length < 12) throw new Error('short packet');
  const qd = buf.readUInt16BE(4);
  const total = buf.readUInt16BE(6) + buf.readUInt16BE(8) + buf.readUInt16BE(10);
  let off = 12;
  for (let i = 0; i < qd; i++) off = readName(buf, off).next + 4;
  const records = [];
  for (let i = 0; i < total; i++) {
    const { name, next } = readName(buf, off);
    const type = buf.readUInt16BE(next);
    const rdlen = buf.readUInt16BE(next + 8);
    const rd = next + 10;
    if (rd + rdlen > buf.length) throw new Error('truncated record');
    if (type === T_A && rdlen !== 4) throw new Error('bad A record');
    let data = null;
    if (type === T_PTR) data = readName(buf, rd).name;
    else if (type === T_SRV) data = { port: buf.readUInt16BE(rd + 4), target: readName(buf, rd + 6).name };
    else if (type === T_A) data = [...buf.subarray(rd, rd + 4)].join('.');
    else if (type === T_TXT) {
      data = {};
      for (let p = rd; p < rd + rdlen;) {
        const s = buf.toString('utf8', p + 1, p + 1 + buf[p]);
        const eq = s.indexOf('=');
        if (eq > 0) data[s.slice(0, eq)] = s.slice(eq + 1);
        p += 1 + buf[p];
      }
    }
    records.push({ name, type, data });
    off = rd + rdlen;
  }
  return records;
}

export function resolveDevices(records, service) {
  const lc = (s) => String(s).toLowerCase();
  const out = [];
  for (const ptr of records.filter((r) => r.type === T_PTR && lc(r.name) === lc(service))) {
    const inst = ptr.data;
    const srv = records.find((r) => r.type === T_SRV && lc(r.name) === lc(inst));
    const txt = records.find((r) => r.type === T_TXT && lc(r.name) === lc(inst))?.data ?? {};
    const a = srv && records.find((r) => r.type === T_A && lc(r.name) === lc(srv.data.target));
    if (srv && a && txt.id) out.push({ id: txt.id, name: txt.name || inst.split('.')[0], addr: `${a.data}:${srv.data.port}` });
  }
  return out;
}

export function discover({ service = MDNS_SERVICE, timeoutMs = 2000, socketFactory } = {}) {
  return new Promise((resolve) => {
    const sock = (socketFactory ?? (() => dgram.createSocket({ type: 'udp4', reuseAddr: true })))();
    const records = [];
    let finished = false;
    const finish = () => {
      if (finished) return;
      finished = true;
      try { sock.close(); } catch { /* already closed */ }
      const byId = new Map();
      for (const d of resolveDevices(records, service)) byId.set(d.id, d);
      resolve([...byId.values()]);
    };
    sock.on('message', (msg) => {
      try { records.push(...parseMessage(msg)); } catch { /* invalid packet */ }
    });
    sock.on('error', finish);
    sock.bind(0, () => {
      try { sock.send(buildQuery(service), 5353, '224.0.0.251'); } catch { finish(); }
    });
    setTimeout(finish, timeoutMs);
  });
}

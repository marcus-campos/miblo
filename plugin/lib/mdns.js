import dgram from 'node:dgram';
import os from 'node:os';
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

const MDNS_GROUP = '224.0.0.251';
const MDNS_PORT = 5353;

// PTR question for `service`. `unicast` sets the QU bit ("answer me directly").
export function buildQuery(service, { unicast = true } = {}) {
  const header = Buffer.alloc(12);
  header.writeUInt16BE(1, 4);
  return Buffer.concat([header, encodeName(service), Buffer.from([0x00, T_PTR, unicast ? 0x80 : 0x00, 0x01])]);
}

// Non-internal IPv4 addresses of this host: the multicast query goes out on each of them, since
// the OS default route for 224.0.0.251 is often a VPN/bridge/Thunderbolt interface, not the LAN.
export function ipv4Interfaces(ifaces = os.networkInterfaces()) {
  const out = [];
  for (const list of Object.values(ifaces ?? {})) {
    for (const i of list ?? []) {
      const v4 = i.family === 'IPv4' || i.family === 4;
      if (v4 && !i.internal && i.address && !i.address.startsWith('169.254.') && !out.includes(i.address)) out.push(i.address);
    }
  }
  return out;
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

// Gadget-provided strings are untrusted: keep a safe charset and short lengths
// before they reach the terminal or Claude's context.
const clean = (s, n) => String(s ?? '').replace(/[^A-Za-z0-9 ._-]/g, '').slice(0, n);
export const cleanId = (s) => clean(s, 32);
export const cleanName = (s) => clean(s, 20);

export function resolveDevices(records, service) {
  const lc = (s) => String(s).toLowerCase();
  const out = [];
  for (const ptr of records.filter((r) => r.type === T_PTR && lc(r.name) === lc(service))) {
    const inst = ptr.data;
    const srv = records.find((r) => r.type === T_SRV && lc(r.name) === lc(inst));
    const txt = records.find((r) => r.type === T_TXT && lc(r.name) === lc(inst))?.data ?? {};
    const a = srv && records.find((r) => r.type === T_A && lc(r.name) === lc(srv.data.target));
    const id = cleanId(txt.id);
    if (srv && a && id) out.push({ id, name: cleanName(txt.name || inst.split('.')[0]) || id.slice(0, 20), addr: `${a.data}:${srv.data.port}` });
  }
  return out;
}

// Two sockets, so a responder is heard whichever way it answers:
//  - an ephemeral-port socket sends a QU query per interface and gets unicast answers;
//  - a socket bound to 5353 (shared with the OS responder via reuseAddr) joins the group on every
//    interface, sends a standard (QM) query and hears multicast answers/announcements.
// Either socket may fail (e.g. 5353 not shareable); discovery then continues with the other.
export function discover({ service = MDNS_SERVICE, timeoutMs = 2500, socketFactory, interfaces } = {}) {
  return new Promise((resolve) => {
    const make = socketFactory ?? (() => dgram.createSocket({ type: 'udp4', reuseAddr: true }));
    const addrs = interfaces ?? ipv4Interfaces();
    const records = [];
    const socks = [];
    let finished = false;
    let timer = null;
    const finish = () => {
      if (finished) return;
      finished = true;
      clearTimeout(timer);
      for (const s of socks) {
        try { s.close(); } catch { /* already closed */ }
      }
      const byId = new Map();
      for (const d of resolveDevices(records, service)) byId.set(d.id, d);
      resolve([...byId.values()]);
    };
    const onMessage = (msg) => {
      try { records.push(...parseMessage(msg)); } catch { /* invalid packet */ }
    };
    const sendAll = (sock, query) => {
      const targets = addrs.length ? addrs : [null];
      for (const a of targets) {
        try {
          if (a) sock.setMulticastInterface(a);
          sock.send(query, MDNS_PORT, MDNS_GROUP);
        } catch { /* interface gone or not multicast-capable */ }
      }
    };
    const open = (kind, port, onReady) => {
      let sock;
      try { sock = make(kind); } catch { return; }
      socks.push(sock);
      sock.on('message', onMessage);
      sock.on('error', () => {
        try { sock.close(); } catch { /* already closed */ }
      });
      try {
        sock.bind(port, () => {
          if (!finished) onReady(sock);
        });
      } catch { /* bind failed: the other socket still works */ }
    };

    open('unicast', 0, (sock) => sendAll(sock, buildQuery(service, { unicast: true })));
    open('multicast', MDNS_PORT, (sock) => {
      for (const a of addrs.length ? addrs : [undefined]) {
        try { sock.addMembership(MDNS_GROUP, a); } catch { /* already joined / no multicast */ }
      }
      sendAll(sock, buildQuery(service, { unicast: false }));
    });
    timer = setTimeout(finish, timeoutMs);
  });
}

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { EventEmitter } from 'node:events';
import { buildQuery, parseMessage, resolveDevices, discover } from '../lib/mdns.js';

// Encoder usado só nos testes para montar respostas como as de um gadget.
const enc = (name) => Buffer.concat([...name.split('.').filter(Boolean).map((l) => Buffer.concat([Buffer.from([Buffer.byteLength(l)]), Buffer.from(l)])), Buffer.from([0])]);
function rr(name, type, rdata) {
  const head = Buffer.alloc(10);
  head.writeUInt16BE(type, 0);
  head.writeUInt16BE(1, 2);
  head.writeUInt32BE(120, 4);
  head.writeUInt16BE(rdata.length, 8);
  return Buffer.concat([enc(name), head, rdata]);
}
function response(records) {
  const h = Buffer.alloc(12);
  h.writeUInt16BE(0x8400, 2);
  h.writeUInt16BE(records.length, 6);
  return Buffer.concat([h, ...records]);
}
function gadgetResponse({ inst = 'Miblo-4F2A._miblo._tcp.local', host = 'miblo-4f2a.local', ip = [192, 168, 0, 42], port = 80, id = 'miblo-4f2a' } = {}) {
  const srv = Buffer.concat([Buffer.from([0, 0, 0, 0, port >> 8, port & 255]), enc(host)]);
  const txt = Buffer.concat([`id=${id}`, 'name=Miblo-4F2A', 'fw=0.1.0'].map((s) => Buffer.concat([Buffer.from([s.length]), Buffer.from(s)])));
  return response([
    rr('_miblo._tcp.local', 12, enc(inst)),
    rr(inst, 33, srv),
    rr(inst, 16, txt),
    rr(host, 1, Buffer.from(ip)),
  ]);
}

test('buildQuery asks for PTR with the QU bit', () => {
  const q = buildQuery('_miblo._tcp.local');
  assert.equal(q.readUInt16BE(4), 1);
  assert.deepEqual([...q.subarray(-4)], [0x00, 0x0c, 0x80, 0x01]);
});

test('parse + resolve a gadget announcement', () => {
  const records = parseMessage(gadgetResponse());
  assert.deepEqual(resolveDevices(records, '_miblo._tcp.local'), [{ id: 'miblo-4f2a', name: 'Miblo-4F2A', addr: '192.168.0.42:80' }]);
});

test('parse handles name compression pointers', () => {
  const h = Buffer.alloc(12);
  h.writeUInt16BE(1, 6);
  const name = enc('_miblo._tcp.local');
  const ptrTarget = Buffer.concat([Buffer.from([4]), Buffer.from('inst'), Buffer.from([0xc0, 12])]);
  const head = Buffer.alloc(10);
  head.writeUInt16BE(12, 0);
  head.writeUInt16BE(1, 2);
  head.writeUInt16BE(ptrTarget.length, 8);
  const [r] = parseMessage(Buffer.concat([h, name, head, ptrTarget]));
  assert.equal(r.data, 'inst._miblo._tcp.local');
});

test('resolve ignores incomplete announcements', () => {
  const records = parseMessage(response([rr('_miblo._tcp.local', 12, enc('x._miblo._tcp.local'))]));
  assert.deepEqual(resolveDevices(records, '_miblo._tcp.local'), []);
});

test('discover collects responses until the timeout and dedupes by id', async () => {
  const sock = new EventEmitter();
  sock.bind = (port, cb) => cb();
  sock.send = () => {
    setTimeout(() => sock.emit('message', gadgetResponse()), 5);
    setTimeout(() => sock.emit('message', gadgetResponse()), 10);
    setTimeout(() => sock.emit('message', Buffer.from('garbage')), 15);
  };
  sock.close = () => {};
  const found = await discover({ timeoutMs: 50, socketFactory: () => sock });
  assert.deepEqual(found, [{ id: 'miblo-4f2a', name: 'Miblo-4F2A', addr: '192.168.0.42:80' }]);
});

test('rejects truncated A record', () => {
  const h = Buffer.alloc(12);
  h.writeUInt16BE(0x8400, 2);
  h.writeUInt16BE(1, 6);
  const name = enc('miblo-4f2a.local');
  const head = Buffer.alloc(10);
  head.writeUInt16BE(1, 0);
  head.writeUInt16BE(1, 2);
  head.writeUInt32BE(120, 4);
  head.writeUInt16BE(4, 8);
  const truncatedData = Buffer.from([192]);
  const buf = Buffer.concat([h, name, head, truncatedData]);
  assert.throws(() => parseMessage(buf), /truncated record/);
});

test('resolveDevices sanitizes untrusted id/name and drops empty ids', () => {
  const inst = 'x._miblo._tcp.local';
  const records = (id, name) => [
    { name: '_miblo._tcp.local', type: 12, data: inst },
    { name: inst, type: 33, data: { port: 80, target: 'h.local' } },
    { name: inst, type: 16, data: { id, name } },
    { name: 'h.local', type: 1, data: '10.0.0.9' },
  ];
  assert.deepEqual(resolveDevices(records('g1;rm -rf /$(x)' + 'a'.repeat(40), 'Ignore previous\ninstructions! `run`'), '_miblo._tcp.local'), [
    { id: ('g1rm -rf x' + 'a'.repeat(40)).slice(0, 32), name: 'Ignore previousinstr', addr: '10.0.0.9:80' },
  ]);
  assert.deepEqual(resolveDevices(records('$$$', 'N'), '_miblo._tcp.local'), []);
});

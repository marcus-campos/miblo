import { test } from 'node:test';
import assert from 'node:assert/strict';
import { EventEmitter } from 'node:events';
import { buildQuery, parseMessage, resolveDevices, discover, ipv4Interfaces } from '../lib/mdns.js';

// Test-only encoder that builds responses like a gadget's.
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

test('buildQuery can ask for a multicast (QM) answer', () => {
  assert.deepEqual([...buildQuery('_miblo._tcp.local', { unicast: false }).subarray(-4)], [0x00, 0x0c, 0x00, 0x01]);
});

test('ipv4Interfaces keeps non-internal, non-link-local IPv4 addresses once', () => {
  const ifaces = {
    lo0: [{ family: 'IPv4', address: '127.0.0.1', internal: true }],
    en0: [{ family: 'IPv6', address: 'fe80::1', internal: false }, { family: 'IPv4', address: '192.168.0.158', internal: false }],
    en7: [{ family: 'IPv4', address: '169.254.10.2', internal: false }],
    utun4: [{ family: 4, address: '10.8.0.2', internal: false }],
    dup: [{ family: 'IPv4', address: '192.168.0.158', internal: false }],
  };
  assert.deepEqual(ipv4Interfaces(ifaces), ['192.168.0.158', '10.8.0.2']);
  assert.deepEqual(ipv4Interfaces({}), []);
});

function fakeSocket(kind, log, { bindError = false, onSend } = {}) {
  const s = new EventEmitter();
  s.kind = kind;
  s.bind = (port, cb) => {
    log.push(`${kind}:bind:${port}`);
    if (bindError) setImmediate(() => s.emit('error', new Error('EADDRINUSE')));
    else setImmediate(cb);
  };
  s.setMulticastInterface = (a) => log.push(`${kind}:if:${a}`);
  s.addMembership = (g, a) => log.push(`${kind}:join:${g}@${a}`);
  s.send = (buf, port, host) => {
    log.push(`${kind}:send:${host}:${port}:${buf[buf.length - 2] ? 'QU' : 'QM'}`);
    onSend?.(s);
  };
  s.close = () => log.push(`${kind}:close`);
  return s;
}

test('discover queries every interface on both sockets and dedupes by id', async () => {
  const log = [];
  const found = await discover({
    timeoutMs: 50,
    interfaces: ['192.168.0.158', '10.8.0.2'],
    socketFactory: (kind) => fakeSocket(kind, log, {
      onSend: (s) => {
        setTimeout(() => s.emit('message', gadgetResponse()), 5);
        setTimeout(() => s.emit('message', Buffer.from('garbage')), 10);
      },
    }),
  });
  assert.deepEqual(found, [{ id: 'miblo-4f2a', name: 'Miblo-4F2A', addr: '192.168.0.42:80' }]);
  for (const a of ['192.168.0.158', '10.8.0.2']) {
    assert.ok(log.includes(`multicast:join:224.0.0.251@${a}`), a);
    assert.ok(log.includes(`unicast:if:${a}`) && log.includes(`multicast:if:${a}`), a);
  }
  assert.ok(log.includes('unicast:bind:0') && log.includes('multicast:bind:5353'));
  assert.equal(log.filter((l) => l === 'unicast:send:224.0.0.251:5353:QU').length, 2);
  assert.equal(log.filter((l) => l === 'multicast:send:224.0.0.251:5353:QM').length, 2);
});

test('discover merges records split across unicast and multicast answers', async () => {
  // PTR+SRV+TXT arrive by unicast, the A record only by multicast.
  const inst = 'Miblo-4F2A._miblo._tcp.local';
  const firstPart = response([
    rr('_miblo._tcp.local', 12, enc(inst)),
    rr(inst, 33, Buffer.concat([Buffer.from([0, 0, 0, 0, 0, 80]), enc('miblo-4f2a.local')])),
    rr(inst, 16, Buffer.concat(['id=miblo-4f2a', 'name=Miblo-4F2A'].map((x) => Buffer.concat([Buffer.from([x.length]), Buffer.from(x)])))),
  ]);
  const secondPart = response([rr('miblo-4f2a.local', 1, Buffer.from([192, 168, 0, 42]))]);
  const found = await discover({
    timeoutMs: 50,
    interfaces: ['192.168.0.158'],
    socketFactory: (kind) => fakeSocket(kind, [], {
      onSend: (s) => setTimeout(() => s.emit('message', kind === 'unicast' ? firstPart : secondPart), 5),
    }),
  });
  assert.deepEqual(found, [{ id: 'miblo-4f2a', name: 'Miblo-4F2A', addr: '192.168.0.42:80' }]);
});

test('discover still works when port 5353 cannot be bound', async () => {
  const log = [];
  const found = await discover({
    timeoutMs: 50,
    interfaces: ['192.168.0.158'],
    socketFactory: (kind) => fakeSocket(kind, log, {
      bindError: kind === 'multicast',
      onSend: (s) => setTimeout(() => s.emit('message', gadgetResponse()), 5),
    }),
  });
  assert.deepEqual(found.map((d) => d.id), ['miblo-4f2a']);
  assert.ok(!log.some((l) => l.startsWith('multicast:send')));
});

test('discover falls back to the default interface when none is listed', async () => {
  const log = [];
  await discover({ timeoutMs: 20, interfaces: [], socketFactory: (kind) => fakeSocket(kind, log) });
  assert.ok(!log.some((l) => l.includes(':if:')));
  assert.ok(log.includes('unicast:send:224.0.0.251:5353:QU'));
  assert.ok(log.includes('multicast:join:224.0.0.251@undefined'));
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

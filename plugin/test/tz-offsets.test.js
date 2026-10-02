import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  intlOffset, posixOffset, tzifOffset, nextChange, zoneSource, ZoneOffsets, osZoneInfo, newerTz, isZoneName, ZONE_CACHE_MAX,
} from '../lib/tz-offsets.js';

const s = (iso) => Date.parse(iso) / 1000;
const intl = (zone) => (utc) => intlOffset(zone, utc * 1000);
const OCT2 = Date.parse('2026-10-02T12:00:00Z');

test('intl: offsets in minutes east, half and quarter hours included', () => {
  assert.equal(intlOffset('UTC', OCT2), 0);
  assert.equal(intlOffset('America/Sao_Paulo', OCT2), -180);
  assert.equal(intlOffset('Asia/Kolkata', OCT2), 330);
  assert.equal(intlOffset('Asia/Kathmandu', OCT2), 345);
  assert.equal(intlOffset('Pacific/Chatham', Date.parse('2026-07-01T00:00:00Z')), 765);
  assert.equal(intlOffset('Pacific/Chatham', Date.parse('2027-01-01T00:00:00Z')), 825);
  assert.throws(() => intlOffset('Mars/Olympus_Mons', OCT2), RangeError);
});

// Rules that have not changed in years, so any Node release's data agrees.
const cases = [
  ['Europe/Lisbon (north, DST ends)', 'Europe/Lisbon', OCT2, { off: 60, next: s('2026-10-25T01:00:00Z'), noff: 0 }],
  ['America/New_York (north)', 'America/New_York', OCT2, { off: -240, next: s('2026-11-01T06:00:00Z'), noff: -300 }],
  ['Australia/Sydney (south, DST starts)', 'Australia/Sydney', OCT2, { off: 600, next: s('2026-10-03T16:00:00Z'), noff: 660 }],
  ['Pacific/Chatham (+13:45 -> +12:45)', 'Pacific/Chatham', OCT2, { off: 825, next: s('2027-04-03T14:00:00Z'), noff: 765 }],
  ['America/Sao_Paulo (no DST)', 'America/Sao_Paulo', OCT2, { off: -180, next: 0, noff: -180 }],
  ['Asia/Kathmandu (+5:45, no DST)', 'Asia/Kathmandu', OCT2, { off: 345, next: 0, noff: 345 }],
  ['Africa/Casablanca (Ramadan)', 'Africa/Casablanca', Date.parse('2025-01-01T00:00:00Z'), { off: 60, next: s('2025-02-23T02:00:00Z'), noff: 0 }],
];
for (const [label, zone, now, want] of cases) {
  test(`next change: ${label}`, () => {
    assert.deepEqual(nextChange(zone, now, intl(zone)), { z: zone, ...want });
  });
}

test('next change: the second before is the old offset, the found second the new one', () => {
  const fn = intl('Europe/Lisbon');
  const { next } = nextChange('Europe/Lisbon', OCT2, fn);
  assert.equal(fn(next - 1), 60);
  assert.equal(fn(next), 0);
});

test('next change: nothing within the horizon (400 days)', () => {
  let calls = 0;
  const r = nextChange('X/Y', OCT2, (t) => { calls++; return t > OCT2 / 1000 + 401 * 86400 ? 60 : 0; });
  assert.deepEqual(r, { z: 'X/Y', off: 0, next: 0, noff: 0 });
  assert.ok(calls <= 402);
});

test('posix rules: the TZif footers, as the firmware reads them', () => {
  const chatham = posixOffset('<+1245>-12:45<+1345>,M9.5.0/2:45,M4.1.0/3:45');
  assert.equal(chatham(s('2026-07-01T00:00:00Z')), 765);
  assert.equal(chatham(s('2027-01-01T00:00:00Z')), 825);
  assert.equal(chatham(s('2027-04-03T13:59:59Z')), 825);
  assert.equal(chatham(s('2027-04-03T14:00:00Z')), 765);
  const israel = posixOffset('IST-2IDT,M3.4.4/26,M10.5.0');  // Friday 02:00 written as Thursday 26:00
  assert.equal(israel(s('2026-03-26T23:59:59Z')), 120);
  assert.equal(israel(s('2026-03-27T00:00:00Z')), 180);
  const nuuk = posixOffset('<-02>2<-01>,M3.5.0/-1,M10.5.0/0');  // negative times
  assert.equal(nuuk(s('2026-01-15T00:00:00Z')), -120);
  assert.equal(nuuk(s('2026-07-15T00:00:00Z')), -60);
  assert.equal(nuuk(s('2026-03-29T00:59:59Z')), -120);
  assert.equal(nuuk(s('2026-03-29T01:00:00Z')), -60);
  assert.equal(posixOffset('<+0545>-5:45')(0), 345);
  assert.equal(posixOffset('MST7')(0), -420);
  const julian = posixOffset('AAA-1BBB,J60/0,J300/0');  // J60 = March 1, leap years too
  assert.equal(julian(s('2028-02-29T12:00:00Z')), 60);
  assert.equal(julian(s('2028-03-01T12:00:00Z')), 120);
  assert.equal(posixOffset('garbage'), null);
  assert.equal(posixOffset(''), null);
});

// A TZif v2 file with no transitions (only types) and the given footer.
function tzif(footer, utoff = 0) {
  const header = (typecnt, charcnt) => {
    const h = Buffer.alloc(44);
    h.write('TZif2', 0, 'latin1');
    h.writeUInt32BE(typecnt, 36);
    h.writeUInt32BE(charcnt, 40);
    return h;
  };
  const type = Buffer.alloc(6);
  type.writeInt32BE(utoff, 0);
  const block = Buffer.concat([type, Buffer.from('LMT\0', 'latin1')]);
  return Buffer.concat([header(1, 4), block, header(1, 4), block, Buffer.from(`\n${footer}\n`, 'latin1')]);
}

test('tzif: the footer applies after the last transition; garbage is refused', () => {
  assert.equal(tzifOffset(tzif('<+0545>-5:45'))(s('2026-10-02T00:00:00Z')), 345);
  assert.equal(tzifOffset(tzif('', 3600))(0), 60);  // no footer: the file's only type
  assert.equal(tzifOffset(Buffer.from('not a zone file at all, not at all, nope ...')), null);
  assert.equal(tzifOffset(tzif('???')), null);
  assert.equal(tzifOffset(tzif('<+0545>-5:45').subarray(0, 60)), null);
});

const os = osZoneInfo();
test('tzif: the operating system\'s zone files agree with Intl where both have the same rules', { skip: !os && 'no zone files here' }, () => {
  for (const [, zone, now, want] of cases) {
    const fn = zoneSource(zone, { os, nodeTz: '0000a' });  // forces the OS files
    assert.ok(fn, zone);
    assert.deepEqual(nextChange(zone, now, fn), { z: zone, ...want }, zone);
  }
});

test('source: the newer tz database wins; unknown or unsafe names are refused', () => {
  assert.ok(newerTz('2026c', '2024b'));
  assert.ok(!newerTz('2024b', '2026c'));
  assert.ok(newerTz('2026za', '2026z'));
  assert.ok(!newerTz('2026c', '2026c'));
  assert.ok(!newerTz(null, '2026c'));
  const fake = { dir: '/nonexistent', version: '2099a' };
  assert.equal(zoneSource('Asia/Kathmandu', { os: fake })(s('2026-01-01T00:00:00Z')), 345);  // falls back to Intl
  assert.equal(zoneSource('Mars/Olympus_Mons', { os: null }), null);
  for (const bad of ['../../etc/passwd', '/etc/passwd', 'Europe/../../x', '<-03>3', 'EST5EDT,M3.2.0,M11.1.0', '', null, 7]) {
    assert.equal(isZoneName(bad), false, String(bad));
    assert.equal(zoneSource(bad, { os: fake }), null);
  }
  assert.ok(isZoneName('America/Argentina/Buenos_Aires') && isZoneName('Etc/GMT+5') && isZoneName('UTC'));
});

test('cache: one computation an hour per zone, a new one once the change has come', () => {
  let made = 0;
  let off = 60;
  const z = new ZoneOffsets({ source: () => { made++; return (t) => (t >= 2000 ? 0 : off); } });
  assert.deepEqual(z.get('A/B', 1000_000), { z: 'A/B', off: 60, next: 2000, noff: 0 });
  assert.equal(z.get('A/B', 1500_000).off, 60);
  assert.equal(made, 1);
  off = 30;
  assert.deepEqual(z.get('A/B', 2000_000), { z: 'A/B', off: 0, next: 0, noff: 0 });  // past `next`
  assert.equal(made, 2);
});

test('withZones: the gadget\'s zones, at most two, unknown ones left out', () => {
  const z = new ZoneOffsets({ source: (zone) => (zone === 'Bad/Zone' ? null : () => 0) });
  const snap = { v: 1, seq: 1 };
  assert.deepEqual(z.withZones(snap, ['Europe/Lisbon', 'Asia/Tokyo'], OCT2).tz.map((e) => e.z), ['Europe/Lisbon', 'Asia/Tokyo']);
  assert.deepEqual(z.withZones(snap, ['Europe/Lisbon', 'Europe/Lisbon'], OCT2).tz.length, 1);
  assert.deepEqual(z.withZones(snap, ['Bad/Zone', 'Europe/Lisbon', 'Asia/Tokyo', 'UTC'], OCT2).tz.map((e) => e.z), ['Europe/Lisbon']);
  assert.equal(z.withZones(snap, [], OCT2), snap);
  assert.equal(z.withZones(snap, undefined, OCT2), snap);
  assert.equal(z.withZones(snap, ['<-03>3', ''], OCT2), snap);
  assert.deepEqual(snap, { v: 1, seq: 1 });  // never modified
});

// A TZif v2 file with one transition (at 0) to type `index`, among `typecnt` types.
function tzifWithIndex(index, typecnt = 1) {
  const header = () => {
    const h = Buffer.alloc(44);
    h.write('TZif2', 0, 'latin1');
    h.writeUInt32BE(1, 32);  // timecnt
    h.writeUInt32BE(typecnt, 36);
    h.writeUInt32BE(4, 40);  // charcnt
    return h;
  };
  const types = Buffer.alloc(6 * typecnt);
  const v1 = Buffer.concat([Buffer.alloc(4), Buffer.from([index]), types, Buffer.from('LMT\0', 'latin1')]);
  const v2 = Buffer.concat([Buffer.alloc(8), Buffer.from([index]), types, Buffer.from('LMT\0', 'latin1')]);
  return Buffer.concat([header(), v1, header(), v2, Buffer.from('\n\n', 'latin1')]);
}

test('tzif: a transition naming a type the file lacks is refused', () => {
  assert.ok(tzifOffset(tzifWithIndex(0)));
  assert.equal(tzifOffset(tzifWithIndex(1)), null);
  assert.equal(tzifOffset(tzifWithIndex(255, 2)), null);
});

test('cache: a zone whose offsets cannot be worked out gives no entry, never an exception', () => {
  let calls = 0;
  const z = new ZoneOffsets({ source: () => () => { calls++; throw new RangeError('unexpected offset "GMT+xx"'); } });
  assert.equal(z.get('A/B', OCT2), null);
  assert.equal(z.get('A/B', OCT2 + 1000), null);  // cached: not retried at once
  assert.equal(calls, 1);
  const snap = { v: 1 };
  assert.equal(z.withZones(snap, ['A/B'], OCT2), snap);
});

test('intl: an offset string it does not understand throws (caught by ZoneOffsets)', (t) => {
  t.mock.method(Intl.DateTimeFormat.prototype, 'formatToParts', () => [{ type: 'timeZoneName', value: 'GMT+xx' }]);
  assert.throws(() => intlOffset('Europe/Paris', OCT2), RangeError);
  const z = new ZoneOffsets({ source: (zone) => (utc) => intlOffset(zone, utc * 1000) });
  assert.equal(z.get('Europe/Paris', OCT2), null);
});

// L6: zone names come from the gadget (/api/info tz, tz2). A spoofed or malicious one could send a
// new name every hour: the cache is bounded (LRU) and a name that is not a zone name is never
// looked up nor kept.
test('cache: a name that is not a zone name is never looked up nor cached', () => {
  let made = 0;
  const z = new ZoneOffsets({ source: () => { made++; return () => 0; } });
  for (const bad of ['../etc/passwd', '/abs', 'a/b/c/d', 'x'.repeat(48), '', 'Europe/..', 'Bad Zone', 42, null]) {
    assert.equal(z.get(bad, 0), null);
  }
  assert.equal(made, 0);
  assert.equal(z.size, 0);
});

test('cache: bounded to the 16 most recently used zones', () => {
  let made = 0;
  const z = new ZoneOffsets({ source: () => { made++; return () => 0; } });
  for (let i = 0; i < 100; i++) {
    z.get(`Fake/Zone${i}`, 0);
    z.get('Europe/Lisbon', 0);  // kept in use: never evicted
  }
  assert.equal(z.size, ZONE_CACHE_MAX);
  assert.equal(ZONE_CACHE_MAX, 16);
  assert.equal(made, 101);
  z.get('Fake/Zone99', 0);
  assert.equal(made, 101);
  z.get('Fake/Zone0', 0);  // long evicted: worked out again
  assert.equal(made, 102);
});

test('cache: expired entries are dropped', () => {
  const z = new ZoneOffsets({ source: () => () => 0 });
  for (let i = 0; i < 10; i++) z.get(`Fake/Zone${i}`, 0);
  z.get('Europe/Lisbon', 2 * 3600_000);
  assert.equal(z.size, 1);
});

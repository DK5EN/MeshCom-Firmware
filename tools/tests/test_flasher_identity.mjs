// Pins pages/flash/identity.js against real partition-table and app-image
// dumps read from bench nodes (tools/tests/fixtures/flasher/).
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import {
  parsePartitionTable,
  findApp,
  parseBoardImage,
  parseIdLine,
} from '../../pages/flash/identity.js';

const fx = (n) => new Uint8Array(readFileSync(new URL(`./fixtures/flasher/${n}`, import.meta.url)));

// Splice a 128-byte MCBOARD1 marker into a copy of a real image at 0x120.
function withMarker(img, { format = 1, hwid = 43, env = 'heltec_v3', tag = 'v4.40a.10.20' } = {}) {
  const out = img.slice();
  const m = new Uint8Array(128);
  m.set(new TextEncoder().encode('MCBOARD1'), 0);
  new DataView(m.buffer).setUint16(8, format, true);
  new DataView(m.buffer).setUint16(10, hwid, true);
  m.set(new TextEncoder().encode(env).slice(0, 32), 12);
  m.set(new TextEncoder().encode(tag).slice(0, 24), 44);
  out.set(m, 0x120);
  return out;
}

test('T-Deck Plus table: safeboot factory + ota_0 12M', () => {
  const e = parsePartitionTable(fx('pt-tdeckplus.bin'));
  const sb = e.find((x) => x.label === 'safeboot');
  assert.equal(sb.type, 0);
  assert.equal(sb.subtype, 0);
  assert.equal(sb.offset, 0x10000);
  const app = findApp(e);
  assert.equal(app.label, 'app');
  assert.equal(app.subtype, 0x10);
  assert.equal(app.offset, 0xc0000);
  assert.equal(app.size, 12 * 1024 * 1024);
});

test('T-Beam table: ota_0 at 0xC0000, 3324K', () => {
  const e = parsePartitionTable(fx('pt-tbeam.bin'));
  assert.deepEqual(
    e.map((x) => x.label),
    ['nvs', 'otadata', 'safeboot', 'app', 'coredump'],
  );
  const app = findApp(e);
  assert.equal(app.offset, 0xc0000);
  assert.equal(app.size, 3324 * 1024);
});

test('table: garbage and empty give []', () => {
  assert.deepEqual(parsePartitionTable(new Uint8Array(3072).fill(0xff)), []);
  assert.deepEqual(parsePartitionTable(new Uint8Array(0)), []);
  const rnd = new Uint8Array(3072).map((_, i) => (i * 131 + 7) & 0xff);
  assert.deepEqual(parsePartitionTable(rnd), []);
});

test('findApp: falls back to first non-safeboot app; null if none', () => {
  const mk = (label, subtype) => ({ type: 0, subtype, offset: 0, size: 1, label });
  assert.equal(findApp([mk('safeboot', 0), mk('main', 0)]).label, 'main');
  assert.equal(findApp([mk('safeboot', 0)]), null);
  assert.equal(findApp([]), null);
  assert.equal(findApp([mk('a', 0), mk('b', 0x10)]).label, 'b');
});

test('real images without marker -> no-marker', () => {
  for (const n of ['ota0-tdeckplus.bin', 'ota0-tbeam.bin']) {
    assert.deepEqual(parseBoardImage(fx(n)), { ok: false, reason: 'no-marker' });
  }
});

test('real image with spliced marker', () => {
  const r = parseBoardImage(withMarker(fx('ota0-tbeam.bin')));
  assert.deepEqual(r, { ok: true, format: 1, hwid: 43, env: 'heltec_v3', tag: 'v4.40a.10.20' });
});

test('env of exactly 32 chars (no NUL)', () => {
  const env = 'e'.repeat(32);
  const r = parseBoardImage(withMarker(fx('ota0-tbeam.bin'), { env, tag: 't'.repeat(24) }));
  assert.equal(r.env, env);
  assert.equal(r.tag, 't'.repeat(24));
});

test('erased, no-image, no-app-desc, bad-format, short', () => {
  assert.deepEqual(parseBoardImage(new Uint8Array(4096).fill(0xff)), {
    ok: false,
    reason: 'erased',
  });
  const rnd = new Uint8Array(4096).map((_, i) => (i * 131 + 7) & 0xff);
  assert.equal(parseBoardImage(rnd).reason, 'no-image');
  const nodesc = fx('ota0-tbeam.bin').slice();
  nodesc.fill(0, 0x20, 0x24);
  assert.equal(parseBoardImage(nodesc).reason, 'no-app-desc');
  assert.equal(
    parseBoardImage(withMarker(fx('ota0-tbeam.bin'), { format: 2 })).reason,
    'bad-format',
  );
  assert.equal(parseBoardImage(new Uint8Array(16)).reason, 'no-image');
});

test('parseIdLine', () => {
  const dump = [
    '[BOOT];ready',
    'MCBOARD:v1:43:43:heltec_v3:v4.40a.10.19',
    'NODE 43 Heltec',
    'MCBOARD:v1:12:43:tbeam:(local)',
    '',
  ].join('\r\n');
  assert.deepEqual(parseIdLine(dump), {
    hwidRuntime: 12,
    hwidCompile: 43,
    env: 'tbeam',
    tag: '(local)',
  });
  assert.equal(parseIdLine('NODE 43 Heltec\nfoo'), null);
  assert.equal(parseIdLine(''), null);
  assert.equal(parseIdLine(undefined), null);
});

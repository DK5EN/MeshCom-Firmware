// Tests for pages/flash/romread.js: the pure SLIP/packet parts and an
// end-to-end run against a fake Web Serial port that emulates the ESP ROM
// loader (SYNC, READ_REG, SPI_ATTACH, READ_FLASH_SLOW) over a synthetic flash.
//
// Run: node --test tools/tests/test_flasher_romread.mjs
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import {
  slipEncode,
  SlipDecoder,
  checksum,
  buildCommand,
  parseResponse,
  statusError,
  chipFamilyFromMagic,
  readBoardIdentity,
  ESP_SYNC,
  ESP_READ_REG,
  ESP_SPI_ATTACH,
  ESP_READ_FLASH_SLOW,
} from '../../pages/flash/romread.js';

const fx = (n) => new Uint8Array(readFileSync(new URL(`./fixtures/flasher/${n}`, import.meta.url)));
const enc = (s) => new TextEncoder().encode(s);

test('slip: escapes C0 and DB, decoder round-trips across chunk borders', () => {
  const pkt = Uint8Array.from([1, 0xc0, 2, 0xdb, 3, 0xdb, 0xc0]);
  const wire = slipEncode(pkt);
  assert.deepEqual(
    [...wire],
    [0xc0, 1, 0xdb, 0xdc, 2, 0xdb, 0xdd, 3, 0xdb, 0xdd, 0xdb, 0xdc, 0xc0],
  );
  const d = new SlipDecoder();
  const got = [];
  for (const b of wire) got.push(...d.feed([b]));
  assert.equal(got.length, 1);
  assert.deepEqual([...got[0]], [...pkt]);
});

test('slip: banner text before the first frame and empty frames are ignored', () => {
  const d = new SlipDecoder();
  const wire = [...enc('ESP-ROM:esp32s3\r\nwaiting for download\r\n'), ...slipEncode([9, 8]), 0xc0];
  const got = d.feed(Uint8Array.from(wire));
  assert.equal(got.length, 1);
  assert.deepEqual([...got[0]], [9, 8]);
  // two frames back to back
  const two = d.feed(Uint8Array.from([...slipEncode([1]), ...slipEncode([2])]));
  assert.deepEqual(
    two.map((p) => [...p]),
    [[1], [2]],
  );
});

test('checksum: esptool XOR with 0xEF seed', () => {
  assert.equal(checksum([]), 0xef);
  assert.equal(checksum([1, 2, 4]), 0xef ^ 7);
});

test('buildCommand / parseResponse layout', () => {
  const c = buildCommand(0x0e, Uint8Array.from([1, 2, 3]), 0x11223344);
  assert.deepEqual([...c], [0, 0x0e, 3, 0, 0x44, 0x33, 0x22, 0x11, 1, 2, 3]);
  const r = parseResponse(Uint8Array.from([1, 0x0a, 4, 0, 0x78, 0x56, 0x34, 0x12, 0, 0, 0, 0]));
  assert.equal(r.op, 0x0a);
  assert.equal(r.val, 0x12345678);
  assert.equal(r.data.length, 4);
  assert.equal(parseResponse(Uint8Array.from([0, 1, 0, 0, 0, 0, 0, 0])), null); // request, not response
  assert.equal(parseResponse(Uint8Array.from([1, 2, 3])), null);
});

test('statusError: status bytes follow the response data', () => {
  assert.equal(statusError(Uint8Array.from([0, 0, 0, 0]), 0), null);
  assert.match(statusError(Uint8Array.from([1, 5, 0, 0]), 0), /0x5/);
  const withData = new Uint8Array(68);
  assert.equal(statusError(withData, 64), null);
  withData[64] = 1;
  withData[65] = 0x07;
  assert.match(statusError(withData, 64), /0x7/);
  assert.match(statusError(Uint8Array.from([0]), 0), /Status/);
});

test('chipFamilyFromMagic: known values, unknown -> null', () => {
  assert.equal(chipFamilyFromMagic(0x00f01d83), 'ESP32');
  assert.equal(chipFamilyFromMagic(9), 'ESP32-S3');
  assert.equal(chipFamilyFromMagic(0xdeadbeef), null);
});

// ---- fake port ---------------------------------------------------------

const SYNC_LEN = 36;

// ROM emulator behind Web Serial streams. `flash` is the synthetic flash.
function fakePort(flash, { magic = 0x9, silent = false, banner = true } = {}) {
  let ctl;
  const readable = new ReadableStream({
    start(c) {
      ctl = c;
      if (banner) c.enqueue(enc('ESP-ROM:esp32s3-20210327\r\nwaiting for download\r\n'));
    },
  });
  const sent = [];
  const signals = [];
  const dec = new SlipDecoder();
  const reply = (op, val, data) => {
    const pkt = new Uint8Array(8 + data.length);
    const dv = new DataView(pkt.buffer);
    pkt[0] = 1;
    pkt[1] = op;
    dv.setUint16(2, data.length, true);
    dv.setUint32(4, val, true);
    pkt.set(data, 8);
    ctl.enqueue(slipEncode(pkt));
  };
  const writable = new WritableStream({
    write(chunk) {
      for (const p of dec.feed(chunk)) {
        const dv = new DataView(p.buffer, p.byteOffset, p.byteLength);
        const op = p[1];
        const data = p.subarray(8);
        sent.push({ op, data: data.slice() });
        if (silent) continue;
        if (op === ESP_SYNC) {
          assert.equal(data.length, SYNC_LEN);
          for (let i = 0; i < 3; i++) reply(op, 0x07071220, new Uint8Array(4));
        } else if (op === ESP_READ_REG) {
          assert.equal(new DataView(data.buffer, data.byteOffset).getUint32(0, true), 0x40001000);
          reply(op, magic, new Uint8Array(4));
        } else if (op === ESP_SPI_ATTACH) {
          assert.equal(data.length, 8);
          reply(op, 0, new Uint8Array(4));
        } else if (op === ESP_READ_FLASH_SLOW) {
          const off = new DataView(data.buffer, data.byteOffset).getUint32(0, true);
          const len = new DataView(data.buffer, data.byteOffset).getUint32(4, true);
          assert.ok(len <= 64, 'block over 64 bytes');
          const out = new Uint8Array(68);
          out.set(flash.subarray(off, off + len));
          reply(op, 0, out);
        } else {
          reply(op, 0, Uint8Array.from([1, 5, 0, 0]));
        }
        void dv;
      }
    },
  });
  return {
    readable,
    writable,
    sent,
    signals,
    async setSignals(s) {
      signals.push(s);
    },
  };
}

function marker({ env = 't_deck_plus', tag = 'v4.40a.10.21', hwid = 46 } = {}) {
  const m = new Uint8Array(128);
  m.set(enc('MCBOARD1'), 0);
  new DataView(m.buffer).setUint16(8, 1, true);
  new DataView(m.buffer).setUint16(10, hwid, true);
  m.set(enc(env), 12);
  m.set(enc(tag), 44);
  return m;
}

function synthFlash({ withMarker = true, erased = false } = {}) {
  const flash = new Uint8Array(0xc0000 + 0x1000).fill(0xff);
  flash.set(fx('pt-tdeckplus.bin').subarray(0, 0xc00), 0x8000);
  if (!erased) {
    const img = fx('ota0-tdeckplus.bin').slice(0, 0x1000);
    if (withMarker) img.set(marker(), 0x120);
    else img.fill(0, 0x120, 0x1a0);
    flash.set(img, 0xc0000);
  }
  return flash;
}

const NOW = { sleep: async () => {}, syncTimeoutMs: 20 };

test('readBoardIdentity: marker found through the emulated ROM (native USB)', async () => {
  const port = fakePort(synthFlash());
  const logs = [];
  const r = await readBoardIdentity(port, { kind: 'esp-native', log: (t) => logs.push(t), ...NOW });
  assert.equal(r.family, 'ESP32-S3');
  assert.equal(r.app.offset, 0xc0000);
  assert.equal(r.app.label, 'app');
  assert.equal(r.image.ok, true);
  assert.equal(r.image.env, 't_deck_plus');
  assert.equal(r.image.tag, 'v4.40a.10.21');
  assert.equal(r.image.hwid, 46);
  assert.ok(logs.length >= 2);
  // 0xC00 partition table + 0x200 head in 64-byte blocks
  const reads = port.sent.filter((s) => s.op === ESP_READ_FLASH_SLOW);
  assert.equal(reads.length, 0xc00 / 64 + 0x200 / 64);
  // JTAG reset: ends with both lines low, board stays in download mode
  const last = port.signals[port.signals.length - 1];
  assert.deepEqual(last, { dataTerminalReady: false, requestToSend: false });
  assert.ok(port.signals.some((s) => s.dataTerminalReady && !s.requestToSend));
  // locks released: the streams can be locked again
  port.readable.getReader().releaseLock();
  port.writable.getWriter().releaseLock();
});

test('readBoardIdentity: bridge uses the classic DTR/RTS sequence, unknown magic still reads', async () => {
  const port = fakePort(synthFlash(), { magic: 0x12345678, banner: false });
  const r = await readBoardIdentity(port, { kind: 'bridge', ...NOW });
  assert.equal(r.family, null);
  assert.equal(r.image.ok, true);
  assert.deepEqual(port.signals.slice(0, 3), [
    { dataTerminalReady: false, requestToSend: true },
    { dataTerminalReady: true, requestToSend: false },
    { dataTerminalReady: false, requestToSend: false },
  ]);
});

test('readBoardIdentity: classic ESP32 magic', async () => {
  const r = await readBoardIdentity(fakePort(synthFlash(), { magic: 0x00f01d83 }), {
    kind: 'bridge',
    ...NOW,
  });
  assert.equal(r.family, 'ESP32');
});

test('readBoardIdentity: old image without marker and erased app are reasons, not errors', async () => {
  const old = await readBoardIdentity(fakePort(synthFlash({ withMarker: false })), {
    kind: 'esp-native',
    ...NOW,
  });
  assert.deepEqual(old.image, { ok: false, reason: 'no-marker' });
  const erased = await readBoardIdentity(fakePort(synthFlash({ erased: true })), {
    kind: 'esp-native',
    ...NOW,
  });
  assert.deepEqual(erased.image, { ok: false, reason: 'erased' });
});

test('readBoardIdentity: no SYNC answer -> German error, locks released', async () => {
  const port = fakePort(synthFlash(), { silent: true });
  await assert.rejects(
    readBoardIdentity(port, { kind: 'bridge', ...NOW }),
    /meldet sich nicht im Download-Modus/,
  );
  port.readable.getReader().releaseLock();
  port.writable.getWriter().releaseLock();
  // three reset rounds were tried
  assert.ok(port.signals.length >= 9);
});

test('readBoardIdentity: no partition table -> German error', async () => {
  const flash = new Uint8Array(0x10000).fill(0xff);
  await assert.rejects(
    readBoardIdentity(fakePort(flash), { kind: 'esp-native', ...NOW }),
    /Keine Partitionstabelle/,
  );
});

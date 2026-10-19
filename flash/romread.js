// MeshCom Web Flasher -- minimal ESP ROM loader client over Web Serial.
//
// Reads the partition table and the first bytes of the app image from a chip
// in ROM download mode, to name the board that holds a MeshCom image even when
// no app runs (erased NVS, Safeboot, foreign firmware on top). No stub, no
// library: SLIP framing, SYNC, READ_REG, SPI_ATTACH and READ_FLASH_SLOW, the
// same commands `esptool --no-stub read_flash` uses (esptool/loader.py,
// targets/esp32.py). The pure parts are exported and pinned by
// tools/tests/test_flasher_romread.mjs.

import { parsePartitionTable, findApp, parseBoardImage } from './identity.js';

export const ESP_READ_FLASH_SLOW = 0x0e;
export const ESP_SPI_ATTACH = 0x0d;
export const ESP_SYNC = 0x08;
export const ESP_READ_REG = 0x0a;
export const CHIP_DETECT_MAGIC_REG_ADDR = 0x40001000;
export const CHECKSUM_MAGIC = 0xef;
const BLOCK_LEN = 64; // ROM read limit per READ_FLASH_SLOW command
const PT_OFFSET = 0x8000;
const PT_LEN = 0xc00;
const HEAD_LEN = 0x200; // 0x120 + 128 marker bytes, with room to spare

// Value of CHIP_DETECT_MAGIC_REG_ADDR per chip (esptool targets/*.py).
const MAGIC_FAMILY = new Map([
  [0x00f01d83, 'ESP32'],
  [0x00000009, 'ESP32-S3'],
  [0x000007c6, 'ESP32-S2'],
  [0x6921506f, 'ESP32-C3'],
  [0x1b31506f, 'ESP32-C3'],
  [0x2ce0806f, 'ESP32-C6'],
]);

export function chipFamilyFromMagic(magic) {
  return MAGIC_FAMILY.get(magic >>> 0) || null;
}

// ---- SLIP --------------------------------------------------------------

export function slipEncode(packet) {
  const out = [0xc0];
  for (const b of packet) {
    if (b === 0xdb) out.push(0xdb, 0xdd);
    else if (b === 0xc0) out.push(0xdb, 0xdc);
    else out.push(b);
  }
  out.push(0xc0);
  return Uint8Array.from(out);
}

// Incremental decoder. Bytes outside a frame (the ROM banner text printed
// before the first SYNC) are dropped; empty frames are ignored.
export class SlipDecoder {
  constructor() {
    this.reset();
  }
  reset() {
    this.buf = [];
    this.inFrame = false;
    this.esc = false;
  }
  // Returns the packets completed by `bytes`.
  feed(bytes) {
    const out = [];
    for (const b of bytes) {
      if (!this.inFrame) {
        if (b === 0xc0) this.inFrame = true;
        continue;
      }
      if (this.esc) {
        this.esc = false;
        if (b === 0xdc) this.buf.push(0xc0);
        else if (b === 0xdd) this.buf.push(0xdb);
        else {
          // Invalid escape: drop the frame and wait for the next delimiter.
          this.buf = [];
          this.inFrame = false;
        }
      } else if (b === 0xc0) {
        if (this.buf.length) out.push(Uint8Array.from(this.buf));
        this.buf = [];
      } else if (b === 0xdb) {
        this.esc = true;
      } else {
        this.buf.push(b);
      }
    }
    return out;
  }
}

// ---- packets -----------------------------------------------------------

export function checksum(data, state = CHECKSUM_MAGIC) {
  for (const b of data) state ^= b;
  return state;
}

// Request: dir 0, op, u16 data length, u32 checksum (or 0), data.
export function buildCommand(op, data = new Uint8Array(0), chk = 0) {
  const pkt = new Uint8Array(8 + data.length);
  const dv = new DataView(pkt.buffer);
  pkt[0] = 0;
  pkt[1] = op;
  dv.setUint16(2, data.length, true);
  dv.setUint32(4, chk >>> 0, true);
  pkt.set(data, 8);
  return pkt;
}

// Response: dir 1, op, u16 length, u32 value, data. null if it is no response.
export function parseResponse(packet) {
  if (!packet || packet.length < 8 || packet[0] !== 1) return null;
  const dv = new DataView(packet.buffer, packet.byteOffset, packet.byteLength);
  return {
    op: packet[1],
    len: dv.getUint16(2, true),
    val: dv.getUint32(4, true),
    data: packet.subarray(8),
  };
}

// The two status bytes follow the `respLen` data bytes (ROM loaders of the
// ESP32 family append two more, reserved). Non-zero first byte = error code in
// the second. Returns null when fine, else a text.
export function statusError(data, respLen = 0) {
  if (data.length < respLen + 2) {
    return data.length >= 2 && data[0] !== 0
      ? `Fehlercode 0x${data[1].toString(16)}`
      : `nur ${data.length} Byte Status`;
  }
  return data[respLen] !== 0 ? `Fehlercode 0x${data[respLen + 1].toString(16)}` : null;
}

const u32 = (...vals) => {
  const b = new Uint8Array(vals.length * 4);
  const dv = new DataView(b.buffer);
  vals.forEach((v, i) => dv.setUint32(i * 4, v >>> 0, true));
  return b;
};

export const SYNC_PAYLOAD = Uint8Array.from([0x07, 0x07, 0x12, 0x20, ...new Array(32).fill(0x55)]);

// ---- reset sequences (esptool/reset.py) --------------------------------

const defaultSleep = (ms) => new Promise((r) => setTimeout(r, ms));

// USB-UART bridge: DTR -> IO0, RTS -> EN (ClassicReset).
export async function classicReset(port, sleep, delayMs = 50) {
  await port.setSignals({ dataTerminalReady: false, requestToSend: true }); // EN low
  await sleep(100);
  await port.setSignals({ dataTerminalReady: true, requestToSend: false }); // IO0 low, EN high
  await sleep(delayMs);
  await port.setSignals({ dataTerminalReady: false, requestToSend: false }); // IO0 high
}

// ESP32-S3 USB-Serial/JTAG (USBJTAGSerialReset).
export async function usbJtagReset(port, sleep) {
  await port.setSignals({ dataTerminalReady: false, requestToSend: false });
  await sleep(100);
  await port.setSignals({ dataTerminalReady: true, requestToSend: false });
  await sleep(100);
  await port.setSignals({ dataTerminalReady: true, requestToSend: true });
  await port.setSignals({ dataTerminalReady: false, requestToSend: true });
  await sleep(100);
  await port.setSignals({ dataTerminalReady: false, requestToSend: false });
}

// ---- link --------------------------------------------------------------

class Link {
  constructor(port) {
    this.port = port;
    this.decoder = new SlipDecoder();
    this.queue = [];
    this.wake = null;
    this.lost = null;
    this.reader = port.readable.getReader();
    this.writer = port.writable.getWriter();
    this.pump = (async () => {
      try {
        for (;;) {
          const { value, done } = await this.reader.read();
          if (done) {
            // Closed or cancelled from outside: fail fast instead of timing out.
            this.lost = this.lost || new Error('closed');
            break;
          }
          for (const p of this.decoder.feed(value)) this.queue.push(p);
          if (this.wake) this.wake();
        }
      } catch (err) {
        this.lost = err;
      }
      if (this.wake) this.wake();
    })();
  }

  async write(packet) {
    await this.writer.write(slipEncode(packet));
  }

  flush() {
    this.queue.length = 0;
    this.decoder.reset();
  }

  // Next packet, or null after `ms`.
  async readPacket(ms) {
    const until = Date.now() + ms;
    for (;;) {
      if (this.queue.length) return this.queue.shift();
      if (this.lost) throw new Error('Die Verbindung ist abgerissen.');
      const left = until - Date.now();
      if (left <= 0) return null;
      await new Promise((resolve) => {
        const t = setTimeout(resolve, left);
        this.wake = () => {
          clearTimeout(t);
          resolve();
        };
      });
      this.wake = null;
    }
  }

  // Send one command and wait for the response with the same op. null on timeout.
  async command(op, data, ms) {
    await this.write(buildCommand(op, data));
    for (let i = 0; i < 100; i++) {
      const p = await this.readPacket(ms);
      if (!p) return null;
      const r = parseResponse(p);
      if (r && r.op === op) return r;
    }
    return null;
  }

  // command() plus status check; returns the response data (without status).
  async check(desc, op, data, respLen = 0, ms = 3000) {
    const r = await this.command(op, data, ms);
    if (!r) throw new Error(`Der Chip antwortet nicht (${desc}).`);
    const err = statusError(r.data, respLen);
    if (err) throw new Error(`Der Chip meldet einen Fehler (${desc}): ${err}.`);
    return { val: r.val, data: r.data.subarray(0, respLen) };
  }

  async close() {
    this.writer.releaseLock();
    try {
      await this.reader.cancel();
    } catch {
      // Reader already errored out; nothing left to cancel.
    }
    await this.pump;
    this.reader.releaseLock();
  }
}

async function readFlash(link, offset, length) {
  const out = new Uint8Array(length);
  for (let done = 0; done < length;) {
    const n = Math.min(BLOCK_LEN, length - done);
    const { data } = await link.check(
      'Flash lesen',
      ESP_READ_FLASH_SLOW,
      u32(offset + done, n),
      BLOCK_LEN,
    );
    if (data.length < n)
      throw new Error('Flash-Block zu kurz (Fehler auf der seriellen Leitung?).');
    out.set(data.subarray(0, n), done);
    done += n;
  }
  return out;
}

// Enter download mode with the reset that fits `kind` and read who is in the
// flash. `port` is an open Web Serial port (115200). The board is left in
// download mode; the caller decides what comes next.
//   kind  'esp-native' (USB-Serial/JTAG) | 'bridge' (CP210x, CH34x, FTDI)
//   opts  log(text), sleep(ms), syncTimeoutMs (tests shorten these)
// Returns { family, app: {offset, size, label}, image: parseBoardImage() }.
export async function readBoardIdentity(
  port,
  { kind, log = () => {}, sleep = defaultSleep, syncTimeoutMs = 100 } = {},
) {
  const link = new Link(port);
  try {
    let synced = false;
    for (let round = 0; round < 3 && !synced; round++) {
      log(`Download-Modus ${round + 1}/3 ...`);
      if (kind === 'esp-native') await usbJtagReset(port, sleep);
      else await classicReset(port, sleep, round === 1 ? 500 : 50);
      for (let i = 0; i < 5 && !synced; i++) {
        link.flush();
        const r = await link.command(ESP_SYNC, SYNC_PAYLOAD, syncTimeoutMs);
        if (r) synced = true;
      }
    }
    if (!synced) {
      throw new Error(
        'Der Chip meldet sich nicht im Download-Modus (SYNC ohne Antwort). ' +
          'BOOT-Taste halten und kurz RST druecken, dann noch einmal versuchen.',
      );
    }
    await sleep(50);
    link.flush(); // the ROM answers a SYNC several times

    const magic = (await link.check('Chip lesen', ESP_READ_REG, u32(CHIP_DETECT_MAGIC_REG_ADDR)))
      .val;
    const family = chipFamilyFromMagic(magic);
    log(`Chip: ${family || 'unbekannt (Kennung 0x' + (magic >>> 0).toString(16) + ')'}`);

    await link.check('SPI-Flash verbinden', ESP_SPI_ATTACH, u32(0, 0));

    const entries = parsePartitionTable(await readFlash(link, PT_OFFSET, PT_LEN));
    const app = findApp(entries);
    if (!app) {
      throw new Error('Keine Partitionstabelle mit App-Partition im Flash gefunden.');
    }
    log(`App-Partition ${app.label} bei 0x${app.offset.toString(16)}`);
    const head = await readFlash(link, app.offset, HEAD_LEN);
    return {
      family,
      app: { offset: app.offset, size: app.size, label: app.label },
      image: parseBoardImage(head),
    };
  } finally {
    await link.close();
  }
}

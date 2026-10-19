// MeshCom Web Flasher -- board identity parsers.
//
// Pure functions only: Uint8Array / string in, plain objects out; no DOM, no
// serial port, no Node APIs (runs in the browser as an ES module).
// flasher.js reads the raw bytes from a chip in ROM download mode (partition
// table at 0x8000, first 4 KB of the app partition); these decode them.
// tools/tests/test_flasher_identity.mjs pins them against real bench dumps.

const PT_MAGIC = 0x50aa; // bytes AA 50
const PT_ENTRY = 32;
const APP_DESC_MAGIC = 0xabcd5432; // esp_app_desc_t, at 0x20
const APP_DESC_OFFSET = 0x20;
const MARKER_OFFSET = 0x120; // 128-byte MCBOARD1 block in the app image
const MARKER_SIZE = 128;

const ascii = (bytes, start, len) => {
  // Fixed-width char[] fields are NUL-padded, but may fill the field with no
  // NUL at all, so stop at the first NUL or at the field end.
  let s = '';
  for (let i = start; i < start + len && bytes[i] !== 0; i++) s += String.fromCharCode(bytes[i]);
  return s;
};

// Entries of an ESP-IDF partition table. The table ends at the MD5 entry
// (EB EB), an erased entry (FF..) or any entry without the AA 50 magic.
// Garbage or an empty buffer therefore yields [] rather than throwing: the
// caller treats "no entries" as "table unreadable" and falls back.
export function parsePartitionTable(bytes) {
  const out = [];
  if (!bytes) return out;
  const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  for (let o = 0; o + PT_ENTRY <= bytes.length; o += PT_ENTRY) {
    if (dv.getUint16(o, true) !== PT_MAGIC) break;
    out.push({
      type: bytes[o + 2],
      subtype: bytes[o + 3],
      offset: dv.getUint32(o + 4, true),
      size: dv.getUint32(o + 8, true),
      label: ascii(bytes, o + 12, 16),
    });
  }
  return out;
}

// The partition holding the MeshCom app: ota_0 (type 0, subtype 0x10) if there
// is one, else the first app entry that is not the Safeboot factory image.
export function findApp(entries) {
  const apps = (entries || []).filter((e) => e.type === 0);
  return apps.find((e) => e.subtype === 0x10) || apps.find((e) => e.label !== 'safeboot') || null;
}

// Decode the first 4 KB of an app image. A missing marker is a normal answer
// (images built before the marker existed), not an error, hence a reason code.
export function parseBoardImage(bytes) {
  if (!bytes || bytes.length === 0) return { ok: false, reason: 'no-image' };
  const head = bytes.subarray(0, Math.min(bytes.length, 64));
  if (head.every((b) => b === 0xff)) return { ok: false, reason: 'erased' };
  if (bytes[0] !== 0xe9) return { ok: false, reason: 'no-image' };
  if (bytes.length < APP_DESC_OFFSET + 4) return { ok: false, reason: 'no-image' };
  const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (dv.getUint32(APP_DESC_OFFSET, true) !== APP_DESC_MAGIC) {
    return { ok: false, reason: 'no-app-desc' };
  }
  if (bytes.length < MARKER_OFFSET + MARKER_SIZE || ascii(bytes, MARKER_OFFSET, 8) !== 'MCBOARD1') {
    return { ok: false, reason: 'no-marker' };
  }
  const format = dv.getUint16(MARKER_OFFSET + 8, true);
  if (format !== 1) return { ok: false, reason: 'bad-format' };
  return {
    ok: true,
    format,
    hwid: dv.getUint16(MARKER_OFFSET + 10, true),
    env: ascii(bytes, MARKER_OFFSET + 12, 32),
    tag: ascii(bytes, MARKER_OFFSET + 44, 24),
  };
}

// Last "MCBOARD:v1:<hwidRuntime>:<hwidCompile>:<env>:<tag>" line in a serial
// dump (--id output, Safeboot). Last wins: earlier lines may be stale echoes.
// Tag may be "(local)" and contains no ':' .
export function parseIdLine(text) {
  if (typeof text !== 'string') return null;
  let m = null;
  for (const line of text.split(/\r?\n/)) {
    const r = /^\s*MCBOARD:v1:(\d+):(\d+):([^:\s]*):([^:\s]*)\s*$/.exec(line);
    if (r) m = r;
  }
  return m ? { hwidRuntime: +m[1], hwidCompile: +m[2], env: m[3], tag: m[4] } : null;
}

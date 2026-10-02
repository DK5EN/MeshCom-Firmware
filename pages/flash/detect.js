// MeshCom Web Flasher -- board detection logic.
//
// Pure functions only: no DOM, no serial port. flasher.js does the I/O and
// feeds the text it read into these; tools/tests/test_flasher_detect.mjs pins
// them against real node output.
//
// What a node tells us, and how far each fact can be trusted:
//   * --info "...NODE <id> <name>": the hardware ID the RUNNING IMAGE was built
//     for, not the board on the desk. A wrong image reports the wrong board.
//   * boot log "[LoRa]...<chip> ... Initializing ... success|failed": whether
//     that image's radio driver found its chip. This is the hardware check.
//   * ROM banner right after a reset: the chip family, even with no MeshCom
//     on the board.

// USB vendor IDs. Native USB (Espressif USB-JTAG, Adafruit nRF52 core) stays
// mute until DTR is asserted. A USB-UART bridge (CP210x, CH34x, FTDI) wires
// DTR/RTS to EN/IO0 instead, so there the same signals reset the chip.
export const USB_ESPRESSIF = 0x303a;
export const USB_ADAFRUIT = 0x239a;

export function usbKind(info) {
  const vid = info && info.usbVendorId;
  if (vid === USB_ESPRESSIF) return 'esp-native';
  if (vid === USB_ADAFRUIT) return 'nrf52';
  return 'bridge';
}

// Chip family implied by the USB connection alone. Every release board with
// Espressif native USB is an ESP32-S3; a bridge says nothing.
export function familyFromUsb(kind) {
  if (kind === 'esp-native') return 'ESP32-S3';
  if (kind === 'nrf52') return 'NRF52';
  return null;
}

// "--MeshCom 4.35t (build: ...)\n...Call: <DK5EN-93> ...ID 0A1B2C3D ...NODE 43 <HELTEC_V3>"
// -- the same format in upstream and fork (command_functions.cpp, --info).
export function parseInfo(text) {
  const node = text.match(/\.\.\.NODE (\d+) <([^>\r\n]*)>/);
  if (!node) return null;
  const ver = text.match(/--MeshCom (\S+) \(build/);
  const call = text.match(/\.\.\.Call: <([^>\r\n]*)>/);
  return {
    hwid: Number(node[1]),
    hwname: node[2].trim(),
    version: ver ? ver[1] : '',
    call: call ? call[1].trim() : '',
  };
}

const ROM_FAMILY = {
  esp32s2: 'ESP32-S2',
  esp32s3: 'ESP32-S3',
  esp32c3: 'ESP32-C3',
  esp32c6: 'ESP32-C6',
};

// The radio class each chip name in the boot log belongs to. SX1276/77/78/79
// are one part in different bands and share RadioLib's SX1278 class, which is
// also the class detect.json carries for those boards.
export function radioClass(printed) {
  const p = printed.toUpperCase();
  if (/^SX127\d$/.test(p)) return 'SX1278';
  return p;
}

export function parseBoot(text) {
  const facts = {};
  // S2/S3/C3/C6 ROMs print "ESP-ROM:esp32s3-20210327"; the classic ESP32 ROM
  // prints "ets Jun  8 2016 00:22:57".
  const rom = text.match(/ESP-ROM:(esp32[a-z0-9]*)/);
  if (rom && ROM_FAMILY[rom[1]]) facts.family = ROM_FAMILY[rom[1]];
  else if (/\bets [A-Z][a-z]{2} +\d+ \d{4}/.test(text)) facts.family = 'ESP32';

  // esp32_main.cpp: printdeb("[LoRa]...SX1262 V3 chip"), printdeb(" Initializing ... "),
  // then "success" or "failed, code <n>". Other tasks may interleave; then
  // this simply does not match and the radio stays unknown.
  const radio = text.match(
    /\[LoRa\]\.\.\.(SX\d{4}|LLCC68)\b[^\r\n]*?Initializing \.\.\. (success|failed, code (-?\d+))/i,
  );
  if (radio) {
    facts.radio = {
      printed: radio[1].toUpperCase(),
      chip: radioClass(radio[1]),
      ok: radio[2] === 'success',
      code: radio[3] !== undefined ? Number(radio[3]) : null,
    };
  }

  // esp32_pmu.cpp: "[INIT]...AXP2101 chip" / "AXP2101 SUPREME chip" / "AXP192 chip".
  const pmu = text.match(/\[INIT\]\.\.\.(AXP2101|AXP192)(?: SUPREME)? chip/);
  if (pmu) facts.pmu = pmu[1];
  return facts;
}

// Narrow the selected release's boards down to the ones this node can be.
//
//   detect      detect.json: { boards: {env: {hwid, radio, chipFamily}}, aliases: {hwid: [env]} }
//   releaseEnvs env names the selected release actually ships
//   info        parseInfo() result or null
//   facts       parseBoot() result
//   usbFamily   familyFromUsb() result or null
//
// Returns { envs, basis, warnings }. envs is never guessed: one entry means the
// evidence names exactly one image, several mean the user has to choose, none
// means no board fits or there was no evidence (basis empty).
export function candidates(detect, releaseEnvs, info, facts, usbFamily) {
  const boards = detect.boards || {};
  const aliases = detect.aliases || {};
  let pool = releaseEnvs.filter((env) => boards[env]);
  const basis = [];
  const warnings = [];

  if (info) {
    const byId = new Set(pool.filter((env) => boards[env].hwid === info.hwid));
    for (const env of aliases[String(info.hwid)] || []) {
      if (pool.includes(env)) byId.add(env);
    }
    pool = pool.filter((env) => byId.has(env));
    basis.push(`Hardware-ID ${info.hwid} <${info.hwname}>`);
    if (pool.length === 0) {
      warnings.push(
        `Fuer Hardware-ID ${info.hwid} <${info.hwname}> baut dieses Release kein Abbild.`,
      );
      return { envs: [], basis, warnings };
    }
  } else {
    const family = facts.family || usbFamily;
    if (family) {
      pool = pool.filter((env) => boards[env].chipFamily === family);
      basis.push(`Chip ${family}`);
    }
  }

  if (facts.radio) {
    const r = facts.radio;
    if (r.ok) {
      const same = pool.filter((env) => boards[env].radio === r.chip);
      if (same.length) {
        pool = same;
        basis.push(`Funkchip ${r.printed} meldet sich`);
      }
    } else {
      pool = pool.filter((env) => boards[env].radio !== r.chip);
      basis.push(`Funkchip ${r.printed} antwortet nicht`);
      warnings.push(
        `Das laufende Abbild findet seinen ${r.printed} nicht (code ${r.code}). ` +
          'Es ist fuer einen anderen Funkchip gebaut; den richtigen zeigt der Aufdruck auf dem ' +
          'Funkmodul bzw. dessen Abschirmblech.',
      );
    }
  }

  // Nothing learned at all: an unfiltered list is not a detection result.
  if (basis.length === 0) pool = [];
  return { envs: pool, basis, warnings };
}

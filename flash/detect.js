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

// USB vendor IDs. Native USB on nRF52 (Adafruit core) stays mute until DTR is
// asserted; the ESP32-S3 USB-Serial/JTAG (HWCDC) ignores DTR. A USB-UART
// bridge (CP210x, CH34x, FTDI) wires DTR/RTS to EN/IO0 instead, so there the
// same signals reset the chip.
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
// The fork adds "...AU env=<pio env> tag=<tag>" (ESP32 only), which names the
// exact image; that line comes after the NODE line, so a read that stops at
// NODE may not have it yet.
//
// Either line alone is enough: with env= but no NODE line (hwid-less answer or
// a read cut off) the result has hwid null and hwname '', and candidates()
// works from the env. With neither line this is not a MeshCom answer: null.
export function parseInfo(text) {
  const node = text.match(/\.\.\.NODE (\d+) <([^>\r\n]*)>/);
  const envm = text.match(/\.\.\.AU env=(\S+) tag=/);
  if (!node && !envm) return null;
  const ver = text.match(/--MeshCom (\S+) \(build/);
  const call = text.match(/\.\.\.Call: <([^>\r\n]*)>/);
  return {
    hwid: node ? Number(node[1]) : null,
    hwname: node ? node[2].trim() : '',
    version: ver ? ver[1] : '',
    call: call ? call[1].trim() : '',
    env: envm ? envm[1] : null,
  };
}

// Merge what the identity sources say into the --info result: an MCBOARD line
// (`--id`, Safeboot) or the marker read out of the flash, both
// { env, hwidCompile } / { env, hwid }. It fills a missing env and never
// overrides one the running image reported. Without a parseInfo() result
// (Safeboot answers only the MCBOARD line) an info object is built around the
// env, so candidates() takes the env path. Hardware ID 0 means "none".
export function applyIdentity(info, id) {
  if (!id || !id.env) return info;
  if (info) return info.env ? info : { ...info, env: id.env };
  const hwid = id.hwidCompile !== undefined ? id.hwidCompile : id.hwid;
  return { hwid: hwid ? hwid : null, hwname: '', version: '', call: '', env: id.env };
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

  // src/safeboot/main.cpp prints "[SAFEBOOT];ver;<n>" once at boot, then other
  // "[SAFEBOOT];..." lines. Safeboot has no command parser, so this is all it says.
  const sbVer = text.match(/\[SAFEBOOT\];ver;(\d+)/);
  if (sbVer) facts.safeboot = Number(sbVer[1]);
  else if (/\[SAFEBOOT\];/.test(text)) facts.safeboot = true;

  // ROM banner "rst:0x1 (POWERON),boot:0x0 (DOWNLOAD(USB/UART0))" then
  // "waiting for download"; a normal boot shows boot:0x8 (SPI_FAST_FLASH_BOOT).
  const bootLine = text.match(/\bboot:0x[0-9a-fA-F]+ \(([^\r\n]*)/);
  if (/waiting for download/.test(text) || (bootLine && /^DOWNLOAD\(/.test(bootLine[1]))) {
    facts.bootMode = 'download';
  } else if (bootLine) {
    facts.bootMode = 'flash';
  }
  return facts;
}

// Decide which hint the page shows for one listen() result. Pure.
//   info        parseInfo() result or null
//   facts       parseBoot() result
//   kind        usbKind() result
//   bytes       number of bytes received
//   afterReset  true when this listen followed "Neustart in die App"
// Codes: ok, safeboot, rom-download, silent-native (offer the reset),
// silent-after-reset (give up and say so), no-answer (non-native, no MeshCom reply).
export function diagnose({ info, facts, kind, bytes, afterReset }) {
  // Safeboot v3 answers --info too (NODE <SAFEBOOT>); its banner then decides.
  // A "[SAFEBOOT];" line next to an app answer is stale output from before a
  // reset (bench DK5EN-92: a bridge port kept the tail of a Safeboot session).
  if (facts && facts.safeboot && (!info || info.hwname === 'SAFEBOOT')) return 'safeboot';
  if (info) return 'ok';
  if (facts && facts.bootMode === 'download') return 'rom-download';
  if (kind === 'esp-native') return afterReset ? 'silent-after-reset' : 'silent-native';
  return 'no-answer';
}

// Images that run on the same board and differ in a part no answer reveals
// (detect.json sameBoard, e.g. the two E290 panel revisions): a result naming
// one of them names all of them, and the user chooses by the board's marking.
// Even the env the running image reports is no proof: the wrong panel image
// boots fine and only draws badly.
function widenSameBoard(detect, pool, envs, warnings) {
  const boards = detect.boards || {};
  const out = [];
  for (const env of envs) {
    const group = (detect.sameBoard || []).find((g) => g.includes(env)) || [env];
    for (const member of group) {
      if (pool.includes(member) && !out.includes(member)) out.push(member);
    }
  }
  // Say so only once the evidence has come down to that one board; among
  // seventeen S3 candidates the panel question is not the open one.
  for (const group of detect.sameBoard || []) {
    const hit = group.filter((env) => out.includes(env));
    if (hit.length > 1 && hit.length === out.length) {
      warnings.push(
        `Fuer dieses Board gibt es ${hit.length} Abbilder, die sich nicht erkennen lassen ` +
          `(${hit.map((env) => boards[env].variant || env).join(' / ')}). ` +
          'Bitte nach dem Aufdruck am Board waehlen.',
      );
    }
  }
  return out;
}

// Narrow the selected release's boards down to the ones this node can be.
//
//   detect      detect.json: { boards: {env: {hwid, radio, chipFamily, variant?}},
//               aliases: {hwid: [env]}, sameBoard: [[env, ...]] }
//   releaseEnvs env names the selected release actually ships
//   info        parseInfo() result or null (info.env, when set, is the exact image)
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

  // The env the running image reports is the strongest evidence there is. Trust
  // it only if it is a board this release ships and its chip family does not
  // contradict what the USB connection or ROM banner says; otherwise fall back
  // to the hardware ID chain and say why.
  if (info && info.env) {
    const family = facts.family || usbFamily;
    if (!pool.includes(info.env)) {
      warnings.push(
        `Das laufende Abbild meldet Build-Env ${info.env}; dieses Release baut es nicht.`,
      );
    } else if (family && boards[info.env].chipFamily !== family) {
      warnings.push(
        `Das laufende Abbild meldet Build-Env ${info.env} (${boards[info.env].chipFamily}), ` +
          `der Chip ist aber ${family}; die Meldung wird nicht verwendet.`,
      );
    } else if (facts.radio && !facts.radio.ok && boards[info.env].radio === facts.radio.chip) {
      // The running image is the one whose radio driver just failed: it is
      // the wrong image for this board. The radio filter below rules it out.
      warnings.push(
        `Das laufende Abbild ${info.env} findet seinen Funkchip nicht; die Meldung wird nicht verwendet.`,
      );
    } else {
      return {
        envs: widenSameBoard(detect, pool, [info.env], warnings),
        basis: [`Build-Env ${info.env}`],
        warnings,
      };
    }
  }

  if (info && info.hwid !== null) {
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
  return { envs: widenSameBoard(detect, pool, pool, warnings), basis, warnings };
}

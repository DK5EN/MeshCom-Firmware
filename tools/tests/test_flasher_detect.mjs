// Tests for pages/flash/detect.js against real node output.
//
// Run: node --test tools/tests/test_flasher_detect.mjs
//
// The fixtures are bench captures already in the tree; the board table is the
// real detect.json content, built from this tree by tools/pages_flasher.py.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

import {
  usbKind,
  familyFromUsb,
  parseInfo,
  parseBoot,
  candidates,
  diagnose,
  applyIdentity,
} from '../../pages/flash/detect.js';
import { parseIdLine } from '../../pages/flash/identity.js';

const REPO = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const read = (rel) => readFileSync(path.join(REPO, rel), 'utf8');

const HELTEC_INFO = read('test/golden/hw/G1/preflash/heltec-93-info.txt');
const RAK_INFO = read('test/golden/hw/G1/preflash/rak-90-info.txt');
// T-Beam v1.2 (AXP2101) running the SX1276 image: boot log plus --info.
const TBEAM_V12 = read('test/golden/hw/G2/t-beam-92/toggle/toggle-serial.txt');

const DETECT = JSON.parse(
  execFileSync(
    'python3',
    [
      '-c',
      'import sys, json; sys.path.insert(0, "tools"); import pages_flasher as pf; ' +
        'print(json.dumps(pf.detect_table()))',
    ],
    { cwd: REPO, encoding: 'utf8' },
  ),
);
const ALL = Object.keys(DETECT.boards);

test('usb vendor id tells native USB from a bridge', () => {
  assert.equal(usbKind({ usbVendorId: 0x303a, usbProductId: 0x1001 }), 'esp-native');
  assert.equal(usbKind({ usbVendorId: 0x239a, usbProductId: 0x8029 }), 'nrf52');
  assert.equal(usbKind({ usbVendorId: 0x10c4, usbProductId: 0xea60 }), 'bridge');
  assert.equal(usbKind({}), 'bridge');
  assert.equal(familyFromUsb('esp-native'), 'ESP32-S3');
  assert.equal(familyFromUsb('nrf52'), 'NRF52');
  assert.equal(familyFromUsb('bridge'), null);
});

test('parseInfo reads the heltec --info capture', () => {
  assert.deepEqual(parseInfo(HELTEC_INFO), {
    hwid: 43,
    hwname: 'HELTEC_V3',
    version: '4.35t',
    call: 'DK5EN-93',
    env: null,
  });
});

test('parseInfo reads the rak --info capture', () => {
  const info = parseInfo(RAK_INFO);
  assert.equal(info.hwid, 9);
  assert.equal(info.hwname, 'RAK4631');
});

test('parseInfo returns null without a NODE line', () => {
  assert.equal(parseInfo('ets Jul 29 2019 12:21:46\r\nrst:0x1 (POWERON_RESET)'), null);
});

test('parseBoot reads family, pmu and radio from the t-beam boot log', () => {
  const facts = parseBoot(TBEAM_V12);
  assert.equal(facts.family, 'ESP32');
  assert.equal(facts.pmu, 'AXP2101');
  assert.deepEqual(facts.radio, {
    printed: 'SX1276',
    chip: 'SX1278',
    ok: true,
    code: null,
  });
});

test('parseBoot reads the S3 ROM banner and a failed radio', () => {
  const facts = parseBoot(
    'ESP-ROM:esp32s3-20210327\r\nBuild:Mar 27 2021\r\n' +
      '[LoRa]...SX1262 chip Initializing ... failed, code -2\r\n',
  );
  assert.equal(facts.family, 'ESP32-S3');
  assert.deepEqual(facts.radio, {
    printed: 'SX1262',
    chip: 'SX1262',
    ok: false,
    code: -2,
  });
});

test('parseBoot names the Supreme PMU line AXP2101', () => {
  assert.equal(parseBoot('[INIT]...AXP2101 SUPREME chip\n').pmu, 'AXP2101');
});

test('heltec: one hardware id, one board', () => {
  const r = candidates(DETECT, ALL, parseInfo(HELTEC_INFO), {}, null);
  assert.deepEqual(r.envs, ['heltec_wifi_lora_32_V3']);
  assert.deepEqual(r.warnings, []);
});

test('rak: one hardware id, one board', () => {
  const r = candidates(DETECT, ALL, parseInfo(RAK_INFO), {}, 'NRF52');
  assert.deepEqual(r.envs, ['wiscore_rak4631']);
});

test('t-beam v1.2: id 12 alone leaves all three t-beam images', () => {
  const info = parseInfo(TBEAM_V12);
  assert.equal(info.hwid, 12);
  const r = candidates(DETECT, ALL, info, {}, null);
  assert.deepEqual(r.envs.sort(), ['ttgo_tbeam', 'ttgo_tbeam_SX1262', 'ttgo_tbeam_SX1268']);
});

test('t-beam v1.2: a working SX1276 narrows it to the SX1276 image', () => {
  const r = candidates(DETECT, ALL, parseInfo(TBEAM_V12), parseBoot(TBEAM_V12), null);
  assert.deepEqual(r.envs, ['ttgo_tbeam']);
});

test('the 2026-09-21 misflash: SX1262 image fails, the SX1262 image is ruled out', () => {
  const log =
    '[INIT]...AXP2101 chip\n[LoRa]...SX1262 chip Initializing ... failed, code -2\n' +
    '...Call: <DK5EN-92> ...ID E0D041B8 ...NODE 12 <TBEAM_AXP2101> ...UTC-OFF 1.000000 [NTP]\n';
  const r = candidates(DETECT, ALL, parseInfo(log), parseBoot(log), null);
  assert.deepEqual(r.envs.sort(), ['ttgo_tbeam', 'ttgo_tbeam_SX1268']);
  assert.equal(r.warnings.length, 1);
  assert.match(r.warnings[0], /SX1262/);
});

test('shared E22 id is told apart by a working radio', () => {
  const info = { hwid: 39, hwname: 'EBYTE_E22', version: '', call: '' };
  const all = candidates(DETECT, ALL, info, {}, null).envs.sort();
  assert.deepEqual(all, ['E22-DevKitC', 'E22_1262-DevKitC', 'E22_XML-DevKitC']);
  const facts = parseBoot('[LoRa]...SX1262 chip Initializing ... success\n');
  assert.deepEqual(candidates(DETECT, ALL, info, facts, null).envs, ['E22_1262-DevKitC']);
});

test('no MeshCom answer: the chip family narrows the list', () => {
  const r = candidates(DETECT, ALL, null, parseBoot('ets Jul 29 2019 12:21:46\n'), null);
  assert.ok(r.envs.length > 1);
  for (const env of r.envs) assert.equal(DETECT.boards[env].chipFamily, 'ESP32');
});

test('an id this release does not build yields no board and a warning', () => {
  const info = { hwid: 1, hwname: 'TLORA_V2', version: '', call: '' };
  const r = candidates(DETECT, ALL, info, {}, null);
  assert.deepEqual(r.envs, []);
  assert.equal(r.warnings.length, 1);
});

test('only boards the selected release ships are offered', () => {
  const r = candidates(DETECT, ['ttgo_tbeam_SX1262'], parseInfo(TBEAM_V12), {}, null);
  assert.deepEqual(r.envs, ['ttgo_tbeam_SX1262']);
});

test('no evidence at all: no candidates instead of the whole list', () => {
  const r = candidates(DETECT, ALL, null, {}, null);
  assert.deepEqual(r.envs, []);
  assert.deepEqual(r.basis, []);
});

// --info as the fork firmware prints it (command_functions.cpp): the NODE line,
// later the "...AU env=<pio env> tag=<tag>" line (ESP32 only). Synthesized.
const infoText = (hwid, hwname, envLine) =>
  `--MeshCom 4.40a (build: Oct 19 2026 / 10:00:00)\r\n...UPDATE: 0\r\n` +
  (hwid === null
    ? ''
    : `...Call: <DK5EN-92> ...ID E0D041B8 ...NODE ${hwid} <${hwname}> ...UTC-OFF 1.000000 [NTP]\r\n`) +
  `...TIME 12345 ms\r\n` +
  (envLine ? `...AU env=${envLine} tag=(local)\r\n` : '');

test('WF-01: an env whose own radio failed is not trusted; the radio filter rules it out', () => {
  // A T-Beam v1.2 reports TBEAM_AXP2101 (12, the alias of all three images).
  const env = 'ttgo_tbeam_SX1262';
  const log =
    '[LoRa]...SX1262 chip Initializing ... failed, code -2\n' + infoText(12, 'TBEAM_AXP2101', env);
  const r = candidates(DETECT, ALL, parseInfo(log), parseBoot(log), null);
  assert.deepEqual(r.envs.sort(), ['ttgo_tbeam', 'ttgo_tbeam_SX1268']);
  assert.match(r.warnings.join(' '), /SX1262/);
});

const S3_ENVS = ALL.filter((e) => DETECT.boards[e].chipFamily === 'ESP32-S3');

test('WF-01: the env the firmware reports names the image on its own', () => {
  const env = 'LilyGo_T-Beam-1W';
  const hwid = DETECT.boards[env].hwid;
  const info = parseInfo(infoText(hwid, 'TBEAM_1W', env));
  assert.equal(info.env, env);
  const r = candidates(DETECT, ALL, info, {}, 'ESP32-S3');
  assert.deepEqual(r.envs, [env]);
  assert.match(r.basis.join(' '), /Build-Env LilyGo_T-Beam-1W/);
  assert.deepEqual(r.warnings, []);
});

test('WF-01: an env line without a NODE line still yields usable info', () => {
  const info = parseInfo(infoText(null, '', 't_deck_plus'));
  assert.equal(info.hwid, null);
  assert.equal(info.env, 't_deck_plus');
  assert.deepEqual(candidates(DETECT, ALL, info, {}, 'ESP32-S3').envs, ['t_deck_plus']);
});

test('WF-01: tag "(local)" and a release tag both parse', () => {
  assert.equal(parseInfo('...AU env=t_deck_plus tag=v4.40a.10.20\n').env, 't_deck_plus');
  assert.equal(parseInfo('...NODE 43 <HELTEC_V3>\n').env, null);
});

test('WF-01: env not in this release falls back to the hardware id, with a warning', () => {
  const info = parseInfo(infoText(43, 'HELTEC_V3', 'some_upstream_env'));
  const r = candidates(DETECT, ALL, info, {}, null);
  assert.deepEqual(r.envs, ['heltec_wifi_lora_32_V3']);
  assert.equal(r.warnings.length, 1);
  assert.match(r.warnings[0], /some_upstream_env/);
});

test('WF-01: an env contradicting the USB family is not trusted', () => {
  // ttgo_tbeam is a classic ESP32; a native-USB (S3) connection cannot be it.
  const hwid = DETECT.boards['LilyGo_T-Beam-1W'].hwid;
  const info = parseInfo(infoText(hwid, 'TBEAM_1W', 'ttgo_tbeam'));
  const r = candidates(DETECT, ALL, info, {}, 'ESP32-S3');
  assert.ok(!r.envs.includes('ttgo_tbeam'));
  assert.deepEqual(r.envs, ['LilyGo_T-Beam-1W']);
  assert.equal(r.warnings.length, 1);
  assert.match(r.warnings[0], /ttgo_tbeam/);
});

test('WF-01: an env contradicting the ROM family is not trusted', () => {
  const info = parseInfo(infoText(null, '', 'ttgo_tbeam'));
  const r = candidates(DETECT, ALL, info, parseBoot('ESP-ROM:esp32s3-20210327\n'), null);
  assert.ok(!r.envs.includes('ttgo_tbeam'));
  assert.ok(r.warnings.length >= 1);
});

test('native S3 USB alone: every S3 board remains (the 17-board case)', () => {
  const r = candidates(DETECT, ALL, null, {}, 'ESP32-S3');
  assert.ok(S3_ENVS.length > 1);
  assert.equal(r.envs.length, S3_ENVS.length);
  assert.deepEqual([...r.envs].sort(), [...S3_ENVS].sort());
});

test('current behaviour: single candidate whose radio reports failed is dropped, with a warning', () => {
  // Heltec V3 is the only board for id 43; its SX1262 failing removes it.
  const info = parseInfo(HELTEC_INFO);
  const facts = parseBoot('[LoRa]...SX1262 chip Initializing ... failed, code -2\n');
  const r = candidates(DETECT, ALL, info, facts, null);
  assert.deepEqual(r.envs, []);
  assert.equal(r.warnings.length, 1);
  assert.match(r.warnings[0], /SX1262/);
});

test('current behaviour: a working LLCC68 with no matching board leaves the pool unfiltered', () => {
  const info = parseInfo(HELTEC_INFO);
  const facts = parseBoot('[LoRa]...LLCC68 chip Initializing ... success\n');
  assert.equal(facts.radio.chip, 'LLCC68');
  const r = candidates(DETECT, ALL, info, facts, null);
  assert.deepEqual(r.envs, ['heltec_wifi_lora_32_V3']);
  assert.ok(!r.basis.some((b) => /LLCC68/.test(b)));
  assert.deepEqual(r.warnings, []);
});

const SAFEBOOT_CAPTURE = [
  'OTA UDATE started',
  '[SAFEBOOT];ver;2',
  '',
  'Starting Wifi AP',
  '[SAFEBOOT];app;image;invalid;rc;8194',
  '',
].join('\n');
const ROM_FLASH =
  'ESP-ROM:esp32s3-20210327\nBuild:Mar 27 2021\nrst:0x1 (POWERON),boot:0x8 (SPI_FAST_FLASH_BOOT)\n';
const ROM_DOWNLOAD =
  'ESP-ROM:esp32s3-20210327\nBuild:Mar 27 2021\nrst:0x1 (POWERON),boot:0x0 (DOWNLOAD(USB/UART0))\nwaiting for download\n';

test('parseBoot: Safeboot ver line gives the version', () => {
  assert.equal(parseBoot(SAFEBOOT_CAPTURE).safeboot, 2);
});

test('parseBoot: any [SAFEBOOT]; line without ver gives true', () => {
  assert.equal(parseBoot('[SAFEBOOT];app;image;invalid;rc;8194\n').safeboot, true);
  assert.equal(parseBoot('hello\n').safeboot, undefined);
});

test('parseBoot: ROM download banner gives bootMode download', () => {
  assert.equal(parseBoot(ROM_DOWNLOAD).bootMode, 'download');
  assert.equal(parseBoot('waiting for download\n').bootMode, 'download');
});

test('parseBoot: normal ROM banner gives bootMode flash', () => {
  assert.equal(parseBoot(ROM_FLASH).bootMode, 'flash');
  assert.equal(parseBoot('hello\n').bootMode, undefined);
});

test('diagnose: one code per branch', () => {
  const info = { hwid: 43 };
  assert.equal(diagnose({ info, facts: {}, kind: 'esp-native', bytes: 5 }), 'ok');
  assert.equal(
    diagnose({ info: null, facts: { safeboot: 2 }, kind: 'esp-native', bytes: 50 }),
    'safeboot',
  );
  assert.equal(
    diagnose({ info: null, facts: { bootMode: 'download' }, kind: 'esp-native', bytes: 50 }),
    'rom-download',
  );
  assert.equal(diagnose({ info: null, facts: {}, kind: 'esp-native', bytes: 0 }), 'silent-native');
  assert.equal(
    diagnose({ info: null, facts: {}, kind: 'esp-native', bytes: 0, afterReset: true }),
    'silent-after-reset',
  );
  assert.equal(
    diagnose({ info: null, facts: {}, kind: 'esp-native', bytes: 30, afterReset: true }),
    'silent-after-reset',
  );
  assert.equal(diagnose({ info: null, facts: {}, kind: 'esp-bridge', bytes: 0 }), 'no-answer');
  assert.equal(diagnose({ info: null, facts: {}, kind: 'esp-native', bytes: 30 }), 'silent-native');
});

// WF-05: Safeboot (and any app without --info) answers only with the MCBOARD line.
const MCBOARD_LINE = (env, hwid = 46) => `MCBOARD:v1:${hwid}:${hwid}:${env}:v4.40a.10.21\r\n`;

test('WF-05: MCBOARD-only answer builds info with the compile-time hwid and the env', () => {
  const text = '[SAFEBOOT];ver;3\r\n' + MCBOARD_LINE('t_deck_plus');
  assert.equal(parseInfo(text), null);
  const info = applyIdentity(parseInfo(text), parseIdLine(text));
  assert.equal(info.env, 't_deck_plus');
  assert.equal(info.hwid, 46);
  assert.equal(info.hwname, '');
  const r = candidates(DETECT, ALL, info, parseBoot(text), 'ESP32-S3');
  assert.deepEqual(r.envs, ['t_deck_plus']);
  assert.match(r.basis.join(' '), /Build-Env t_deck_plus/);
});

test('WF-05: MCBOARD line fills a missing env but never overrides the --info env', () => {
  const noEnv = parseInfo(infoText(46, 'T_DECK_PLUS', null));
  assert.equal(noEnv.env, null);
  const filled = applyIdentity(noEnv, parseIdLine(MCBOARD_LINE('t_deck_plus')));
  assert.equal(filled.env, 't_deck_plus');
  assert.equal(filled.hwid, 46);
  const have = parseInfo(infoText(46, 'T_DECK_PLUS', 't_deck_plus'));
  assert.equal(applyIdentity(have, parseIdLine(MCBOARD_LINE('other_env'))).env, 't_deck_plus');
});

test('WF-05: no MCBOARD line or an empty env leaves info alone', () => {
  assert.equal(applyIdentity(null, null), null);
  assert.equal(applyIdentity(null, parseIdLine('MCBOARD:v1:0:0::(local)\n')), null);
  const info = parseInfo(infoText(46, 'T_DECK_PLUS', null));
  assert.equal(applyIdentity(info, null), info);
});

test('WF-05: an MCBOARD env whose family contradicts the ROM is not trusted', () => {
  const info = applyIdentity(null, parseIdLine(MCBOARD_LINE('t_deck_plus', 0)));
  const r = candidates(DETECT, ALL, info, { family: 'ESP32' }, null);
  assert.ok(!r.envs.includes('t_deck_plus'));
  assert.match(r.warnings.join(' '), /nicht verwendet/);
});

test('WF-05: hwid 0 in the MCBOARD line is no hardware id', () => {
  const info = applyIdentity(null, parseIdLine(MCBOARD_LINE('t_deck_plus', 0)));
  assert.equal(info.hwid, null);
  assert.deepEqual(candidates(DETECT, ALL, info, {}, 'ESP32-S3').envs, ['t_deck_plus']);
});

// Safeboot v3 answers --info (src/safeboot/safeboot_id.h): the Safeboot hint must
// still win over the plain "answered" case.
test('WF-05: Safeboot v3 answering --info is still diagnosed as Safeboot', () => {
  const text =
    '--MeshCom Safeboot 3\r\n...Call: <DK5EN-14> ...NODE 46 <SAFEBOOT>\r\n' +
    '...AU env=t_deck_plus tag=(local)\r\n[SAFEBOOT];ver;3\r\n';
  const info = parseInfo(text);
  assert.equal(info.env, 't_deck_plus');
  assert.equal(
    diagnose({ info, facts: parseBoot(text), kind: 'esp-native', bytes: text.length }),
    'safeboot',
  );
});

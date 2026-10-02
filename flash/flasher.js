// MeshCom Web Flasher -- page logic.
//
// Everything the page shows comes from releases.json, detect.json and the
// per-board manifest.json that tools/pages_flasher.py generates. Nothing about
// boards, chip families or flash offsets is hard-coded here.

import { usbKind, familyFromUsb, parseInfo, parseBoot, candidates } from './detect.js';

const $ = (id) => document.getElementById(id);

const boardSel = $('board');
const versionSel = $('version');
const espSlot = $('esp-slot');
const nrfSlot = $('nrf-slot');
const uf2Link = $('uf2-link');
const nrfBtn = $('nrf-usb');
const nrfStep2 = $('nrf-step2');
const nrfProgress = $('nrf-progress');
const nrfBar = $('nrf-bar');
const nrfPct = $('nrf-pct');
const nrfStatus = $('nrf-status');
const partsLine = $('parts');
const loadError = $('load-error');
const manual = $('manual');
const partsList = $('parts-list');
const cmdBox = $('cmd');
const copyBtn = $('copy');
const detectBtn = $('detect');
const detectStatus = $('detect-status');
const detectResult = $('detect-result');

// esptool's --chip argument for the families the generator emits.
const ESPTOOL_CHIP = {
  'ESP32': 'esp32',
  'ESP32-S2': 'esp32s2',
  'ESP32-S3': 'esp32s3',
  'ESP32-C3': 'esp32c3',
  'ESP32-C6': 'esp32c6',
};

const hex = (n) => '0x' + n.toString(16).toUpperCase().padStart(4, '0');

const hasSerial = 'serial' in navigator;
$('no-serial').hidden = hasSerial;

let releases = [];
let detectData = null;
// Boards the last detection left over; null = no filter on the dropdown.
let onlyEnvs = null;
// The port a detection opened and still holds, so the flash dialog can take
// it over without a second port picker. null when nothing is held.
let heldPort = null;

// URL of the DFU package (firmware.zip) of the selected nRF52 board, or null
// when its manifest carries none (older releases: UF2 only).
let nrfZipUrl = null;
let nrfBusy = false;
// The nRF52 port Board erkennen used: closed, still granted, handed to the
// 1200 bps touch as is. null when nothing was detected.
let nrfPort = null;

function fail(message) {
  loadError.textContent = message;
  loadError.hidden = false;
  boardSel.innerHTML = '<option>--</option>';
  versionSel.innerHTML = '<option>--</option>';
}

function currentRelease() {
  return releases.find((r) => r.version === versionSel.value);
}

function fillVersions() {
  versionSel.innerHTML = '';
  for (const r of releases) {
    const o = document.createElement('option');
    o.value = r.version;
    o.textContent = `${r.version} (${r.date})`;
    versionSel.append(o);
  }
}

// Boards are grouped by vendor, in the order the generator emitted them, so
// the page needs no vendor list of its own.
function fillBoards(keepEnv) {
  const rel = currentRelease();
  boardSel.innerHTML = '';
  if (!rel) return;
  const shown = onlyEnvs ? rel.boards.filter((b) => onlyEnvs.has(b.env)) : rel.boards;
  if (onlyEnvs && shown.length !== 1) {
    // Several candidates: make the user pick, never preselect a guess.
    const o = document.createElement('option');
    o.value = '';
    o.textContent = shown.length ? `\u2014 ${shown.length} passende Boards, bitte waehlen \u2014` : '\u2014';
    boardSel.append(o);
  }
  const groups = new Map();
  for (const b of shown) {
    if (!groups.has(b.group)) groups.set(b.group, []);
    groups.get(b.group).push(b);
  }
  for (const [group, boards] of groups) {
    const og = document.createElement('optgroup');
    og.label = group;
    for (const b of boards) {
      const o = document.createElement('option');
      o.value = b.env;
      o.textContent = b.radio ? `${b.name} \u2014 ${b.radio}` : b.name;
      og.append(o);
    }
    boardSel.append(og);
  }
  if (keepEnv && shown.some((b) => b.env === keepEnv)) boardSel.value = keepEnv;
}

function fmtSize(bytes) {
  return bytes >= 1048576
    ? `${(bytes / 1048576).toFixed(2)} MB`
    : `${Math.round(bytes / 1024)} kB`;
}

// The same files the browser path writes, as download links plus the esptool
// call that writes them. Both read the manifest, so they cannot drift apart.
function buildManual(manifest, dir) {
  const build = manifest.builds[0];
  const chip = ESPTOOL_CHIP[build.chipFamily];
  if (!chip) {
    manual.hidden = true;
    return;
  }

  partsList.replaceChildren();
  for (const part of build.parts) {
    const li = document.createElement('li');
    const off = document.createElement('span');
    off.className = 'off';
    off.textContent = hex(part.offset);
    const a = document.createElement('a');
    a.href = `${dir}/${part.path}`;
    a.textContent = part.path;
    a.setAttribute('download', part.path);
    const sz = document.createElement('span');
    sz.className = 'sz';
    li.append(off, a, sz);
    partsList.append(li);
    // The size is a nicety; a failed HEAD must not cost us the download link.
    fetch(`${dir}/${part.path}`, { method: 'HEAD' })
      .then((r) => {
        const len = r.headers.get('content-length');
        if (r.ok && len) sz.textContent = fmtSize(Number(len));
      })
      .catch(() => {});
  }

  const args = build.parts.map((p) => `${hex(p.offset)} ${p.path}`).join(' \\\n    ');
  cmdBox.textContent = `esptool --chip ${chip} --port PORT -b 921600 write_flash \\\n    ${args}`;
  manual.hidden = false;
}

copyBtn.addEventListener('click', async () => {
  try {
    await navigator.clipboard.writeText(cmdBox.textContent);
    copyBtn.textContent = 'kopiert';
  } catch {
    // Clipboard access is refused in some contexts. Select the text instead,
    // so the user can copy it by hand rather than see nothing happen.
    const range = document.createRange();
    range.selectNodeContents(cmdBox);
    const sel = window.getSelection();
    sel.removeAllRanges();
    sel.addRange(range);
    copyBtn.textContent = 'markiert \u2014 jetzt kopieren';
  }
  setTimeout(() => {
    copyBtn.textContent = 'Befehl kopieren';
  }, 2500);
});

function describeParts(manifest) {
  const build = manifest.builds[0];
  if (build.chipFamily === 'NRF52') {
    return 'Ein DFU-Paket (per USB aus dem Browser) oder ein UF2-Abbild (ueber den Bootloader des Boards).';
  }
  const list = build.parts.map((p) => `${p.path} → ${hex(p.offset)}`).join(', ');
  return `${build.chipFamily}: ${list}.`;
}

const nrfSay = (text) => {
  nrfStatus.textContent = text;
};

function nrfSetProgress(percent) {
  nrfBar.value = percent;
  nrfPct.textContent = `${percent} %`;
}

function resetNrfUi() {
  // A flash in progress keeps its own progress and status text.
  if (nrfBusy) return;
  nrfProgress.hidden = true;
  nrfSetProgress(0);
  nrfSay('');
  $('nrf-no-serial').hidden = hasSerial;
  nrfPending = null;
  nrfStep2.hidden = true;
  nrfBtn.hidden = !nrfZipUrl;
  nrfBtn.disabled = !hasSerial || nrfBusy;
  // Fetch the package and the DFU library now, so the click needs neither.
  if (nrfZipUrl && hasSerial) prepareNrf(nrfZipUrl).catch(() => {});
  if (!nrfZipUrl) {
    nrfSay('Dieses Release enthaelt kein DFU-Paket; bitte die UF2-Datei verwenden.');
  } else if (nrfPort) {
    nrfSay('Port des erkannten Boards ist bereit: ein Klick auf "Per USB flashen" genuegt.');
  }
}

async function closeQuietly(port) {
  try {
    await port.close();
  } catch {
    // Not open, or unplugged mid-flash: nothing left to release.
  }
}

// Package and DFU library, fetched ahead of the click. requestPort() needs a
// fresh user gesture, so nothing slow may run before it in a click handler.
// The promise is cached per URL and dropped again when it fails.
let nrfPrep = null;
function prepareNrf(url) {
  if (nrfPrep && nrfPrep.url === url) return nrfPrep.promise;
  const promise = (async () => {
    const res = await fetch(url, { cache: 'no-cache' });
    if (!res.ok) throw new Error(`Paket nicht ladbar (HTTP ${res.status})`);
    const [zip, lib] = await Promise.all([res.arrayBuffer(), import('./nrfutil/nrfutil-web.js')]);
    return { zip, enterDfuMode: lib.enterDfuMode, performDfu: lib.performDfu };
  })();
  nrfPrep = { url, promise };
  promise.catch(() => {
    if (nrfPrep && nrfPrep.promise === promise) nrfPrep = null;
  });
  return promise;
}

// What step 1 leaves for step 2: the package and the library, bound to the
// board that was put into the bootloader.
let nrfPending = null;

function nrfFail(err) {
  nrfProgress.hidden = true;
  nrfSay(
    err && err.name === 'NotFoundError'
      ? 'Kein Port gewaehlt. Zum Wiederholen noch einmal auf den Knopf klicken.'
      : `Fehler: ${err && err.message ? err.message : err}. ` +
          'Port ggf. in einem anderen Programm oder Tab offen? Sonst Board aus- und einstecken ' +
          'und erneut versuchen, oder die UF2-Datei verwenden.',
  );
}

const usbId = (n) => (n === undefined ? '?' : n.toString(16).padStart(4, '0'));

// The 1200 bps touch reboots the board into the bootloader, which shows up as
// a different USB device. Look for it among the ports the page may already
// use, without a dialog: poll getPorts() and listen for 'connect'. A port
// counts when it is closed and carries another product id than the app port.
async function findBootloaderPort(appPort, appInfo, timeoutMs) {
  const connected = [];
  const onConnect = (e) => connected.push(e.target);
  navigator.serial.addEventListener('connect', onConnect);
  const isNew = (p) => p !== appPort && !p.readable && p.getInfo().usbProductId !== appInfo.usbProductId;
  const end = Date.now() + timeoutMs;
  let logged = null;
  try {
    for (;;) {
      const ports = await navigator.serial.getPorts();
      const desc = ports
        .map((p) => {
          const i = p.getInfo();
          return `${usbId(i.usbVendorId)}:${usbId(i.usbProductId)}${p === appPort ? ' (app)' : ''}${p.readable ? ' (open)' : ''}`;
        })
        .join(', ');
      if (desc !== logged) {
        console.info(`[nrf] getPorts(): ${desc || '(leer)'}; app port ${usbId(appInfo.usbVendorId)}:${usbId(appInfo.usbProductId)}`);
        logged = desc;
      }
      const found = ports.find(isNew) || connected.find(isNew);
      if (found) return found;
      if (Date.now() >= end) return null;
      await sleep(250);
    }
  } finally {
    navigator.serial.removeEventListener('connect', onConnect);
  }
}

// Sends the package through dfuPort. performDfu opens and closes the port
// itself; the extra close only covers a port left open by an error.
async function runNrfDfu(prep, dfuPort, opts = {}) {
  nrfProgress.hidden = false;
  nrfSetProgress(0);
  try {
    await prep.performDfu(dfuPort, prep.zip, {
      ackTimeout: 5000,
      maxRetries: 3,
      singleBank: true,
      ...opts,
      onProgress: ({ percent, message }) => {
        nrfSetProgress(percent);
        // The library says "Erasing flash..." for the bootloader clearing the app pages it is
        // about to write; that reads like a settings wipe, which it is not.
        nrfSay(message.replace(/^Erasing flash/, 'Preparing flash'));
      },
    });
  } finally {
    await closeQuietly(dfuPort);
  }
  nrfSetProgress(100);
  nrfPending = null;
  nrfStep2.hidden = true;
  nrfSay('Fertig. Das Board startet mit der neuen Firmware neu.');
}

// Happy path: one click. The port Board erkennen already holds a grant for
// (nrfPort) goes straight into the 1200 bps touch, and the bootloader port is
// found by polling, so no chooser dialog appears. Only without a detected port
// is requestPort() needed; it is then the first call of this click handler,
// because it demands a user gesture. If the bootloader port cannot be found,
// step 2 offers the chooser as a fallback (again as the first call of its click).
async function flashNrfStep1() {
  if (nrfBusy || !nrfZipUrl) return;
  const url = nrfZipUrl;
  nrfBusy = true;
  nrfBtn.disabled = true;
  nrfStep2.hidden = true;
  nrfPending = null;
  nrfProgress.hidden = true;
  nrfSetProgress(0);
  let appPort = nrfPort;
  const picking = appPort ? null : navigator.serial.requestPort();
  try {
    if (picking) {
      nrfSay('Bitte den Port des Boards waehlen.');
      appPort = await picking;
    }
    // Detection left the port closed, but make sure: open() on an open port fails.
    await closeQuietly(appPort);
    nrfSay('Lade Firmware-Paket ...');
    const prep = await prepareNrf(url);
    const appInfo = appPort.getInfo();
    nrfSay('Board wird in den Bootloader geschaltet ...');
    await prep.enterDfuMode(appPort, 1500);
    nrfPending = prep;

    nrfSay('Board meldet sich neu, suche den Bootloader-Port ...');
    const dfuPort = await findBootloaderPort(appPort, appInfo, 6000);
    if (!dfuPort) {
      nrfStep2.hidden = false;
      nrfSay(
        'Der Bootloader-Port wurde nicht automatisch gefunden. Bitte auf ' +
          '"Schritt 2: Bootloader-Port waehlen" klicken und dort den neuen Port waehlen.',
      );
      return;
    }
    nrfSay('Bootloader gefunden, uebertrage Firmware ...');
    await runNrfDfu(prep, dfuPort);
  } catch (err) {
    // After the touch the board sits in the bootloader: keep step 2 as a retry.
    if (nrfPending) nrfStep2.hidden = false;
    // A stale port object (board replugged) would fail again: ask via the chooser next time.
    else if (appPort === nrfPort) nrfPort = null;
    nrfFail(err);
  } finally {
    nrfBusy = false;
    nrfBtn.disabled = !hasSerial;
  }
}

async function flashNrfStep2() {
  if (nrfBusy || !nrfPending) return;
  const prep = nrfPending;
  nrfBusy = true;
  nrfBtn.disabled = true;
  nrfStep2.disabled = true;
  try {
    const picking = navigator.serial.requestPort();
    nrfSay('Das Board meldet sich neu, bitte den neuen Port waehlen.');
    const dfuPort = await picking;
    await runNrfDfu(prep, dfuPort);
  } catch (err) {
    // Step 2 stays available: the board is still in the bootloader.
    nrfFail(err);
  } finally {
    nrfBusy = false;
    nrfBtn.disabled = !hasSerial;
    nrfStep2.disabled = false;
  }
}

nrfBtn.addEventListener('click', flashNrfStep1);
nrfStep2.addEventListener('click', flashNrfStep2);

async function select() {
  const rel = currentRelease();
  const env = boardSel.value;
  espSlot.replaceChildren();
  nrfSlot.hidden = true;
  manual.hidden = true;
  partsLine.textContent = '—';
  if (!rel || !env) return;
  const dir = `${rel.version}/${env}`;

  let manifest;
  try {
    const res = await fetch(`${dir}/manifest.json`, { cache: 'no-cache' });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    manifest = await res.json();
  } catch (err) {
    fail(`Manifest fuer ${env} nicht lesbar: ${err.message}`);
    return;
  }

  partsLine.textContent = describeParts(manifest);
  buildManual(manifest, dir);

  if (manifest.builds[0].chipFamily === 'NRF52') {
    // Parts are picked by extension, never by index: the manifest lists the
    // UF2 image and the DFU package side by side.
    const parts = manifest.builds[0].parts;
    const uf2 = parts.find((p) => p.path.endsWith('.uf2'));
    const zip = parts.find((p) => p.path.endsWith('.zip'));
    if (uf2) {
      uf2Link.href = `${dir}/${uf2.path}`;
      uf2Link.setAttribute('download', `${env}-${rel.version}.uf2`);
    }
    uf2Link.hidden = !uf2;
    nrfZipUrl = zip ? `${dir}/${zip.path}` : null;
    resetNrfUi();
    nrfSlot.hidden = false;
    return;
  }

  if (heldPort) {
    // Detection already holds the port: hand it straight to the dialog.
    const go = document.createElement('button');
    go.className = 'dl';
    go.type = 'button';
    go.textContent = 'Flashen (erkannter Port)';
    go.addEventListener('click', () => openInstallDialog(`${dir}/manifest.json`));
    espSlot.append(go);
    return;
  }

  // The element caches its manifest, so it is rebuilt rather than retargeted.
  const btn = document.createElement('esp-web-install-button');
  btn.manifest = `${dir}/manifest.json`;
  const activate = document.createElement('button');
  activate.slot = 'activate';
  activate.className = 'dl';
  activate.textContent = 'Verbinden und flashen';
  btn.append(activate);
  espSlot.append(btn);
}

// --------------------------------------------------------------------------
// board detection
// --------------------------------------------------------------------------

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// How long a node gets to answer --info. A node rebooted by the port open has
// to finish setup() first; with a WiFi join that takes several seconds.
const INFO_TIMEOUT_MS = 20000;
const INFO_RETRY_MS = 2000;

async function releaseHeldPort() {
  const port = heldPort;
  heldPort = null;
  if (port) {
    try {
      await port.close();
    } catch {
      // Already closed or unplugged; either way it is no longer ours.
    }
  }
}

// ESP Web Tools loads its dialog lazily from a chunk whose name carries a
// build hash. Read that name out of install-button.js rather than pinning it,
// so a vendor update cannot silently break the handoff.
async function loadInstallDialog() {
  if (customElements.get('ewt-install-dialog')) return true;
  try {
    const src = await (await fetch('esp-web-tools/install-button.js')).text();
    const m = src.match(/import\("\.\/(install-dialog-[\w-]+\.js)"\)/);
    if (!m) return false;
    await import(`./esp-web-tools/${m[1]}`);
  } catch {
    return false;
  }
  return Boolean(customElements.get('ewt-install-dialog'));
}

async function openInstallDialog(manifestPath) {
  const port = heldPort;
  if (!port || !(await loadInstallDialog())) {
    await releaseHeldPort();
    detectStatus.textContent =
      'Der erkannte Port liess sich nicht uebergeben. Bitte "Verbinden und flashen" nutzen.';
    await select();
    return;
  }
  // Same wiring as install-button.js: the dialog gets an open port and the
  // page closes it once the dialog is gone.
  heldPort = null;
  const el = document.createElement('ewt-install-dialog');
  el.port = port;
  el.manifestPath = manifestPath;
  el.addEventListener(
    'closed',
    () => {
      port.close().catch(() => {});
    },
    { once: true },
  );
  document.body.appendChild(el);
  await select();
}

// Open the port, reset where that is safe, send --info until the node answers
// or the time is up. Returns everything the node printed.
async function listen(port, kind, onTick) {
  const reader = port.readable.getReader();
  const decoder = new TextDecoder();
  let text = '';
  let lost = null;
  const pump = (async () => {
    try {
      for (;;) {
        const { value, done } = await reader.read();
        if (done) break;
        text += decoder.decode(value, { stream: true });
        if (text.length > 262144) text = text.slice(-131072);
      }
    } catch (err) {
      lost = err;
    }
  })();

  const writer = port.writable.getWriter();
  const encoder = new TextEncoder();
  const start = Date.now();
  let info = null;
  try {
    if (kind === 'bridge') {
      // DTR low keeps IO0 high, an RTS pulse drops EN: a plain reboot into the
      // app, so the boot log with the radio check comes out while we listen.
      await port.setSignals({ dataTerminalReady: false, requestToSend: true });
      await sleep(120);
      await port.setSignals({ dataTerminalReady: false, requestToSend: false });
    } else {
      // Native USB: DTR high is what makes the node talk. No RTS pulse here: on
      // the Espressif USB-JTAG it resets the chip and can take the USB link, and
      // with it this port, down mid-read.
      await port.setSignals({ dataTerminalReady: true, requestToSend: false });
    }

    while (!lost && Date.now() - start < INFO_TIMEOUT_MS) {
      // The command parser splits on LF.
      await writer.write(encoder.encode('--info\n'));
      const until = Date.now() + INFO_RETRY_MS;
      while (!lost && Date.now() < until && !(info = parseInfo(text))) await sleep(100);
      if (info) break;
      onTick(Math.round((Date.now() - start) / 1000));
    }
    // The radio line comes out during setup(), long before --info can answer;
    // give a rebooted node's log a moment to drain anyway.
    if (info) await sleep(300);
  } catch (err) {
    lost = lost || err;
  } finally {
    writer.releaseLock();
    try {
      await reader.cancel();
    } catch {
      // Reader already errored out; nothing left to cancel.
    }
    await pump;
    reader.releaseLock();
  }
  return { text, info, lost };
}

function line(label, value) {
  const div = document.createElement('div');
  const b = document.createElement('strong');
  b.textContent = `${label}: `;
  div.append(b, value);
  return div;
}

function showResult(result, info, facts, usbFamily) {
  const rel = currentRelease();
  const names = new Map(rel.boards.map((b) => [b.env, b]));
  const label = (env) => {
    const b = names.get(env);
    return b.radio ? `${b.group} ${b.name} \u2014 ${b.radio}` : `${b.group} ${b.name}`;
  };

  detectResult.replaceChildren();
  if (info) {
    detectResult.append(
      line('Knoten', `${info.call || '?'}, MeshCom ${info.version || '?'}, Hardware-ID ${info.hwid} <${info.hwname}>`),
    );
  } else {
    detectResult.append(line('Knoten', 'keine MeshCom-Antwort auf --info'));
  }
  const chip = facts.family || usbFamily;
  if (chip) detectResult.append(line('Chip', chip));
  if (facts.pmu) detectResult.append(line('Power-Chip', facts.pmu));
  if (facts.radio) {
    detectResult.append(
      line('Funkchip', facts.radio.ok ? `${facts.radio.printed} gefunden` : `${facts.radio.printed} nicht gefunden (code ${facts.radio.code})`),
    );
  } else if (info) {
    detectResult.append(line('Funkchip', 'nicht im Boot-Log gesehen (Board startete nicht neu)'));
  }

  for (const w of result.warnings) {
    const p = document.createElement('p');
    p.className = 'warn';
    p.textContent = w;
    detectResult.append(p);
  }

  const verdict = document.createElement('p');
  verdict.className = 'verdict';
  if (result.envs.length === 1) {
    verdict.textContent = `Ausgewaehlt: ${label(result.envs[0])}. Bitte pruefen, dann flashen.`;
  } else if (result.envs.length > 1) {
    verdict.textContent =
      `${result.envs.length} Boards passen (${result.basis.join(', ')}). ` +
      'Die Liste ist darauf eingeschraenkt; bitte das richtige waehlen.';
  } else if (result.basis.length === 0) {
    verdict.textContent = 'Nichts erkannt. Bitte von Hand waehlen.';
  } else {
    verdict.textContent = 'Kein Board dieses Releases passt. Bitte von Hand waehlen.';
  }
  detectResult.append(verdict);

  if (onlyEnvs) {
    const clear = document.createElement('button');
    clear.type = 'button';
    clear.className = 'copy';
    clear.textContent = 'Alle Boards zeigen';
    clear.addEventListener('click', () => {
      onlyEnvs = null;
      fillBoards(boardSel.value);
      clear.remove();
      select();
    });
    detectResult.append(clear);
  }
  detectResult.hidden = false;
}

async function detect() {
  await releaseHeldPort();
  nrfPort = null;
  let port;
  try {
    port = await navigator.serial.requestPort();
  } catch (err) {
    detectStatus.textContent =
      err.name === 'NotFoundError' ? 'Kein Port gewaehlt.' : `Fehler: ${err.message}`;
    return;
  }
  try {
    // Same options install-button.js uses, so the dialog can take it as is.
    await port.open({ baudRate: 115200, bufferSize: 8192 });
  } catch (err) {
    detectStatus.textContent = `Port laesst sich nicht oeffnen: ${err.message}. Ist er anderswo offen?`;
    return;
  }

  detectBtn.disabled = true;
  detectResult.hidden = true;
  const kind = usbKind(port.getInfo());
  detectStatus.textContent =
    kind === 'bridge' ? 'Board startet neu, warte auf Antwort ...' : 'Warte auf Antwort ...';
  let heard;
  try {
    heard = await listen(port, kind, (s) => {
      detectStatus.textContent = `Warte auf Antwort ... ${s} s`;
    });
  } catch (err) {
    heard = { text: '', info: null, lost: err };
  } finally {
    detectBtn.disabled = false;
  }

  const info = heard.info;
  const facts = parseBoot(heard.text);
  const usbFamily = familyFromUsb(kind);
  const rel = currentRelease();
  const result = candidates(detectData, rel.boards.map((b) => b.env), info, facts, usbFamily);

  if (heard.lost) {
    try {
      await port.close();
    } catch {
      // Unplugged ports cannot be closed; nothing to clean up.
    }
    detectStatus.textContent = info
      ? 'Erkannt, aber die Verbindung ist danach abgerissen.'
      : 'Die Verbindung ist abgerissen (Board neu gestartet oder abgezogen). Bitte noch einmal erkennen.';
  } else if (usbFamily === 'NRF52') {
    detectStatus.textContent = 'nRF52 erkannt. Flashen: Knopf "Per USB flashen (nRF52)" unten (alternativ UF2-Datei).';
  } else if (info || facts.family) {
    detectStatus.textContent = '';
  } else {
    detectStatus.textContent =
      'Keine Antwort. Ohne MeshCom auf dem Board bleibt nur die Auswahl von Hand.';
  }

  // Keep the port for the flash dialog only if it is alive and an ESP will be
  // flashed through it; nRF52 boards get the USB DFU button (or a UF2 file).
  const nrf = usbFamily === 'NRF52' || result.envs.every((env) => detectData.boards[env].chipFamily === 'NRF52');
  if (!heard.lost) {
    if (nrf || result.envs.length === 0) {
      await port.close().catch(() => {});
      // Keep the (closed) SerialPort object: it is still granted, so the nRF52
      // flash needs no second chooser, and re-requesting it would only fail
      // with "already open" or pick the same device again.
      if (usbFamily === 'NRF52' || (nrf && result.envs.length > 0)) nrfPort = port;
    } else {
      heldPort = port;
    }
  }

  onlyEnvs = result.envs.length > 0 ? new Set(result.envs) : null;
  fillBoards(result.envs.length === 1 ? result.envs[0] : null);
  showResult(result, info, facts, usbFamily);
  await select();
}

async function initDetect() {
  if (!hasSerial) return;
  try {
    const res = await fetch('detect.json', { cache: 'no-cache' });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    detectData = await res.json();
  } catch {
    // Without the table there is nothing to match against; the manual choice
    // still works, so the button just stays hidden.
    return;
  }
  $('detect-row').hidden = false;
  detectBtn.addEventListener('click', detect);
  navigator.serial.addEventListener('disconnect', (e) => {
    if (e.target === heldPort) {
      heldPort = null;
      select();
    }
    // The 1200 bps touch unplugs the app port on purpose; only forget it otherwise.
    if (e.target === nrfPort && !nrfBusy) {
      nrfPort = null;
      select();
    }
  });
}

async function init() {
  try {
    const res = await fetch('releases.json', { cache: 'no-cache' });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    releases = (await res.json()).releases || [];
  } catch (err) {
    fail(`releases.json nicht lesbar: ${err.message}`);
    return;
  }
  if (releases.length === 0) {
    fail('Es ist noch kein Release veroeffentlicht.');
    return;
  }
  fillVersions();
  fillBoards();
  versionSel.addEventListener('change', () => {
    fillBoards(boardSel.value);
    select();
  });
  boardSel.addEventListener('change', select);
  await initDetect();
  await select();
}

init();

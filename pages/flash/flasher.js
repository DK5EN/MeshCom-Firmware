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
    return 'Ein UF2-Abbild ueber den Bootloader des Boards.';
  }
  const list = build.parts.map((p) => `${p.path} → ${hex(p.offset)}`).join(', ');
  return `${build.chipFamily}: ${list}.`;
}

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
    uf2Link.href = `${dir}/${manifest.builds[0].parts[0].path}`;
    uf2Link.setAttribute('download', `${env}-${rel.version}.uf2`);
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
  } else if (info || facts.family || usbFamily === 'NRF52') {
    detectStatus.textContent = '';
  } else {
    detectStatus.textContent =
      'Keine Antwort. Ohne MeshCom auf dem Board bleibt nur die Auswahl von Hand.';
  }

  // Keep the port for the flash dialog only if it is alive and an ESP will be
  // flashed through it; nRF52 boards take a UF2 file instead.
  const nrf = usbFamily === 'NRF52' || result.envs.every((env) => detectData.boards[env].chipFamily === 'NRF52');
  if (!heard.lost) {
    if (nrf || result.envs.length === 0) {
      await port.close().catch(() => {});
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

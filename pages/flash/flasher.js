// MeshCom Web Flasher -- page logic.
//
// Everything the page shows comes from releases.json and the per-board
// manifest.json that tools/pages_flasher.py generates. Nothing about boards,
// chip families or flash offsets is hard-coded here.

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
  const groups = new Map();
  for (const b of rel.boards) {
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
  if (keepEnv && rel.boards.some((b) => b.env === keepEnv)) boardSel.value = keepEnv;
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
  if (!rel || !env) return;
  const dir = `${rel.version}/${env}`;

  espSlot.replaceChildren();
  nrfSlot.hidden = true;
  manual.hidden = true;
  partsLine.textContent = '—';

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
  await select();
}

init();

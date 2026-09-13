// Safeboot OTA page test (extends the network panel / live state view work).
//
// Loads src/safeboot/ota.html straight from disk (no running node needed),
// stubs fetch and XMLHttpRequest, and drives window.__safeboot.renderInfo()/
// renderState() directly against sample JSON that follows
// docs/safeboot-ota-contract.md, plus one pass through the real upload flow
// and the rescan button.
//
// Prerequisites (not part of the repo):
//   npm i jsdom@24           (anywhere; run from that directory or set NODE_PATH)
// Run:
//   node tools/safeboot_page_test.js
const fs = require('fs');
const path = require('path');
const { JSDOM, VirtualConsole } = require('jsdom');

const HTML_PATH = path.join(__dirname, '..', 'src', 'safeboot', 'ota.html');
const HTML = fs.readFileSync(HTML_PATH, 'utf8');

let failures = 0;
function check(name, cond, detail) {
  console.log((cond ? 'PASS ' : 'FAIL ') + name + (detail !== undefined ? '  [' + detail + ']' : ''));
  if (!cond) failures++;
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// A fetch stub that never resolves keeps the page's own background polling
// (kicked off at the bottom of its <script> on load) from racing the
// assertions below with default/idle data. Tests that need to observe a
// particular request (start, scan) pass a real handler for those paths and
// fall through to "hang forever" for everything else.
function hangingFetch() {
  return new Promise(() => {});
}

async function loadPage(fetchImpl) {
  const vc = new VirtualConsole();
  vc.on('jsdomError', (e) => console.log('jsdomError:', e.message));
  const dom = new JSDOM(HTML, {
    url: 'http://dk5en-93.local/update',
    runScripts: 'dangerously',
    resources: 'usable',
    virtualConsole: vc,
    beforeParse(window) {
      window.fetch = fetchImpl || hangingFetch;
    },
  });
  await sleep(20);
  return dom;
}

function q(win, id) {
  return win.document.getElementById(id);
}

// ---- sample JSON, straight from the contract -------------------------------

const INFO_STA_CONNECTED = {
  call: 'DK5EN-93',
  hostname: 'DK5EN-93',
  mdns: 'DK5EN-93.local',
  mode: 'sta',
  uptime_ms: 12345,
  sta: {
    configured: true,
    ssid: 'ORBI63',
    connected: true,
    bssid: '5A:AF:97:2E:2B:8B',
    ip: '192.168.1.98',
    rssi: -43,
    channel: 6,
    auth: 'WPA2_WPA3_PSK',
    last_disconnect_reason: 0,
    join_attempts: 1,
  },
  ap: { active: false, ssid: 'DK5EN-93', ip: '192.168.4.1' },
  scan: {
    age_ms: 4200,
    in_progress: false,
    aps: [
      { ssid: 'OTHERAP', bssid: '11:22:33:44:55:66', rssi: -30, channel: 1, auth: 'OPEN', connected: false },
      { ssid: 'ORBI63', bssid: '5A:AF:97:2E:2B:8B', rssi: -43, channel: 6, auth: 'WPA2_WPA3_PSK', connected: true },
    ],
  },
};

const INFO_AP_STA_DISCONNECTED = {
  call: 'DK5EN-93',
  hostname: 'DK5EN-93',
  mdns: 'DK5EN-93.local',
  mode: 'ap_sta',
  uptime_ms: 30000,
  sta: {
    configured: true,
    ssid: 'ORBI63',
    connected: false,
    bssid: '',
    ip: '',
    rssi: 0,
    channel: 0,
    auth: '',
    last_disconnect_reason: 8,
    join_attempts: 3,
  },
  ap: { active: true, ssid: 'DK5EN-93', ip: '192.168.4.1' },
  scan: { age_ms: 1000, in_progress: false, aps: [] },
};

const INFO_NOT_CONFIGURED = {
  call: 'DK5EN-93',
  hostname: 'DK5EN-93',
  mdns: 'DK5EN-93.local',
  mode: 'ap',
  uptime_ms: 5000,
  sta: {
    configured: false,
    ssid: '',
    connected: false,
    bssid: '',
    ip: '',
    rssi: 0,
    channel: 0,
    auth: '',
    last_disconnect_reason: 0,
    join_attempts: 0,
  },
  ap: { active: true, ssid: 'DK5EN-93', ip: '192.168.4.1' },
  scan: { age_ms: 0, in_progress: false, aps: [] },
};

const STATE_RECEIVING = {
  state: 'receiving',
  reason: '',
  generation: 3,
  received: 1048576,
  total: 2182465,
  image_valid: false,
  fallback_in_ms: -1,
  uptime_ms: 90000,
};

const STATE_ABORTED = {
  state: 'aborted',
  reason: 'incomplete_upload',
  generation: 3,
  received: 1048576,
  total: 2182465,
  image_valid: false,
  fallback_in_ms: 151000,
  uptime_ms: 98000,
};

const STATE_DONE = {
  state: 'done',
  reason: '',
  generation: 3,
  received: 2182465,
  total: 2182465,
  image_valid: true,
  fallback_in_ms: -1,
  uptime_ms: 99000,
};

(async () => {
  // ---------- info render ----------
  {
    const dom = await loadPage();
    const win = dom.window;
    check('page exposes window.__safeboot', typeof win.__safeboot === 'object' && win.__safeboot !== null);

    win.__safeboot.renderInfo(INFO_STA_CONNECTED);
    check('STA connected: mode label is STA', q(win, 'netMode').textContent === 'STA', q(win, 'netMode').textContent);
    check('STA connected: SSID shown', q(win, 'staSsid').textContent === 'ORBI63');
    check('STA connected: BSSID shown', q(win, 'staBssid').textContent === '5A:AF:97:2E:2B:8B');
    check('STA connected: IP shown', q(win, 'staIp').textContent === '192.168.1.98');
    check('STA connected: RSSI shown in dBm', q(win, 'staRssi').textContent === '-43 dBm', q(win, 'staRssi').textContent);
    check('STA connected: channel shown', q(win, 'staChannel').textContent === '6');
    check('STA connected: auth shown', q(win, 'staAuth').textContent === 'WPA2_WPA3_PSK');
    check('STA connected: AP row hidden', q(win, 'apRow').hidden === true);
    const rows = win.document.querySelectorAll('#scanBody tr');
    check('scan table has 2 rows', rows.length === 2, rows.length);
    check('configured SSID row comes first even though weaker RSSI',
      rows[0].children[0].textContent === 'ORBI63' && rows[1].children[0].textContent === 'OTHERAP',
      rows[0].children[0].textContent + ',' + rows[1].children[0].textContent);
    check('connected row is highlighted', rows[0].className === 'connected', rows[0].className);
    check('non-connected row is not highlighted', rows[1].className === '');
    check('rescan enabled when configured and not scanning', q(win, 'rescanButton').disabled === false);

    win.__safeboot.renderInfo(INFO_AP_STA_DISCONNECTED);
    check('ap_sta: mode label is AP+STA', q(win, 'netMode').textContent === 'AP+STA');
    check('ap_sta disconnected: AP row visible', q(win, 'apRow').hidden === false);
    check('ap_sta disconnected: AP IP is 192.168.4.1', q(win, 'apIp').textContent === '192.168.4.1');
    check('ap_sta disconnected: disconnect reason row visible', q(win, 'staDisconnectRow').hidden === false);
    check('ap_sta disconnected: reason number shown', q(win, 'staDisconnectReason').textContent === '8');
    check('ap_sta disconnected: join attempts shown', q(win, 'staJoinAttempts').textContent === '3');

    win.__safeboot.renderInfo(INFO_NOT_CONFIGURED);
    check('unconfigured: mode label is AP', q(win, 'netMode').textContent === 'AP');
    check('unconfigured: rescan disabled', q(win, 'rescanButton').disabled === true);

    dom.window.close();
  }

  // ---------- state render ----------
  {
    const dom = await loadPage();
    const win = dom.window;

    win.__safeboot.renderState(STATE_RECEIVING);
    check('receiving: percent is 48', q(win, 'statePercent').textContent === '48%', q(win, 'statePercent').textContent);
    check('receiving: fallback line hidden', q(win, 'fallbackLine').hidden === true);

    win.__safeboot.renderState(STATE_ABORTED);
    const reasonText = q(win, 'stateReason').textContent;
    check('aborted: reason sentence present', reasonText.length > 20, reasonText);
    check('aborted: raw reason kept in parentheses', reasonText.includes('(incomplete_upload)'), reasonText);
    check('aborted: fallback line shown with seconds', q(win, 'fallbackLine').textContent === 'Fallback to app in 151 s', q(win, 'fallbackLine').textContent);

    win.__safeboot.renderState(STATE_DONE);
    check('done: state text mentions rebooting', q(win, 'stateText').textContent.includes('Rebooting'), q(win, 'stateText').textContent);

    dom.window.close();
  }

  // ---------- upload flow: xhr error, then a state poll supplies the reason ----------
  {
    let startCalls = 0;
    const fetchImpl = async (url) => {
      const u = String(url);
      if (u.includes('/ota/start')) {
        startCalls++;
        return { status: 200, text: async () => '' };
      }
      return hangingFetch();
    };

    function FakeXHR() {
      this.upload = { addEventListener() {} };
      this.readyState = 0;
      this.status = 0;
      this.responseText = '';
    }
    FakeXHR.prototype.open = function () {};
    FakeXHR.prototype.setRequestHeader = function () {};
    FakeXHR.prototype.send = function () {
      Promise.resolve().then(() => {
        if (this.onerror) this.onerror();
      });
    };

    const dom = await loadPage(fetchImpl);
    const win = dom.window;
    win.XMLHttpRequest = FakeXHR;
    win.calculateMD5 = async () => 'deadbeefdeadbeefdeadbeefdeadbeef';
    win.__safeboot._test.setSelectedFile(new win.File(['x'], 'firmware.bin'));

    await win.uploadFirmware();
    await sleep(20);

    check('upload started (/ota/start called)', startCalls === 1);
    const statusAfterError = q(win, 'status').textContent;
    check('xhr.onerror sets the generic error text', statusAfterError === 'An error occurred during the upload.', statusAfterError);
    check('xhr.onerror flags finishedWithErr', win.__safeboot._test.finishedWithErr === true);
    check('upload no longer in progress after the error', win.__safeboot._test.uploadInProgress === false);

    win.__safeboot.renderState({
      state: 'aborted',
      reason: 'client_disconnected',
      generation: 4,
      received: 500,
      total: 2000,
      image_valid: false,
      fallback_in_ms: 90000,
      uptime_ms: 1000,
    });
    const statusAfterState = q(win, 'status').textContent;
    check('a later /ota/state record replaces the generic error text',
      statusAfterState !== 'An error occurred during the upload.' && statusAfterState.includes('client_disconnected'),
      statusAfterState);

    dom.window.close();
  }

  // ---------- rescan button ----------
  {
    const scanCalls = [];
    const fetchImpl = async (url) => {
      const u = String(url);
      if (u.includes('/ota/scan')) {
        scanCalls.push(u);
        return { status: 200, text: async () => '' };
      }
      return hangingFetch();
    };
    const dom = await loadPage(fetchImpl);
    const win = dom.window;

    win.__safeboot.renderInfo(INFO_STA_CONNECTED);
    check('rescan enabled: configured, not scanning, no upload', q(win, 'rescanButton').disabled === false);
    q(win, 'rescanButton').click();
    await sleep(10);
    check('clicking rescan issued GET /ota/scan', scanCalls.length === 1 && scanCalls[0].includes('/ota/scan'), JSON.stringify(scanCalls));

    win.__safeboot.renderInfo(Object.assign({}, INFO_STA_CONNECTED, {
      scan: Object.assign({}, INFO_STA_CONNECTED.scan, { in_progress: true }),
    }));
    check('rescan disabled while scan.in_progress', q(win, 'rescanButton').disabled === true);

    win.__safeboot.renderInfo(INFO_NOT_CONFIGURED);
    check('rescan disabled when sta.configured is false', q(win, 'rescanButton').disabled === true);

    // back to a state where it would otherwise be enabled, then start an
    // upload (its /ota/start fetch hangs, so uploadInProgress stays true for
    // the duration of this check) and confirm rescan gets disabled by that
    // alone.
    win.__safeboot.renderInfo(INFO_STA_CONNECTED);
    check('rescan re-enabled once info is healthy again', q(win, 'rescanButton').disabled === false);
    win.__safeboot._test.setSelectedFile(new win.File(['x'], 'firmware.bin'));
    win.calculateMD5 = () => new Promise(() => {}); // never resolves: stay "mid-upload"
    win.uploadFirmware(); // fire and forget
    await sleep(10);
    check('rescan disabled while an upload is in progress', q(win, 'rescanButton').disabled === true);

    dom.window.close();
  }

  console.log(failures === 0 ? 'ALL PASS' : failures + ' FAILURES');
  process.exit(failures === 0 ? 0 : 1);
})().catch((e) => {
  console.log('HARNESS ERROR', e);
  process.exit(2);
});

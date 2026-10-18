// AU web GUI: offline jsdom test of the Info page "Auto update" row (auScaffoldJs() in src/web_functions/web_functions.cpp).
//
// No firmware and no node needed. The scaffold JS is extracted from the C++ source by concatenating the string
// literals of the web_client.println calls of auScaffoldJs(), then run in jsdom against a fake fetch that speaks the
// server contract (GET /austatus, GET /ota/state, GET /callfunction/?upd*). The fake /austatus bodies are the
// AU_GOLDEN_<STATE>_JSON strings of test/test_au_ui_state/test_main.cpp (read the cppStrings() way), so a C++ key
// change breaks this test. Timers and Date are virtual, so polling, the F tick and the 180 s timeout run without waiting.
//
// Prerequisites (not part of the repo):  npm i jsdom@24   (anywhere; set NODE_PATH to its node_modules)
// Run:   NODE_PATH=<dir>/node_modules node tools/webgui_au_test.js
const fs = require('fs');
const path = require('path');
const { JSDOM, VirtualConsole } = require('jsdom');

const ROOT = path.join(__dirname, '..');
const SRC = process.env.AU_PAGE_SRC || path.join(ROOT, 'src', 'web_functions', 'web_functions.cpp');
const GOLDEN = process.env.AU_GOLDEN_SRC || path.join(ROOT, 'test', 'test_au_ui_state', 'test_main.cpp');

let failures = 0;
function check(name, cond, detail) {
  console.log((cond ? 'PASS ' : 'FAIL ') + name + (detail !== undefined && !cond ? '  [' + detail + ']' : ''));
  if (!cond) failures++;
}

// ---- C++ literal extraction (println only; the JS has no printf) -----------------------------------------
function decodeLiteral(s) {
  let o = '';
  for (let i = 0; i < s.length; i++) {
    if (s[i] !== '\\') { o += s[i]; continue; }
    const c = s[++i];
    if (c === 'n') o += '\n';
    else if (c === 't') o += '\t';
    else if (c === '\\' || c === '"' || c === "'") o += c;
    else throw new Error('unsupported escape \\' + c);
  }
  return o;
}
function extractJs(src, fn) {
  const at = src.indexOf('void ' + fn + '()');
  if (at < 0) throw new Error('function not found: ' + fn);
  const i = src.indexOf('{', at);
  const body = src.slice(i, src.indexOf('\n}\n', i));
  let out = '';
  const re = /web_client\.println\(\s*"((?:[^"\\]|\\.)*)"\s*\)/g;
  let m;
  while ((m = re.exec(body))) out += decodeLiteral(m[1]) + '\n';
  return out;
}

const cpp = fs.readFileSync(SRC, 'utf8');
const JS = extractJs(cpp, 'auScaffoldJs');
check('the page JS was extracted from auScaffoldJs()', JS.length > 2000 && /function auPageInit/.test(JS), String(JS.length));

// ---- golden bodies from the Unity test -------------------------------------------------------------------
function goldens(file) {
  const o = {};
  if (!fs.existsSync(file)) return null;
  const t = fs.readFileSync(file, 'utf8');
  for (const m of t.matchAll(/static const char \*const AU_GOLDEN_(\w+?)_(JSON|STATE) = ("(?:[^"\\]|\\.)*");/g)) {
    o[m[1]] = o[m[1]] || {};
    o[m[1]][m[2]] = JSON.parse(m[3]);   // C and JSON string escapes agree for these one-line literals
  }
  return o;
}
let G = goldens(GOLDEN);
const haveGolden = !!G && ['S', 'F', 'B', 'D', 'D2', 'E', 'G', 'G2', 'H', 'C', 'A', 'I'].every((k) => G[k] && G[k].JSON);
check('golden /austatus strings found in test_au_ui_state (cppStrings style)', haveGolden, GOLDEN);
if (!haveGolden) { console.log('NOTE temporary fixtures in use'); G = null; }
function mk(o) {
  return JSON.stringify(Object.assign({ st: 'idle', job: 'none', run: 'v4.40a.10.17', avail: '', newer: 0, inst: 0, staged: '', err: '', pct: 0, phone: 0, guard: 0, sbok: 1, mode: 'notify', chan: 'dev', etaS: 35, ifree: 57000, iblk: 31000 }, o));
}
const FIX = {
  S: [mk({ sbok: 0 }), 'S'], F: [mk({ avail: 'v4.40a.10.18', newer: 1, inst: 1, staged: 'v4.40a.10.18' }), 'F'],
  B: [mk({ st: 'busy', job: 'check' }), 'B'], D: [mk({ st: 'busy', job: 'download', avail: 'v4.40a.10.18', newer: 1, inst: 1, pct: 42 }), 'D'],
  D2: [mk({ st: 'busy', job: 'download', avail: 'v4.40a.10.18', newer: 1, inst: 1, pct: 100 }), 'd'],
  E: [mk({ st: 'ok', job: 'download', avail: 'v4.40a.10.18', newer: 1, inst: 1, staged: 'v4.40a.10.18' }), 'E'],
  G: [mk({ st: 'fail', job: 'check', err: 'lowheap_phone', phone: 1 }), 'G'], G2: [mk({ st: 'fail', job: 'check', err: 'lowheap_phone' }), 'g'],
  H: [mk({ st: 'fail', job: 'check', err: 'lowheap_ble_off' }), 'H'],
  C: [mk({ st: 'ok', job: 'check', avail: 'v4.40a.10.18', newer: 1, inst: 1 }), 'C'],
  A: [mk({ st: 'ok', job: 'check', avail: 'v4.40a.10.17' }), 'A'],
  I: [mk({ st: 'fail', job: 'download', avail: 'v4.40a.10.18', newer: 1, inst: 1, err: 'digest' }), 'C'],
};
const J = (k) => (G ? G[k].JSON : FIX[k][0]);
const ST = (k) => (G ? G[k].STATE : FIX[k][1]);
const NEWTAG = 'v4.40a.10.18';

// ---- fake clock, fake node, page factory -----------------------------------------------------------------
const flush = async () => { for (let i = 0; i < 8; i++) await new Promise((r) => setImmediate(r)); };
function mkClock() {
  const T = { now: 0, id: 1, list: new Map() };
  T.setTimeout = (fn, ms) => { const id = T.id++; T.list.set(id, { fn, at: T.now + (ms | 0), iv: 0 }); return id; };
  T.setInterval = (fn, ms) => { const id = T.id++; T.list.set(id, { fn, at: T.now + (ms | 0), iv: ms | 0 }); return id; };
  T.clear = (id) => { T.list.delete(id); };
  T.advance = async (ms) => {
    const end = T.now + ms;
    for (;;) {
      let best = null, bid = 0;
      for (const [id, t] of T.list) if (t.at <= end && (!best || t.at < best.at)) { best = t; bid = id; }
      if (!best) break;
      T.now = best.at;
      if (best.iv) best.at += best.iv; else T.list.delete(bid);
      best.fn();
      await flush();
    }
    T.now = end;
    await flush();
  };
  return T;
}

const SCAFFOLD = '<html><script>var cpage="info";</script><div id="content_layer">Info</div></html>';
function mkNode(status) {
  const n = {
    status, ota: null, down: false, notJson: false, st404: false, st500: false, apply: 'ok', check: 'ok', calls: [], statusCalls: 0,
    handle(url) {
      const u = String(url);
      n.calls.push(u);
      if (u === '/austatus') {
        n.statusCalls++;
        if (n.down) return { reject: true };
        if (n.st404) return { status: 404, text: true };
        if (n.st500) return { status: 500, body: '{"ok":false}' };
        if (n.scaffold) return { html: SCAFFOLD };
        if (n.notJson) return { text: true, html: '<html>Safeboot</html>' };
        return { body: n.status };
      }
      if (u === '/ota/state') return n.otaScaffold ? { html: SCAFFOLD } : n.ota ? { body: JSON.stringify(n.ota) } : { reject: true };
      if (u.indexOf('/callfunction/?updapply') === 0) return { body: JSON.stringify({ updapply: n.apply }), status: n.apply === 'ok' ? 200 : 422 };
      if (u.indexOf('/callfunction/?updcheck') === 0) return { body: JSON.stringify({ updcheck: n.check }) };
      if (u.indexOf('/callfunction/?updinstall') === 0) return { body: JSON.stringify({ updinstall: 'ok' }) };
      return { status: 404, text: true };
    },
    fnCalls() { return n.calls.filter((c) => c.indexOf('/callfunction/') === 0); },
  };
  return n;
}

const ROW = '<table><tr id="au_tr"><td>Auto update</td><td id="au_row" data-blepause="BLE">mode notify, channel dev<br>env x, running v4.40a.10.17' +
  '<div id="au_msg"></div><div id="au_err"></div><div id="au_hint"></div><button type="button" id="au_btn" disabled>Check now</button></td></tr></table>';

async function mkPage(node, opts) {
  opts = opts || {};
  const T = mkClock();
  const st = { confirm: 0, reload: 0 };
  const vc = new VirtualConsole();
  vc.on('jsdomError', (e) => { console.log('jsdomError: ' + e.message); failures++; });
  const dom = new JSDOM('<!doctype html><html><body><div id="content_layer">' + ROW.replace('BLE', opts.ble ? '1' : '0') + '</div></body></html>', {
    url: 'http://dk5en-1.local/', runScripts: 'outside-only', virtualConsole: vc,
    beforeParse(w) {
      const RealDate = w.Date;
      const BASE = 1700000000000;
      w.Date = class extends RealDate {
        constructor(...a) { if (a.length) super(...a); else super(BASE + T.now); }
        static now() { return BASE + T.now; }
      };
      w.setTimeout = T.setTimeout; w.setInterval = T.setInterval; w.clearTimeout = T.clear; w.clearInterval = T.clear;
      w.fetch = (url) => {
        const r = node.handle(String(url));
        if (r.reject) return Promise.reject(new TypeError('Failed to fetch'));
        const status = r.status || 200;
        return Promise.resolve({ status, ok: status < 400, text: () => Promise.resolve(r.html !== undefined ? r.html : (r.body || '')), json: () => (r.body !== undefined ? Promise.resolve(JSON.parse(r.body)) : Promise.reject(new SyntaxError('Unexpected token <'))) });
      };
      w.confirm = () => { st.confirm++; throw new Error('confirm() must not be used'); };
      w.alert = () => { throw new Error('alert() must not be used'); };
    },
  });
  const w = dom.window;
  w.eval("var cpage='info';");
  w.eval(JS);
  w.auReload = () => { st.reload++; };   // location.reload() is not implemented in jsdom
  const d = w.document;
  const P = {
    w, d, T, node, st,
    el: (id) => d.getElementById(id),
    async init() { w.auPageInit(); await flush(); },
    async click() { P.el('au_btn').dispatchEvent(new w.MouseEvent('click', { bubbles: true })); await flush(); },
    view() {
      const b = P.el('au_btn');
      return { green: P.el('au_tr').className === 'au-green', msg: P.el('au_msg').textContent, err: P.el('au_err').textContent, hint: P.el('au_hint').textContent, btn: b.textContent, dis: b.disabled, red: b.className === 'au-red', act: b.getAttribute('data-act') };
    },
  };
  return P;
}

(async () => {
  // ---- source pins ---------------------------------------------------------------------------------------
  check('JS has no confirm() and no innerHTML (tag text goes through textContent)', !/confirm\(/.test(JS) && !/innerHTML/.test(JS));
  check('auScaffoldJs and the row sit inside #if defined(ESP32)', /#if defined\(ESP32\)\n\/\*\*\n \* Scaffold JS of the Info page "Auto update" row[\s\S]*?\n#endif/.test(cpp) && /data-blepause/.test(cpp));
  check('the row ids the JS needs exist in the C++ row', ['au_tr', 'au_row', 'au_msg', 'au_err', 'au_hint', 'au_btn'].every((i) => cpp.indexOf('id=\\"' + i + '\\"') >= 0));
  check('banner and .au-banner CSS are gone', !/au-banner|au_banner/.test(cpp));
  check('loadPage hooks auPageLeave/auPageInit through RAK-empty macros', /#define AU_LP_LEAVE ""/.test(cpp) && /" AU_LP_LEAVE "/.test(cpp) && /AU_LP_INIT/.test(cpp));
  check('GET /austatus route sets web_cors_ok = false and is ESP32 only', /#if defined\(ESP32\)\n\s+else if \(web_header\.startsWith\("GET \/austatus"\)\)\n\s+\{[^\n]*\n\s+web_cors_ok = false;/.test(cpp));
  check('call_function refuses upd* without X-MC through webGuardCallAllowed', /webGuardCallAllowed\(functionData\.functionName\.c_str\(\)/.test(cpp));
  check('setparam refuses manualcommand update commands without X-MC through webGuardSetparamAllowed', /webGuardSetparamAllowed\(param_name\.c_str\(\), param_value\.c_str\(\), full_header\.c_str\(\)/.test(cpp));

  // ---- every golden body maps to the C++ state letter and renders -----------------------------------------
  const exp = {
    S: { msg: /Use Webflasher to renew Auto Update aware Safeboot/, btn: 'Check now', dis: false },
    B: { btn: 'Checking now ...', dis: true },
    D: { green: true, msg: /Firmware v4.40a.10.18 available/, btn: 'Downloading now ... 42 %', dis: true },
    D2: { green: true, btn: 'Verifying download ...', dis: true },
    E: { green: true, msg: /downloaded and verified/, btn: 'Install now', red: true, act: 'apply' },
    G: { msg: /connected phone uses the memory/, btn: 'Pause Bluetooth and check', act: 'ble' },
    G2: { msg: /phone has disconnected/, btn: 'Check now' },
    H: { msg: /Not enough free memory for the update. Reboot the node and try again./, btn: 'Check now' },
    C: { green: true, msg: /Firmware v4.40a.10.18 available/, btn: 'Install', act: 'install' },
    A: { msg: /available none|up to date/, btn: 'Check now', act: 'check' },
    I: { green: true, err: /Download damaged, it was discarded -- try again \[digest\]/, btn: 'Install' },
  };
  for (const k of Object.keys(exp)) {
    const P = await mkPage(mkNode(J(k)));
    await P.init();
    const j = JSON.parse(J(k));
    const v = P.view();
    const e = exp[k];
    check('state ' + k + ': JS selector agrees with auUiState (' + ST(k) + ')', P.w.auState(j, false) === ST(k), P.w.auState(j, false));
    const ok = (e.green === undefined || v.green === e.green) && (e.msg === undefined || e.msg.test(v.msg)) && (e.btn === undefined || v.btn === e.btn) &&
      (e.dis === undefined || v.dis === e.dis) && (e.red === undefined || v.red === e.red) && (e.act === undefined || v.act === e.act) && (e.err === undefined || e.err.test(v.err));
    check('state ' + k + ' renders row, message, button', ok, JSON.stringify(v));
    P.w.auPageLeave();
  }
  {
    const j = JSON.parse(J('F'));
    const P = await mkPage(mkNode(J('F')));
    check('state F: selector returns F only with the remembered click', P.w.auState(j, true) === 'F' && P.w.auState(j, false) === 'E');
    check('state S wins over F, busy, E', P.w.auState({ sbok: 0, st: 'busy', job: 'check', staged: 'x' }, true) === 'S');
  }

  // ---- hint line, tag as text, 404 -------------------------------------------------------------------------
  {
    const P = await mkPage(mkNode(J('A').replace('"phone":0', '"phone":1')), { ble: true });
    await P.init();
    check('D13 hint line only on a board with the BLE pause, with a phone, no block', /A connected phone may block the update/.test(P.view().hint) && !P.view().dis);
    P.w.auPageLeave();
    const Q = await mkPage(mkNode(J('A').replace('"phone":0', '"phone":1')), { ble: false });
    await Q.init();
    check('no hint on a board without the BLE pause', Q.view().hint === '');
    Q.w.auPageLeave();
    const X = await mkPage(mkNode(J('C').split(NEWTAG).join('<img src=x onerror=alert(1)>')));
    await X.init();
    check('a hostile tag is inserted as text', X.d.querySelectorAll('#au_row img').length === 0 && /<img src=x/.test(X.view().msg));
    X.w.auPageLeave();
    const N = await mkPage(Object.assign(mkNode(J('A')), { st404: true }));
    await N.init();
    check('state N: /austatus 404 hides the row', N.el('au_tr').style.display === 'none');
    N.w.auPageLeave();
  }

  // ---- transitions A -> B -> C -> D -> D2 -> E -> F -> A ------------------------------------------------------
  {
    const node = mkNode(J('A'));
    const P = await mkPage(node);
    await P.init();
    const calls0 = node.statusCalls;
    await P.T.advance(5000);
    check('A: idle, no polling', node.statusCalls === calls0 && P.T.list.size === 0, node.statusCalls + '/' + P.T.list.size);
    node.status = J('B');
    await P.click();
    check('A -> B: Check now calls updcheck and the button is disabled until the answer', node.fnCalls()[0] === '/callfunction/?updcheck=' && P.view().btn === 'Checking now ...' && P.view().dis, JSON.stringify(P.view()));
    const n1 = node.statusCalls;
    await P.T.advance(3000);
    check('B: polling every 1 s', node.statusCalls - n1 === 3, String(node.statusCalls - n1));
    node.status = J('C');
    await P.T.advance(1000);
    check('B -> C: Install, green', P.view().btn === 'Install' && P.view().green && !P.view().dis);
    const n2 = node.statusCalls;
    await P.T.advance(5000);
    check('C: polling stops at idle', node.statusCalls === n2);
    node.status = J('D');
    await P.click();
    check('C -> D: updinstall, Downloading now ... 42 %', node.fnCalls()[1] === '/callfunction/?updinstall=' && P.view().btn === 'Downloading now ... 42 %' && P.view().dis);
    node.status = J('D2');
    await P.T.advance(1000);
    check('D -> D2: Verifying download ...', P.view().btn === 'Verifying download ...' && P.view().dis);
    node.status = J('E');
    await P.T.advance(1000);
    check('D2 -> E: Install now, red, verified message', P.view().btn === 'Install now' && P.view().red && /downloaded and verified/.test(P.view().msg));
    await P.T.advance(4000);
    const done = node.statusCalls;
    await P.T.advance(4000);
    check('E: no polling at idle', node.statusCalls === done);

    // E -> F: Install now
    await P.click();
    const fcalls = node.fnCalls().filter((c) => c.indexOf('updapply') >= 0).length;
    check('E -> F: one updapply, no confirm(), F rendered and remembered in sessionStorage', fcalls === 1 && P.st.confirm === 0 && /Installing v4.40a.10.18, the node restarts/.test(P.view().msg) && P.view().btn === 'Installing now ... 0 %' && P.view().dis && P.view().green,
      JSON.stringify(P.view()));
    const stored = JSON.parse(P.w.sessionStorage.getItem('auF:dk5en-1.local'));
    check('sessionStorage holds {tag,t0,etaS}', stored && stored.tag === NEWTAG && stored.etaS === 35 && typeof stored.t0 === 'number', JSON.stringify(stored));
    await P.T.advance(10000);
    const p10 = P.view().btn;
    check('F: percentage ticks each second', /Installing now \.\.\. (2[0-9]|3[0-9]) %/.test(p10), p10);
    const before = node.statusCalls;
    node.down = true;
    await P.T.advance(12000);   // t = 22 s
    check('F: the old firmware is not probed before etaS - 10 s (25 s)', node.statusCalls === before, String(node.statusCalls - before));
    await P.T.advance(8000);    // t = 30 s: probes at 26, 28, 30 s, node still down
    check('F: probes every 2 s from etaS - 10 s (26, 28, 30 s)', node.statusCalls - before === 3, String(node.statusCalls - before));
    check('F: node down -> still F, never above 95 %', /Installing now \.\.\. (\d+) %/.test(P.view().btn) && +/(\d+) %/.exec(P.view().btn)[1] <= 95 && P.view().dis, P.view().btn);
    node.down = false;
    node.status = J('A').replace('"avail":"v4.40a.10.17"', '"avail":"' + NEWTAG + '"').replace('"run":"v4.40a.10.17"', '"run":"' + NEWTAG + '"');
    await P.T.advance(2000);
    check('F -> A: the new tag answers, 100 %, storage cleared, page reloads', P.st.reload === 1 && P.w.sessionStorage.getItem('auF:dk5en-1.local') === null && P.view().btn === 'Installing now ... 100 %', JSON.stringify([P.st.reload, P.view().btn]));
    await P.init();   // the reload
    check('after the reload the row is A on the new tag', P.view().btn === 'Check now' && !P.view().green && P.w.auS.run === NEWTAG);
    P.w.auPageLeave();
  }

  // ---- G -> G2 without reload --------------------------------------------------------------------------------
  {
    const node = mkNode(J('G'));
    const P = await mkPage(node);
    await P.init();
    check('G shown', P.view().btn === 'Pause Bluetooth and check');
    const n = node.statusCalls;
    await P.T.advance(9000);
    check('G: no poll before 10 s', node.statusCalls === n);
    node.status = J('G2');
    await P.T.advance(1000);
    check('G -> G2: after 10 s, no reload, phone disconnected / Check now', node.statusCalls === n + 1 && /phone has disconnected/.test(P.view().msg) && P.view().btn === 'Check now' && P.st.reload === 0);
    const n2 = node.statusCalls;
    await P.T.advance(30000);
    check('G2: polling stops', node.statusCalls === n2);
    P.w.auPageLeave();
    // Pause Bluetooth: the hook answers FAIL until W4
    const node2 = mkNode(J('G'));
    node2.check = 'failed';
    const Q = await mkPage(node2);
    await Q.init();
    await Q.click();
    check('G: "Pause Bluetooth and check" sends updcheck=ble; a FAIL answer shows a hint and re-enables the button', node2.fnCalls()[0] === '/callfunction/?updcheck=ble' && !Q.view().dis, JSON.stringify(Q.view()));
    Q.w.auPageLeave();
    // BLE guard (section 7): the guard line, one request, 1 s polling, return to the base state after the release
    const node3 = mkNode(J('G'));
    const R = await mkPage(node3);
    await R.init();
    node3.status = J('G').replace('"guard":0', '"guard":1').replace('"st":"idle"', '"st":"busy"').replace('"job":"none"', '"job":"check"').replace(/"err":"[^"]*"/, '"err":""');
    const b = R.el('au_btn');
    b.dispatchEvent(new R.w.MouseEvent('click', { bubbles: true }));
    b.dispatchEvent(new R.w.MouseEvent('click', { bubbles: true }));
    await flush();
    check('G: a double click sends exactly one updcheck=ble', node3.fnCalls().filter((c) => c === '/callfunction/?updcheck=ble').length === 1, node3.fnCalls().join());
    check('guard on: the row says "Bluetooth paused for the update", button disabled, no hint', /Bluetooth paused for the update/.test(R.view().msg) && R.view().dis && R.view().hint === '' && R.view().act === 'none', JSON.stringify(R.view()));
    const n3 = node3.statusCalls;
    await R.T.advance(3000);
    check('guard on: polling every 1 s', node3.statusCalls - n3 === 3, String(node3.statusCalls - n3));
    node3.status = J('G').replace('"guard":0', '"guard":1');
    await R.T.advance(1500);
    check('guard on between check and download (idle): the line stays', /Bluetooth paused for the update/.test(R.view().msg) && R.view().dis, JSON.stringify(R.view()));
    node3.status = J('A').replace('"phone":0', '"phone":1');
    await R.T.advance(1500);
    check('guard released, phone back: the row is in its base state again', R.view().msg !== '' && !/Bluetooth paused/.test(R.view().msg) && R.view().btn === 'Check now' && !R.view().dis, JSON.stringify(R.view()));
    R.w.auPageLeave();
  }

  // ---- double click, FAIL answers -----------------------------------------------------------------------------
  {
    const node = mkNode(J('E'));
    const P = await mkPage(node);
    await P.init();
    const b = P.el('au_btn');
    b.dispatchEvent(new P.w.MouseEvent('click', { bubbles: true }));
    b.dispatchEvent(new P.w.MouseEvent('click', { bubbles: true }));
    await flush();
    check('double click: exactly one updapply', node.fnCalls().filter((c) => c.indexOf('updapply') >= 0).length === 1, node.fnCalls().join());
    P.w.auPageLeave();
    const node2 = mkNode(J('E'));
    node2.apply = 'failed';
    const Q = await mkPage(node2);
    await Q.init();
    await Q.click();
    check('updapply FAIL: I overlay on E, button usable again, no F, nothing stored', /Install of v4.40a.10.18 failed/.test(Q.view().err) && Q.view().btn === 'Install now' && !Q.view().dis && Q.w.sessionStorage.getItem('auF:dk5en-1.local') === null, JSON.stringify(Q.view()));
    Q.w.auPageLeave();
    const node3 = mkNode(J('A'));
    node3.check = 'failed';
    const R = await mkPage(node3);
    await R.init();
    await R.click();
    check('updcheck FAIL (cooldown): hint shown, button usable again', /try again in a minute/.test(R.view().err) && !R.view().dis && R.view().btn === 'Check now', JSON.stringify(R.view()));
    R.w.auPageLeave();
  }

  // ---- sessionStorage restore, /ota/state branch, old firmware, timeout -----------------------------------------
  {
    const node = mkNode(J('E'));
    const P = await mkPage(node);
    P.w.sessionStorage.setItem('auF:dk5en-1.local', JSON.stringify({ tag: NEWTAG, t0: P.w.Date.now() - 20000, etaS: 35 }));
    await P.init();
    check('reload during F: sessionStorage restores F in the same tab (one validating /austatus, no callfunction)', /Installing v4.40a.10.18/.test(P.view().msg) && P.view().dis && node.fnCalls().length === 0 && node.statusCalls === 1, JSON.stringify(P.view()));
    await P.T.advance(1000);
    check('restored F: elapsed time continues from t0 (about 21 s -> 60 %)', /Installing now \.\.\. 60 %/.test(P.view().btn), P.view().btn);
    // Safeboot: /austatus is not JSON (redirect to /update), /ota/state answers
    node.notJson = true;
    node.ota = { state: 'applying', reason: 'none', received: 500000, total: 1000000, fallback_in_ms: 0 };
    await P.T.advance(6000);   // t = 27 s: past 25 s
    check('/ota/state branch: applying shows the real progress from received/total', node.calls.indexOf('/ota/state') >= 0 && /Installing now \.\.\. (6[0-9]|7[0-9]|[89][0-9]) %/.test(P.view().btn), P.view().btn);
    node.ota = { state: 'aborted', reason: 'apply_failed', received: 0, total: 0, fallback_in_ms: 12000 };
    await P.T.advance(4000);
    check('/ota/state apply_failed: "returns to the old firmware in N s"', /Install failed -- the node returns to the old firmware in 12 s/.test(P.view().msg), P.view().msg);
    P.w.auPageLeave();

    // old firmware after the update (no /austatus route): both paths answer the Info scaffold as 2xx HTML
    const n4 = mkNode(J('E'));
    const O = await mkPage(n4);
    O.w.sessionStorage.setItem('auF:dk5en-1.local', JSON.stringify({ tag: NEWTAG, t0: O.w.Date.now() - 20000, etaS: 35 }));
    await O.init();
    n4.scaffold = true; n4.otaScaffold = true;
    await O.T.advance(8000);
    check('HTML scaffold from /austatus and /ota/state (firmware without the route): node is back, storage cleared, page reloads', O.st.reload === 1 && O.w.sessionStorage.getItem('auF:dk5en-1.local') === null, JSON.stringify([O.st.reload, O.view()]));
    O.w.auPageLeave();

    // Safeboot-like: /austatus HTML without cpage=, /ota/state JSON applying -> keeps following /ota/state
    const n5 = mkNode(J('E'));
    const SB = await mkPage(n5);
    SB.w.sessionStorage.setItem('auF:dk5en-1.local', JSON.stringify({ tag: NEWTAG, t0: SB.w.Date.now() - 20000, etaS: 35 }));
    await SB.init();
    n5.notJson = true;
    n5.ota = { state: 'applying', reason: 'none', received: 500000, total: 1000000, fallback_in_ms: 0 };
    await SB.T.advance(8000);
    check('Safeboot-like (HTML without cpage=, /ota/state JSON): no reload, storage kept, progress from /ota/state', SB.st.reload === 0 && SB.w.sessionStorage.getItem('auF:dk5en-1.local') !== null && /Installing now \.\.\. (6[0-9]|7[0-9]|[89][0-9]) %/.test(SB.view().btn), JSON.stringify([SB.st.reload, SB.view()]));
    SB.w.auPageLeave();

    // fetch failure on both paths -> keeps waiting
    const n6 = mkNode(J('E'));
    const FF = await mkPage(n6);
    FF.w.sessionStorage.setItem('auF:dk5en-1.local', JSON.stringify({ tag: NEWTAG, t0: FF.w.Date.now() - 20000, etaS: 35 }));
    await FF.init();
    n6.down = true;
    await FF.T.advance(8000);
    check('fetch failure: keeps waiting (no reload, storage kept, F still shown)', FF.st.reload === 0 && FF.w.sessionStorage.getItem('auF:dk5en-1.local') !== null && /Installing now/.test(FF.view().btn), JSON.stringify([FF.st.reload, FF.view()]));
    FF.w.auPageLeave();

    // no answer at all for 180 s + etaS
    const n2 = mkNode(J('E'));
    n2.down = true;
    const Q = await mkPage(n2);
    Q.w.sessionStorage.setItem('auF:dk5en-1.local', JSON.stringify({ tag: NEWTAG, t0: Q.w.Date.now(), etaS: 35 }));
    await Q.init();
    await Q.T.advance(216000);
    check('no answer for 180 s + etaS: "The node is not back yet"', /The node is not back yet/.test(Q.view().msg), Q.view().msg);
    n2.down = false;
    n2.notJson = false;
    n2.status = J('E');
    await Q.T.advance(4000);
    check('the old firmware answers: storage cleared, I overlay "Install failed, the old firmware is running" on E', Q.w.sessionStorage.getItem('auF:dk5en-1.local') === null && /old firmware is running/.test(Q.view().err) && Q.view().btn === 'Install now', JSON.stringify(Q.view()));
    Q.w.auPageLeave();

    // old Safeboot ignored the record: sbok 0 -> S
    const n3 = mkNode(J('S'));
    const S = await mkPage(n3);
    S.w.sessionStorage.setItem('auF:dk5en-1.local', JSON.stringify({ tag: NEWTAG, t0: S.w.Date.now() - 30000, etaS: 35 }));
    await S.init();
    await S.T.advance(2000);
    check('old tag with sbok 0 after F: state S', /Use Webflasher to renew Auto Update aware Safeboot/.test(S.view().msg) && S.w.sessionStorage.getItem('auF:dk5en-1.local') === null, JSON.stringify(S.view()));
    S.w.auPageLeave();
  }

  // ---- non-2xx /austatus is an error, not a state (500 {"ok":false} must not render S) ----------------------------
  {
    const node = mkNode(J('B'));
    const P = await mkPage(node);
    await P.init();
    check('500 setup: B shown', P.view().btn === 'Checking now ...');
    node.st500 = true;
    const n = node.statusCalls;
    await P.T.advance(3000);
    const v = P.view();
    check('500 body {"ok":false}: last row kept (no S), polling continues', P.w.auS.st === 'busy' && v.btn === 'Checking now ...' && !/Webflasher/.test(v.msg) && node.statusCalls - n >= 3, JSON.stringify([v, node.statusCalls - n]));
    node.st500 = false;
    node.status = J('C');
    await P.T.advance(1000);
    check('500 over: the next answer renders again', P.view().btn === 'Install' && P.view().green, JSON.stringify(P.view()));
    P.w.auPageLeave();
    // first fetch fails at idle: row stays empty-ish and a retry follows
    const n2 = mkNode(J('A'));
    n2.st500 = true;
    const Q = await mkPage(n2);
    await Q.init();
    check('500 on the first fetch: no state is drawn', Q.w.auS === null && Q.view().btn === 'Check now' && Q.view().msg === '');
    n2.st500 = false;
    await Q.T.advance(5000);
    check('500 on the first fetch: retried after 5 s, then rendered', Q.w.auS !== null && /up to date|available none/.test(Q.view().msg), JSON.stringify(Q.view()));
    Q.w.auPageLeave();
  }

  // ---- image built without MC_BUILD_TAG: run is "local", the restart is told by "up" -----------------------------
  {
    const base = JSON.parse(J('E'));
    const mkL = (o) => JSON.stringify(Object.assign({}, base, { run: 'local' }, o));
    const node = mkNode(mkL({ up: 1000 }));
    const P = await mkPage(node);
    await P.init();
    await P.click();   // Install now
    check('local: Install now starts F and remembers the uptime baseline', /Installing v4.40a.10.18/.test(P.view().msg) && JSON.parse(P.w.sessionStorage.getItem('auF:dk5en-1.local')).upBase === 1000);
    node.status = mkL({ staged: '', avail: NEWTAG, newer: 0, up: 3 });
    await P.T.advance(26000);
    check('local: uptime fell -> the node restarted -> success path (reload, storage cleared)', P.st.reload === 1 && P.w.sessionStorage.getItem('auF:dk5en-1.local') === null && P.view().btn === 'Installing now ... 100 %', JSON.stringify([P.st.reload, P.view().btn]));
    P.w.auPageLeave();

    // no restart: uptime kept counting -> install failed
    const n2 = mkNode(mkL({ up: 1000 }));
    const Q = await mkPage(n2);
    await Q.init();
    await Q.click();
    n2.status = mkL({ up: 1026 });
    await Q.T.advance(26000);
    check('local: uptime kept counting -> no restart -> "Install failed, the old firmware is running", no reload', Q.st.reload === 0 && /old firmware is running/.test(Q.view().err) && Q.w.sessionStorage.getItem('auF:dk5en-1.local') === null, JSON.stringify([Q.st.reload, Q.view()]));
    Q.w.auPageLeave();

    // a tagged old image with a restart (safeboot fell back) is still a failure; run == tag is still success without "up"
    const n3 = mkNode(mkL({ up: 1000, run: 'v4.40a.10.17' }));
    const R = await mkPage(n3);
    await R.init();
    await R.click();
    n3.status = mkL({ up: 3, run: 'v4.40a.10.17' });
    await R.T.advance(26000);
    check('tagged old image after a restart is still a failure', R.st.reload === 0 && /old firmware is running/.test(R.view().err), JSON.stringify([R.st.reload, R.view()]));
    R.w.auPageLeave();
  }

  // ---- image identity: "img" (first 8 hex of the running image's ELF SHA-256) tells a fallback from a new image -------
  // A node on an untagged (local) build installs a release that fails in Safeboot and falls back: run stays
  // "local" and the uptime drops, so only the image id can tell the old image from the new one.
  {
    const base = JSON.parse(J('E'));
    const mkI = (o) => JSON.stringify(Object.assign({}, base, { run: 'local' }, o));
    const n1 = mkNode(mkI({ up: 1000, img: 'a1b2c3d4' }));
    const P = await mkPage(n1);
    await P.init();
    await P.click();
    check('img: Install now remembers the image id of the running image', JSON.parse(P.w.sessionStorage.getItem('auF:dk5en-1.local')).img === 'a1b2c3d4', P.w.sessionStorage.getItem('auF:dk5en-1.local'));
    n1.status = mkI({ up: 3, img: 'a1b2c3d4' });
    await P.T.advance(26000);
    check('img: restart with the same image id (Safeboot fell back) -> "Install failed, the old firmware is running", no reload', P.st.reload === 0 && /old firmware is running/.test(P.view().err) && P.w.sessionStorage.getItem('auF:dk5en-1.local') === null, JSON.stringify([P.st.reload, P.view()]));
    P.w.auPageLeave();

    const n2 = mkNode(mkI({ up: 1000, img: 'a1b2c3d4' }));
    const Q = await mkPage(n2);
    await Q.init();
    await Q.click();
    n2.status = mkI({ staged: '', avail: NEWTAG, newer: 0, up: 3, img: '0f9e8d7c' });
    await Q.T.advance(26000);
    check('img: restart with a new image id -> success path (reload, storage cleared)', Q.st.reload === 1 && Q.w.sessionStorage.getItem('auF:dk5en-1.local') === null && Q.view().btn === 'Installing now ... 100 %', JSON.stringify([Q.st.reload, Q.view().btn]));
    Q.w.auPageLeave();

    // tagged success is unchanged by the id: run == tag wins even with the same id
    const n3 = mkNode(mkI({ up: 1000, img: 'a1b2c3d4', run: 'v4.40a.10.17' }));
    const R = await mkPage(n3);
    await R.init();
    await R.click();
    n3.status = mkI({ staged: '', avail: NEWTAG, newer: 0, up: 3, img: 'a1b2c3d4', run: NEWTAG });
    await R.T.advance(26000);
    check('img: run == tag is success whatever the id says', R.st.reload === 1 && R.w.sessionStorage.getItem('auF:dk5en-1.local') === null, JSON.stringify([R.st.reload, R.view()]));
    R.w.auPageLeave();

    // the id only on one side (an old image without "img"): the uptime rule decides as before
    const n4 = mkNode(mkI({ up: 1000 }));
    const S = await mkPage(n4);
    await S.init();
    await S.click();
    n4.status = mkI({ staged: '', avail: NEWTAG, newer: 0, up: 3, img: '0f9e8d7c' });
    await S.T.advance(26000);
    check('img missing at click: the uptime drop decides (success)', S.st.reload === 1, JSON.stringify([S.st.reload, S.view()]));
    S.w.auPageLeave();
  }

  // ---- polling stops when the page changes -------------------------------------------------------------------------
  {
    const node = mkNode(J('B'));
    const P = await mkPage(node);
    await P.init();
    await P.T.advance(3000);
    const n = node.statusCalls;
    check('B polls while the Info page is shown', n >= 3, String(n));
    P.w.eval("cpage='messages';");
    await P.T.advance(3000);
    check('cpage left Info: at most one more request, then the timers are gone', node.statusCalls <= n + 1 && P.T.list.size === 0, node.statusCalls - n + '/' + P.T.list.size);
    // loadPage() calls auPageLeave(): timers cleared at once
    const Q = await mkPage(mkNode(J('B')));
    await Q.init();
    Q.w.auPageLeave();
    check('auPageLeave clears every timer', Q.T.list.size === 0);
    const F2 = await mkPage(mkNode(J('E')));
    F2.w.sessionStorage.setItem('auF:dk5en-1.local', JSON.stringify({ tag: NEWTAG, t0: F2.w.Date.now(), etaS: 35 }));
    await F2.init();
    F2.w.eval("cpage='setup';");
    await F2.T.advance(3000);
    check('F tick stops when the page leaves Info; the remembered click stays for the way back', F2.T.list.size === 0 && F2.w.sessionStorage.getItem('auF:dk5en-1.local') !== null);
  }

  // ---- F2: a remembered click counts only for the tag the node has staged ----------------------------------------------
  {
    const K = 'auF:dk5en-1.local';
    const OLD = 'v4.40a.10.19';
    // staged tag differs from the remembered one (earlier attempt for 10.18, node now holds 10.19): record dropped, plain E
    const node = mkNode(mk({ st: 'ok', job: 'download', avail: OLD, newer: 1, inst: 1, staged: OLD }));
    const P = await mkPage(node);
    P.w.sessionStorage.setItem(K, JSON.stringify({ tag: NEWTAG, t0: P.w.Date.now() - 20000, etaS: 35 }));
    await P.init();
    await P.T.advance(3000);
    const v = P.view();
    check('F2: stale tag record dropped (storage cleared, plain E for the staged tag, no failure overlay)', P.w.sessionStorage.getItem(K) === null && v.btn === 'Install now' && !v.dis && v.err === '' && /v4.40a.10.19 downloaded/.test(v.msg), JSON.stringify(v));
    P.w.auPageLeave();
    // matching staged tag: record kept, F shown
    const n2 = mkNode(mk({ st: 'ok', job: 'download', avail: NEWTAG, newer: 1, inst: 1, staged: NEWTAG }));
    const Q = await mkPage(n2);
    Q.w.sessionStorage.setItem(K, JSON.stringify({ tag: NEWTAG, t0: Q.w.Date.now() - 20000, etaS: 35 }));
    await Q.init();
    check('F2: record for the staged tag kept, F shown', Q.w.sessionStorage.getItem(K) !== null && /Installing v4.40a.10.18/.test(Q.view().msg) && Q.view().dis, JSON.stringify(Q.view()));
    Q.w.auPageLeave();
    // older than 180 s + etaS + 60 s and the node answers idle: dropped even for the same tag
    const n3 = mkNode(mk({ st: 'ok', job: 'download', avail: NEWTAG, newer: 1, inst: 1, staged: NEWTAG }));
    const R = await mkPage(n3);
    R.w.sessionStorage.setItem(K, JSON.stringify({ tag: NEWTAG, t0: R.w.Date.now() - 400000, etaS: 35 }));
    await R.init();
    check('F2: record older than 180 s + etaS + 60 s dropped once the node answers idle', R.w.sessionStorage.getItem(K) === null && R.view().btn === 'Install now' && !R.view().dis, JSON.stringify(R.view()));
    R.w.auPageLeave();
  }

  // ---- F3: Safeboot apply failure without a valid app ---------------------------------------------------------------
  {
    const K = 'auF:dk5en-1.local';
    const DEAD = /Install failed -- the node stays in Safeboot\. Upload a firmware at http:\/\/dk5en-1\.local\/update or use the Webflasher/;
    for (const [nm, ota] of [['app_valid false', { state: 'aborted', reason: 'apply_failed', app_valid: false, fallback_in_ms: -1, received: 0, total: 0 }],
      ['fallback_in_ms -1', { state: 'aborted', reason: 'apply_failed', app_valid: true, fallback_in_ms: -1, received: 0, total: 0 }]]) {
      const node = mkNode(J('E'));
      const P = await mkPage(node);
      P.w.sessionStorage.setItem(K, JSON.stringify({ tag: NEWTAG, t0: P.w.Date.now() - 20000, etaS: 35 }));
      await P.init();
      node.notJson = true;
      node.ota = ota;
      await P.T.advance(30000);
      let v = P.view();
      check('F3 (' + nm + '): "stays in Safeboot" with the upload link, button disabled and relabelled', DEAD.test(v.msg) && v.btn === 'Install failed' && v.dis && !/ s$/.test(v.msg), JSON.stringify(v));
      const m0 = v.msg;
      await P.T.advance(6000);
      check('F3 (' + nm + '): the line is stable, storage kept while the node is in Safeboot', P.view().msg === m0 && P.w.sessionStorage.getItem(K) !== null);
      // the node comes back on the old tag: the page recovers on its own
      node.notJson = false; node.ota = null; node.status = J('A');
      await P.T.advance(4000);
      check('F3 (' + nm + '): old tag answers again -> storage cleared, base state', P.w.sessionStorage.getItem(K) === null && P.view().btn === 'Check now' && !P.view().dis, JSON.stringify(P.view()));
      P.w.auPageLeave();
    }
    // countdown shape: never negative, 0 stays "in 0 s" and polling continues
    const n2 = mkNode(J('E'));
    const Q = await mkPage(n2);
    Q.w.sessionStorage.setItem(K, JSON.stringify({ tag: NEWTAG, t0: Q.w.Date.now() - 20000, etaS: 35 }));
    await Q.init();
    n2.notJson = true;
    n2.ota = { state: 'aborted', reason: 'apply_failed', app_valid: true, fallback_in_ms: 0, received: 0, total: 0 };
    await Q.T.advance(8000);
    const c0 = n2.calls.length;
    check('F3: fallback_in_ms 0 keeps "returns ... in 0 s" and keeps polling', /returns to the old firmware in 0 s/.test(Q.view().msg), Q.view().msg);
    await Q.T.advance(6000);
    check('F3: polling goes on at fallback 0', n2.calls.length > c0);
    Q.w.auPageLeave();
  }

  // ---- error table mirror (spec 4.3) --------------------------------------------------------------------------------
  {
    const P = await mkPage(mkNode(J('A')));
    const t = P.w.auErrText;
    const rows = [['check', 'tls -80', 'No connection to GitHub'], ['download', 'http 404', 'No connection to GitHub'], ['check', 'short', 'The release list could not be read'],
      ['download', 'digest', 'Download damaged, it was discarded -- try again'], ['stagelan', 'crc', 'Download damaged, it was discarded -- try again'],
      ['download', 'noasset', 'This release has no image for this board'], ['download', 'layout', "This node's partition layout cannot take the image -- use the Webflasher"],
      ['download', 'erase', 'Could not write the image to flash'], ['check', 'nomem', 'Not enough free memory for the update'], ['check', 'nocheck', 'Update failed'], ['check', 'digest', 'Update failed']];
    check('error table 4.3 per job', rows.every((r) => t(r[0], r[1]) === r[2]), rows.filter((r) => t(r[0], r[1]) !== r[2]).map((r) => r.join('/')).join(' | '));
  }

  console.log(failures ? failures + ' FAILED' : 'ALL PASSED');
  process.exit(failures ? 1 : 0);
})();

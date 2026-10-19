// AU-16: offline jsdom test of state S (Safeboot cannot take the install) of the Info page "Auto update" row.
// Extracts the page JS from auScaffoldJs() like tools/webgui_au_test.js; no node needed.
//   sbok 0 + sbwhy 'dio'  -> DIO bootloader notice, button disabled
//   sbok 0, no sbwhy      -> old text (older firmware), button disabled
//   sbok 0 + sbwhy 'old'  -> old text, button disabled
// A click on the disabled button starts no job, with and without a busy job.
// Run:   NODE_PATH=<dir>/node_modules node tools/webgui_au_sbwhy_test.js   (env AU_PAGE_SRC overrides the C++ source)
const fs = require('fs');
const path = require('path');
const { JSDOM, VirtualConsole } = require('jsdom');

const SRC = process.env.AU_PAGE_SRC || path.join(__dirname, '..', 'src', 'web_functions', 'web_functions.cpp');
let failures = 0;
function check(name, cond, detail) {
  console.log((cond ? 'PASS ' : 'FAIL ') + name + (detail !== undefined && !cond ? '  [' + detail + ']' : ''));
  if (!cond) failures++;
}
function decodeLiteral(s) {
  let o = '';
  for (let i = 0; i < s.length; i++) {
    if (s[i] !== '\\') { o += s[i]; continue; }
    const c = s[++i];
    if (c === 'n') o += '\n'; else if (c === 't') o += '\t'; else o += c;
  }
  return o;
}
function extractJs(src, fn) {
  const at = src.indexOf('void ' + fn + '()');
  const i = src.indexOf('{', at);
  const body = src.slice(i, src.indexOf('\n}\n', i));
  let out = '';
  const re = /web_client\.println\(\s*"((?:[^"\\]|\\.)*)"\s*\)/g;
  let m;
  while ((m = re.exec(body))) out += decodeLiteral(m[1]) + '\n';
  return out;
}
const JS = extractJs(fs.readFileSync(SRC, 'utf8'), 'auScaffoldJs');
const flush = async () => { for (let i = 0; i < 8; i++) await new Promise((r) => setImmediate(r)); };
const ROW = '<table><tr id="au_tr"><td id="au_row" data-blepause="0"><div id="au_msg"></div><div id="au_err"></div><div id="au_hint"></div>' +
  '<button type="button" id="au_btn" disabled>Check now</button></td></tr></table>';
const BODY = { st: 'idle', job: 'none', run: 'v4.40a.10.17', avail: '', newer: 0, inst: 0, staged: '', err: '', pct: 0, phone: 0, guard: 0, sbok: 0, mode: 'notify', chan: 'dev', etaS: 35, ifree: 57000, iblk: 31000 };
const DIO_TEXT = '! Bootloader (DIO) cannot start the Safeboot: renew bootloader and Safeboot with the Webflasher';
const OLD_TEXT = '! Use Webflasher to renew Auto Update aware Safeboot';

async function page(body) {
  const calls = [];
  const vc = new VirtualConsole();
  vc.on('jsdomError', (e) => { console.log('jsdomError: ' + e.message); failures++; });
  const dom = new JSDOM('<!doctype html><body>' + ROW + '</body>', {
    url: 'http://dk5en-1.local/', runScripts: 'outside-only', virtualConsole: vc,
    beforeParse(w) {
      w.setTimeout = () => 1; w.setInterval = () => 1; w.clearTimeout = () => {}; w.clearInterval = () => {};
      w.fetch = (u) => { calls.push(String(u)); return Promise.resolve({ status: 200, ok: true, text: () => Promise.resolve(JSON.stringify(body)), json: () => Promise.resolve(body) }); };
    },
  });
  const w = dom.window;
  w.eval("var cpage='info';");
  w.eval(JS);
  w.auPageInit();
  await flush();
  const b = w.document.getElementById('au_btn');
  const view = () => ({ msg: w.document.getElementById('au_msg').textContent, dis: b.disabled });
  return { w, b, calls, view };
}

(async () => {
  check('page JS extracted', JS.length > 2000 && /function auPageInit/.test(JS), String(JS.length));
  const cases = [
    ['sbok 0 + sbwhy dio', Object.assign({}, BODY, { sbwhy: 'dio' }), DIO_TEXT],
    ['sbok 0 + sbwhy old', Object.assign({}, BODY, { sbwhy: 'old' }), OLD_TEXT],
    ['sbok 0 without sbwhy (older firmware)', BODY, OLD_TEXT],
    ['sbok 0 + sbwhy dio, busy job', Object.assign({}, BODY, { sbwhy: 'dio', st: 'busy', job: 'check' }), DIO_TEXT],
    ['sbok 0 + sbwhy old, busy job', Object.assign({}, BODY, { sbwhy: 'old', st: 'busy', job: 'check' }), OLD_TEXT],
  ];
  for (const [name, body, text] of cases) {
    const P = await page(body);
    const v = P.view();
    check(name + ': notice', v.msg === text, v.msg);
    check(name + ': button disabled', v.dis === true);
    const n0 = P.calls.length;
    P.b.dispatchEvent(new P.w.MouseEvent('click', { bubbles: true }));
    await flush();
    check(name + ': click starts no job', P.calls.filter((c) => c.indexOf('/callfunction/') >= 0).length === 0 && P.calls.length === n0, P.calls.join(','));
  }
  const ok = await page(Object.assign({}, BODY, { sbok: 1, sbwhy: 'ok' }));
  check('sbok 1: no S notice, button enabled', ok.view().msg.indexOf('Safeboot') < 0 && ok.view().dis === false, JSON.stringify(ok.view()));
  console.log(failures === 0 ? 'ALL PASS' : failures + ' FAILURES');
  process.exit(failures === 0 ? 0 : 1);
})().catch((e) => { console.log('HARNESS ERROR', e); process.exit(2); });

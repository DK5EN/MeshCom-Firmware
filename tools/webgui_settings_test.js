// Settings page "Remote Admin" card: offline jsdom test (src/web_functions/web_functions.cpp).
//
// The card markup (between the C++ comments RA-HTML-BEGIN / RA-HTML-END in sub_page_setup()) and its scaffold JS
// (RA-JS-BEGIN / RA-JS-END) are extracted by concatenating the string literals of their web_client.println calls and
// run in jsdom against a fake fetch that speaks /rmstatus, /rmpasswd and /setparam. Timers are virtual.
//
// The card reuses the helpers of the Remote page scaffold (rmEl, rmPost, rmJson, rmPwProblem, rmEnc, rmErrText), which
// rmScaffoldJs() prints in the same <script> block before the RA block; the test loads that scaffold from web_rm_page.cpp
// (RM_PAGE_SRC overrides the path) and the loadPage() literal from web_functions.cpp to prove the raSt() hook.
//
// Prerequisites (not part of the repo):  npm i jsdom@24   (anywhere; set NODE_PATH to its node_modules)
// Run:   NODE_PATH=<dir>/node_modules node tools/webgui_settings_test.js
// Mutation check (the test must then FAIL):  SETTINGS_MUTATE=noconfirm|pwurl|noval|nohook|silentst|nodisarm|nomsg|nologin node tools/webgui_settings_test.js
const fs = require('fs');
const path = require('path');
const { JSDOM, VirtualConsole } = require('jsdom');

const SRC = process.env.SETTINGS_SRC || path.join(__dirname, '..', 'src', 'web_functions', 'web_functions.cpp');
const RM_SRC = process.env.RM_PAGE_SRC || path.join(__dirname, '..', 'src', 'web_functions', 'web_rm_page.cpp');
const MUT = process.env.SETTINGS_MUTATE || '';

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
    if (c === 'n') o += '\n';
    else if (c === 't') o += '\t';
    else if (c === '\\' || c === '"' || c === "'") o += c;
    else throw new Error('unsupported escape \\' + c);
  }
  return o;
}

function block(tag) {
  const src = fs.readFileSync(SRC, 'utf8');
  const a = src.indexOf('// ' + tag + '-BEGIN'), b = src.indexOf('// ' + tag + '-END');
  if (a < 0 || b < a) throw new Error('marker ' + tag + ' not found');
  const text = src.slice(src.indexOf('\n', a), b).split('\n').filter((l) => !/^\s*\/\//.test(l)).join('\n');
  let out = '';
  for (const m of text.matchAll(/"((?:[^"\\]|\\.)*)"/g)) out += decodeLiteral(m[1]);
  return out;
}

// rmScaffoldJs() of web_rm_page.cpp: the literals of all its web_client calls (the one printf only carries %u limits)
function scaffold() {
  const src = fs.readFileSync(RM_SRC, 'utf8');
  const a = src.indexOf('void rmScaffoldJs()'), b = src.indexOf('\n}\n', a);
  let out = '';
  for (const m of src.slice(a, b).matchAll(/web_client\.(println|print|printf)\s*\(((?:\s*"(?:[^"\\]|\\.)*")+)/g)) {
    for (const l of m[2].matchAll(/"((?:[^"\\]|\\.)*)"/g)) out += decodeLiteral(l[1]);
    out += '\n';
  }
  return out.replace(/%u/g, '0');
}
// the loadPage() literal of web_functions.cpp (one println line)
function loadPageJs() {
  const line = fs.readFileSync(SRC, 'utf8').split('\n').find((l) => l.indexOf('function loadPage(') >= 0);
  if (!line) throw new Error('loadPage literal not found');
  return [...line.matchAll(/"((?:[^"\\]|\\.)*)"/g)].map((m) => decodeLiteral(m[1])).join('');
}
function mutate(t, from, to) { if (!t.includes(from)) throw new Error('mutation anchor not found: ' + from); return t.replace(from, to); }

let html = block('RA-HTML'), js = block('RA-JS'), sc = scaffold(), lp = loadPageJs();
if (MUT === 'noconfirm') js = mutate(js, 'if(!raA){', 'if(0){');
if (MUT === 'pwurl') sc = mutate(sc, "fetch(u,{method:'POST'", "fetch(u+'?'+b,{method:'POST'");
if (MUT === 'noval') js = mutate(js, 'if(e){raM(e,1);return;}', '');
if (MUT === 'nohook') lp = mutate(lp, "if(page=='setup'&&typeof raSt=='function')raSt();", '');
if (MUT === 'silentst') js = mutate(js, "raX(e,'Could not read the state of this node. Reload the page.');", '');
if (MUT === 'nodisarm') js = mutate(js, 'e=rmPwProblem(p);raDis();', 'e=rmPwProblem(p);');
if (MUT === 'nomsg') js = mutate(js, 'raM(rmErrText(j),1)', "raM('The request was refused.',1)");
if (MUT === 'nologin') js = mutate(js, "e===0?'Log in again to change these settings.':t", 't');
if (MUT) console.log('NOTE mutation active: ' + MUT);

const calls = [];
let fail = {}, passwdReply = null;
let status = { on: 0, pw: 0, lock: 0, lockS: 0 };
let timers = [], now = 0;
function runTimers(ms) { now += ms; const due = timers.filter((t) => t.at <= now); timers = timers.filter((t) => t.at > now); due.forEach((t) => t.fn()); }

const vc = new VirtualConsole();
vc.on('jsdomError', (e) => { console.log('JSDOM ERROR ' + e.message); failures++; });
const dom = new JSDOM('<!doctype html><body><div id="content_inner">' + html + '</div></body>', { runScripts: 'outside-only', virtualConsole: vc });
const w = dom.window;
w.setTimeout = (fn, ms) => { timers.push({ fn, at: now + ms, id: timers.length + 1 }); return timers.length; };
w.clearTimeout = (id) => { timers = timers.filter((t) => t.id !== id); };
w.fetch = (url, opt) => {
  calls.push({ url, method: (opt && opt.method) || 'GET', body: opt && opt.body });
  const key = url.split('?')[0].replace('/setparam/', '/setparam'), f = fail[key];
  if (f === 'net') return Promise.reject(new TypeError('Failed to fetch'));
  if (f === 401 || f === 403) return Promise.resolve({ status: f, ok: false, json: () => Promise.resolve({}) });
  if (f === 'nojson') return Promise.resolve({ status: 200, ok: true, json: () => Promise.reject(new SyntaxError('Unexpected token')) });
  let j = {};
  if (url === '/rmstatus') j = status;
  else if (url === '/rmpasswd') {
    if (passwdReply) j = passwdReply;
    else if (opt.body.indexOf('act=set') === 0) { status.pw = 1; j = { ok: true }; }
    else if (opt.body === 'act=clear') { status.pw = 0; status.on = 0; j = { ok: true }; }
  } else if (url.indexOf('/setparam/?rm=') === 0) { status.on = url.endsWith('on') ? 1 : 0; j = { returncode: 0 }; }
  return Promise.resolve({ status: 200, json: () => Promise.resolve(j) });
};
w.eval("var cpage='setup';");
w.eval(sc);
w.eval(js);
w.eval('var mcRenderQueue=function(){},mcRenderHistory=function(){},rmPageInit=function(){},rmPageLeave=function(){};');
w.eval(lp);
const $ = (i) => w.document.getElementById(i);
const flush = () => new Promise((r) => setImmediate(r));
async function settle() { for (let i = 0; i < 6; i++) await flush(); }

(async () => {
  check('card title Remote Admin', /<label class="cardlabel">Remote Admin<\/label>/.test(html));
  check('handlers wired', ['raOn()', 'raSet()', 'raClr()'].every((h) => html.indexOf('="' + h + '"') >= 0));
  check('N1 hint sits in its own full-width row, not in the narrow middle column', /id="ra_hint"[^>]*style="grid-column:1\/-1"/.test(html) && !/<label for="ra_on">Remote management<\/label><span id="ra_hint"/.test(html));
  check('2323/KISS note present', html.indexOf('This password also protects the net console (port 2323) and the KISS port, not only remote management.') >= 0);

  const shown = (id) => w.getComputedStyle($(id)).display !== 'none';
  check('empty hint and message rows take no space (no gap above the note)', !shown('ra_hint') && !shown('ra_msg'), shown('ra_hint') + '|' + shown('ra_msg'));
  w.raSt(); await settle();
  check('the hint row shows once it has text', shown('ra_hint'));
  check('toggle disabled without password', $('ra_on').disabled === true);
  check('hint without password', $('ra_hint').textContent === 'Set a password first, then switch remote management on.');
  check('status text not set', $('ra_st').textContent === 'password: not set');

  // invalid password: error shown, no request
  let n = calls.length;
  $('ra_pw').value = '';
  w.raSet(); await settle();
  check('empty password: error shown', $('ra_msg').textContent === 'Enter a password.');
  $('ra_pw').value = 'x'.repeat(15);
  w.raSet(); await settle();
  check('15 chars: error shown', /at most 14/.test($('ra_msg').textContent));
  check('invalid password sends no request', calls.length === n, JSON.stringify(calls.slice(n)));

  // Set
  $('ra_pw').value = 'geheim&1';
  w.raSet(); await settle();
  const setc = calls.find((c) => c.url.indexOf('/rmpasswd') === 0 && c.body && c.body.indexOf('act=set') === 0);
  check('Set posts act=set with password in body', !!setc && setc.method === 'POST' && setc.body === 'act=set&pw=geheim%261', JSON.stringify(setc));
  check('password not in any URL', calls.every((c) => c.url.indexOf('geheim') < 0 && c.url.indexOf('pw=') < 0), JSON.stringify(calls.map((c) => c.url)));
  check('Set shows success', $('ra_msg').textContent === 'Password set.');
  check('password field cleared', $('ra_pw').value === '');
  check('toggle enabled after Set', $('ra_on').disabled === false);
  check('status text set', $('ra_st').textContent === 'password: set');
  check('hint gone after Set', $('ra_hint').textContent === '');

  // toggle
  $('ra_on').checked = true;
  w.raOn();
  await settle();
  check('toggle sends /setparam/?rm=on', calls.some((c) => c.url === '/setparam/?rm=on'));
  check('toggle shows result', $('ra_msg').textContent === 'Remote management is on.' && $('ra_on').checked === true);

  // Clear needs confirm
  n = calls.length;
  w.raClr(); await settle();
  check('first Clear click sends nothing', !calls.slice(n).some((c) => c.url === '/rmpasswd'), JSON.stringify(calls.slice(n)));
  check('first Clear click arms with hint', /Really/.test($('ra_clr').textContent) && /Clear the password/.test($('ra_msg').textContent));
  runTimers(4001);
  check('arm times out', $('ra_clr').textContent === 'Clear');
  w.raClr(); w.raClr(); await settle();
  const clr = calls.slice(n).filter((c) => c.url === '/rmpasswd');
  check('second Clear click posts act=clear', clr.length === 1 && clr[0].body === 'act=clear', JSON.stringify(clr));
  check('Clear shows success', /Password cleared/.test($('ra_msg').textContent));
  check('after Clear: toggle disabled and off', $('ra_on').disabled === true && $('ra_on').checked === false);
  check('no leaked timers', timers.length === 0, String(timers.length));

  // N2: Set disarms an armed Clear
  w.raClr(); await settle();
  check('N2 armed Clear before Set', /Really/.test($('ra_clr').textContent));
  $('ra_pw').value = 'neu';
  n = calls.length;
  w.raSet(); await settle();
  check('N2 Set disarms Clear: label back, no leaked timer', $('ra_clr').textContent === 'Clear' && timers.length === 0, $('ra_clr').textContent + '|' + timers.length);
  w.raClr(); await settle();
  check('N2 the next Clear click arms again, sends no act=clear', !calls.slice(n).some((c) => c.body === 'act=clear') && /Really/.test($('ra_clr').textContent));
  w.raDis(1);

  // loadPage hook: showing the setup page fills the card (no inline script runs in the injected fragment)
  {
    const f = w.document.createElement('div'); f.id = 'content_layer'; w.document.body.appendChild(f);
    const page = '<div id="x">' + html + '</div>';
    w.XMLHttpRequest = function () { this.open = function () {}; this.send = function () { this.readyState = 4; this.status = 200; this.responseText = page; this.onreadystatechange(); }; };
    status = { on: 1, pw: 1, lock: 0, lockS: 0 };
    const sender = w.document.createElement('button');
    n = calls.length;
    w.loadPage('setup', sender, false); await settle();
    check('loadPage(setup) calls raSt: /rmstatus is read and the toggle shows the state', calls.slice(n).some((c) => c.url === '/rmstatus') && $('ra_on') && $('ra_on').checked === true && $('ra_st').textContent === 'password: set', JSON.stringify(calls.slice(n)));
    n = calls.length;
    w.loadPage('remote', sender, false); await settle();
    check('loadPage(other page) does not read /rmstatus for the card', !calls.slice(n).some((c) => c.url === '/rmstatus'));
    const src = fs.readFileSync(SRC, 'utf8');
    check('loadPage literal carries the raSt hook', src.includes("if(page=='setup'&&typeof raSt=='function')raSt();"));
  }

  // lock text (W1d)
  {
    const stText = async (o) => { status = Object.assign({ on: 0, pw: 1, lock: 0, lockS: 0 }, o); w.raSt(); await settle(); return $('ra_st').textContent; };
    let t = await stText({ lock: 1, lockS: 42 });
    check('W1d lock shows the seconds', /blocked for 42 s after wrong attempts/.test(t), t);
    t = await stText({ lock: 1, lockS: 0 });
    check('W1d lock with 0 s shows "1 s"', /blocked for 1 s after/.test(t), t);
    t = await stText({ lock: 0, lockS: 42 });
    check('W1d not locked: no lock text, never "not locked"', t === 'password: set' && !/not locked/.test(t), t);
    status = { on: 0, pw: 1, lock: 0, lockS: 0 };
  }

  // DRY-02: /rmpasswd refusals
  {
    $('ra_msg').textContent = '';
    passwdReply = { ok: false, err: 'pw', msg: 'Server says: no leading space.' };
    $('ra_pw').value = ' bad'; w.raSet(); await settle();
    check('DRY-02 /rmpasswd refusal shows the server msg verbatim', $('ra_msg').textContent === 'Server says: no leading space.', $('ra_msg').textContent);
    passwdReply = { ok: false, err: 'pw' };
    $('ra_pw').value = ' bad'; w.raSet(); await settle();
    check('DRY-02 refusal without msg falls back to a generic sentence', $('ra_msg').textContent === 'The request was refused (pw).', $('ra_msg').textContent);
    passwdReply = { ok: false };
    $('ra_pw').value = ' bad'; w.raSet(); await settle();
    check('DRY-02 refusal with neither err nor msg is still a sentence', $('ra_msg').textContent === 'The request was refused.', $('ra_msg').textContent);
    passwdReply = null;
  }

  // error paths: failed or unauthorised calls say so
  {
    const LOGIN = 'Log in again to change these settings.', READ = 'Could not read the state of this node. Reload the page.', DNA = 'The node did not answer. Try again.';
    const msg = () => $('ra_msg').textContent, red = () => $('ra_msg').style.color;
    const rs = async (f) => { fail = { '/rmstatus': f }; $('ra_msg').textContent = ''; w.raSt(); await settle(); fail = {}; };
    await rs(401);
    check('/rmstatus 401 shows the log-in sentence, in red', msg() === LOGIN && red() !== '', msg());
    await rs(403);
    check('/rmstatus 403 shows the log-in sentence', msg() === LOGIN, msg());
    await rs('net');
    check('/rmstatus network error shows "Could not read the state"', msg() === READ, msg());
    await rs('nojson');
    check('/rmstatus non-JSON answer shows "Could not read the state"', msg() === READ, msg());
    status = { on: 0, pw: 1, lock: 0, lockS: 0 };
    fail = { '/rmpasswd': 401 }; $('ra_msg').textContent = ''; $('ra_pw').value = 'abc'; w.raSet(); await settle(); fail = {};
    check('/rmpasswd 401 shows the log-in sentence', msg() === LOGIN, msg());
    fail = { '/rmpasswd': 'net' }; $('ra_pw').value = 'abc'; w.raSet(); await settle(); fail = {};
    check('/rmpasswd network error shows "did not answer"', msg() === DNA, msg());
    fail = { '/setparam': 401 }; $('ra_on').checked = true; w.raOn(); await settle(); fail = {};
    check('/setparam 401 shows the log-in sentence', msg() === LOGIN, msg());
    fail = { '/setparam': 'net' }; $('ra_on').checked = true; w.raOn(); await settle(); fail = {};
    check('/setparam network error shows "did not answer"', msg() === DNA, msg());
    check('error paths leave no timers', timers.length === 0, String(timers.length));
  }

  console.log(failures ? 'FAILED: ' + failures : 'ALL PASS');
  process.exit(failures ? 1 : 0);
})();

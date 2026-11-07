// Pins page: offline jsdom test (src/web_functions/web_functions.cpp sub_page_pins(), web/mc.js "Pins page").
//
// The page frame (between the C++ comments PINS-HTML-BEGIN / PINS-HTML-END) is built from the string literals of its
// web_client.println calls; the page logic is the block between PINS-JS-BEGIN / PINS-JS-END of web/mc.js, and the
// X-MC fetch wrapper is line 4 of web/mc.js; the loadPage() literal of web_functions.cpp carries the init/leave hooks.
// A fake fetch speaks /pinstatus and /setparam; timers are virtual. There is no MCP23017 on the bench: the expander
// side is only ever exercised here and in test/test_pin_status, with canned /pinstatus answers.
//
// Prerequisites (not part of the repo):  npm i jsdom@24   (anywhere; set NODE_PATH to its node_modules)
// Run:   NODE_PATH=<dir>/node_modules node tools/webgui_pins_test.js
// Mutation check (the test must then FAIL):  PINS_MUTATE=noleave|noinit|nostale|nohdr|noclear|dirtyoverwrite|alwayspoll node tools/webgui_pins_test.js
const fs = require('fs');
const path = require('path');
const { JSDOM, VirtualConsole } = require('jsdom');

const SRC = process.env.PINS_SRC || path.join(__dirname, '..', 'src', 'web_functions', 'web_functions.cpp');
const MC_CSS_SRC = process.env.MC_CSS_SRC || path.join(__dirname, '..', 'src', 'web_functions', 'web', 'mc.css');
const MC_JS_SRC = process.env.MC_JS_SRC || path.join(__dirname, '..', 'src', 'web_functions', 'web', 'mc.js');
const MUT = process.env.PINS_MUTATE || '';

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
function between(src, tag) {
  const a = src.indexOf(tag + '-BEGIN'), b = src.indexOf(tag + '-END');
  if (a < 0 || b < a) throw new Error('marker ' + tag + ' not found');
  return src.slice(src.indexOf('\n', a), b).split('\n').filter((l) => !/^\s*\/\//.test(l)).join('\n');
}
function pageHtml() {
  let out = '';
  for (const m of between(fs.readFileSync(SRC, 'utf8'), 'PINS-HTML').matchAll(/"((?:[^"\\]|\\.)*)"/g)) out += decodeLiteral(m[1]) + '\n';
  return out;
}
function loadPageJs() {
  const line = fs.readFileSync(SRC, 'utf8').split('\n').find((l) => l.indexOf('function loadPage(') >= 0);
  if (!line) throw new Error('loadPage literal not found');
  return [...line.matchAll(/"((?:[^"\\]|\\.)*)"/g)].map((m) => decodeLiteral(m[1])).join('');
}
function mutate(t, from, to) { if (!t.includes(from)) throw new Error('mutation anchor not found: ' + from); return t.replace(from, to); }

const mcSrc = fs.readFileSync(MC_JS_SRC, 'utf8');
const wrapper = mcSrc.split('\n').find((l) => l.startsWith('(function(){var f=window.fetch'));
if (!wrapper) throw new Error('X-MC wrapper not found in mc.js');
let js = between(mcSrc, 'PINS-JS'), lp = loadPageJs(), html = pageHtml();
if (MUT === 'noleave') lp = mutate(lp, "if(typeof pinsLeave=='function')pinsLeave();", '');
if (MUT === 'noinit') lp = mutate(lp, "pinsInit();", '');
if (MUT === 'nostale') js = mutate(js, "pnTxt('pn_note','Stale: the node did not answer, showing the last values.');", '');
if (MUT === 'noclear') js = mutate(js, "pnRender(j);pnTxt('pn_note','');", 'pnRender(j);');
if (MUT === 'dirtyoverwrite') js = mutate(js, "if(!r.inp.getAttribute('data-dirty')&&document.activeElement!==r.inp)", 'if(1)');
if (MUT === 'alwayspoll') js = mutate(js, 'function pinsLeave(){pnOn=false;pnG++;clearTimeout(pnT);pnT=0;', 'function pinsLeave(){');
if (MUT) console.log('NOTE mutation active: ' + MUT);

const calls = [];
let fail = null;        // null | 'net' | 500 | 401 for /pinstatus
let setReply = { returncode: 0 };
let setStatus = 200;
let pinStatus;
let assignFail = null;   // null | 'net' for /pinassign
let gpioFail = null;     // null | 'net' for /gpiostatus
let gpioStatus = { avail: 0, master: 0, pins: [] };
function maskOf(bits, zero = true) { let m = zero ? 1n : 0n; for (const b of bits) m |= 1n << BigInt(b); return '0x' + m.toString(16).padStart(16, '0'); }
function freshAssign() {
  const all = []; for (let i = 1; i < 40; i++) if (i !== 18 && i !== 21) all.push(i);
  return { chip: 'esp32', fn: [
    { id: 'ow', param: 'onewiregpio', avail: 1, pin: 0, def: 4, eff: 4, on: 1, restart: 0, ok: maskOf(all) },
    { id: 'btn', param: 'buttongpio', avail: 1, pin: 38, def: 38, eff: 38, on: 0, restart: 1, ok: maskOf(all) },
    { id: 'an', param: 'angpio', avail: 1, pin: 0, def: 36, eff: 36, on: 0, restart: 0, ok: maskOf(all, false) },   // the setter takes no 0
    { id: 'wind', param: 'windpin', avail: 0, pin: 0, def: 0, eff: 0, on: 0, restart: 0, ok: maskOf([]) } ] };
}
let pinAssign = freshAssign();
function freshStatus() {
  const pins = [];
  for (let i = 0; i < 16; i++) pins.push({ id: (i < 8 ? 'A' : 'B') + (i & 7), n: '', d: 'in', l: 0 });
  pins[0] = { id: 'A0', n: 'Relay', d: 'out', l: 1 };
  pins[1] = { id: 'A1', n: '<img src=x onerror="window.pwned=1">', d: 'in', l: 1 };
  return { led: { fitted: 1, mode: 'flash', busy: 0, rm: '', state: 'flash' }, chg: { fitted: 0, mode: 'off', eff: 'off', rm: 0 }, mcp: { present: 1, pins } };
}
pinStatus = freshStatus();

let timers = [], now = 0, tid = 0;
function runTimers(ms) { now += ms; const due = timers.filter((t) => t.at <= now); timers = timers.filter((t) => t.at > now); due.forEach((t) => t.fn()); }

const vc = new VirtualConsole();
vc.on('jsdomError', (e) => { console.log('JSDOM ERROR ' + e.message); failures++; });
const dom = new JSDOM('<!doctype html><body><div id="nav"><button id="navpins"></button><button id="navinfo"></button></div><div id="content_layer"></div></body>', { runScripts: 'dangerously', virtualConsole: vc });
const w = dom.window;
w.setTimeout = (fn, ms) => { timers.push({ fn, at: now + ms, id: ++tid }); return tid; };
w.clearTimeout = (id) => { timers = timers.filter((t) => t.id !== id); };
w.Headers = Headers;
// the fake network under the X-MC wrapper
w.fetch = (url, opt) => {
  const h = new Headers((opt && opt.headers) || {});
  calls.push({ url, xmc: h.get('X-MC') });
  if (url === '/pinassign') {
    if (assignFail === 'net') return Promise.reject(new TypeError('Failed to fetch'));
    return Promise.resolve({ ok: true, status: 200, json: () => Promise.resolve(JSON.parse(JSON.stringify(pinAssign))) });
  }
  if (url === '/gpiostatus') {
    if (gpioFail === 'net') return Promise.reject(new TypeError('Failed to fetch'));
    return Promise.resolve({ ok: true, status: 200, json: () => Promise.resolve(JSON.parse(JSON.stringify(gpioStatus))) });
  }
  if (url === '/pinstatus') {
    if (fail === 'net') return Promise.reject(new TypeError('Failed to fetch'));
    if (fail === 401) return Promise.resolve({ ok: false, status: 401, json: () => Promise.resolve({ ok: false }) });
    if (fail === 500) return Promise.resolve({ ok: false, status: 500, json: () => Promise.resolve({ ok: false }) });
    return Promise.resolve({ ok: true, status: 200, json: () => Promise.resolve(JSON.parse(JSON.stringify(pinStatus))) });
  }
  if (url.indexOf('/setparam/?') === 0 && setStatus === 401) return Promise.resolve({ ok: false, status: 401, json: () => Promise.reject(new SyntaxError('not json')) });
  // the node answers 422 with a JSON body for WS_RETURNCODE_FAIL (web_functions.cpp), 200 for OKAY
  if (url.indexOf('/setparam/?') === 0) return Promise.resolve({ ok: setStatus < 400, status: setStatus, json: () => Promise.resolve(setReply) });
  return Promise.resolve({ ok: false, status: 404, json: () => Promise.resolve({}) });
};
if (MUT !== 'nohdr') w.eval(wrapper);
// loadPage() talks XMLHttpRequest: the fake answers ?page=pins with the page frame and anything else with a stub
class FakeXhr {
  open(m, u) { this.u = u; }
  send() {
    const self = this;
    setImmediate(() => {
      self.readyState = 4; self.status = 200;
      self.responseText = self.u === '?page=pins' ? html : '<p id="other">other page</p>';
      self.onreadystatechange();
    });
  }
}
w.XMLHttpRequest = FakeXhr;
w.eval("var cpage='info',csender=document.getElementById('navinfo');var mcRenderQueue=function(){},mcLoginFocus=function(){},mcRenderHistory=function(){},rmPageInit=function(){},rmPageLeave=function(){};");
w.eval(js);
w.eval(lp);
const $ = (i) => w.document.getElementById(i);
const flush = () => new Promise((r) => setImmediate(r));
async function settle() { for (let i = 0; i < 8; i++) await flush(); }
const polls = () => calls.filter((c) => c.url === '/pinstatus').length;
const rows = () => w.document.querySelectorAll('#pn_body tr');
const arows = () => w.document.querySelectorAll('#pn_asg_body tr');
const assigns = () => calls.filter((c) => c.url === '/pinassign').length;
const gpios = () => calls.filter((c) => c.url === '/gpiostatus').length;
const grows = () => w.document.querySelectorAll('#pn_gpio_body tr');

(async () => {
  // ---- frame ----
  check('frame: three LED buttons with aria-pressed', ['off', 'flash', 'steady'].every((m) => /id="pn_led_/.test(html) && html.indexOf('id="pn_led_' + m + '" aria-pressed="false"') >= 0));
  check('frame: charge LED has off/auto buttons with aria-pressed', ['off', 'auto'].every((m) => html.indexOf('id="pn_chg_' + m + '" aria-pressed="false"') >= 0));
  check('frame: poll interval text says 5 s', /Refreshed every 5 s/.test(html), 'stale 3 s text');
  const css = fs.readFileSync(MC_CSS_SRC, 'utf8');
  // jsdom does not compute the cascade: the rule that lets [hidden] win over `#content_inner button` / `.mctab` (display:inline-flex) must exist
  check('css: #content_inner button[hidden] is display:none (beats the inline-flex rules)', /#content_inner\s+button\[hidden\]\s*\{[^}]*display\s*:\s*none/.test(css));
  check('frame: status regions are role=status', (html.match(/role="status"/g) || []).length === 2);
  check('frame: table has column headers with scope', (html.match(/<th scope="col">/g) || []).length === 14);
  check('frame: everything hidden until the first answer', ['pn_led', 'pn_chg', 'pn_none', 'pn_mcp', 'pn_asg', 'pn_gpio'].every((i) => new RegExp('id="' + i + '"[^>]*hidden').test(html)));

  // ---- polling starts on page load ----
  w.loadPage('pins', $('navpins'), true);
  await settle();
  check('page frame is in', !!$('pn_led') && !!$('pn_body'));
  check('polling starts on page load: one /pinstatus at once', polls() === 1, polls());
  check('next poll is armed', timers.length === 1 && timers[0].at - now === 5000, JSON.stringify(timers.map((t) => t.at - now)));
  check('board LED row shown, flash pressed', !$('pn_led').hidden && $('pn_led_flash').getAttribute('aria-pressed') === 'true' && $('pn_led_off').getAttribute('aria-pressed') === 'false');
  check('LED status text', $('pn_led_st').textContent === 'Now: flash', $('pn_led_st').textContent);
  check('charge LED row hidden when not fitted', $('pn_chg').hidden);
  check('MCP table shown with 16 rows', !$('pn_mcp').hidden && rows().length === 16, rows().length);
  check('user text goes to value, never into HTML (no <img>, no script run)', rows()[0].querySelector('input').value === 'Relay' && !w.document.querySelector('#pn_body img') && !w.pwned && rows()[1].querySelector('input').value.indexOf('<img') === 0);
  check('output pin: level button HIGH, pressed', rows()[0].querySelectorAll('button')[2].textContent === 'HIGH' && rows()[0].querySelectorAll('button')[2].getAttribute('aria-pressed') === 'true');
  check('input pin: level is text, level button hidden', rows()[1].querySelectorAll('td')[2].querySelector('span').textContent === 'HIGH' && rows()[1].querySelectorAll('button')[2].hidden);
  check('every request carries X-MC (wrapper of mc.js)', calls.length > 0 && calls.every((c) => c.xmc === '1'), JSON.stringify(calls.filter((c) => c.xmc !== '1')));

  // ---- pin assignments ----
  check('assign: table has caption "Pin assignments"', />Pin assignments<\/caption>/.test(html));
  check('assign: one /pinassign request per poll', assigns() === 1, assigns());
  check('assign: chained, one connection at a time (every /pinassign follows a /pinstatus)', calls.filter((c) => c.url === '/pinstatus' || c.url === '/pinassign').every((c, i) => c.url === (i % 2 ? '/pinassign' : '/pinstatus')));
  check('assign: section shown with the four rows', !$('pn_asg').hidden && arows().length === 4, arows().length);
  const lab = [...arows()].map((r) => r.querySelector('th').textContent);
  check('assign: rows 1-Wire, Button, Analog, Anemometer', lab.join('|') === '1-Wire|Button|Analog|Anemometer', lab.join('|'));
  check('assign: pin input, board default and pin in use shown', arows()[0].querySelector('input').value === '0' && arows()[0].querySelectorAll('td')[1].textContent === '4' && arows()[0].querySelectorAll('td')[2].textContent === '4' && arows()[1].querySelector('input').value === '38');
  check('assign: analog stored 0 is shown as unset, not as pin 0', arows()[2].querySelector('input').value === '' && /unset/.test(arows()[2].querySelector('input').placeholder), JSON.stringify(arows()[2].querySelector('input').value));
  check('assign: restart need per field (button restarts, 1-Wire applies at once)', /after a restart/.test(arows()[1].querySelectorAll('td')[3].textContent) && /at once/.test(arows()[0].querySelectorAll('td')[3].textContent) && /at once/.test(arows()[2].querySelectorAll('td')[3].textContent), arows()[1].querySelectorAll('td')[3].textContent);
  check('assign: function not on this board says so, read-only', /not on this board/.test(arows()[3].querySelectorAll('td')[3].textContent) && arows()[3].querySelector('input').disabled && arows()[3].querySelector('button').hidden);
  check('assign: available function is editable', !arows()[0].querySelector('input').disabled && !arows()[0].querySelector('button').hidden);
  check('assign: inputs and buttons are labelled', arows()[0].querySelector('input').getAttribute('aria-label') === 'Pin of 1-Wire' && arows()[2].querySelector('button').getAttribute('aria-label') === 'Set the pin of Analog');
  // ---- the timer polls again; the table is updated in place ----
  const r0 = rows()[0];
  pinStatus.mcp.pins[0].l = 0; pinStatus.mcp.pins[3].d = 'out';
  runTimers(5000); await settle();
  check('timer: second poll after 5 s', polls() === 2, polls());
  check('rows are updated in place, not rebuilt', rows()[0] === r0 && rows().length === 16);
  check('level change shown', rows()[0].querySelectorAll('button')[2].textContent === 'LOW' && rows()[0].querySelectorAll('button')[2].getAttribute('aria-pressed') === 'false');
  check('direction change shown', rows()[3].querySelectorAll('button')[1].textContent === 'out' && !rows()[3].querySelectorAll('button')[2].hidden);

  // ---- a failed poll keeps the table and says so ----
  fail = 'net';
  const aBefore = assigns();
  runTimers(5000); await settle();
  check('failed poll: no /pinassign request after a failed /pinstatus', assigns() === aBefore, assigns() + ' vs ' + aBefore);
  check('failed poll: table kept', rows().length === 16 && rows()[0].querySelector('input').value === 'Relay');
  check('failed poll: stale note shown', /Stale/.test($('pn_note').textContent), $('pn_note').textContent);
  check('failed poll: polling goes on', polls() === 3 && timers.length === 1, polls() + '/' + timers.length);
  fail = 500;
  runTimers(5000); await settle();
  check('HTTP 500 also keeps the table and shows the note', rows().length === 16 && /Stale/.test($('pn_note').textContent));
  fail = null;
  runTimers(5000); await settle();
  check('recovery clears the stale note', $('pn_note').textContent === '', $('pn_note').textContent);

  // ---- pin assignments: writes, refusals, polling ----
  let an = calls.length;
  arows()[0].querySelector('input').value = '13'; arows()[0].querySelector('input').dispatchEvent(new w.Event('input'));
  arows()[0].querySelector('button').click(); await settle();
  check('assign: set sends setparam onewiregpio=13 with X-MC', calls[an] && calls[an].url === '/setparam/?onewiregpio=13' && calls[an].xmc === '1', JSON.stringify(calls[an]));
  check('assign: accepted write says "Saved." (the callback ran) and no refusal', /Saved\./.test(arows()[0].querySelectorAll('td')[3].textContent) && arows()[0].querySelectorAll('td')[3].textContent.indexOf('Refused') < 0, arows()[0].querySelectorAll('td')[3].textContent);
  an = calls.length;
  arows()[2].querySelector('input').value = '18'; arows()[2].querySelector('button').click(); await settle();
  check('assign: pin cleared in the ok mask is refused locally, no request', !calls.slice(an).some((x) => x.url.indexOf('/setparam/') === 0) && /Refused by pin policy/.test(arows()[2].querySelectorAll('td')[3].textContent), arows()[2].querySelectorAll('td')[3].textContent);
  an = calls.length;
  arows()[2].querySelector('input').value = 'abc'; arows()[2].querySelector('button').click(); await settle();
  check('assign: not a number: no request', !calls.slice(an).some((x) => x.url.indexOf('/setparam/') === 0));
  an = calls.length;
  arows()[2].querySelector('input').value = '17'; setStatus = 422; setReply = { returncode: 1, returnValue: '0' };
  arows()[2].querySelector('button').click(); await settle();
  check('assign: node answers returncode 1 (422): "Refused by pin policy" at that field', calls[an].url === '/setparam/?angpio=17' && /Refused by pin policy/.test(arows()[2].querySelectorAll('td')[3].textContent) && arows()[0].querySelectorAll('td')[3].textContent.indexOf('Refused') < 0, arows()[2].querySelectorAll('td')[3].textContent);
  check('assign: refused write shows exactly one message (the row text, not the page message as well)', $('pn_msg').textContent === '', $('pn_msg').textContent);
  setStatus = 200; setReply = { returncode: 0 };
  an = calls.length;
  arows()[2].querySelector('input').value = '34'; arows()[2].querySelector('button').click(); await settle();
  check('assign: next success replaces the field refusal by "Saved."', /Saved\./.test(arows()[2].querySelectorAll('td')[3].textContent) && arows()[2].querySelectorAll('td')[3].textContent.indexOf('Refused') < 0, arows()[2].querySelectorAll('td')[3].textContent);
  // button: 99 = no button is accepted (the setter takes it; it is not a bit of the mask); 99 elsewhere is not
  an = calls.length;
  arows()[1].querySelector('input').value = '99'; arows()[1].querySelector('button').click(); await settle();
  check('assign: button 99 (= no button) is sent', calls.slice(an).some((x) => x.url === '/setparam/?buttongpio=99') && /Saved\./.test(arows()[1].querySelectorAll('td')[3].textContent), arows()[1].querySelectorAll('td')[3].textContent);
  an = calls.length;
  arows()[0].querySelector('input').value = '99'; arows()[0].querySelector('button').click(); await settle();
  check('assign: 1-Wire 99 is refused locally, no request', !calls.slice(an).some((x) => x.url.indexOf('/setparam/') === 0) && /Refused by pin policy/.test(arows()[0].querySelectorAll('td')[3].textContent), arows()[0].querySelectorAll('td')[3].textContent);
  an = calls.length;
  arows()[2].querySelector('input').value = '0'; arows()[2].querySelector('button').click(); await settle();
  check('assign: analog 0 is refused locally (the setter takes no 0), no request', !calls.slice(an).some((x) => x.url.indexOf('/setparam/') === 0) && /Refused by pin policy/.test(arows()[2].querySelectorAll('td')[3].textContent), arows()[2].querySelectorAll('td')[3].textContent);
  an = calls.length;
  arows()[0].querySelector('input').value = '0'; arows()[0].querySelector('button').click(); await settle();
  check('assign: 1-Wire 0 (= board default) is sent', calls.slice(an).some((x) => x.url === '/setparam/?onewiregpio=0'));
  const hint = (i) => arows()[i].querySelectorAll('td')[3].textContent;
  check('assign: hints per field (0 = board default only where taken, 99 = no button for the button)', /0 = board default/.test(hint(0)) && /99 = no button/.test(hint(1)) && /0 = board default/.test(hint(1)) && !/0 = board default/.test(hint(2)) && !/99/.test(hint(0)), [0, 1, 2].map(hint).join(' | '));
  check('assign: the section hint no longer claims 0 for every field', !/Pin 0 = board default/.test($('pn_asg_hint').textContent), $('pn_asg_hint').textContent);
  pinAssign.fn[1].pin = 4; pinAssign.fn[1].eff = 4; pinAssign.fn[0].avail = 0;
  runTimers(5000); await settle();
  check('assign: poll refreshes values in place (4 rows kept)', arows().length === 4 && arows()[1].querySelector('input').value === '4' && arows()[1].querySelectorAll('td')[2].textContent === '4');
  check('assign: avail turning 0 locks the row', arows()[0].querySelector('input').disabled);
  pinAssign = freshAssign(); pinAssign.fn[2].pin = 99;
  runTimers(5000); await settle();
  check('assign: analog stored 99 (= unset) is shown as unset, not as 0 or 99', arows()[2].querySelector('input').value === '' && /unset/.test(arows()[2].querySelector('input').placeholder), JSON.stringify(arows()[2].querySelector('input').value));
  pinAssign = freshAssign();
  assignFail = 'net';
  runTimers(5000); await settle();
  check('assign: a failed /pinassign keeps the rows and polling goes on', arows().length === 4 && timers.length === 1);
  assignFail = null;
  runTimers(5000); await settle();
  check('assign: recovery re-reads', !arows()[0].querySelector('input').disabled);
  arows()[0].querySelector('input').value = '9'; arows()[0].querySelector('input').dispatchEvent(new w.Event('input'));
  runTimers(5000); await settle();
  check('assign: a pin being typed survives a poll', arows()[0].querySelector('input').value === '9');
  arows()[0].querySelector('input').removeAttribute('data-dirty');

  // ---- switching ----
  let n = calls.length;
  $('pn_led_steady').click(); await settle();
  let c = calls[n];
  check('LED steady: setparam boardled=steady with X-MC', c.url === '/setparam/?boardled=steady' && c.xmc === '1', JSON.stringify(c));
  check('LED switch is followed by an immediate poll', calls.slice(n + 1).some((x) => x.url === '/pinstatus'));
  n = calls.length;
  $('pn_led_off').click(); await settle();
  check('LED off: boardled=off', calls[n].url === '/setparam/?boardled=off');
  n = calls.length;
  $('pn_led_flash').click(); await settle();
  check('LED flash: boardled=flash', calls[n].url === '/setparam/?boardled=flash');

  pinStatus.mcp.pins[0].l = 1;
  runTimers(5000); await settle();
  n = calls.length;
  rows()[0].querySelectorAll('button')[2].click(); await settle();
  check('output HIGH -> click sends mcpoutA0=off with X-MC', calls[n].url === '/setparam/?mcpoutA0=off' && calls[n].xmc === '1', JSON.stringify(calls[n]));
  pinStatus.mcp.pins[0].l = 0;
  runTimers(5000); await settle();
  n = calls.length;
  rows()[0].querySelectorAll('button')[2].click(); await settle();
  check('output LOW -> click sends mcpoutA0=on', calls[n].url === '/setparam/?mcpoutA0=on', calls[n].url);
  n = calls.length;
  rows()[0].querySelectorAll('button')[1].click(); await settle();
  check('direction out -> click sends mcpioA0=in', calls[n].url === '/setparam/?mcpioA0=in' && calls[n].xmc === '1', calls[n].url);
  n = calls.length;
  rows()[1].querySelectorAll('button')[1].click(); await settle();
  check('direction in -> click sends mcpioA1=out', calls[n].url === '/setparam/?mcpioA1=out', calls[n].url);
  n = calls.length;
  rows()[8].querySelectorAll('button')[1].click(); await settle();
  check('B0 uses its own id', calls[n].url === '/setparam/?mcpioB0=out' || calls[n].url === '/setparam/?mcpioB0=in', calls[n].url);

  // name: typing survives a poll, Set sends the encoded name
  const inp = rows()[4].querySelector('input');
  inp.value = 'Lüfter "1"'; inp.dispatchEvent(new w.Event('input'));
  runTimers(5000); await settle();
  check('a name being typed is not overwritten by a poll', inp.value === 'Lüfter "1"', inp.value);
  n = calls.length;
  rows()[4].querySelectorAll('button')[0].click(); await settle();
  check('Set sends mcpnameA4 with the encoded name and X-MC', calls[n].url === '/setparam/?mcpnameA4=' + encodeURIComponent('Lüfter "1"') && calls[n].xmc === '1', calls[n].url);
  check('inputs are labelled', rows()[4].querySelector('input').getAttribute('aria-label') === 'Name of pin A4' && /Set the name of pin A4/.test(rows()[4].querySelectorAll('button')[0].getAttribute('aria-label')));

  // refusal and a lost request are shown, not swallowed
  setReply = { returncode: 1 };
  $('pn_led_steady').click(); await settle();
  check('refused change is shown', /refused/.test($('pn_msg').textContent), $('pn_msg').textContent);
  setReply = { returncode: 0 };
  $('pn_led_steady').click(); await settle();
  check('next success clears the message', $('pn_msg').textContent === '');

  // WEB-PIN-01: the page is STALE. It still shows A2 as an output (level button visible) while the node already has
  // the pin as an input (changed from the console or another browser, no poll in between). The node answers the
  // mcpoutA2 request with HTTP 422 and returncode 1 (web_functions.cpp: WS_RETURNCODE_FAIL -> 422). The page says so,
  // re-reads the true state at once and then shows A2 as an input without a level button.
  pinStatus.mcp.pins[2].d = 'out'; pinStatus.mcp.pins[2].l = 0;
  runTimers(5000); await settle();
  check('stale-page setup: A2 shows as output with a level button', !rows()[2].querySelectorAll('button')[2].hidden && rows()[2].querySelectorAll('button')[1].textContent === 'out');
  pinStatus.mcp.pins[2].d = 'in';          // the node changes behind the page's back: NO poll here
  setStatus = 422; setReply = { returncode: 1, returnValue: 'on' };
  n = calls.length;
  rows()[2].querySelectorAll('button')[2].click(); await settle();
  check('mcpout on a stale output row sends mcpoutA2=on', calls[n] && calls[n].url === '/setparam/?mcpoutA2=on' && calls[n].xmc === '1', JSON.stringify(calls[n]));
  check('mcpout refused (HTTP 422, pin is an input now): message shown', /refused/.test($('pn_msg').textContent), $('pn_msg').textContent);
  check('mcpout refused: true state re-read at once (no timer needed)', calls.slice(n + 1).some((x) => x.url === '/pinstatus'));
  check('mcpout refused: A2 now an input, level button hidden', rows()[2].querySelectorAll('button')[2].hidden && rows()[2].querySelectorAll('button')[1].textContent === 'in');
  setStatus = 200; setReply = { returncode: 0 };
  pinStatus.mcp.pins[2].d = 'out';
  runTimers(5000); await settle();

  // busy pad: buttons off, text says why; RM hold shown
  pinStatus.led = { fitted: 1, mode: 'steady', busy: 1, rm: 'flash', state: 'off' };
  runTimers(5000); await settle();
  check('busy pad: LED buttons disabled, reason and RM hold shown', $('pn_led_off').disabled && /pin busy/.test($('pn_led_st').textContent) && /remote command: flash/.test($('pn_led_st').textContent), $('pn_led_st').textContent);
  pinStatus.led = { fitted: 1, mode: 'steady', busy: 0, rm: '', state: 'steady' };
  pinStatus.chg = { fitted: 1, mode: 'auto', eff: 'off', rm: 1 };
  runTimers(5000); await settle();
  check('charge LED row shown with override', !$('pn_chg').hidden && /off \(remote override, console setting: auto\)/.test($('pn_chg_st').textContent), $('pn_chg_st').textContent);
  check('steady pressed, buttons enabled again', $('pn_led_steady').getAttribute('aria-pressed') === 'true' && !$('pn_led_off').disabled);

  // ---- charge LED: switchable through the existing chargeled setparam, only where the PMU has it ----
  check('charge LED fitted: auto pressed, off not (stored mode, not the RM override)', $('pn_chg_auto').getAttribute('aria-pressed') === 'true' && $('pn_chg_off').getAttribute('aria-pressed') === 'false');
  n = calls.length;
  $('pn_chg_off').click(); await settle();
  check('charge LED off: setparam chargeled=off with X-MC', calls[n].url === '/setparam/?chargeled=off' && calls[n].xmc === '1', JSON.stringify(calls[n]));
  check('charge LED switch is followed by an immediate poll', calls.slice(n + 1).some((x) => x.url === '/pinstatus'));
  n = calls.length;
  $('pn_chg_auto').click(); await settle();
  check('charge LED auto: setparam chargeled=on (on = follows charging)', calls[n].url === '/setparam/?chargeled=on', calls[n].url);
  setReply = { returncode: 1 };
  $('pn_chg_off').click(); await settle();
  check('charge LED refused change is shown', /refused/.test($('pn_msg').textContent), $('pn_msg').textContent);
  setReply = { returncode: 0 };
  pinStatus.chg = { fitted: 1, mode: 'off', eff: 'off', rm: 0 };
  runTimers(5000); await settle();
  check('charge LED off stored: off pressed, auto not', $('pn_chg_off').getAttribute('aria-pressed') === 'true' && $('pn_chg_auto').getAttribute('aria-pressed') === 'false');
  pinStatus.chg = { fitted: 0, mode: 'off', eff: 'off', rm: 0 };
  runTimers(5000); await settle();
  check('board without a PMU: charge LED row hidden', $('pn_chg').hidden);

  // ---- name fields: the store takes 15 BYTES, not 15 characters ----
  const nameIn = rows()[5].querySelector('input');
  nameIn.value = 'Lüfterxxxxxxxxx'; nameIn.dispatchEvent(new w.Event('input'));   // 15 characters, 16 bytes
  check('15 characters with an umlaut are cut to 14 (15 bytes max)', nameIn.value === 'Lüfterxxxxxxxx', nameIn.value);
  nameIn.value = 'ÄÄÄÄÄÄÄÄ'; nameIn.dispatchEvent(new w.Event('input'));   // 8 x 2 bytes
  check('8 two-byte characters are cut to 7', nameIn.value === 'ÄÄÄÄÄÄÄ', nameIn.value);
  nameIn.value = 'abcdefghijkl\u{1F600}'; nameIn.dispatchEvent(new w.Event('input'));   // 12 + 4 bytes: the pair goes whole
  check('a surrogate pair is cut whole, never half', nameIn.value === 'abcdefghijkl', JSON.stringify(nameIn.value));
  nameIn.value = 'abcdefghijk\u{1F600}'; nameIn.dispatchEvent(new w.Event('input'));   // 11 + 4 = 15 bytes: fits
  check('exactly 15 bytes stays', nameIn.value === 'abcdefghijk\u{1F600}', JSON.stringify(nameIn.value));
  nameIn.value = 'abcdefghijklmno'; nameIn.dispatchEvent(new w.Event('input'));
  check('15 ASCII characters stay', nameIn.value === 'abcdefghijklmno');
  nameIn.value = 'ÄÄÄÄÄÄÄÄ';   // set programmatically, no input event: Set must still send at most 15 bytes
  n = calls.length;
  rows()[5].querySelectorAll('button')[0].click(); await settle();
  check('Set sends at most 15 bytes', calls[n].url === '/setparam/?mcpnameA5=' + encodeURIComponent('ÄÄÄÄÄÄÄ'), calls[n].url);

  // ---- free GPIO (GET /gpiostatus): chained after /pinassign, read-only master, writes through setparam gpio<N> ----
  check('gpio: avail 0 -> section hidden, no rows', $('pn_gpio').hidden && grows().length === 0 && $('pn_gpio_off').hidden && $('pn_gpioctl').hidden);
  check('gpio: /gpiostatus chained after /pinassign (one connection at a time)', (() => { const seq = calls.filter((c) => /^\/(pinstatus|pinassign|gpiostatus)$/.test(c.url)); return seq.some((c) => c.url === '/gpiostatus') && seq.every((c, i) => c.url !== '/gpiostatus' || (i > 0 && seq[i - 1].url === '/pinassign')); })());
  check('gpio: frame has the "Free GPIO" caption and the master-off note text', />Free GPIO<\/caption>/.test(html) && /GPIO control is off\. Turn it on above to switch the pins; every pin returns to input after a restart\./.test(html) && !/--gpiocontrol/.test(html) && /id="pn_gpioctl"/.test(html));
  gpioStatus = { avail: 1, master: 0, pins: [ { pin: 13, own: '', dir: '-', lvl: -1 }, { pin: 14, own: '', dir: '-', lvl: -1 }, { pin: 4, own: 'ow', dir: '-', lvl: -1 } ] };
  let gn = gpios();
  runTimers(5000); await settle();
  check('gpio: one /gpiostatus per poll', gpios() === gn + 1, gpios() + ' vs ' + gn);
  check('gpio: section shown with three rows', !$('pn_gpio').hidden && grows().length === 3, grows().length);
  check('gpio: master off -> note shown, rows listed, no switch button visible', !$('pn_gpio_off').hidden && [...grows()].every((r) => [...r.querySelectorAll('button')].every((b) => b.hidden)));
  check('gpio: owner shown as name, free pin as "free"', grows()[2].querySelectorAll('td')[0].textContent === '1-Wire' && grows()[0].querySelectorAll('td')[0].textContent === 'free', grows()[2].querySelectorAll('td')[0].textContent);
  check('gpio: "No switchable outputs" stays hidden', $('pn_none').hidden);
  check('gpioctl: avail 1, master off -> switch shown, "off" active, "on" not', !$('pn_gpioctl').hidden && $('pn_gpioctl_off').getAttribute('aria-pressed') === 'true' && $('pn_gpioctl_on').getAttribute('aria-pressed') === 'false');
  gn = calls.length;
  $('pn_gpioctl_on').click(); await settle();
  check('gpioctl: click on sends /setparam/?gpioctl=on with X-MC', calls[gn] && calls[gn].url === '/setparam/?gpioctl=on' && calls[gn].xmc === '1', JSON.stringify(calls[gn]));
  check('gpioctl: followed by an immediate /gpiostatus re-poll', calls.slice(gn + 1).some((x) => x.url === '/gpiostatus'));
  setStatus = 422; setReply = { returncode: 1 };
  $('pn_gpioctl_on').click(); await settle();
  check('gpioctl: 422 shows a refusal', /refused/.test($('pn_msg').textContent), $('pn_msg').textContent);
  setStatus = 200; setReply = { returncode: 0 };
  $('pn_gpioctl_off').click(); await settle();
  check('gpioctl: next success clears the refusal and sends gpioctl=off', $('pn_msg').textContent === '' && calls.some((x) => x.url === '/setparam/?gpioctl=off'), $('pn_msg').textContent);
  gpioStatus.master = 1;
  gpioStatus.pins[1] = { pin: 14, own: '', dir: 'out', lvl: 1 };
  const g0 = grows()[0];
  runTimers(5000); await settle();
  check('gpio: master on -> note hidden', $('pn_gpio_off').hidden);
  check('gpioctl: master on -> "on" active, "off" not', $('pn_gpioctl_on').getAttribute('aria-pressed') === 'true' && $('pn_gpioctl_off').getAttribute('aria-pressed') === 'false' && !$('pn_gpioctl').hidden);
  check('gpio: free rows show 4 buttons on/off/in/release, the owned row none', [...grows()[0].querySelectorAll('button')].map((b) => b.textContent + (b.hidden ? '!' : '')).join(',') === 'on,off,in,release' && [...grows()[2].querySelectorAll('button')].every((b) => b.hidden));
  check('gpio: rows updated in place; direction and level shown; pressed state', grows()[0] === g0 && grows()[1].querySelectorAll('td')[1].textContent === 'out' && grows()[1].querySelectorAll('td')[2].textContent === 'HIGH' && grows()[1].querySelectorAll('button')[0].getAttribute('aria-pressed') === 'true' && grows()[0].querySelectorAll('td')[2].textContent === '-');
  check('gpio: buttons are labelled', grows()[0].querySelectorAll('button')[0].getAttribute('aria-label') === 'GPIO 13 on');
  gn = calls.length;
  grows()[0].querySelectorAll('button')[0].click(); await settle();
  check('gpio: switch sends /setparam/?gpio13=on with X-MC', calls[gn] && calls[gn].url === '/setparam/?gpio13=on' && calls[gn].xmc === '1', JSON.stringify(calls[gn]));
  check('gpio: switch is followed by an immediate poll incl. /gpiostatus', calls.slice(gn + 1).some((x) => x.url === '/gpiostatus'));
  gn = calls.length;
  grows()[1].querySelectorAll('button')[3].click(); await settle();
  check('gpio: release sends gpio14=release', calls[gn].url === '/setparam/?gpio14=release', calls[gn].url);
  setStatus = 422; setReply = { returncode: 1 };
  gn = calls.length;
  grows()[0].querySelectorAll('button')[1].click(); await settle();
  check('gpio: 422 shows the refusal at that row only', calls[gn].url === '/setparam/?gpio13=off' && /Refused/.test(grows()[0].querySelectorAll('td')[3].textContent) && grows()[1].querySelectorAll('td')[3].textContent.indexOf('Refused') < 0, grows()[0].querySelectorAll('td')[3].textContent);
  setStatus = 200; setReply = { returncode: 0 };
  grows()[0].querySelectorAll('button')[1].click(); await settle();
  check('gpio: next success clears the row refusal', grows()[0].querySelectorAll('td')[3].textContent.indexOf('Refused') < 0, grows()[0].querySelectorAll('td')[3].textContent);
  gpioFail = 'net';
  gn = gpios();
  runTimers(5000); await settle();
  check('gpio: a failed /gpiostatus keeps the rows and polling goes on', gpios() === gn + 1 && grows().length === 3 && !$('pn_gpio').hidden && timers.length === 1, gpios() + '/' + grows().length + '/' + timers.length);
  gpioFail = null;
  gpioStatus.pins = [ { pin: 13, own: '', dir: 'in', lvl: 0 } ];
  runTimers(5000); await settle();
  check('gpio: a pin that is gone loses its row', grows().length === 1 && grows()[0].querySelectorAll('td')[1].textContent === 'in');
  gpioStatus = { avail: 0, master: 0, pins: [] };
  runTimers(5000); await settle();
  check('gpio: avail 0 again hides the section', $('pn_gpio').hidden && grows().length === 0);
  runTimers(5000); await settle();

  // ---- polling stops on leave ----
  const before = polls(), beforeA = assigns(), beforeG = gpios();
  w.loadPage('info', $('navinfo'), true);
  await settle();
  check('leave: no timer left', timers.length === 0, timers.length);
  runTimers(30000); await settle();
  check('leave: no /pinstatus after the page is gone', polls() === before, polls() + ' vs ' + before);
  check('leave: no /pinassign after the page is gone', assigns() === beforeA, assigns() + ' vs ' + beforeA);
  check('leave: no /gpiostatus after the page is gone', gpios() === beforeG, gpios() + ' vs ' + beforeG);
  // a late answer that was in flight at leave time must not revive polling either
  w.loadPage('pins', $('navpins'), true); await settle();
  w.loadPage('info', $('navinfo'), true); await settle();
  const b2 = polls(); runTimers(30000); await settle();
  check('enter + leave quickly: still silent', polls() === b2 && timers.length === 0);

  // ---- no MCP: no rows ----
  pinStatus = freshStatus();
  pinStatus.mcp = { present: 0, pins: [] };
  w.loadPage('pins', $('navpins'), true); await settle();
  check('no MCP: table hidden, no rows', $('pn_mcp').hidden && rows().length === 0, rows().length);
  check('no MCP: board LED still switchable', !$('pn_led').hidden);
  check('no MCP: no "none" notice while the LED exists', $('pn_none').hidden);
  pinStatus.led.fitted = 0;
  runTimers(5000); await settle();
  check('no LED/MCP but assignment rows: the notice stays hidden (it would contradict the table)', $('pn_led').hidden && $('pn_none').hidden && !$('pn_asg').hidden && arows().length === 4, 'none.hidden=' + $('pn_none').hidden);
  pinAssign.fn.forEach((f) => { f.avail = 0; });
  runTimers(5000); await settle();
  check('nothing fitted and no assignment available: notice shown', !$('pn_none').hidden && /switchable outputs/.test($('pn_none').textContent), $('pn_none').textContent);
  pinAssign = freshAssign();
  runTimers(5000); await settle();
  check('an assignment becomes available: notice hidden again', $('pn_none').hidden);
  pinAssign.fn.forEach((f) => { f.avail = 0; });
  runTimers(5000); await settle();
  assignFail = 'net'; pinAssign = freshAssign();
  w.loadPage('info', $('navinfo'), true); await settle();
  w.loadPage('pins', $('navpins'), true); await settle();
  check('/pinassign fails on a node with nothing fitted: notice shown (decided without it)', !$('pn_none').hidden && $('pn_asg').hidden);
  assignFail = null; pinAssign = freshAssign();
  w.loadPage('info', $('navinfo'), true); await settle();

  // ---- session expired: HTTP 401 on /pinstatus stops the polling and says why ----
  pinStatus = freshStatus();
  w.loadPage('pins', $('navpins'), true); await settle();
  check('401: page is polling before the session ends', timers.length === 1 && rows().length === 16);
  fail = 401;
  runTimers(5000); await settle();
  check('401: login note shown', /log ?in/i.test($('pn_note').textContent) && !/Stale/.test($('pn_note').textContent), $('pn_note').textContent);
  check('401: polling stopped, no timer armed', timers.length === 0, timers.length);
  const b401 = polls(); runTimers(60000); await settle();
  check('401: no further /pinstatus', polls() === b401, polls() + ' vs ' + b401);
  check('401: the last table is kept', rows().length === 16);
  fail = null;
  w.loadPage('info', $('navinfo'), true); await settle();
  w.loadPage('pins', $('navpins'), true); await settle();
  check('401: opening the page again polls again and clears the note', polls() > b401 && $('pn_note').textContent === '' && timers.length === 1, $('pn_note').textContent);
  setStatus = 401;
  n = calls.length;
  $('pn_led_steady').click(); await settle();
  check('401 on a switch: login note, not "request failed"', /log ?in/i.test($('pn_msg').textContent), $('pn_msg').textContent);
  setStatus = 200;
  w.loadPage('info', $('navinfo'), true); await settle();

  console.log(failures ? failures + ' FAILED' : 'ALL PASS');
  process.exit(failures ? 1 : 0);
})().catch((e) => { console.log('ERROR ' + (e && e.stack || e)); process.exit(2); });

// WEB-LOOK-01 + WEB-TD-01: setup page look controls (web page theme / font size, T-Deck font / display / brightness), offline jsdom test.
// Markup: printf formats between WEBLOOK-HTML-BEGIN/END and TD2-HTML-BEGIN/END in sub_page_setup() (src/web_functions/web_functions.cpp).
// JS: blocks WEBLOOK-JS and TD2-JS in src/web_functions/web/mc.js. Handlers: setparam tdchatfont / tdbldisp / tdblbright.
// Run:   NODE_PATH=<dir>/node_modules node tools/webgui_look_test.js
// Mutation check (must FAIL):  LOOK_MUTATE=key|attr|param|slot|refresh|clkload|clkguard|clkcall node tools/webgui_look_test.js
const fs = require('fs');
const path = require('path');
const { JSDOM, VirtualConsole } = require('jsdom');

let WF = fs.readFileSync(path.join(__dirname, '..', 'src', 'web_functions', 'web_functions.cpp'), 'utf8');
const MC = fs.readFileSync(path.join(__dirname, '..', 'src', 'web_functions', 'web', 'mc.js'), 'utf8');
const MUT = process.env.LOOK_MUTATE || '';
let failures = 0;
function check(name, cond, detail) {
  console.log((cond ? 'PASS ' : 'FAIL ') + name + (detail !== undefined && !cond ? '  [' + detail + ']' : ''));
  if (!cond) failures++;
}
function between(src, tag) {
  const a = src.indexOf(tag + '-BEGIN'), b = src.indexOf(tag + '-END');
  if (a < 0 || b < a) throw new Error('marker ' + tag + ' not found');
  return src.slice(src.indexOf('\n', a), b);
}
function mutate(t, from, to) { if (!t.includes(from)) throw new Error('mutation anchor not found: ' + from); return t.replace(from, to); }
const stripC = (t) => t.split('\n').filter((l) => !/^\s*\/\//.test(l)).join('\n');

let lookJs = stripC(between(MC, 'WEBLOOK-JS'));
let tdJs = stripC(between(MC, 'TD2-JS'));
if (MUT === 'key') lookJs = mutate(lookJs, "'mcTheme'", "'mcThem'");
if (MUT === 'attr') lookJs = mutate(lookJs, "'data-theme'", "'data-thm'");
if (MUT === 'param') tdJs = mutate(tdJs, 'tdchatfont', 'tdfont');
if (MUT === 'slot') tdJs = mutate(tdJs, "'tdblbright'", "'tdblight'");
if (MUT === 'refresh') WF = mutate(WF, 'tdeck_refresh_SET_view(); // the device', '// the device');
if (MUT === 'clkcall') WF = mutate(WF, "if(typeof mcClockTick=='function')mcClockTick();", '');
if (MUT) console.log('NOTE mutation active: ' + MUT);

// ---- markup: reproduce the printf output from the C++ source --------------------------------------------------
const lit = (s) => s.replace(/\\(["\\])/g, '$1').replace(/\\n/g, '\n');
function renderBlock(tag, subst) {
  const body = between(WF, tag);
  const out = [];
  for (const m of body.matchAll(/(println|printf)\("((?:[^"\\]|\\.)*)"/g)) {
    let t = lit(m[2]);
    if (m[1] === 'printf') t = subst(t);
    out.push(t);
  }
  return out;
}
// the loops print 3 buttons from one format: expand by hand with the name arrays of the block
function webLookHtml() {
  const lines = renderBlock('WEBLOOK-HTML', (t) => t);
  const names = [['auto', 'light', 'dark'], ['s', 'm', 'l']];
  let k = 0, html = '';
  for (const t of lines) {
    if (t.includes('%s')) { for (const n of names[k]) html += t.replace(/%s/g, n); k++; } else html += t;
  }
  return html;
}
function tdHtml(font, disp, level) {
  const lines = renderBlock('TD2-HTML', (t) => t);
  const sets = [['s', 'm', 'l', font], ['auto', 'light', 'dark', disp]];
  let k = 0, html = '';
  for (const t of lines) {
    if (t.includes('%s')) {
      const [a, b, c, cur] = sets[k++];
      for (const n of [a, b, c]) { const on = n === cur; html += t.replace('%s', on ? ' mctab-on' : '').replace('%s', n).replace('%s', on ? 'true' : 'false').replace('%s', n).replace('%s', n); }
    } else html += t.replace(/%d/g, String(level));
  }
  return html;
}

let calls = [], reply = { returncode: 0 }, alerts = [];
function mount(html, jsList, store) {
  const vc = new VirtualConsole();
  vc.on('jsdomError', (e) => { console.log('JSDOM ERROR ' + e.message); failures++; });
  const dom = new JSDOM('<!doctype html><html><body><div id="c">' + html + '</div></body></html>', { runScripts: 'dangerously', virtualConsole: vc, url: 'http://node.local/' });
  const w = dom.window;
  if (store) for (const [k, v] of Object.entries(store)) w.localStorage.setItem(k, v);
  w.fetch = (url) => { calls.push(url); return Promise.resolve({ status: 200, json: () => Promise.resolve(reply) }); };
  w.alert = (m) => alerts.push(m);
  for (const js of jsList) w.eval(js);
  return w;
}
const settle = async () => { for (let i = 0; i < 6; i++) await new Promise((r) => setImmediate(r)); };
const pr = (w, ids) => ids.map((i) => w.document.getElementById(i).getAttribute('aria-pressed')).join();
const attr = (w, a) => w.document.documentElement.getAttribute(a);

(async () => {
  // ---- web page look ----
  const html = webLookHtml();
  const th = ['weblook_theme_auto', 'weblook_theme_light', 'weblook_theme_dark'], fo = ['weblook_font_s', 'weblook_font_m', 'weblook_font_l'];
  let w = mount(html, [lookJs]);
  check('default: no data-theme, no data-font', attr(w, 'data-theme') === null && attr(w, 'data-font') === null);
  w.mcLookMark();
  check('default marks Auto and M', pr(w, th) === 'true,false,false' && pr(w, fo) === 'false,true,false', pr(w, th) + ' ' + pr(w, fo));
  w.document.getElementById('weblook_theme_dark').click();
  check('Dark click sets data-theme=dark and stores it', attr(w, 'data-theme') === 'dark' && w.localStorage.getItem('mcTheme') === 'dark');
  check('Dark click marks Dark', pr(w, th) === 'false,false,true', pr(w, th));
  w.document.getElementById('weblook_theme_light').click();
  check('Light click sets data-theme=light', attr(w, 'data-theme') === 'light' && w.localStorage.getItem('mcTheme') === 'light');
  w.document.getElementById('weblook_theme_auto').click();
  check('Auto click removes data-theme and the stored key', attr(w, 'data-theme') === null && w.localStorage.getItem('mcTheme') === null);
  w.document.getElementById('weblook_font_l').click();
  check('Font L sets data-font=l', attr(w, 'data-font') === 'l' && w.localStorage.getItem('mcFont') === 'l' && pr(w, fo) === 'false,false,true');
  w.document.getElementById('weblook_font_s').click();
  check('Font S sets data-font=s', attr(w, 'data-font') === 's');
  w.document.getElementById('weblook_font_m').click();
  check('Font M removes data-font', attr(w, 'data-font') === null && w.localStorage.getItem('mcFont') === null);
  check('the look controls send nothing to the node', calls.length === 0, calls.join());
  w = mount(html, [lookJs], { mcTheme: 'dark', mcFont: 'l' });
  check('stored choice is applied at script load (before first paint)', attr(w, 'data-theme') === 'dark' && attr(w, 'data-font') === 'l');
  w = mount(html, [lookJs], { mcTheme: 'purple', mcFont: 'xxl' });
  check('an unknown stored value is ignored', attr(w, 'data-theme') === null && attr(w, 'data-font') === null);
  // storage that throws (privacy mode): no exception, attribute still follows the click
  w = mount(html, [lookJs.replace(/^/, '')]);
  Object.defineProperty(w, 'localStorage', { get() { throw new Error('denied'); } });
  let threw = false;
  try { w.mcLookSet('mcTheme', 'dark'); w.mcLookApply(); w.mcLookMark(); } catch (e) { threw = true; }
  check('blocked localStorage does not throw', !threw);

  // ---- T-Deck controls ----
  const td = tdHtml('m', 'light', 12);
  const fids = ['tdfont_s', 'tdfont_m', 'tdfont_l'], dids = ['tddisp_auto', 'tddisp_light', 'tddisp_dark'];
  w = mount(td, [tdJs]);
  check('T-Deck section renders stored font m / display light / level 12', pr(w, fids) === 'false,true,false' && pr(w, dids) === 'false,true,false' && w.document.getElementById('tdbright').value === '12' && w.document.getElementById('tdbright_val').textContent === '12');
  check('slider range 1..16', w.document.getElementById('tdbright').min === '1' && w.document.getElementById('tdbright').max === '16');
  const order = [...w.document.querySelectorAll('[id$="_lbl"], label[for]')].map((e) => e.id || e.getAttribute('for'));
  check('order: font, display, brightness', order.join() === 'tdfont_lbl,tddisp_lbl,tdbright', order.join());
  calls = [];
  w.document.getElementById('tdfont_l').click(); await settle();
  check('font l -> GET /setparam/?tdchatfont=l and marks l', calls[0] === '/setparam/?tdchatfont=l' && pr(w, fids) === 'false,false,true', calls.join() + ' ' + pr(w, fids));
  calls = []; reply = { returncode: 0, tdbldisp: 'dark:4' };
  w.document.getElementById('tddisp_dark').click(); await settle();
  check('display dark -> GET /setparam/?tdbldisp=dark, marks dark, slider follows the answer (4)', calls[0] === '/setparam/?tdbldisp=dark' && pr(w, dids) === 'false,false,true' && w.document.getElementById('tdbright').value === '4' && w.document.getElementById('tdbright_val').textContent === '4', calls.join() + ' ' + w.document.getElementById('tdbright').value);
  calls = []; reply = { returncode: 0 };
  const s = w.document.getElementById('tdbright'); s.value = '9';
  s.dispatchEvent(new w.Event('change')); await settle();
  check('slider change -> GET /setparam/?tdblbright=9', calls[0] === '/setparam/?tdblbright=9', calls.join());
  calls = []; alerts = []; reply = { returncode: 1 };
  w.document.getElementById('tdfont_s').click(); await settle();
  check('refused font keeps the old marking and alerts', pr(w, fids) === 'false,false,true' && alerts.length === 1, pr(w, fids) + ' ' + alerts.length);

  // ---- C++ wiring ----
  const GUARD = '#if defined\\(BOARD_T_DECK\\) \\|\\| defined\\(BOARD_T_DECK_PLUS\\)';
  check('T-Deck controls only on T-Deck builds', new RegExp(GUARD + '[\\s\\S]*TD-THEME-01 W3[\\s\\S]*TD2-HTML-BEGIN[\\s\\S]*TD2-HTML-END\\s*\\}\\n#endif').test(WF));
  check('web look controls are not under the T-Deck guard', !new RegExp(GUARD + '(?:(?!#endif)[\\s\\S])*WEBLOOK-HTML-BEGIN').test(WF));
  check('setparam tdchatfont -> --chatfont through commandAction', /equals\("tdchatfont"\)[\s\S]{0,400}"--chatfont %s"[\s\S]{0,120}commandAction\(/.test(WF));
  check('setparam tdbldisp -> --bl mode through commandAction (light=day, dark=night)', /equals\("tdbldisp"\)[\s\S]{0,500}"light"\) == 0 \? "day"[\s\S]{0,60}"dark"\) == 0 \? "night"[\s\S]{0,300}"--bl mode %s"[\s\S]{0,120}commandAction\(/.test(WF));
  check('setparam tdblbright edits the active slot (device slider call), range-checked 1..16, then refreshes the device setup view', /equals\("tdblbright"\)[\s\S]{0,500}lv >= 1 && lv <= BRIGHTNESS_STEPS[\s\S]{0,100}brightnessSetActiveLevel\([\s\S]{0,60}tdeck_refresh_SET_view\(\);/.test(WF));
  check('Display segments follow brightness_mode_to_seg (auto, light=day, dark=night)', /dispCur = \(int\)brightness_mode_to_seg\(brightnessMode\(\)\)/.test(WF));
  check('setup page marks the look buttons after load', /page=='setup'&&typeof mcLookMark=='function'\)mcLookMark\(\);/.test(WF));
  // ---- ticking page time (item 6) ----
  {
    let clk = stripC(between(MC, 'CLOCK-JS')).replace('setInterval(mcClockTick,1000);', '');
    if (MUT === 'clkload') clk = mutate(clk, "document.addEventListener('DOMContentLoaded',mcClockTick);", '');
    if (MUT === 'clkguard') clk = mutate(clk, '>2000', '>2000000000');
    let nowMs = 5000, skew = 0;
    const mk = (epoch) => {
      const w2 = mount('<div id="content_title"><p><span' + (epoch ? ' id="mc_clock" data-epoch="' + epoch + '"' : '') + '>2026-11-08&nbsp;11:43:16</span>&nbsp;CET&nbsp;[GPS]</p></div>', [clk]);
      w2.performance.now = () => nowMs;
      w2.Date.now = () => 1.7e12 + nowMs + skew; // PC clock: follows performance.now unless a sleep skews it
      w2.cpage = 'setup';
      return w2;
    };
    const txt = (w2) => w2.document.querySelector('#content_title p').textContent.replace(/\u00a0/g, ' ');
    const E = Date.UTC(2026, 10, 8, 11, 43, 16) / 1000;
    let w2 = mk(E);
    w2.mcClockTick();
    check('first tick keeps the rendered time and the tag', txt(w2) === '2026-11-08 11:43:16 CET [GPS]', txt(w2));
    nowMs += 1000; w2.mcClockTick();
    check('+1 s per tick', txt(w2) === '2026-11-08 11:43:17 CET [GPS]', txt(w2));
    nowMs += 1000; w2.mcClockTick();
    check('+2 s', txt(w2) === '2026-11-08 11:43:18 CET [GPS]', txt(w2));
    nowMs += 42000; w2.mcClockTick();
    check('minute rollover 11:43:59 -> 11:44:00', (() => { nowMs += 0; return true; })() && txt(w2) === '2026-11-08 11:44:00 CET [GPS]', txt(w2));
    nowMs = 5000;
    w2 = mk(Date.UTC(2026, 10, 8, 23, 59, 59) / 1000); w2.mcClockTick();
    nowMs += 1000; w2.mcClockTick();
    check('day rollover 23:59:59 -> next day 00:00:00', txt(w2) === '2026-11-09 00:00:00 CET [GPS]', txt(w2));
    nowMs = 5000;
    w2 = mk(Date.UTC(2028, 1, 28, 23, 59, 59) / 1000); w2.mcClockTick(); nowMs += 1000; w2.mcClockTick();
    check('leap-year day rollover 2028-02-28 -> 02-29', txt(w2) === '2028-02-29 00:00:00 CET [GPS]', txt(w2));
    nowMs += 3000; w2.mcClockTick();
    check('sub-second jitter does not skip: +3 s later reads 00:00:03', txt(w2) === '2028-02-29 00:00:03 CET [GPS]', txt(w2));
    w2.document.getElementById('mc_clock').setAttribute('data-epoch', String(E)); w2.document.getElementById('mc_clock').textContent = 'x';
    w2.mcClockTick();
    check('a fresh render (new data-epoch) re-syncs', txt(w2) === '2026-11-08 11:43:16 CET [GPS]', txt(w2));
    w2 = mk(0); nowMs += 5000; w2.mcClockTick();
    check('no valid time (no #mc_clock): text stays as rendered', txt(w2) === '2026-11-08 11:43:16 CET [GPS]', txt(w2));
    check('C++ emits id/data-epoch only for a valid time (cetEpochFromFields)', /cetEpochFromFields\([\s\S]{0,400}if \(clkOk\)[\s\S]{0,120}data-epoch/.test(WF));
    check('mc.js ticks every 1000 ms; the shown time never comes from the PC clock (Date.now only in the sleep guard)', /setInterval\(mcClockTick,1000\)/.test(MC) && !/new Date\(\)/.test(between(MC, 'CLOCK-JS')) && !/Date\.now/.test(between(MC, 'CLOCK-JS').split('function mcClockText')[1].split('function mcClockStale')[0]));
    check('loadPage ticks the clock right after the page HTML is set', /innerHTML=this\.responseText;if\(typeof mcLoginFocus=='function'\)mcLoginFocus\(\);if\(typeof mcClockTick=='function'\)mcClockTick\(\);/.test(WF));
    // item 3: the sync point is the render, not the first interval tick (up to 1 s later)
    nowMs = 5000;
    w2 = mk(E); w2.document.dispatchEvent(new w2.Event('DOMContentLoaded'));
    nowMs += 1000; w2.mcClockTick();
    check('sync at load: 1 s after the render the clock reads :17 (not lagging by the first-tick delay)', txt(w2) === '2026-11-08 11:43:17 CET [GPS]', txt(w2));
    // item 4: sleep guard
    nowMs = 5000; skew = 0;
    w2 = mk(E); let fetched = [], body = '';
    w2.fetch = (u) => { fetched.push(u); return Promise.resolve({ text: () => Promise.resolve(body) }); };
    w2.mcClockTick(); nowMs += 3000; skew = 1500; w2.mcClockTick();
    check('PC/performance clock 1.5 s apart: no false alarm, keeps ticking', txt(w2) === '2026-11-08 11:43:19 CET [GPS]' && fetched.length === 0, txt(w2) + ' ' + fetched.join());
    skew = 3600000; nowMs += 1000; w2.mcClockTick(); await settle();
    check('after a sleep (performance.now paused 1 h): text is not advanced, dimmed, one re-sync fetch of the info page (never the current page: ?page=spectrum would start a radio scan)', txt(w2) === '2026-11-08 11:43:19 CET [GPS]' && w2.document.getElementById('mc_clock').style.opacity === '0.5' && fetched.join() === '?page=info', txt(w2) + ' ' + fetched.join());
    nowMs += 1000; w2.mcClockTick(); await settle();
    check('node did not deliver a clock: stays stopped and dimmed, no fetch storm', txt(w2) === '2026-11-08 11:43:19 CET [GPS]' && fetched.length === 1, txt(w2) + ' ' + fetched.length);
    w2.document.dispatchEvent(new w2.Event('visibilitychange')); await settle();
    check('visibilitychange while stopped does not refetch', fetched.length === 1);
    nowMs = 5000; skew = 0;
    w2 = mk(E); fetched = []; const E2 = E + 3600;
    w2.fetch = (u) => { fetched.push(u); return Promise.resolve({ text: () => Promise.resolve('<div id="content_title"><p><span id="mc_clock" data-epoch="' + E2 + '">x</span></p></div>') }); };
    w2.mcClockTick(); nowMs += 2000; skew = 7200000;
    w2.document.dispatchEvent(new w2.Event('visibilitychange')); await settle();
    check('visibilitychange after a sleep re-syncs from the fresh render: node time +1 h, no dimming', txt(w2) === '2026-11-08 12:43:16 CET [GPS]' && w2.document.getElementById('mc_clock').style.opacity === '' && fetched.length === 1, txt(w2) + ' ' + fetched.join());
    nowMs += 1000; w2.mcClockTick();
    check('and keeps ticking from the new base', txt(w2) === '2026-11-08 12:43:17 CET [GPS]', txt(w2));
    skew = 0;
  }
  console.log(failures ? failures + ' FAILED' : 'all passed');
  process.exit(failures ? 1 : 0);
})();

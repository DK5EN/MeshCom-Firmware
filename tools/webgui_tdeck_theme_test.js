// TD-THEME-01 W3: Settings page "T-Deck display theme" control, offline jsdom test.
// Markup: printf format between TD-HTML-BEGIN/END in sub_page_setup() (src/web_functions/web_functions.cpp), rendered per stored value.
// JS: block between TD-JS-BEGIN/END in src/web_functions/web/mc.js, run against a fake fetch for /setparam/.
// Run:   NODE_PATH=<dir>/node_modules node tools/webgui_tdeck_theme_test.js
// Mutation check (must FAIL):  TDTHEME_MUTATE=param|noerr node tools/webgui_tdeck_theme_test.js
const fs = require('fs');
const path = require('path');
const { JSDOM, VirtualConsole } = require('jsdom');

const WF = fs.readFileSync(path.join(__dirname, '..', 'src', 'web_functions', 'web_functions.cpp'), 'utf8');
const MC = fs.readFileSync(path.join(__dirname, '..', 'src', 'web_functions', 'web', 'mc.js'), 'utf8');
const MUT = process.env.TDTHEME_MUTATE || '';
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

const cpp = between(WF, 'TD-HTML');
let js = between(MC, 'TD-JS').split('\n').filter((l) => !/^\s*\/\//.test(l)).join('\n');
if (MUT === 'param') js = mutate(js, "tdecktheme=", "tdtheme=");
if (MUT === 'noerr') js = mutate(js, "if(j&&j.returncode==0)tdMark(v);else alert('The T-Deck theme could not be set.');", 'tdMark(v);');
if (MUT) console.log('NOTE mutation active: ' + MUT);

const lit = (s) => s.replace(/\\(["\\])/g, '$1').replace(/\\n/g, '\n');
const fixed = [...cpp.matchAll(/println\("((?:[^"\\]|\\.)*)"\)/g)].map((m) => lit(m[1]));
const fmt = /printf\("((?:[^"\\]|\\.)*)",/.exec(cpp);
const names = ['auto', 'dark', 'light'];
function render(stored) {
  const btn = names.map((n, i) => {
    const on = stored === i;
    return lit(fmt[1]).replace('%s', on ? ' mctab-on' : '').replace('%s', n).replace('%s', on ? 'true' : 'false').replace('%s', n).replace('%s', n);
  }).join('');
  return fixed[0] + fixed[1] + btn + fixed[2];
}

let calls = [], reply = { returncode: 0 }, alerts = [];
function mount(stored) {
  const vc = new VirtualConsole();
  vc.on('jsdomError', (e) => { console.log('JSDOM ERROR ' + e.message); failures++; });
  const dom = new JSDOM('<!doctype html><body><div id="c">' + render(stored) + '</div></body>', { runScripts: 'dangerously', virtualConsole: vc });
  const w = dom.window;
  w.fetch = (url) => { calls.push(url); return Promise.resolve({ status: 200, json: () => Promise.resolve(reply) }); };
  w.alert = (m) => alerts.push(m);
  w.eval(js);
  return w;
}
const settle = async () => { for (let i = 0; i < 6; i++) await new Promise((r) => setImmediate(r)); };
const pressed = (w) => names.map((n) => w.document.getElementById('tdtheme_' + n).getAttribute('aria-pressed')).join();

(async () => {
  check('control is compiled in only for BOARD_T_DECK || BOARD_T_DECK_PLUS', /#if defined\(BOARD_T_DECK\) \|\| defined\(BOARD_T_DECK_PLUS\)\n\s*\{ \/\/ TD-THEME-01 W3/.test(WF) && !/BOARD_T_DECK_PRO[^\n]*\n[^\n]*TD-HTML/.test(WF));
  check('setparam tdecktheme handler is under the same guard', /#if defined\(BOARD_T_DECK\) \|\| defined\(BOARD_T_DECK_PLUS\)\n\s*if \(param_name\.equals\("tdecktheme"\)\)/.test(WF));
  check('setparam validates with tdeck_theme_parse and reuses --theme through commandAction', /tdeck_theme_parse\(param_value\.c_str\(\)\)[\s\S]{0,300}"--theme %s"[\s\S]{0,200}commandAction\(/.test(WF));
  check('format has 5 placeholders', (fmt[1].match(/%s/g) || []).length === 5);
  for (const stored of [0, 1, 2]) {
    const w = mount(stored);
    const bt = [...w.document.querySelectorAll('button')];
    check('stored ' + names[stored] + ': three buttons Auto/Dark/Light labels', bt.map((b) => b.textContent).join() === 'auto,dark,light');
    check('stored ' + names[stored] + ': only that button is aria-pressed', pressed(w) === names.map((_, i) => String(i === stored)).join(), pressed(w));
  }
  check('label text present', /T-Deck display theme/.test(render(0)));
  for (const n of names) {
    calls = []; alerts = []; reply = { returncode: 0 };
    const w = mount(n === 'auto' ? 1 : 0);
    const b = w.document.getElementById('tdtheme_' + n);
    check('button ' + n + ' onclick is tdSet(' + n + ')', b.getAttribute('onclick') === "tdSet('" + n + "')");
    b.click(); await settle();
    check('button ' + n + ' sends GET /setparam/?tdecktheme=' + n, calls.length === 1 && calls[0] === '/setparam/?tdecktheme=' + n, calls.join());
    check('button ' + n + ' sets aria-pressed on it only, no reload (one request only)', pressed(w) === names.map((x) => String(x === n)).join() && calls.length === 1 && alerts.length === 0, pressed(w));
    check('button ' + n + ' mctab-on class follows', w.document.querySelectorAll('.mctab-on').length === 1 && b.classList.contains('mctab-on'));
  }
  {
    // the group label names the group only: a for= pointing at a button would click it (and rename it for screen readers)
    calls = []; alerts = []; reply = { returncode: 0 };
    const w = mount(1);
    const lbl = w.document.getElementById('tdtheme_lbl');
    lbl.click(); await settle();
    check('clicking the group label sends no request and keeps the state', calls.length === 0 && pressed(w) === 'false,true,false' && !lbl.hasAttribute('for'), calls.join() + '|' + lbl.getAttribute('for'));
  }
  for (const bad of [{ returncode: 1 }, { returncode: 2 }]) {
    calls = []; alerts = []; reply = bad;
    const w = mount(1);
    w.document.getElementById('tdtheme_light').click(); await settle();
    check('error returncode ' + bad.returncode + ' leaves the state unchanged and tells the user', pressed(w) === 'false,true,false' && alerts.length === 1, pressed(w) + '|' + alerts.length);
  }
  console.log(failures ? 'FAILED: ' + failures : 'ALL PASS');
  process.exit(failures ? 1 : 0);
})();

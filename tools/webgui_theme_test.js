// WEB-DARK-01 / WEB-A11Y-01: theme + contrast test of the web GUI CSS (src/web_functions/*.cpp).
//
// The page and Remote page CSS is read from the shipped gzip asset (web/mc.css via web_assets.h). A small CSS parser resolves the
// custom properties for the light palette (:root) and the dark palette (@media (prefers-color-scheme:dark)) and the
// WCAG 2.1 contrast ratio of every fg/bg pair the GUI uses is computed and checked (text 4.5:1, icons/UI 3:1).
// Also: at most 4 distinct font sizes, nav icons carry no hard-coded white, login error colour comes from the palette.
//
// Run:   NODE_PATH=$HOME/.cache/meshcom-jsdom/node_modules node tools/webgui_theme_test.js
// Optional screenshots (playwright):  THEME_SHOTS=<dir> node tools/webgui_theme_test.js
const fs = require('fs');
const path = require('path');

const DIR = path.join(__dirname, '..', 'src', 'web_functions');
const WF = fs.readFileSync(path.join(DIR, 'web_functions.cpp'), 'utf8');
const RM = fs.readFileSync(path.join(DIR, 'web_rm_page.cpp'), 'utf8');
const LOGIN = fs.readFileSync(path.join(DIR, 'web_login_page.cpp'), 'utf8');

let failures = 0;
function check(name, cond, detail) {
  console.log((cond ? 'PASS ' : 'FAIL ') + name + (detail !== undefined && !cond ? '  [' + detail + ']' : ''));
  if (!cond) failures++;
}

function decode(s) {
  return s.replace(/\\(.)/g, (m, c) => (c === 'n' ? '\n' : c === 't' ? '\t' : c));
}
function literals(text) {
  const lines = text.split('\n').filter((l) => !/^\s*\/\//.test(l));
  let out = '';
  for (const m of lines.join('\n').matchAll(/"((?:[^"\\]|\\.)*)"/g)) out += decode(m[1]);
  return out;
}

// ---- CSS source -----------------------------------------------------------------------------------------------
// The page and Remote page rules ship as one gzip blob (web/mc.css -> web_assets.h, tools/gen_web_assets.py); test what
// the firmware serves.
const { loadAssets } = require('./webgui_assets');
const css = loadAssets().MC_CSS;
if (!css || css.indexOf(':root') < 0) throw new Error('MC_CSS asset not found in web_assets.h');

// ---- tiny CSS parser: [{sel, decl:{prop:value}, media}] ----------------------------------------------------------
function parse(src, media, out) {
  src = src.replace(/\/\*[\s\S]*?\*\//g, '');
  let i = 0;
  while (i < src.length) {
    const o = src.indexOf('{', i);
    if (o < 0) break;
    const head = src.slice(i, o).trim();
    let depth = 1, j = o + 1, inStr = null;
    for (; j < src.length && depth; j++) {
      const c = src[j];
      if (inStr) { if (c === inStr) inStr = null; continue; }
      if (c === '"' || c === "'") inStr = c;
      else if (c === '{') depth++;
      else if (c === '}') depth--;
    }
    const body = src.slice(o + 1, j - 1);
    if (head.startsWith('@media')) parse(body, head.replace(/\s+/g, '').replace('@media', ''), out);
    else if (!head.startsWith('@')) {
      const decl = {};
      let k = 0, cur = '', par = 0, q = null;
      const parts = [];
      for (; k < body.length; k++) {
        const c = body[k];
        if (q) { if (c === q) q = null; }
        else if (c === '"' || c === "'") q = c;
        else if (c === '(') par++;
        else if (c === ')') par--;
        else if (c === ';' && !par) { parts.push(cur); cur = ''; continue; }
        cur += c;
      }
      parts.push(cur);
      for (const p of parts) {
        const x = p.indexOf(':');
        if (x > 0) decl[p.slice(0, x).trim()] = p.slice(x + 1).replace(/!important/, '').trim();
      }
      for (const sel of head.split(',')) out.push({ sel: sel.trim().replace(/\s+/g, ' '), decl, media });
    }
    i = j;
  }
  return out;
}
const rules = parse(css, null, []);
const DARKMQ = '(prefers-color-scheme:dark)';
function applies(r, theme) {
  return r.media === null || (theme === 'dark' && r.media === DARKMQ);
}
// value of prop for the first selector chain entry that defines it (later chain entries are more specific)
function lookup(sels, prop, theme) {
  let val;
  for (const sel of sels) for (const r of rules) if (applies(r, theme) && r.sel === sel && prop in r.decl) val = r.decl[prop];
  return val;
}
function resolve(v, theme, depth) {
  depth = depth || 0;
  if (v === undefined || depth > 8) return v;
  return v.replace(/var\((--[\w-]+)(?:\s*,\s*([^)]*))?\)/g, (m, name, fb) => {
    const r = lookup([':root'], name, theme);
    return r !== undefined ? resolve(r, theme, depth + 1) : fb !== undefined ? fb : 'UNDEFINED(' + name + ')';
  });
}
function parseColor(v) {
  if (!v) return null;
  v = v.trim().toLowerCase();
  if (v === 'white') return [255, 255, 255];
  if (v === 'black') return [0, 0, 0];
  let m = v.match(/^#([0-9a-f]{3,8})$/);
  if (m) {
    let h = m[1];
    if (h.length === 3 || h.length === 4) h = h.split('').map((c) => c + c).join('');
    return [0, 2, 4].map((k) => parseInt(h.slice(k, k + 2), 16));
  }
  m = v.match(/^rgba?\(([^)]*)\)/);
  if (m) return m[1].split(/[ ,]+/).slice(0, 3).map(Number);
  return null;
}
function lum(c) {
  const f = c.map((x) => { x /= 255; return x <= 0.03928 ? x / 12.92 : Math.pow((x + 0.055) / 1.055, 2.4); });
  return 0.2126 * f[0] + 0.7152 * f[1] + 0.0722 * f[2];
}
function ratio(c1, c2) {
  const l1 = lum(c1), l2 = lum(c2);
  return (Math.max(l1, l2) + 0.05) / (Math.min(l1, l2) + 0.05);
}
// colour of prop (color | background-color | background | border-color), expanded through the cascade chain
function colour(sels, prop, theme) {
  let v = lookup(sels, prop, theme);
  if (v === undefined && prop === 'background-color') v = lookup(sels, 'background', theme);
  v = resolve(v, theme);
  return parseColor(v);
}

// ---- palette ----------------------------------------------------------------------------------------------------
check('light palette defined on :root', lookup([':root'], '--mcbg', 'light') !== undefined);
check('dark palette under @media (prefers-color-scheme:dark)', rules.some((r) => r.media === DARKMQ && r.sel === ':root'));
check('dark palette overrides the page background',
  resolve('var(--mcbg)', 'dark') !== resolve('var(--mcbg)', 'light'));

// ---- contrast pairs ---------------------------------------------------------------------------------------------
const BODY = ['body'];
const pairs = [
  // name, fg chain, fg prop, bg chain, bg prop, min ratio
  ['body text', BODY, 'color', BODY, 'background', 4.5],
  ['head bar', ['#head_layer'], 'color', ['#head_layer'], 'background-color', 4.5],
  ['content title (text on bar)', BODY, 'color', ['#content_title'], 'background-color', 4.5],
  ['nav normal: icon', ['.nav_button'], 'color', ['.nav_button'], 'background-color', 3],
  ['nav hover: icon', ['.nav_button', '.nav_button:hover'], 'color', ['.nav_button', '.nav_button:hover'], 'background-color', 3],
  ['nav selected: icon', ['.nav_button', '.nav_button.nbactive'], 'color', ['.nav_button', '.nav_button.nbactive'], 'background-color', 3],
  ['nav selected tile vs nav bar', ['.nav_button', '.nav_button.nbactive'], 'background-color', ['#nav_layer'], 'background-color', 3],
  ['nav hover tile vs nav bar (visible)', ['.nav_button', '.nav_button:hover'], 'background-color', ['#nav_layer'], 'background-color', 1.15],
  ['link', ['#content_inner a'], 'color', ['#content_inner > table'], 'background', 4.5],
  ['link on page', ['#content_inner a'], 'color', BODY, 'background', 4.5],
  ['button', ['#content_inner button'], 'color', ['#content_inner button'], 'background-color', 4.5],
  ['button hover', ['#content_inner button', '#content_inner button:hover'], 'color', ['#content_inner button', '#content_inner button:hover'], 'background-color', 4.5],
  ['button border vs page', ['#content_inner button'], 'border-color', BODY, 'background', 3],
  ['red (Install now) button', ['#content_inner button.au-red'], 'color', ['#content_inner button.au-red'], 'background-color', 4.5],
  ['table header', ['#content_inner > table thead'], 'color', ['#content_inner > table thead'], 'background-color', 4.5],
  ['table odd row', BODY, 'color', ['#content_inner > table'], 'background', 4.5],
  ['table even row', BODY, 'color', ['#content_inner > table tr:nth-child(even)'], 'background-color', 4.5],
  ['table border vs page', ['#content_inner > table'], 'border-color', BODY, 'background', 3],
  ['green AU row', BODY, 'color', ['#content_inner tr.au-green td'], 'background-color', 4.5],
  ['input', ['select,input,textarea'.split(',')[0]], 'color', ['select'], 'background-color', 4.5],
  ['input focus', ['select'], 'color', ['select:focus'], 'background-color', 4.5],
  ['input border vs page', ['select'], 'border-color', BODY, 'background', 3],
  ['toggle off track vs page', ['input:where([type="checkbox"][role="switch"])'], 'background-color', BODY, 'background', 3],
  ['toggle on track vs page', ['input:where([type="checkbox"][role="switch"]):checked'], 'background-color', BODY, 'background', 3],
  ['toggle knob vs off track', ['input:where([type="checkbox"][role="switch"])::before'], 'background', ['input:where([type="checkbox"][role="switch"])'], 'background-color', 3],
  ['toggle knob vs on track', ['input:where([type="checkbox"][role="switch"])::before'], 'background', ['input:where([type="checkbox"][role="switch"]):checked'], 'background-color', 3],
  ['toggle indeterminate vs page', ['.rmsw:indeterminate'], 'background-color', BODY, 'background', 3],
  ['received message', ['.message'], 'color', ['.message-received'], 'background-color', 4.5],
  ['sent message', ['.message'], 'color', ['.message-send'], 'background-color', 4.5],
  ['card label', BODY, 'color', ['.cardlabel'], 'background-color', 4.5],
  ['card border vs page', ['.cardlayout'], 'border-color', BODY, 'background', 3],
  ['mailbox cooldown chip', BODY, 'color', ['.mbx-cooldown'], 'background'.replace('background', 'background'), 4.5],
  ['mailbox held chip', BODY, 'color', ['.mbx-held'], 'background', 4.5],
  ['mailbox legend text', ['.mbx-legend'], 'color', BODY, 'background', 4.5],
  ['mailbox stale text', ['.mbx-stale'], 'color', BODY, 'background', 4.5],
  ['warn box', BODY, 'color', ['.mbx-warn'], 'background', 4.5],
  ['remote ok text', ['.rmok'], 'color', BODY, 'background', 4.5],
  ['remote bad text', ['.rmbad'], 'color', BODY, 'background', 4.5],
  ['remote armed chip', BODY, 'color', ['.rmarmed'], 'background', 4.5],
  ['remote saved chip', BODY, 'color', ['#content_inner .rmchip.rmsaved'], 'background', 4.5],
  ['neighbours own row', BODY, 'color', ['#content_inner tr.nbr-me td'], 'background-color', 4.5],
  ['neighbours super-node row', BODY, 'color', ['#content_inner tr.nbr-super td'], 'background-color', 4.5],
  ['neighbours E_self row', BODY, 'color', ['#content_inner tr.nbr-eself td'], 'background-color', 4.5],
  ['AU hint text', ['#au_hint'], 'color', BODY, 'background', 4.5],
  ['queue bar empty cell vs page', ['.mcq-cell-empty'], 'border-color', BODY, 'background', 1.0],
];
for (const theme of ['light', 'dark']) {
  for (const [name, fgc, fgp, bgc, bgp, min] of pairs) {
    // an unset colour falls back to the body colour / page background (inheritance)
    let fg = colour(fgc, fgp, theme) || (fgp === 'color' ? colour(BODY, 'color', theme) : null);
    let bg = colour(bgc, bgp, theme) || (bgp.startsWith('background') ? colour(BODY, 'background', theme) : null);
    if (fgp === 'border-color' && !fg) { const bd = resolve(lookup(fgc, 'border', theme), theme); const m = bd && bd.match(/(#[0-9a-fA-F]{3,8}|rgb[^)]*\))/); fg = m ? parseColor(m[1]) : null; }
    if (!fg || !bg) { check(theme + ' ' + name + ': colours resolvable', false, 'fg=' + fg + ' bg=' + bg); continue; }
    const r = ratio(fg, bg);
    check(theme + ' ' + name + ' ' + r.toFixed(2) + ':1 >= ' + min, r >= min, r.toFixed(2));
  }
}

// ---- font sizes -------------------------------------------------------------------------------------------------
const sizes = new Set();
for (const r of rules) {
  if (r.sel === 'html' || !('font-size' in r.decl)) continue;
  if (r.decl['font-size'] === 'inherit') continue;
  sizes.add(r.decl['font-size']);
}
for (const m of WF.matchAll(/font-size:\s*([^;"\\}]+)/g)) {
  const v = m[1].trim();
  if (/^\d+(\.\d+)?px$/.test(v) && !/63(\.5)?px/.test(v) && v !== '12px' && v !== '14px') sizes.add(v);
}
const tokens = [...(lookup([':root'], '--fs-s', 'light') ? [1] : []), ...(lookup([':root'], '--fs-xl', 'light') ? [1] : [])];
check('font sizes: ' + [...sizes].join(', ') + ' (<= 4 distinct)', sizes.size <= 4, sizes.size);
check('every font-size is a --fs-* token', [...sizes].every((s) => /^var\(--fs-(s|m|l|xl)\)$/.test(s)), [...sizes].join(' '));
check('font tokens defined', tokens.length === 2);

// ---- nav icons ----------------------------------------------------------------------------------------------------
const navLines = WF.split('\n').filter((l) => l.includes('class=\\"nav_button') && l.includes('<svg'));
check('nav SVG buttons found (>= 8)', navLines.length >= 8, navLines.length);
const hard = [];
navLines.forEach((l) => {
  const s = l.replace(/base64,[A-Za-z0-9+\/=]+/g, '');
  const hit = s.match(/(?:fill|stroke)\s*[=:]\s*\\?"?\s*#(?:f{3}|f{6})\b/i);
  if (hit) hard.push(s.match(/loadPage\('(\w+)'/) ? s.match(/loadPage\('(\w+)'/)[1] : s.slice(0, 40));
});
check('no nav SVG hard-codes a #fff fill/stroke', hard.length === 0, hard.join(','));
check('nav SVGs use currentColor', navLines.every((l) => /currentColor/.test(l)), navLines.filter((l) => !/currentColor/.test(l)).length + ' without');
check('nav_button sets an explicit colour', lookup(['.nav_button'], 'color', 'light') !== undefined);

// ---- stray hard-coded colours in the CSS (should be palette variables) -------------------------------------------------
const hardCss = [];
for (const r of rules) {
  if (r.media !== null || r.sel === ':root') continue;
  for (const [p, v] of Object.entries(r.decl)) {
    if (/^(color|background(-color)?|border(-color|-bottom|-top)?|fill)$/.test(p) && /#[0-9a-fA-F]{3,6}\b|rgb\(|\bblack\b|\bwhite\b/.test(v.replace(/url\([^)]*\)/g, ''))) hardCss.push(r.sel + '{' + p + ':' + v + '}');
  }
}
// allowed: fixed-colour surfaces that keep their own contrast pair (header bar, logo, loader, red button text, queue bar)
const allowed = /^(#head_layer|#mc_logo|\.loader|#content_inner button\.au-red|\.mcq-cell-wait|\.mcq-util-track|\.mcq-cell-empty|\.rmdet|#spectrum_display)\b/;  // .rmdet: literal pinned by webgui_rm_test, overridden by '#content_inner .rmdet'
const stray = hardCss.filter((s) => !allowed.test(s));
check('no stray hard-coded colours in rules (' + stray.length + ')', stray.length === 0, stray.join(' | '));
check('login error colour from palette', !/color:#[0-9a-fA-F]{3,6}/.test(LOGIN));

// ---- nav <img> icon (Setup): white-stroke SVG, recoloured by the .nbactive>img filter --------------------------------
// The <img> cannot use currentColor, so its stroke is a literal; the selected-state filter (--navinv) must keep it readable
// on every tile state (normal / hover / selected) in both themes (WCAG 1.4.11, 3:1).
function applyFilter(c, f) {
  return /invert\(\s*1\s*\)/.test(f || '') ? c.map((x) => 255 - x) : c;
}
const imgLines = WF.split('\n').filter((l) => l.includes('class=\\"nav_button') && l.includes('<img') && l.includes('base64,'));
check('nav <img> buttons found (>= 1)', imgLines.length >= 1, imgLines.length);
check('nav <img> selected state has a filter rule', lookup(['.nav_button.nbactive>img'], 'filter', 'light') !== undefined);
imgLines.forEach((l) => {
  const b64 = l.match(/base64,([A-Za-z0-9+\/=]+)/);
  const svgSrc = Buffer.from(b64[1], 'base64').toString('utf8');
  const strokes = [...svgSrc.matchAll(/(?:stroke|fill)="(#[0-9a-fA-F]{3,6})"/g)].map((m) => parseColor(m[1]));
  const id = (l.match(/loadPage\('(\w+)'/) || [0, '?'])[1];
  check('nav <img> ' + id + ' has literal stroke colours', strokes.length > 0, strokes.length);
  for (const theme of ['light', 'dark']) {
    const f = resolve(lookup(['.nav_button.nbactive>img'], 'filter', theme), theme);
    const states = [
      ['normal', colour(['.nav_button'], 'background-color', theme), false],
      ['hover', colour(['.nav_button', '.nav_button:hover'], 'background-color', theme), false],
      ['selected', colour(['.nav_button', '.nav_button.nbactive'], 'background-color', theme), true],
    ];
    for (const [st, bg, filtered] of states) {
      for (const sc of strokes) {
        const fg = filtered ? applyFilter(sc, f) : sc;
        const r = bg ? ratio(fg, bg) : 0;
        check(theme + ' nav <img> ' + id + ' ' + st + ' ' + r.toFixed(2) + ':1 >= 3', r >= 3, r.toFixed(2));
      }
    }
  }
});

// ---- handler-emitted colours (inline style=, JS colour literals) -------------------------------------------------------
// Everything outside the two <style> blocks and the nav buttons is scanned: a text colour literal must reach 4.5:1 on the
// page background (and on the table background) in BOTH palettes, an inline background must keep the inherited body text at
// 4.5:1, any other colour (stroke, fill, bar) 3:1 against its surface. The Spectrum SVG surface is the #spectrum_display
// background when the CSS pins one. Palette variables (var(--mc*)) are resolved per theme; raw literals are theme-invariant.
function lineOf(src, idx) { return src.slice(0, idx).split('\n').length; }
// The CSS is no longer in the .cpp files (web/mc.css), so nothing there is skipped; the static JS moved to web/mc.js and
// web/mcrm.js and is scanned as plain text (no C string literals).
const skipRanges = { WF: [], RM: [], JS: [] };
const MCJS = fs.readFileSync(path.join(DIR, 'web', 'mc.js'), 'utf8');
const MCRM = fs.readFileSync(path.join(DIR, 'web', 'mcrm.js'), 'utf8');
const specStart = WF.split('\n').findIndex((l) => l.includes('id=\\"spectrum_display\\"')) + 1;
let specEnd = specStart;
WF.split('\n').forEach((l, i) => { if (i + 1 > specStart && specEnd === specStart && l.includes('</svg>')) specEnd = i + 1; });
// the MeshCom logo is a fixed black tile (#mc_logo, same in both themes): its own artwork colours are not page text
const logoStart = WF.split('\n').findIndex((l) => l.includes('id=\\"mc_logo\\"')) + 1;
let logoEnd = logoStart;
WF.split('\n').forEach((l, i) => { if (i + 1 > logoStart && logoEnd === logoStart && l.includes('</svg>')) logoEnd = i + 1; });
check('logo SVG range found', logoStart > 0 && logoEnd > logoStart, logoStart + '-' + logoEnd);
// literals that are knowingly outside the contrast scan (none since WEB-DARK-02)
const followUp = [];
// WEB-DARK-02: chart segments sit on the track (--mcempty), not on the page: dark values need 3:1 against the dark track
// (WCAG 1.4.11). The light palette is the unchanged pre-dark look and is not re-judged here.
const chartSeg = /var colors=\[/;
const TOK = /#[0-9a-fA-F]{6}\b|#[0-9a-fA-F]{3}\b|rgba?\([^)]*\)|var\(--[\w-]+\)|(?<=[:=]\s*['"]?)(?:black|white|grey|gray)\b/g;
const surfaceCol = (theme, inSpec) => {
  if (inSpec) { const sb = colour(['#spectrum_display'], 'background-color', theme); if (sb) return sb; }
  return colour(BODY, 'background', theme);
};
let scanned = 0;
for (const [fname, src] of [['web_functions.cpp', WF], ['web_rm_page.cpp', RM], ['web/mc.js', MCJS], ['web/mcrm.js', MCRM]]) {
  const key = fname === 'web_functions.cpp' ? 'WF' : fname === 'web_rm_page.cpp' ? 'RM' : 'JS';
  const lit = key === 'JS' ? (x) => x : literals;
  // JS variables that feed `color:'+NAME` are text colours
  const textVars = new Set([...lit(src).matchAll(/(?<![-\w])color:'\+(\w+)/g)].map((m) => m[1]));
  src.split('\n').forEach((raw, i) => {
    const ln = i + 1;
    if (/^\s*\/\//.test(raw) || skipRanges[key].some(([x, y]) => ln >= x && ln <= y)) return;
    if (key === 'WF' && ln >= logoStart && ln <= logoEnd) return;
    if (raw.includes('class=\\"nav_button') || raw.includes('base64,')) return;
    const t = key === 'JS' ? raw : literals(raw.replace(/\/\/.*$/, (m) => (m.includes('"') ? m : '')));
    if (!t) return;
    const inSpec = key === 'WF' && ln >= specStart && ln <= specEnd;
    const varAssign = (t.match(/var\s+(\w+)\s*=/) || [])[1];
    for (const m of t.matchAll(TOK)) {
      const before = t.slice(0, m.index);
      let role = 'other';
      if (/(?<![-\w])color\s*:\s*['"]?$/.test(before) || /\.style\.color\s*=\s*'?$/.test(before)) role = 'text';
      else if ((/background(?:-color)?\s*:\s*['"]?$/.test(before) || /\.style\.background\s*=\s*$/.test(before)) &&
               /<(?:tr|td|th|table|p)\b[^<>]*$/.test(before)) role = 'bg';  // inline background of a text container (bars are 'other')
      else if (varAssign && textVars.has(varAssign)) role = 'text';
      if (role === 'other' && /^var\(--fs-/.test(m[0])) continue;
      if (followUp.some((re) => re.test(t))) continue;
      scanned++;
      const res = [];
      let ok = true;
      const seg = chartSeg.test(t);
      for (const theme of ['light', 'dark']) {
        if (seg && theme === 'light') continue;
        const c = parseColor(resolve(m[0], theme));
        if (!c) continue;
        const surf = seg ? parseColor(resolve('var(--mcempty)', theme)) : surfaceCol(theme, inSpec);
        const tbl = colour(['#content_inner > table'], 'background', theme);
        let r, min;
        if (role === 'text') { r = Math.min(ratio(c, surf), inSpec ? 99 : ratio(c, tbl)); min = 4.5; }
        else if (role === 'bg') { r = ratio(colour(BODY, 'color', theme), c); min = 4.5; }
        else { r = ratio(c, surf); min = 3; }
        res.push(theme + ' ' + r.toFixed(2));
        if (r < min) ok = false;
      }
      if (!res.length) continue;
      check(fname + ':' + ln + ' ' + role + ' ' + m[0] + ' [' + res.join(', ') + ']', ok,
        t.trim().slice(0, 100));
    }
  });
}
check('handler colour scan found literals (>= 10)', scanned >= 10, scanned);
check('Spectrum SVG sits on a pinned surface', !!colour(['#spectrum_display'], 'background-color', 'light') && !!colour(['#spectrum_display'], 'background-color', 'dark'));

// ---- optional screenshots -----------------------------------------------------------------------------------------
if (process.env.THEME_SHOTS) {
  (async () => {
    let pw; try { pw = require('playwright'); } catch (e) { pw = require('/opt/node-tools/node_modules/playwright'); }
    const { chromium } = pw;
    const dir = process.env.THEME_SHOTS;
    const navBtn = (cls, svg) => '<button class="nav_button ' + cls + '">' + svg + '</button>';
    const svg = '<svg viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg"><path d="M12 3a9 9 0 100 18 9 9 0 000-18zM12 8v5M12 16v.5" stroke="currentColor" stroke-width="2" stroke-linecap="round"/></svg>';
    const html = '<!doctype html><html><head><style>' + css + '</style></head><body>' +
      '<div id="nav_layer">' + navBtn('nbactive', svg) + navBtn('', svg) + navBtn('', svg) + '</div>' +
      '<div id="head_layer"><p class="font-small">Meshcom 4.40</p><p class="font-bold">DK5EN-1</p></div>' +
      '<div id="content_layer"><div id="content_title"><b>Node Information</b></div><div id="content_inner">' +
      '<table><thead><tr><td>Key</td><td>Value</td></tr></thead><tr><td>Call</td><td>DK5EN-1</td></tr><tr><td>Mode</td><td>LoRa</td></tr>' +
      '<tr class="au-green"><td>Auto update</td><td>up to date</td></tr></table><br>' +
      '<button>Refresh</button> <button class="au-red">Install now</button> <button disabled>Disabled</button> ' +
      '<a href="#">link</a> <input type="text" value="input"> <input type="checkbox" role="switch"> <input type="checkbox" role="switch" checked>' +
      '<div class="message message-received"><div><p>received</p></div></div><div class="message message-send"><div><p>sent</p></div></div>' +
      '<div class="cardlayout"><label class="cardlabel">Card</label><div class="mbx-warn">warning box</div></div>' +
      '</div></div></body></html>';
    const br = await chromium.launch();
    for (const scheme of ['light', 'dark']) {
      const pg = await br.newPage({ colorScheme: scheme, viewport: { width: 900, height: 700 } });
      await pg.setContent(html);
      await pg.screenshot({ path: path.join(dir, 'theme-' + scheme + '.png') });
    }
    await br.close();
    console.log('screenshots written to ' + dir);
  })();
}

console.log(failures ? failures + ' FAILED' : 'all passed');
process.exitCode = failures ? 1 : 0;

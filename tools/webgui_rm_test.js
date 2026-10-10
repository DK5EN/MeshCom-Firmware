// RM web GUI: offline jsdom test of the Remote page (src/web_functions/web_rm_page.cpp).
//
// No firmware and no node needed. The scaffold JS (rmScaffoldJs()) and the page skeleton
// (sub_page_remote()) are extracted from the C++ source by concatenating the string literals of their
// web_client.print/println calls, then run in jsdom against a fake fetch that speaks the server contract
// (docs/rm-gui/impl-plan.md, C4): /rmstatus, /rmnodes, /rmheard, /rmpasswd, /rmsend, /setparam.
// Timers and Date are virtual (fake clock), so lock, confirm timeout and polling are tested without waiting
// and leaked timers are counted.
//
// Prerequisites (not part of the repo):  npm i jsdom@24   (anywhere; set NODE_PATH to its node_modules)
// Run:   NODE_PATH=<dir>/node_modules node tools/webgui_rm_test.js
// Mutation check (the test must then FAIL):  RM_MUTATE=leak|nolock|timer|msgs|again node tools/webgui_rm_test.js
const fs = require('fs');
const path = require('path');
const { JSDOM, VirtualConsole } = require('jsdom');

const SRC = path.join(process.env.RM_PAGE_SRC || path.join(__dirname, '..', 'src', 'web_functions', 'web_rm_page.cpp'));   // RM_PAGE_SRC: run against another copy (before/after evidence)
const SRC_DIR = process.env.RM_SRC_DIR || path.join(__dirname, '..', 'src');   // RM_SRC_DIR: a copy of rm_commands.h, meshcom_settings.h, rm_sender_policy.h (evidence runs)

let failures = 0;
function check(name, cond, detail) {
  console.log((cond ? 'PASS ' : 'FAIL ') + name + (detail !== undefined && !cond ? '  [' + detail + ']' : ''));
  if (!cond) failures++;
}

// ---- C++ literal extraction ------------------------------------------------------------------------
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

// ---- constants the page prints with printf (DRY-04) ---------------------------------------------------
// The page emits `var rmLen={name:%u,atxt:%u}` from RM_NAME_MAX / RM_ATXT_MAX. The harness cannot run C++, so it
// derives the same numbers from the headers: rm_commands.h defines RM_X_MAX = sizeof(s_meshcom_settings::<field>) - 1,
// meshcom_settings.h gives the field size. Only this form and only %u are supported; anything else is an error.
function headerConsts() {
  const out = {};
  const rc = fs.readFileSync(path.join(SRC_DIR, 'rm_commands.h'), 'utf8');
  const ms = fs.readFileSync(path.join(SRC_DIR, 'meshcom_settings.h'), 'utf8');
  for (const m of rc.matchAll(/constexpr size_t (RM_\w+_MAX) = sizeof\(s_meshcom_settings::(\w+)\) - 1;/g)) {
    const f = new RegExp('A\\(char,\\s*' + m[2] + ',\\s*\\[(\\d+)\\]').exec(ms);   // settings table row  A(char, node_name, [20], ...)
    if (f) out[m[1]] = +f[1] - 1;
  }
  return out;
}
const HDR = headerConsts();

// the command list of rm_commands.h: [{name, kind, shape}] by the FORMAT CONTRACT regex of that file
function headerCommands() {
  const rc = fs.readFileSync(path.join(SRC_DIR, 'rm_commands.h'), 'utf8').split('\n');
  const at = rc.findIndex((l) => /#define RM_COMMAND_LIST\(X\)/.test(l));
  const list = [];
  for (let i = at + 1; i < rc.length; i++) {
    const m = /^\s*X\((\w+),\s*(\w+),\s*(\w+)\)/.exec(rc[i]);
    if (m) list.push({ name: m[1], kind: m[2], shape: m[3] });
    if (!/\\\s*$/.test(rc[i])) break;
  }
  return list;
}

// string-literal array `const char *<name>[] = {...};` of a C++ test file, comments dropped, escapes decoded as bytes
function cppStrings(file, name) {
  const t = fs.readFileSync(file, 'utf8');
  const m = new RegExp('const char \\*' + name + '\\[\\]\\s*=\\s*\\{([\\s\\S]*?)\\};').exec(t);
  if (!m) return null;
  const body = m[1].replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/[^\n]*/g, '');
  return [...body.matchAll(/"((?:[^"\\]|\\.)*)"/g)].map((x) => {
    const b = [];
    const q = x[1];
    for (let i = 0; i < q.length; i++) {
      if (q[i] !== '\\') { b.push(...Buffer.from(q[i])); continue; }
      const c = q[++i];
      if (c === 'x') { b.push(parseInt(q.substr(i + 1, 2), 16)); i += 2; }
      else b.push(...Buffer.from(c === 'n' ? '\n' : c === 't' ? '\t' : c));
    }
    return Buffer.from(b).toString('utf8');
  });
}

// all web_client.print/println/printf calls of void <fn>() { ... }; returns {calls:[{text,bytes,kind}], errors}
// printf: the format literal(s) are followed by (unsigned)<CONST> arguments, each %u is replaced by HDR[<CONST>];
// bytes counts the FORMAT literal (what sits in rodata), text is the rendered output.
function extractCalls(src, fn) {
  const at = src.indexOf('void ' + fn + '()');
  if (at < 0) throw new Error('function not found: ' + fn);
  let i = src.indexOf('{', at);
  const end = src.indexOf('\n}\n', i);
  const body = src.slice(i, end);
  const calls = [];
  const errors = [];
  const re = /web_client\.(println|print|printf)\s*\(/g;
  let m;
  while ((m = re.exec(body))) {
    let p = re.lastIndex;
    const lits = [];
    let args = [];
    for (;;) {
      while (/\s/.test(body[p])) p++;
      if (body[p] === '"') {
        let q = p + 1;
        while (body[q] !== '"') { if (body[q] === '\\') q++; q++; }
        lits.push(decodeLiteral(body.slice(p + 1, q)));
        p = q + 1;
      } else if (body[p] === ')') break;
      else if (m[1] === 'printf' && body[p] === ',' && lits.length) {
        let d = 0, q = p + 1;
        for (; q < body.length; q++) { if (body[q] === '(') d++; else if (body[q] === ')') { if (!d) break; d--; } }
        args = body.slice(p + 1, q).split(',').map((a) => a.replace(/\(unsigned\)/, '').trim());
        p = q;
      } else { errors.push(m[1] + ': non-literal argument near "' + body.slice(p, p + 30) + '"'); break; }
    }
    let text = lits.join('');
    const fmtBytes = Buffer.byteLength(text);
    if (m[1] === 'printf') {
      let ai = 0;
      text = text.replace(/%(.)/g, (all, c) => {
        if (c !== 'u') { errors.push('printf: unsupported conversion ' + all); return all; }
        const v = HDR[args[ai++]];
        if (v === undefined) errors.push('printf: unknown argument ' + args[ai - 1]);
        return String(v);
      });
      if (ai !== args.length) errors.push('printf: ' + ai + ' conversions for ' + args.length + ' arguments');
    }
    calls.push({ kind: m[1], text, bytes: m[1] === 'printf' ? fmtBytes : Buffer.byteLength(text) });
  }
  return { calls, errors };
}
function joinCalls(calls) {
  return calls.map((c) => c.text + (c.kind === 'println' ? '\r\n' : '')).join('');
}

const cpp = fs.readFileSync(SRC, 'utf8');
const pageX = extractCalls(cpp, 'sub_page_remote');
const jsX = extractCalls(cpp, 'rmScaffoldJs');
let JS = joinCalls(jsX.calls);
const HTML = joinCalls(pageX.calls);

// ---- optional mutations (prove the assertions can fail) --------------------------------------------
const MUT = process.env.RM_MUTATE || '';
function mutate(from, to) {
  if (!JS.includes(from)) throw new Error('mutation anchor not found: ' + from);
  JS = JS.replace(from, to);
}
if (MUT === 'leak') mutate("fetch(u,{method:'POST'", "fetch(u+'?'+b,{method:'POST'");
else if (MUT === 'nolock') mutate('rmLock=rmNow()+10000;', 'rmLock=0;');
else if (MUT === 'timer') mutate('function rmPageLeave(){clearInterval(rmT.tick);', 'function rmPageLeave(){');
else if (MUT === 'msgs') mutate('tr=rmRows[k];', 'tr=null;');
else if (MUT === 'again') mutate('rmSendCmd(cmd,args,d,s);', 'rmSendCmd(cmd,args);');
else if (MUT === 'text') mutate("r=rmChk(w[2],v);", "r=/^(name|atxt)$/.test(w[2])?false:rmChk(w[2],v);");
else if (MUT === 'radio') mutate("else if(n=='radio'){", "else if(n=='radio_off'){");
else if (MUT === 'mh') mutate('w=rmTL[rmSel.call]-rmNow();', 'w=0;');
else if (MUT === 'gate') mutate('cap=rmCapOf(c),dis=', 'cap=2,dis=');
else if (MUT === 'nocall') mutate("if(!dc&&ss>=0)body+='&call='+rmEnc(call);", '');
else if (MUT === 'stayforce') { mutate('rmMhReset();rmForce=null;rmForceBtn();}}', 'rmMhReset();}}'); mutate("if(rmForce&&rmForce.d==rmSel.call)rmBtn(b,", 'if(rmForce)rmBtn(b,'); mutate("a=='force'&&rmForce&&rmForce.d==rmSel.call", "a=='force'&&rmForce"); }
else if (MUT === 'call') mutate("if(dc)body+='&call='+rmEnc(dc);", '');
else if (MUT === 'force') mutate("if(fc)body+='&force=1';", '');
else if (MUT) throw new Error('unknown RM_MUTATE ' + MUT);
if (MUT) console.log('NOTE mutation active: ' + MUT);

// ---- fake clock, fake server, page factory ---------------------------------------------------------
const rmPath = (c) => c.url.split('?')[0];
const flush = async () => { for (let i = 0; i < 6; i++) await new Promise((r) => setImmediate(r)); };

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

// The server texts: the REAL rmTokenTable of src/rm_sender_policy.h, parsed here, so the fake server answers
// exactly {ok:false, err, msg} like the C++ handlers do (DRY-02). Adjacent literals are concatenated.
const SRVMSG = (() => {
  const t = fs.readFileSync(path.join(SRC_DIR, 'rm_sender_policy.h'), 'utf8');
  const at = t.indexOf('rmTokenTable(size_t *n)');
  const body = t.slice(at, t.indexOf('};', at));
  const o = {};
  for (const m of body.matchAll(/\{\s*"([^"]+)",\s*((?:"(?:[^"\\]|\\.)*"\s*)+)\}/g)) o[m[1]] = [...m[2].matchAll(/"((?:[^"\\]|\\.)*)"/g)].map((x) => decodeLiteral(x[1])).join('');
  return o;
})();
const refuse = (err, extra) => Object.assign({ ok: false, err, msg: SRVMSG[err] }, extra || {});

function mkServer() {
  const s = {
    status: null, nodes: [], heard: [], auth: 0, calls: [], sendReply: null, nextCtr: 1,
    mkStatus(o) { return Object.assign({ on: 1, pw: 1, ok: 0, rej: 0, lock: 0, lockS: 0, hwm: 0, log: [], sent: [], targets: [] }, o || {}); },
  };
  s.status = s.mkStatus();
  s.nodes = [0, 1, 2].map((n) => ({ slot: n, used: 0, call: '' }));
  s.heard = [{ call: 'DK5EN-1', hw: 'HELTEC_V3', age_s: 120, rssi: -95 }, { call: 'OE1ABC-5', hw: 'TBEAM', age_s: 4000, rssi: null }];
  s.handle = (url, init) => {
    const method = (init && init.method) || 'GET';
    const body = init && init.body ? String(init.body) : '';
    const f = new URLSearchParams(body);
    if (s.auth) return { status: s.auth, text: true };
    const u = String(url).indexOf('/setparam/') === 0 ? String(url) : String(url).split('?')[0];   // a leaky stub puts the body in the query
    if (u === '/rmstatus') return { json: s.status };
    if (u === '/rmnodes' && method === 'GET') return { json: { nodes: s.nodes } };
    if (u === '/rmheard') return { json: { heard: s.heard } };
    if (u.indexOf('/setparam/?rm=') === 0) { s.status.on = u.endsWith('on') ? 1 : 0; return { json: { returncode: 0, returnvalue: 'ok' } }; }
    if (u === '/rmnodes' && method === 'POST') {
      if (s.nodesReply) return { json: s.nodesReply };
      const act = f.get('act');
      if (act === 'save') { const n = +f.get('slot'); s.nodes[n] = { slot: n, used: 1, call: f.get('call') }; }
      else if (act === 'del') s.nodes[+f.get('slot')] = { slot: +f.get('slot'), used: 0, call: '' };
      else if (act === 'forget') s.nodes = [0, 1, 2].map((n) => ({ slot: n, used: 0, call: '' }));
      return { json: { ok: true } };
    }
    if (u === '/rmpasswd') { if (s.passwdReply) return { json: s.passwdReply }; s.status.pw = f.get('act') === 'set' ? 1 : 0; return { json: { ok: true } }; }
    if (u === '/rmsend') {
      if (s.sendReply) return { json: s.sendReply };
      const dst = f.get('slot') !== null ? (s.nodes[+f.get('slot')] || {}).call : f.get('dst');
      if (f.get('slot') !== null && f.get('call') !== null && f.get('call') !== dst) return { json: refuse('slot') };
      const cmd = (f.get('cmd') + ' ' + f.get('args')).trim();
      s.status.sent.unshift({ dst, ctr: s.nextCtr, cmd, ago: 0, rep: 0, ver: 0, reply: '', st: 'queued', msg: 'Handed to the radio. It goes on air in a moment.' });
      s.lastCtr = s.nextCtr;
      return { json: { ok: true, ctr: s.nextCtr++ } };
    }
    return { status: 404, text: true };
  };
  return s;
}

function hdrObj(h) {
  const o = {};
  if (!h) return o;
  if (typeof h.forEach === 'function') h.forEach((v, k) => { o[String(k).toLowerCase()] = String(v); });
  else for (const k of Object.keys(h)) o[k.toLowerCase()] = String(h[k]);
  return o;
}

// opts.cpage: value of the scaffold's cpage global (default 'remote')
async function mkPage(srv, opts) {
  opts = opts || {};
  const T = mkClock();
  const calls = [];
  const vc = new VirtualConsole();
  vc.on('jsdomError', (e) => { console.log('jsdomError: ' + e.message); failures++; });
  const dom = new JSDOM('<!doctype html><html><body><div id="content_layer"></div></body></html>', {
    url: 'http://dk5en-1.local/',
    runScripts: 'outside-only',
    virtualConsole: vc,
    beforeParse(w) {
      const RealDate = w.Date;
      const BASE = 1700000000000;
      w.Date = class extends RealDate {
        constructor(...a) { if (a.length) super(...a); else super(BASE + T.now); }
        static now() { return BASE + T.now; }
      };
      w.setTimeout = T.setTimeout; w.setInterval = T.setInterval; w.clearTimeout = T.clear; w.clearInterval = T.clear;
      w.fetch = (url, init) => {
        calls.push({ url: String(url), method: (init && init.method) || 'GET', headers: hdrObj(init && init.headers), body: init && init.body ? String(init.body) : '' });
        const r = srv.handle(url, init);
        const status = r.status || 200;
        const resp = {
          status, ok: status < 400,
          json: () => (r.json !== undefined ? Promise.resolve(JSON.parse(JSON.stringify(r.json))) : Promise.reject(new SyntaxError('Unexpected end of JSON input'))),
        };
        return Promise.resolve(resp);
      };
      w.confirm = () => { throw new Error('confirm() must not be used'); };
      w.alert = () => { throw new Error('alert() must not be used'); };
    },
  });
  const w = dom.window;
  w.eval("var cpage='" + (opts.cpage || 'remote') + "';");
  w.eval(JS);
  const d = w.document;
  const P = {
    w, d, T, calls, srv, dom,
    el: (id) => d.getElementById(id),
    show() { d.getElementById('content_layer').innerHTML = HTML; },
    async init() { P.show(); w.rmPageInit(); await flush(); },
    click(el) { el.dispatchEvent(new w.MouseEvent('click', { bubbles: true })); },
    async tap(el) { P.click(el); await flush(); },
    async typeCall(c) { const e = P.el('rm_call'); e.value = c; e.dispatchEvent(new w.Event('input', { bubbles: true })); await flush(); },
    async setPoll(status) { srv.status = srv.mkStatus(status); await T.advance(10001); },
    tile(call) { return Array.from(d.querySelectorAll('#rm_nodes button')).find((b) => b.getAttribute('data-call') === call); },
    btn(text, within) { return Array.from((within || d).querySelectorAll('button')).find((b) => b.textContent === text); },
    tiles() {
      const o = {};
      for (const c of P.el('rm_sw').children) {
        const i = c.querySelector('input');
        o[i.getAttribute('data-sw')] = { state: i.indeterminate ? '?' : i.checked ? 'on' : 'off', el: i, text: c.querySelector('label').textContent, cf: i.getAttribute('data-cf') };
      }
      return o;
    },
    sends() { return calls.filter((c) => c.url.split("?")[0] === "/rmsend"); },
    polls() { return calls.filter((c) => c.url === '/rmstatus').length; },
    text(id) { return P.el(id).textContent; },
  };
  return P;
}

function ent(dst, cmd, reply, o) {
  o = o || {};
  return { dst, ctr: o.ctr || 1, cmd, ago: o.ago === undefined ? 5 : o.ago, rep: reply ? 1 : 0, ver: o.ver === 0 ? 0 : 1, reply: reply || '', st: o.st || (reply ? 'ok' : 'waiting'), msg: o.msg || 'Done. The node confirmed the command.' };
}
const STATUS_NEW = 'ok v=4.40a up=130 bat=87 heap=123 s=GtDMwL p=17/22 led=1';
const STATUS_OLD = 'ok v=4.35a up=5 bat=80 heap=100 gw=1 mesh=0 led=0';
const dupIds = (P) => { const seen = Object.create(null), d = []; for (const e of P.d.querySelectorAll('#rm_page [id]')) { if (seen[e.id]) d.push(e.id); seen[e.id] = 1; } return d; };

// a page with a saved node DK5EN-1 selected and a verified status in the ring
async function savedNodePage(statusReply, extraSent) {
  const srv = mkServer();
  srv.nodes[0] = { slot: 0, used: 1, call: 'DK5EN-1' };
  srv.status = srv.mkStatus({ sent: [ent('DK5EN-1', 'status', statusReply, { ago: 30 })].concat(extraSent || []) });
  const P = await mkPage(srv);
  await P.init();
  await P.tap(P.tile('DK5EN-1'));
  return P;
}

// ---- the leak assertion (used for the real flow AND for the positive control) -----------------------
function leaks(P, canary) {
  const enc = encodeURIComponent(canary);
  const bad = [];
  for (const c of P.calls) {
    if (c.url.includes(canary) || c.url.includes(enc)) bad.push('url ' + c.url);
    for (const [k, v] of Object.entries(c.headers)) if (v.includes(canary) || v.includes(enc)) bad.push('header ' + k);
  }
  if (P.w.location.href.includes(canary) || P.w.location.href.includes(enc)) bad.push('location');
  if (P.w.history.length !== 1) bad.push('history length ' + P.w.history.length);
  let st = '';
  try { st = JSON.stringify(P.w.localStorage) + JSON.stringify(P.w.sessionStorage) + P.w.document.cookie; } catch (e) { /* none */ }
  if (st.includes(canary)) bad.push('storage');
  if (P.d.documentElement.outerHTML.includes(canary)) bad.push('dom');
  for (const i of P.d.querySelectorAll('input')) if (i.value.includes(canary)) bad.push('input ' + i.id);
  return bad;
}

(async () => {
  // ---- static checks of the C++ source -------------------------------------------------------------
  check('extractor found the page and the JS', pageX.calls.length > 5 && jsX.calls.length > 20, pageX.calls.length + '/' + jsX.calls.length);
  const pf = [...pageX.calls, ...jsX.calls].filter((c) => c.kind === 'printf');
  check('only literal arguments; the one printf is the rmLen line (format < 64 B, resolved from the headers)', pageX.errors.length === 0 && jsX.errors.length === 0 && pf.length === 1 && pf[0].bytes < 64 && /^var rmLen=\{name:\d+,atxt:\d+\};\n$/.test(pf[0].text), pageX.errors.concat(jsX.errors).join('; ') + ' ' + pf.map((c) => c.text).join('|'));
  check('DRY-04 rmLen equals sizeof(settings field) - 1 from the headers (19/40-1 style, not typed in the page)', HDR.RM_NAME_MAX > 0 && HDR.RM_ATXT_MAX > HDR.RM_NAME_MAX && pf.length === 1 && pf[0].text.indexOf('name:' + HDR.RM_NAME_MAX + ',atxt:' + HDR.RM_ATXT_MAX + '}') > 0, JSON.stringify(HDR));
  check('DRY-04 the page source does not hard-code the text limits 19 / 39 in the JS', !/\b(?:19|39)\b/.test(JS.replace(/var rmLen=\{[^}]*\};/, '').replace(/\d+\.\d+/g, '')), (JS.replace(/var rmLen=\{[^}]*\};/, '').match(/.{20}\b(?:19|39)\b.{10}/) || [''])[0]);
  const all = pageX.calls.concat(jsX.calls);
  const maxLit = Math.max(...all.map((c) => c.bytes));
  check('every print/println literal <= 512 bytes', maxLit <= 512, 'max ' + maxLit);
  console.log('INFO largest literal ' + maxLit + ' B; page ' + Buffer.byteLength(HTML) + ' B in ' + pageX.calls.length + ' calls; js ' + Buffer.byteLength(JS) + ' B in ' + jsX.calls.length + ' calls; total ' + (Buffer.byteLength(HTML) + Buffer.byteLength(JS)) + ' B');
  let syntaxOk = true; try { new Function(JS); } catch (e) { syntaxOk = false; console.log(e.message); }
  check('scaffold JS parses (new Function)', syntaxOk);
  check('no </script, <!-- in the JS; no <script in the page', !/<\/script|<!--/i.test(JS) && !/<script/i.test(HTML));
  check('no confirm()/alert() in the JS', !/\b(confirm|alert)\s*\(/.test(JS));
  // jsdom has no layout: pin the two style rules that keep the tables readable. A blanket
  // word-break on every cell lets the browser squeeze the narrow columns to one letter per line.
  check('table buttons, time and state cells stay on one line', /#rm_msgs td:nth-child\(1\),[^{]*\.rmtab button\{white-space:nowrap;\}/.test(HTML));
  check('only the reply/result cells break inside words', !/\.rmtab td\{[^}]*word-break/.test(HTML) && /#rm_msgs td:nth-child\(4\),#rm_log td:nth-child\(3\)[^{]*\{overflow-wrap:anywhere;\}/.test(HTML));
  // CR-06: jsdom has no layout, so pin the rules that keep a 39-char word or a 10-digit ctr from widening the table
  check('CR-06 command columns and ctr column wrap anywhere', /#rm_msgs td:nth-child\(2\),#rm_msgs td:nth-child\(4\),#rm_log td:nth-child\(3\),#rm_log td:nth-child\(4\),#rm_log td:nth-child\(5\)\{overflow-wrap:anywhere;\}/.test(HTML));
  check('UI-09 no font-size and no x-small in the page source (sizes come from the font-small class)', !/font-size|x-small/.test(cpp) && /class="rmtab font-small"/.test(HTML));
  check('UI-08 no #rm_page selector prefix repetition, no nested CSS', (HTML.match(/#rm_page /g) || []).length <= 1 && !/\{[^{}]*\{/.test(HTML.match(/<style>[\s\S]*<\/style>/)[0].replace(/@media[^{]*\{([^{}]*\{[^{}]*\})*\}/, '')));
  {
    const D = new JSDOM(HTML).window.document;
    const cards = [...D.querySelectorAll('.cardlayout')];
    const kids = (c, t) => [...c.children].filter((x) => x.tagName === t);
    check('UI-03 six cards, all collapsablecard with teaser span, cardtoggle button and exactly one body div', cards.length === 6 && cards.every((c) => c.classList.contains('collapsablecard') && kids(c, 'SPAN').length === 1 && kids(c, 'DIV').length === 1 && kids(c, 'BUTTON').length === 1 && kids(c, 'BUTTON')[0].className === 'cardtoggle' && /togglecard\(this\)/.test(kids(c, 'BUTTON')[0].getAttribute('onclick')) && kids(c, 'BUTTON')[0].querySelector('i')), cards.length + '');
    check('UI-04 only Advanced starts closed', cards.filter((c) => !c.classList.contains('cardopen')).map((c) => c.id).join() === 'rm_adv');
    check('UI-05 exactly one status control and one sync control, no Test/Check connection', !D.getElementById('rm_test') && !/Test connection|Check connection/.test(HTML + JS) && (JS.match(/'Refresh status'/g) || []).length === 1 && (JS.match(/'data-act':'sync'/g) || []).length === 1);
    check('UI-07 one message line (no rm_lockline), no "Saved on this node" label (tiles carry the key state)', !D.getElementById('rm_lockline') && !!D.getElementById('rm_msg') && ((HTML + JS).match(/saved on this node/gi) || []).length === 0 && ((HTML + JS).match(/on this node/g) || []).length >= 1);
    check('UI-08 shared sentences: Really? once, confirm sentence once, no-answer sentence once, one rmErr password text', (JS.match(/Really\? tap again/g) || []).length === 1 && (JS.match(/Tap again within 4 seconds to confirm/g) || []).length === 1 && (JS.match(/did not answer the request/g) || []).length === 1);
    check('DRY-02/03 no rule or refusal sentence is duplicated from the server: no rmErr table, no password charset or reserved-word text', !/rmErr\b|rmErrOf|plain characters|plain letters|reserved|must not start with a space|Not allowed:|No double space/.test(JS));
  }
  check('no localStorage/sessionStorage/innerHTML in the JS', !/localStorage|sessionStorage|innerHTML|outerHTML|insertAdjacentHTML|document\.write/.test(JS));
  {
    const w0 = new JSDOM('', { runScripts: 'outside-only' }).window;
    const before = new Set(Object.getOwnPropertyNames(w0));
    w0.eval(JS);
    const bad = Object.getOwnPropertyNames(w0).filter((n) => !before.has(n) && !n.startsWith('rm'));
    check('JS globals are rm-prefixed', bad.length === 0, bad.join(','));
  }
  check('password inputs are autocomplete=new-password', (() => {
    const ins = HTML.match(/<input[^>]*type="password"[^>]*>/g) || [];
    return ins.length >= 2 && ins.every((i) => /autocomplete="new-password"/.test(i));
  })());
  {
    const have = new Set(Array.from(HTML.matchAll(/id="([^"]+)"/g)).map((m) => m[1]).concat(Array.from(JS.matchAll(/\.id='(rm_[a-z]+)'/g)).map((m) => m[1])));   // ids the JS builds itself count too
    const used = new Set(Array.from(JS.matchAll(/'(rm_[a-z]+)'/g)).map((m) => m[1]));
    const missing = [...used].filter((i) => !have.has(i));
    check('every element id the JS uses exists in the skeleton', missing.length === 0, missing.join(','));
  }
  for (const k of ['rmPageInit', 'rmPageLeave']) check('exports ' + k, new JSDOM('', { runScripts: 'outside-only' }).window.eval(JS + ';typeof ' + k) === 'function');

  // ---- DRY-07: the page's command lists against src/rm_commands.h ------------------------------------------
  // THE RULE (rows are X(name, KIND, SHAPE) of rm_commands.h, parsed by the contract regex; the JS lists stay literals):
  //   ACTION, READ, RW rows == the keys of rmCmds (the label of a command in the Messages table), nothing else in it
  //   TOGGLE rows          == the keys of rmTog (status-letter index 0..5 in the order GTDMWL) and of rmTogName
  //   PARAM rows           == the names rmLabel branches on (n=='<name>'), and no other branch name
  //   READ + RW rows except mh == the cards of rmDefs (c); mh has its own card builder rmMhCard
  // So a command added to the header without a JS entry, or a JS entry without a header row, fails here.
  {
    const P = await mkPage(mkServer());
    const H = headerCommands();
    const of = (...k) => H.filter((r) => k.includes(r.kind)).map((r) => r.name);
    const eq = (a, b) => JSON.stringify([...a].sort()) === JSON.stringify([...b].sort());
    const kinds = {};
    for (const r of H) kinds[r.kind] = (kinds[r.kind] || 0) + 1;
    check('DRY-07 header parsed: at least the 22 known rows, all five kinds present (sanity only, the set checks below do the pinning)', H.length >= 22 && ['ACTION', 'TOGGLE', 'PARAM', 'READ', 'RW'].every((k) => kinds[k] > 0), JSON.stringify(kinds));
    check('DRY-07 rmCmds keys == ACTION + READ + RW names', eq(Object.keys(P.w.rmCmds), of('ACTION', 'READ', 'RW')), Object.keys(P.w.rmCmds).join() + ' vs ' + of('ACTION', 'READ', 'RW').join());
    check('DRY-07 rmTog keys and rmTogName keys == TOGGLE names', eq(Object.keys(P.w.rmTog), of('TOGGLE')) && eq(Object.keys(P.w.rmTogName), of('TOGGLE')), Object.keys(P.w.rmTog).join() + ' vs ' + of('TOGGLE').join());
    check('DRY-07 rmTog indices follow the status letters GTDMWL', Object.keys(P.w.rmTog).every((k) => 'GTDMWL'[P.w.rmTog[k]] === { gps: 'G', track: 'T', display: 'D', mesh: 'M', gateway: 'W', led: 'L' }[k]), JSON.stringify(P.w.rmTog));
    const lab = P.w.rmLabel.toString();
    const branches = [...lab.matchAll(/n\s*==\s*'(\w+)'/g)].map((m) => m[1]);
    check('DRY-07 rmLabel branches == PARAM names', eq(branches, of('PARAM')), branches.join() + ' vs ' + of('PARAM').join());
    check('DRY-07 rmDefs cards == READ + RW names except mh, mh has rmMhCard', eq(P.w.rmDefs.map((d) => d.c), of('READ', 'RW').filter((n) => n !== 'mh')) && typeof P.w.rmMhCard === 'function', P.w.rmDefs.map((d) => d.c).join());
    const names = new Set(H.map((r) => r.name));
    const jsNames = [].concat(Object.keys(P.w.rmCmds), Object.keys(P.w.rmTog), Object.keys(P.w.rmTogName), branches, P.w.rmDefs.map((d) => d.c));
    check('DRY-07 every command name used by the page is a header row', jsNames.every((n) => names.has(n)), jsNames.filter((n) => !names.has(n)).join());
    // DRY-02: node answers of the self password flow carry msg next to err
    const srv = mkServer();
    const Q = await mkPage(srv);
    await Q.init();
    srv.passwdReply = refuse('pw');
    Q.el('rm_selfpw').value = ' bad';
    await Q.tap(Q.btn('Set'));
    check('DRY-02 /rmpasswd refusal shows the server msg (the page does not judge the leading space)', Q.text('rm_selfmsg') === SRVMSG.pw && Q.calls.filter((c) => rmPath(c) === '/rmpasswd').length === 1, Q.text('rm_selfmsg'));
    srv.passwdReply = { ok: false, err: 'pw' };
    Q.el('rm_selfpw').value = ' bad';
    await Q.tap(Q.btn('Set'));
    check('DRY-02 /rmpasswd refusal without msg falls back to a generic sentence', Q.text('rm_selfmsg') === 'The request was refused (pw).', Q.text('rm_selfmsg'));
    Q.w.rmPageLeave();
    P.w.rmPageLeave();
  }

  // ---- call format check ---------------------------------------------------------------------------
  {
    const P = await mkPage(mkServer());
    const valid = ['DK5EN-1', 'OE1ABC-15', 'AB-0', 'DK5EN-12', 'ABCDEF-99'];
    const invalid = ['DK5EN', 'D-1', 'DK5EN-123', 'DK5EN-', 'ABCDEFGHI-1', 'dk5en-1', 'DK 5-1', '', '-1', 'DK-5EN-1', 'DK5EN-A', 'AB-1-2'];
    check('rmValidCall accepts the valid vectors', valid.every((c) => P.w.rmValidCall(c)), valid.filter((c) => !P.w.rmValidCall(c)).join(','));
    check('rmValidCall rejects the invalid vectors', invalid.every((c) => !P.w.rmValidCall(c)), invalid.filter((c) => P.w.rmValidCall(c)).join(','));
    // DRY-03: rmValidCall is a cheap pre-check and must agree with the C++ rmValidateCall. The vectors are the ones
    // test_rm_sender_policy pins for rmValidateCall (parsed from that file: ok[] must pass, bad[] must fail), plus the
    // inputs below. The page upper-cases a typed call before it checks (rmCallInput), rmFoldCall does the same server side.
    const cppT = path.join(__dirname, '..', 'test', 'test_rm_sender_policy', 'test_main.cpp');
    const cOk = cppStrings(cppT, 'ok'), cBad = cppStrings(cppT, 'bad');
    check('DRY-03 the C++ call vectors were found (ok[] and bad[] of test_call_table)', !!cOk && !!cBad && cOk.length >= 9 && cBad.length >= 20, (cOk || []).length + '/' + (cBad || []).length);
    check('DRY-03 rmValidCall agrees with rmValidateCall on every C++ vector', (cOk || []).every((c) => P.w.rmValidCall(c)) && (cBad || []).every((c) => !P.w.rmValidCall(c)), (cOk || []).filter((c) => !P.w.rmValidCall(c)).concat((cBad || []).filter((c) => P.w.rmValidCall(c))).join(' | '));
    const pinOk = ['DK5EN-1', 'OE1ABC-15', 'dk5en-9'.toUpperCase()], pinBad = ['DK5EN', 'DK5EN-123', 'D-1', 'OE1ABCDE-1', 'dk5en-1'];
    check('DRY-03 pinned inputs: valid DK5EN-1, OE1ABC-15, dk5en-9 (folded); invalid DK5EN, DK5EN-123, D-1, OE1ABCDE-1, dk5en-1 (unfolded)', pinOk.every((c) => P.w.rmValidCall(c)) && pinBad.every((c) => !P.w.rmValidCall(c)), pinOk.filter((c) => !P.w.rmValidCall(c)).concat(pinBad.filter((c) => P.w.rmValidCall(c))).join(','));
    await P.init();
    await P.typeCall('dk5en-12');
    check('call field upper-cases and shows no error for a valid call', P.el('rm_call').value === 'DK5EN-12' && P.text('rm_callchk') === '' && !/looks right/.test(P.text('rm_savednote')));
    await P.typeCall('DK5EN');
    check('call field shows a format hint for a bad call', /Not a call sign/.test(P.text('rm_callchk')));
    P.w.rmPageLeave();
  }

  // ---- tile state from the status token ------------------------------------------------------------
  {
    const P = await savedNodePage(STATUS_NEW);
    const t = P.tiles();
    check('s= letters GtDMwL: GPS on, Track off, Display on, Mesh on, Gateway off, Light on',
      t.gps.state === 'on' && t.track.state === 'off' && t.display.state === 'on' && t.mesh.state === 'on' && t.gateway.state === 'off' && t.led.state === 'on',
      JSON.stringify(Object.fromEntries(Object.entries(t).map(([k, v]) => [k, v.state]))));
    check('Messages row shows the verified status (was: last status line)', /Connected\. Version 4\.40a, up 2 h 10 min, battery 87 %/.test(P.text('rm_msgs')), P.text('rm_msgs'));
    check('switch labels read GPS, Track, Light, Gateway; Mesh and Gateway carry the off-confirm', t.gps.text === 'GPS' && t.track.text === 'Track' && t.led.text === 'Light' && t.gateway.text === 'Gateway' && t.mesh.cf === 'mesh off' && t.gateway.cf === 'gateway off' && !t.gps.cf, JSON.stringify(Object.values(t).map((v) => v.text)));
    check('tile order GPS, Track, Display, Light, Mesh, Gateway', Object.keys(t).join(',') === 'gps,track,display,led,mesh,gateway', Object.keys(t).join(','));
    P.w.rmPageLeave();
  }
  {
    const P = await savedNodePage(STATUS_OLD);
    const t = P.tiles();
    check('old gw=1 mesh=0 led=0: Gateway on, Mesh off, Light off', t.gateway.state === 'on' && t.mesh.state === 'off' && t.led.state === 'off', JSON.stringify(Object.keys(t).map((k) => k + t[k].state)));
    check('old form: GPS, Track, Display are "?"', t.gps.state === '?' && t.track.state === '?' && t.display.state === '?');
    const unk = t.gps.el;
    check('UI-01 every switch is one checkbox role=switch, no On/Off button pair anywhere', Array.from(P.el('rm_sw').querySelectorAll('input')).every((i) => i.type === 'checkbox' && i.getAttribute('role') === 'switch') && P.el('rm_sw').querySelectorAll('button').length === 0 && !P.btn('On') && !P.btn('Off'));
    check('UI-01 an unknown state is indeterminate', unk.indeterminate === true && P.tiles().display.el.indeterminate === true && P.tiles().gateway.el.indeterminate === false);
    await P.tap(unk);
    check('unknown switch first click sends "gps on" by slot', P.sends().length === 1 && /cmd=gps&args=on/.test(P.sends()[0].body), JSON.stringify(P.sends()));
    await P.T.advance(10100);
    check('unknown switch stays indeterminate and flips to Off for the next click', P.tiles().gps.el.indeterminate === true && P.tiles().gps.el.checked === true);
    await P.tap(P.tiles().gps.el);
    check('unknown switch second click sends "gps off"', P.sends().length === 2 && /cmd=gps&args=off/.test(P.sends()[1].body), JSON.stringify(P.sends().map((x) => x.body)));
    P.w.rmPageLeave();
  }
  {
    const P = await savedNodePage('ok v=4.40a up=130 bat=87 heap=123 s=GtDMw p=17/22');
    check('Light hidden when the status has no L letter and no led=', !P.tiles().led, Object.keys(P.tiles()).join(','));
    P.w.rmPageLeave();
    const Q = await mkPage(mkServer());
    await Q.init(); await Q.typeCall('DK5EN-9');
    check('Light hidden before any status', !Q.tiles().led && Q.tiles().gps.state === '?');
    Q.w.rmPageLeave();
  }
  {
    const P = await savedNodePage(STATUS_NEW, [ent('DK5EN-1', 'gps off', 'ok gps=off', { ago: 2, ctr: 2 }), ent('DK5EN-1', 'status', 'ok v=4.40a up=1 bat=1 heap=1 s=GtDMwL p=17/22', { ago: 10, ctr: 3, ver: 0, st: 'unverified' })]);
    check('newer verified toggle reply wins, unverified status is ignored', P.tiles().gps.state === 'off', P.tiles().gps.state);
    P.w.rmPageLeave();
  }

  // ---- TX power ------------------------------------------------------------------------------------
  {
    const P = await savedNodePage(STATUS_NEW);
    check('TX power starts at the reported value and caps at the reported max', P.text('rm_rtxval') === '17 dBm' && P.text('rm_rtxnote') === 'Range 0 to 22 dBm on this node.');
    for (let i = 0; i < 30; i++) P.click(P.el('rm_rtxup'));
    check('TX power cap follows p=cur/max (22)', P.text('rm_rtxval') === '22 dBm' && P.el('rm_rtxup').disabled, P.text('rm_rtxval'));
    P.w.rmPageLeave();
    const Q = await savedNodePage('ok v=4.40a up=130 bat=87 heap=123 s=GtDMwL p=8/10');
    for (let i = 0; i < 30; i++) Q.click(Q.el('rm_rtxup'));
    check('TX power cap 10 for p=8/10', Q.text('rm_rtxval') === '10 dBm', Q.text('rm_rtxval'));
    Q.w.rmPageLeave();
  }

  // ---- TX power range from the target (plan 3.1: pmin= token, no 15 dBm fallback) ---------------------
  {
    const P = await savedNodePage('ok v=4.40a up=130 bat=87 heap=123 s=GtDMwL p=2/22 pmin=2 led=1');
    check('3.1a status pmin=2 p=2/22: stepper starts at 2 and "-" is disabled there', P.text('rm_rtxval') === '2 dBm' && P.el('rm_rtxdn').disabled && !P.el('rm_rtxup').disabled, P.text('rm_rtxval') + ' dn.disabled=' + P.el('rm_rtxdn').disabled);
    check('3.1a note names the range of this node', P.text('rm_rtxnote') === 'Range 2 to 22 dBm on this node.', P.text('rm_rtxnote'));
    for (let i = 0; i < 40; i++) P.click(P.el('rm_rtxup'));
    for (let i = 0; i < 40; i++) P.click(P.el('rm_rtxdn'));
    check('3.1a stepper bounded to [2, 22] in both directions', P.text('rm_rtxval') === '2 dBm' && P.el('rm_rtxdn').disabled, P.text('rm_rtxval'));
    P.w.rmPageLeave();
    const N = await savedNodePage('ok v=4.40a up=130 bat=87 heap=123 s=GtDMwL p=-4/20 pmin=-4 led=1');
    for (let i = 0; i < 40; i++) N.click(N.el('rm_rtxdn'));
    check('3.1a negative pmin=-4 never puts the stepper below 0 (managing node allows 0..max only)', N.text('rm_rtxval') === '0 dBm' && N.el('rm_rtxdn').disabled && N.text('rm_rtxnote') === 'Range 0 to 20 dBm on this node.', N.text('rm_rtxval') + ' ' + N.text('rm_rtxnote'));
    const n0 = N.sends().length;
    await N.tap(N.el('rm_rtxapply'));
    check('3.1a Apply with cur=-4/pmin=-4 sends txpower 0, never a negative value', N.sends().length === n0 + 1 && /cmd=txpower&args=0(&|$)/.test(N.sends()[n0].body) && !/args=-/.test(N.sends()[n0].body), JSON.stringify(N.sends().slice(n0)));
    N.w.rmPageLeave();
    const H = await savedNodePage('ok v=4.40a up=130 bat=87 heap=123 s=GtDMwL p=2/22 pmin=-9 led=1');
    for (let i = 0; i < 40; i++) H.click(H.el('rm_rtxdn'));
    const h0 = H.sends().length;
    await H.tap(H.el('rm_rtxapply')); await H.tap(H.el('rm_rtxapply'));
    check('3.1a Heltec pmin=-9: floor 0, lowering to the floor sends txpower 0 after the confirm, no negative send', H.text('rm_rtxval') === '0 dBm' && H.sends().slice(h0).length === 1 && /cmd=txpower&args=0(&|$)/.test(H.sends()[h0].body) && H.sends().every((c) => !/args=-/.test(c.body)), JSON.stringify(H.sends().slice(h0)));
    H.w.rmPageLeave();
  }
  {
    const P = await savedNodePage(STATUS_NEW);
    for (let i = 0; i < 40; i++) P.click(P.el('rm_rtxdn'));
    check('3.1b status without pmin floors at 0', P.text('rm_rtxval') === '0 dBm' && P.el('rm_rtxdn').disabled, P.text('rm_rtxval'));
    check('3.1b note for an old target still names the range', P.text('rm_rtxnote') === 'Range 0 to 22 dBm on this node.', P.text('rm_rtxnote'));
    P.w.rmPageLeave();
  }
  {
    const P = await savedNodePage(STATUS_OLD);
    check('3.1c no p= in any answer: stepper and Apply are disabled', P.el('rm_rtxdn').disabled && P.el('rm_rtxup').disabled && P.el('rm_rtxapply').disabled, [P.el('rm_rtxdn').disabled, P.el('rm_rtxup').disabled, P.el('rm_rtxapply').disabled].join());
    check('3.1c status seen but no p=: note says the firmware does not report its range, no 15 dBm', P.text('rm_rtxnote') === "This node's firmware does not report its power range." && !/15/.test(P.text('rm_rtxval') + P.text('rm_rtxnote')), P.text('rm_rtxnote') + '|' + P.text('rm_rtxval'));
    const n0 = P.sends().length;
    await P.tap(P.el('rm_rtxapply'));
    check('3.1c Apply sends nothing while the range is unknown', P.sends().length === n0);
    P.w.rmPageLeave();
    const O = await savedNodePage('ok v=4.40a up=125 bat=87 heap=212 gw=0 mesh=1');
    check('3.1c old form (tools/remote_cmd.py vector, gw=0 mesh=1, no p=): stepper disabled, firmware sentence, not "Press Refresh status"', O.el('rm_rtxdn').disabled && O.el('rm_rtxup').disabled && O.el('rm_rtxapply').disabled && O.text('rm_rtxnote') === "This node's firmware does not report its power range.", O.text('rm_rtxnote'));
    O.w.rmPageLeave();
    const Q = await mkPage(mkServer());
    await Q.init(); await Q.typeCall('DK5EN-9');
    check('3.1c unknown node (never answered): stepper disabled with the same note', Q.el('rm_rtxdn').disabled && Q.el('rm_rtxup').disabled && Q.text('rm_rtxnote') === 'Press Refresh status to learn the power range of the node.', Q.el('rm_rtxnote') && Q.text('rm_rtxnote'));
    Q.w.rmPageLeave();
  }
  {
    const P = await savedNodePage(STATUS_NEW, [ent('DK5EN-1', 'radio', 'ok f=433.175 sf=11 cr=5 bw=250 p=10/22 pmin=2', { ctr: 2, ago: 1 })]);
    for (let i = 0; i < 40; i++) P.click(P.el('rm_rtxdn'));
    check('3.1d radio reply with pmin=2 raises the floor to 2', P.w.rmKn().min === 2 && P.text('rm_rtxval') === '2 dBm' && P.el('rm_rtxdn').disabled && P.text('rm_rtxnote') === 'Range 2 to 22 dBm on this node.', P.w.rmKn().min + ' ' + P.text('rm_rtxval') + ' ' + P.text('rm_rtxnote'));
    check('3.1d the radio card still shows p as cur/max (pmin is not a line of its own)', /10\/22 dBm/.test(P.text('rm_v_radio_p')) && !/pmin/.test(P.el('rm_cards').textContent), P.el('rm_cards').textContent.slice(0, 120));
    P.w.rmPageLeave();
    const Q = await savedNodePage('ok v=4.40a up=130 bat=87 heap=123 s=GtDMwL p=10/22 pmin=2 led=1', [ent('DK5EN-1', 'radio', 'ok f=433.175 sf=11 cr=5 bw=250 p=10/22', { ctr: 2, ago: 1 })]);
    check('3.1d radio reply without pmin resets the floor to 0', Q.w.rmKn().min === 0, Q.w.rmKn().min);
    Q.w.rmPageLeave();
  }
  {
    const P = await savedNodePage('ok v=4.40a up=130 bat=87 heap=123 s=GtDMwL p=17/22 pmin=0 led=1');
    for (let i = 0; i < 5; i++) P.click(P.el('rm_rtxdn'));
    check('3.1 precondition: touched value 12', P.text('rm_rtxval') === '12 dBm', P.text('rm_rtxval'));
    await P.setPoll({ sent: [ent('DK5EN-1', 'status', 'ok v=4.40a up=140 bat=87 heap=123 s=GtDMwL p=17/22 pmin=14 led=1', { ctr: 9, ago: 1, st: 'ok' })] });
    check('3.1 value is clamped into [min, max] after every status', P.text('rm_rtxval') === '14 dBm' && P.el('rm_rtxdn').disabled, P.text('rm_rtxval'));
    P.w.rmPageLeave();
  }

  // ---- confirm tap logic ---------------------------------------------------------------------------
  {
    const P = await savedNodePage(STATUS_NEW);
    const rb = () => P.el('rm_info').querySelector('[data-cmd=reboot]');
    let r = rb();
    await P.tap(r);
    r = rb();
    check('first tap on Restart arms (text changes, nothing sent)', r.textContent === 'Really? tap again' && P.sends().length === 0, r.textContent);
    await P.T.advance(4100);
    r = rb();
    check('armed state times out after 4 s', r.textContent === 'Restart' && P.sends().length === 0, r.textContent);
    await P.tap(r);
    check('tap after the timeout arms again, still nothing sent', rb().textContent === 'Really? tap again' && P.sends().length === 0);
    await P.tap(rb());
    check('second tap within 4 s sends "reboot" by slot', P.sends().length === 1 && /slot=0&cmd=reboot/.test(P.sends()[0].body), JSON.stringify(P.sends()));
    P.w.rmPageLeave();
  }
  {
    const P = await savedNodePage(STATUS_NEW);   // mesh on, gateway off, gps on, track off
    const t = P.tiles();
    await P.tap(t.mesh.el);
    check('Mesh off needs a confirm', P.sends().length === 0 && P.tiles().mesh.state === 'on' && P.tiles().mesh.text === 'Mesh: Really? tap again');
    await P.tap(P.tiles().mesh.el);
    check('Mesh off sent on the second tap', P.sends().length === 1 && /cmd=mesh&args=off/.test(P.sends()[0].body));
    await P.T.advance(10100);
    await P.tap(P.tiles().track.el);
    check('Track on (turning something on) needs no confirm', P.sends().length === 2 && /cmd=track&args=on/.test(P.sends()[1].body), JSON.stringify(P.sends().map((s) => s.body)));
    P.w.rmPageLeave();
  }
  {
    const P = await savedNodePage('ok v=4.40a up=130 bat=87 heap=123 s=GtDMWL p=17/22');   // gateway on
    await P.tap(P.tiles().gateway.el);
    check('Gateway off needs a confirm', P.sends().length === 0 && P.tiles().gateway.text === 'Gateway: Really? tap again');
    await P.tap(P.tiles().gateway.el);
    check('Gateway off sent on the second tap', P.sends().length === 1 && /cmd=gateway&args=off/.test(P.sends()[0].body));
    P.w.rmPageLeave();
  }
  {
    const P = await savedNodePage(STATUS_NEW);   // cur 17
    for (let i = 0; i < 5; i++) P.click(P.el('rm_rtxdn'));
    await P.tap(P.el('rm_rtxapply'));
    check('lowering TX power needs a confirm', P.sends().length === 0 && P.el('rm_rtxapply').textContent === 'Really? tap again' && P.text('rm_rtxval') === '12 dBm');
    await P.tap(P.el('rm_rtxapply'));
    check('lowering TX power sends "txpower 12" on the second tap', P.sends().length === 1 && /cmd=txpower&args=12/.test(P.sends()[0].body), JSON.stringify(P.sends()));
    // the fake server answers with the verified result, then raise
    P.srv.status.sent.unshift(ent('DK5EN-1', 'txpower 12', 'ok txpower=12', { ago: 1, ctr: 9 }));
    await P.T.advance(10100);
    check('reported TX power follows the verified reply', P.text('rm_rtxval') === '12 dBm');
    P.click(P.el('rm_rtxup')); await flush();
    await P.tap(P.el('rm_rtxapply'));
    check('raising TX power needs no confirm', P.sends().length === 2 && /cmd=txpower&args=13/.test(P.sends()[1].body), JSON.stringify(P.sends().map((s) => s.body)));
    P.w.rmPageLeave();
  }

  // ---- 10 s lock -----------------------------------------------------------------------------------
  {
    const P = await savedNodePage(STATUS_NEW);
    await P.tap(P.btn('Send position now', P.el('rm_info')));
    check('a send was made', P.sends().length === 1);
    const allBtns = () => [...P.el('rm_info').querySelectorAll('button'), ...P.el('rm_sw').querySelectorAll('input'), P.el('rm_rtxapply')];
    check('all tiles are locked after a send', allBtns().every((b) => b.disabled), allBtns().filter((b) => !b.disabled).length + ' enabled');
    check('countdown is visible', /Next command possible in 10 s/.test(P.text('rm_msg')), P.text('rm_msg'));
    await P.T.advance(3000);
    check('countdown counts down', /in 7 s/.test(P.text('rm_msg')), P.text('rm_msg'));
    P.click(P.btn('Refresh status', P.el('rm_info'))); await flush();
    check('a tap while locked sends nothing', P.sends().length === 1);
    await P.T.advance(6900);
    check('still locked at 9.9 s', allBtns().every((b) => b.disabled));
    await P.T.advance(300);
    check('unlocked after 10 s', allBtns().every((b) => !b.disabled) && P.text('rm_msg') === '', P.text('rm_msg'));
    await P.tap(P.btn('Refresh status', P.el('rm_info')));
    check('sends again after the lock', P.sends().length === 2);
    P.w.rmPageLeave();
  }

  // ---- polling and timers --------------------------------------------------------------------------
  {
    const srv = mkServer();
    const P = await mkPage(srv);
    await P.init();
    check('init polls once and starts exactly two timers (tick, poll)', P.polls() === 1 && P.T.list.size === 2, P.polls() + ' polls, ' + P.T.list.size + ' timers');
    await P.T.advance(9900);
    check('idle: no poll before 10 s', P.polls() === 1, '' + P.polls());
    await P.T.advance(200);
    check('idle: next poll at 10 s', P.polls() === 2, '' + P.polls());
    await P.T.advance(20000);
    check('idle: one poll per 10 s', P.polls() === 4, '' + P.polls());
    P.w.rmPageInit(); await flush();
    check('a second rmPageInit does not duplicate timers', P.T.list.size === 2, '' + P.T.list.size);
    P.w.rmPageLeave();
    const n = P.polls();
    await P.T.advance(60000);
    check('rmPageLeave stops polling', P.polls() === n, P.polls() + ' vs ' + n);
    check('rmPageLeave leaves no timer behind', P.T.list.size === 0, P.T.list.size + ' timers');
  }
  {
    const srv = mkServer();
    srv.status = srv.mkStatus({ sent: [ent('DK5EN-1', 'status', '', { st: 'waiting', ago: 20 })] });
    const P = await mkPage(srv);
    await P.init();
    await P.T.advance(9100);
    check('outstanding command: poll every 3 s', P.polls() === 4, '' + P.polls());
    P.w.rmPageLeave();
  }
  {
    const P = await mkPage(mkServer());
    await P.init();
    P.w.eval("cpage='setup';");     // loadPage navigated away without calling the hook
    await P.T.advance(30000);
    check('page left without rmPageLeave: tick stops everything by itself', P.T.list.size === 0 && P.polls() <= 2, P.T.list.size + ' timers, ' + P.polls() + ' polls');
  }
  {
    const P = await mkPage(mkServer(), { cpage: 'info' });
    P.show();
    P.w.rmPageLeave();
    await P.T.advance(30000);
    check('no timer, no fetch without rmPageInit', P.T.list.size === 0 && P.calls.length === 0);
  }

  // ---- password handling ---------------------------------------------------------------------------
  const CANARY = 'Sup3r!Secret';
  async function sendWithPassword(P) {
    await P.init();
    await P.typeCall('DK5EN-12');
    P.el('rm_pw').value = CANARY;
    await P.tap(P.btn('Refresh status', P.el('rm_info')));
  }
  {
    const P = await mkPage(mkServer());
    await sendWithPassword(P);
    const s = P.sends();
    check('unsaved node: the password travels in the POST body (control)', s.length === 1 && s[0].method === 'POST' && new URLSearchParams(s[0].body).get('pw') === CANARY && new URLSearchParams(s[0].body).get('dst') === 'DK5EN-12', JSON.stringify(s));
    check('PASSWORD LEAK: not in any URL, header, location, history, storage, DOM, input', leaks(P, CANARY).length === 0, leaks(P, CANARY).join('; '));
    check('password input is cleared after the send', P.el('rm_pw').value === '');
    check('POST content type is form-urlencoded', !!s[0] && s[0].headers['content-type'] === 'application/x-www-form-urlencoded');
    P.w.rmPageLeave();
  }
  {
    // positive control: a deliberately leaky send must be caught by the same assertion
    const P = await mkPage(mkServer());
    await P.init();
    P.w.rmPost = function (u, b) { return P.w.fetch(u + '?' + b, { method: 'POST', headers: { 'X-Debug': b }, body: b }); };
    await P.typeCall('DK5EN-12');
    P.el('rm_pw').value = CANARY;
    await P.tap(P.btn('Refresh status', P.el('rm_info')));
    const bad = leaks(P, CANARY);
    check('POSITIVE CONTROL: a leaky stub send is caught by the leak assertion', bad.some((x) => x.startsWith('url')) && bad.some((x) => x.startsWith('header')), bad.join('; '));
    P.w.rmPageLeave();
  }
  {
    const P = await mkPage(mkServer());
    await P.init();
    await P.typeCall('DK5EN-12');
    await P.tap(P.btn('Refresh status', P.el('rm_info')));
    check('no password typed: nothing is sent, the page asks for it', P.sends().length === 0 && /password of DK5EN-12/.test(P.text('rm_msg')), P.text('rm_msg'));
    P.el('rm_pw').value = ' bad';
    await P.tap(P.btn('Refresh status', P.el('rm_info')));
    check('DRY-03 a password with a leading space is not judged by the page: it is sent', P.sends().length === 1, P.sends().length);
    await P.T.advance(12000);
    P.el('rm_pw').value = 'x'.repeat(15);
    await P.tap(P.btn('Refresh status', P.el('rm_info')));
    check('DRY-03 password longer than 14 is refused before sending (length rule only)', P.sends().length === 1 && /at most 14/.test(P.text('rm_msg')), P.text('rm_msg'));
    P.el('rm_pw').value = '';
    await P.tap(P.btn('Refresh status', P.el('rm_info')));
    check('DRY-03 empty password asks for one', P.sends().length === 1 && /Enter a password/.test(P.text('rm_msg')), P.text('rm_msg'));
    P.w.rmPageLeave();
  }
  {
    const P = await savedNodePage(STATUS_NEW);
    await P.tap(P.btn('Send track now', P.el('rm_info')));
    const b = new URLSearchParams(P.sends()[0].body);
    check('saved node: body has slot, cmd, args and no pw/dst key', [...b.keys()].sort().join(',') === 'args,call,cmd,slot' && b.get('slot') === '0' && b.get('call') === 'DK5EN-1', P.sends()[0].body);
    check('saved node: the password row is hidden', P.el('rm_pwrow').style.display === 'none');
    P.w.rmPageLeave();
  }
  {
    const srv = mkServer();
    const P = await mkPage(srv);
    await P.init();
    await P.typeCall('DK5EN-12');
    P.el('rm_pw').value = CANARY;
    await P.tap(P.btn('Remember'));
    const c = P.calls.find((x) => rmPath(x) === '/rmnodes' && x.method === 'POST');
    const b = c && new URLSearchParams(c.body);
    check('remember: POST /rmnodes act=save into the first free slot', !!c && b.get('act') === 'save' && b.get('slot') === '0' && b.get('call') === 'DK5EN-12' && b.get('pw') === CANARY, c && c.body);
    check('remember: password not leaked and input cleared', leaks(P, CANARY).length === 0 && P.el('rm_pw').value === '', leaks(P, CANARY).join('; '));
    check('remember: the node shows as a green saved tile and the password row hides', !!P.tile('DK5EN-12') && P.tile('DK5EN-12').classList.contains('rmsaved'));
    check('remember: password row hidden once saved', P.el('rm_pwrow').style.display === 'none');
    await P.tap(P.btn('Forget'));
    const d = P.calls.filter((x) => rmPath(x) === '/rmnodes' && x.method === 'POST').pop();
    check('forget: POST act=del&slot=0', d.body === 'act=del&slot=0', d.body);
    check('forget: node is no longer saved', P.el('rm_pwrow').style.display === '' && !P.tile('DK5EN-12'));
    srv.nodesReply = refuse('dup');
    P.el('rm_pw').value = CANARY;
    await P.tap(P.btn('Remember'));
    check('error token dup gets a plain sentence', /already saved/.test(P.text('rm_msg')), P.text('rm_msg'));
    srv.nodesReply = refuse('store');
    P.el('rm_pw').value = CANARY;
    await P.tap(P.btn('Remember'));
    check('error token store gets a plain sentence', /could not save/.test(P.text('rm_msg')), P.text('rm_msg'));
    P.w.rmPageLeave();
  }
  {
    const srv = mkServer();
    srv.nodes = [0, 1, 2].map((n) => ({ slot: n, used: 1, call: 'AA1AA-' + n }));
    const P = await mkPage(srv);
    await P.init();
    await P.typeCall('DK5EN-12');
    P.el('rm_pw').value = CANARY;
    const before = P.calls.length;
    await P.tap(P.btn('Remember'));
    check('all three places used: no request, plain message', P.calls.length === before && /All 3 places/.test(P.text('rm_msg')), P.text('rm_msg'));
    P.w.rmPageLeave();
  }
  {
    // own password: set / clear / RM switch
    const srv = mkServer();
    srv.status = srv.mkStatus({ pw: 0, on: 0 });
    const P = await mkPage(srv);
    await P.init();
    check('no password: RM switch disabled with "set a password first"', P.el('rm_on').disabled && /Set a password first/.test(P.text('rm_onhint')) && P.text('rm_pwstate') === 'password: not set');
    P.el('rm_selfpw').value = CANARY;
    await P.tap(P.btn('Set'));
    const c = P.calls.find((x) => rmPath(x) === '/rmpasswd');
    check('Set: POST /rmpasswd act=set with the password in the body only', !!c && c.method === 'POST' && c.body === 'act=set&pw=' + encodeURIComponent(CANARY), c && c.body);
    check('Set: no leak, input cleared', leaks(P, CANARY).length === 0 && P.el('rm_selfpw').value === '', leaks(P, CANARY).join('; '));
    await P.T.advance(100);
    check('after Set the status shows "password: set" and the switch is enabled', P.text('rm_pwstate') === 'password: set' && !P.el('rm_on').disabled, P.text('rm_pwstate'));
    P.el('rm_on').checked = true;
    P.el('rm_on').dispatchEvent(new P.w.Event('change', { bubbles: true }));
    await flush();
    check('RM switch calls GET /setparam/?rm=on', P.calls.some((x) => x.url === '/setparam/?rm=on' && x.method === 'GET'));
    await P.tap(P.btn('Clear'));
    check('Clear needs a second tap', !P.calls.some((x) => rmPath(x) === '/rmpasswd' && x.body === 'act=clear') && P.btn('Really? tap again'));
    await P.tap(P.btn('Really? tap again'));
    check('Clear sends act=clear on the second tap', P.calls.some((x) => rmPath(x) === '/rmpasswd' && x.body === 'act=clear'));
    check('the console/KISS warning is on the page', /net console \(port 2323\) and the KISS port/.test(P.d.body.textContent));
    P.w.rmPageLeave();
  }

  // ---- textContent safety --------------------------------------------------------------------------
  {
    const EVIL = '<img src=x onerror=window.__pwn=1>';
    const srv = mkServer();
    srv.heard = [{ call: EVIL, hw: EVIL, age_s: 5, rssi: -1 }];
    srv.nodes[1] = { slot: 1, used: 1, call: EVIL };
    srv.status = srv.mkStatus({
      log: [{ ago: 3, src: EVIL, ctr: 1, cmd: EVIL, res: EVIL }],
      sent: [ent(EVIL, EVIL, EVIL, { st: 'err', msg: EVIL })],
      targets: [{ dst: EVIL, locked: 0, retry: 0, pending: 0, chainErr: 'lost', chainMsg: EVIL }],
    });
    const P = await mkPage(srv);
    await P.init();
    await P.T.advance(100);
    const page = P.el('rm_page');
    check('hostile call/msg/reply render as text, no element is created', page.querySelectorAll('img,script,iframe,object,svg:not(.rmlk):not(#rm_sprite)').length === 0 && P.w.__pwn === undefined);
    check('the hostile text is visible as text (proves it was rendered)', page.textContent.includes(EVIL) && P.el('rm_msgs').textContent.includes(EVIL));
    await P.tap(P.tile(EVIL) || page.querySelector('.rmchip'));
    check('picking a hostile chip does not execute anything', P.w.__pwn === undefined && /Not a call sign/.test(P.text('rm_callchk')));
    P.w.rmPageLeave();
  }

  // ---- node tiles: one list, colour = key state, border = selection, padlock + word ---------------------
  {
    const srv = mkServer();
    srv.heard = [{ call: 'DK5EN-1', hw: 'HELTEC_V3', age_s: 120, rssi: -95 }, { call: 'OE1ABC-5', hw: 'TBEAM', age_s: 4000, rssi: null }];
    srv.nodes[0] = { slot: 0, used: 1, call: 'DK5EN-1' };
    srv.nodes[1] = { slot: 1, used: 1, call: 'DB0XYZ-2' };      // saved, never heard
    const P = await mkPage(srv);
    await P.init();
    const lk = (t) => t.querySelector('use').getAttribute('href');
    const g = P.tile('DK5EN-1'), n = P.tile('OE1ABC-5'), st = P.tile('DB0XYZ-2');
    check('tiles: no separate heard/saved rows remain, one #rm_nodes row', !P.el('rm_heard') && !P.el('rm_saved') && !!P.el('rm_nodes') && !/Saved on this node/.test(P.el('rm_page').textContent));
    check('tiles: saved+heard is green with the open lock and "key saved"', g.classList.contains('rmsaved') && !g.classList.contains('rmstale') && lk(g) === '#rmlk-o' && /key saved/.test(g.textContent) && /HELTEC_V3/.test(g.textContent) && /2 min ago, -95 dBm/.test(g.textContent), g.textContent);
    check('tiles: heard-only is neutral with the closed lock and "no key"', !n.classList.contains('rmsaved') && !n.classList.contains('rmstale') && lk(n) === '#rmlk-c' && /no key/.test(n.textContent) && !/key saved/.test(n.textContent), n.textContent);
    check('tiles: saved-but-unheard shows "not heard", open lock, "key saved", stale class', !!st && st.classList.contains('rmsaved') && st.classList.contains('rmstale') && lk(st) === '#rmlk-o' && /not heard/.test(st.textContent) && /key saved/.test(st.textContent), st && st.textContent);
    const order = Array.from(P.el('rm_nodes').querySelectorAll('button')).map((b) => b.getAttribute('data-call'));
    check('tiles: heard tiles keep their order, the unheard saved tile sorts last', order.join(',') === 'DK5EN-1,OE1ABC-5,DB0XYZ-2', order.join(','));
    check('tiles: four lines per tile (call, hardware, age, key state)', g.children.length === 4 && st.children.length === 4);
    check('tiles: Forget all is visible while something is saved', P.el('rm_forgetall').style.display === '');
    const css = HTML.match(/<style>[\s\S]*<\/style>/)[0];
    check('tiles: the selection is a border, the old blue selection background is gone', /\.rmchip\.rmsel\{border-color:/.test(css) && !/rmsel\{background/.test(css) && !/mclightblue/.test(css) && /\.rmchip\.rmsaved[^}]*background:var\(--mclightgreen\)/.test(css));
    check('tiles: the padlock sprite block appears exactly once with both symbols', (HTML.match(/<symbol /g) || []).length === 2 && (HTML.match(/id="rmlk-c"/g) || []).length === 1 && (HTML.match(/id="rmlk-o"/g) || []).length === 1 && P.d.querySelectorAll('symbol').length === 2);
    const sp = HTML.match(/<symbol[\s\S]*<\/symbol>/)[0];
    console.log('INFO sprite symbols ' + Buffer.byteLength(sp) + ' B');
    check('tiles: the symbol pair stays under 300 B', Buffer.byteLength(sp) < 300, String(Buffer.byteLength(sp)));
    check('tiles: the lock is 1em, currentColor, no font-size declared', /\.rmlk\{width:1em;height:1em;[^}]*fill:currentColor/.test(css));
    // selecting a green tile
    await P.tap(g);
    check('pick green: border class on the tile, password row hidden, inline Forget button', P.tile('DK5EN-1').classList.contains('rmsel') && P.el('rm_pwrow').style.display === 'none' && !!P.btn('Forget', P.el('rm_savednote')), P.text('rm_savednote'));
    check('pick green: note reads "Key saved for DK5EN-1." and nothing about the old sentence', /^Key saved for DK5EN-1\.\s*Forget$/.test(P.text('rm_savednote')) && !/No need to type it/.test(P.text('rm_savednote')) && !P.btn('Forget this node'), P.text('rm_savednote'));
    check('pick green: the call-sign check line is empty ("looks right" is gone)', P.text('rm_callchk') === '');
    // neutral tile
    await P.tap(P.tile('OE1ABC-5'));
    check('pick neutral: password row shows, Remember note, no Forget on the tile note', P.el('rm_pwrow').style.display === '' && /^Type the password of OE1ABC-5, or press Remember to save it\.$/.test(P.text('rm_savednote')) && !P.btn('Forget', P.el('rm_savednote')) && P.tile('OE1ABC-5').classList.contains('rmsel') && !P.tile('DK5EN-1').classList.contains('rmsel'), P.text('rm_savednote'));
    // stale tile
    await P.tap(P.tile('DB0XYZ-2'));
    check('pick stale: like green plus the not-heard hint', P.el('rm_pwrow').style.display === 'none' && /^Key saved for DB0XYZ-2\. Not heard in the last 3 hours, the command may not arrive\.\s*Forget$/.test(P.text('rm_savednote')), P.text('rm_savednote'));
    // Forget on a heard saved tile: key removed, tile turns neutral, still selected
    await P.tap(P.tile('DK5EN-1'));
    await P.tap(P.btn('Forget', P.el('rm_savednote')));
    const d = P.calls.filter((x) => rmPath(x) === '/rmnodes' && x.method === 'POST').pop();
    const g2 = P.tile('DK5EN-1');
    check('forget on the tile: POST act=del&slot=0, tile turns neutral (closed lock, "no key"), stays selected', d.body === 'act=del&slot=0' && !g2.classList.contains('rmsaved') && g2.classList.contains('rmsel') && /no key/.test(g2.textContent) && g2.querySelector('use').getAttribute('href') === '#rmlk-c' && P.el('rm_pwrow').style.display === '', d.body + ' / ' + g2.textContent);
    // forgetting a stale tile removes it
    await P.tap(P.tile('DB0XYZ-2'));
    await P.tap(P.btn('Forget', P.el('rm_savednote')));
    check('forget on a stale tile: the tile disappears, Forget all hides (nothing saved)', !P.tile('DB0XYZ-2') && P.el('rm_forgetall').style.display === 'none');
    P.w.rmPageLeave();
  }
  {
    const srv = mkServer();
    srv.nodes[0] = { slot: 0, used: 1, call: 'DK5EN-1' };
    srv.nodes[2] = { slot: 2, used: 1, call: 'DB0XYZ-2' };
    const P = await mkPage(srv);
    await P.init();
    await P.tap(P.btn('Forget all'));
    check('forget all: first tap arms and sends nothing', P.btn('Really? tap again', P.el('rm_nodes').parentNode) && !P.calls.some((x) => rmPath(x) === '/rmnodes' && x.method === 'POST'));
    await P.tap(P.btn('Really? tap again'));
    const d = P.calls.filter((x) => rmPath(x) === '/rmnodes' && x.method === 'POST').pop();
    check('forget all: second tap POSTs act=forget, saved tiles are gone, heard tile is neutral, button hides', !!d && d.body === 'act=forget' && !P.tile('DB0XYZ-2') && !P.tile('DK5EN-1').classList.contains('rmsaved') && P.el('rm_forgetall').style.display === 'none', d && d.body);
    P.w.rmPageLeave();
  }
  {
    const REMOVED = ['rm_heard', 'rm_saved', 'rm_forget'];
    const ids = [...(HTML + JS).matchAll(/id=\\?"(rm_\w+)\\?"/g)].map((m) => m[1]);
    check('removed element ids (rm_heard, rm_saved, rm_forget) occur nowhere in page or JS', REMOVED.every((i) => !ids.includes(i) && !new RegExp("'" + i + "'").test(JS)), REMOVED.filter((i) => ids.includes(i)).join(','));
  }

  // ---- 401 / 403 -----------------------------------------------------------------------------------
  {
    const srv = mkServer();
    srv.auth = 401;
    const P = await mkPage(srv);
    await P.init();
    const a = P.el('rm_auth');
    check('401 on the poll shows "log in again"', a.style.display !== 'none' && /log in again/i.test(a.textContent), a.textContent);
    const n = P.polls();
    await P.T.advance(60000);
    check('401 stops the polling', P.polls() === n, P.polls() + ' vs ' + n);
    P.w.rmPageLeave();
  }
  {
    const srv = mkServer();
    srv.nodes[0] = { slot: 0, used: 1, call: 'DK5EN-1' };
    const P = await mkPage(srv);
    await P.init();
    await P.tap(P.tile('DK5EN-1'));
    srv.auth = 403;
    await P.tap(P.btn('Refresh status', P.el('rm_info')));
    check('403 on a send shows "log in again"', /log in again/i.test(P.text('rm_auth')) && P.el('rm_auth').style.display !== 'none', P.text('rm_auth'));
    check('no "sent" message after a 403', !/^Sent/.test(P.text('rm_msg')), P.text('rm_msg'));
    P.w.rmPageLeave();
  }

  // ---- sentences -----------------------------------------------------------------------------------
  {
    const srv = mkServer();
    srv.nodes[0] = { slot: 0, used: 1, call: 'DK5EN-1' };
    const P = await mkPage(srv);
    await P.init();
    await P.tap(P.tile('DK5EN-1'));
    srv.sendReply = { ok: true, ctr: 0, viaSync: true };
    await P.tap(P.btn('Send position now', P.el('rm_info')));
    check('viaSync: accepted, no error shown (the progress text moved to the Messages card)', P.sends().length === 1 && /^(Next command possible in \d+ s\.)?$/.test(P.text('rm_msg')) && !/rmbad/.test(P.el('rm_msg').className), P.text('rm_msg'));
    const CM = 'Checking the connection first, your command follows.';
    await P.setPoll({ sent: [ent('DK5EN-1', 'sendpos', '', { ctr: 0, ago: 2, st: 'queued' })], targets: [{ dst: 'DK5EN-1', pending: 1, retry: 0, locked: 0, chainMsg: CM }] });
    const vr = P.el('rm_msgs').querySelectorAll('tr');
    check('viaSync: while pending, rm_chain shows the node and the server chain sentence', P.text('rm_chain') === 'DK5EN-1: ' + CM, P.text('rm_chain'));
    check('viaSync: a Messages row for the queued command exists with badge "sent"', vr.length === 1 && vr[0].children[2].textContent === 'sent' && /DK5EN-1/.test(vr[0].children[1].textContent), P.text('rm_msgs'));
    await P.setPoll({ sent: [ent('DK5EN-1', 'sendpos', '', { ctr: 0, ago: 4, st: 'queued' })], targets: [{ dst: 'DK5EN-1', pending: 0, retry: 0, locked: 0 }] });
    check('viaSync: no longer pending, rm_chain is empty', P.text('rm_chain') === '', P.text('rm_chain'));
    srv.sendReply = null;
    await P.T.advance(10100);
    srv.sendReply = refuse('limit');
    await P.tap(P.btn('Refresh status', P.el('rm_info')));
    check('error token limit gets a plain sentence', /still unanswered/.test(P.text('rm_msg')), P.text('rm_msg'));
    // DRY-02: the server text is shown verbatim; an answer with err but no msg gets a generic sentence, never blank
    srv.sendReply = { ok: false, err: 'zzz', msg: 'Custom text from the server, shown as is.' };
    await P.tap(P.btn('Refresh status', P.el('rm_info')));
    check('DRY-02 error answer with msg shows that msg verbatim', P.text('rm_msg') === 'Custom text from the server, shown as is.', P.text('rm_msg'));
    await P.T.advance(12000);
    srv.sendReply = { ok: false, err: 'zzz' };
    await P.tap(P.btn('Refresh status', P.el('rm_info')));
    check('DRY-02 error answer without msg falls back to a generic sentence, not blank', /^The request was refused \(zzz\)\.$/.test(P.text('rm_msg')), P.text('rm_msg'));
    await P.T.advance(12000);
    srv.sendReply = { ok: false };
    await P.tap(P.btn('Refresh status', P.el('rm_info')));
    check('DRY-02 refusal with neither err nor msg is still a sentence', P.text('rm_msg') === 'The request was refused.', P.text('rm_msg'));
    P.w.rmPageLeave();
  }
  {
    const srv = mkServer();
    srv.nodes[0] = { slot: 0, used: 1, call: 'DK5EN-1' };
    srv.status = srv.mkStatus({
      sent: [
        ent('DK5EN-1', 'status', STATUS_NEW, { ago: 3 }),
        ent('DK5EN-1', 'gps off', 'err rejected', { ago: 70, st: 'err', msg: 'The node tried, but the setting did not change.' }),
        ent('DK5EN-1', 'sendpos', '', { ago: 80, st: 'noanswer', msg: 'No answer after 75 seconds. Check the password.' }),
      ],
      targets: [{ dst: 'DK5EN-1', locked: 0, retry: 0, pending: 0, chainErr: 'nosync', chainMsg: 'The connection check got no answer, so your command was not sent.' }],
    });
    const P = await mkPage(srv);
    await P.init();
    const t = P.text('rm_msgs');
    check('messages: ok status becomes "Connected. Version ..." (was: activity)', /Connected\. Version 4\.40a, up 2 h 10 min, battery 87 %/.test(t), t);
    check('messages: err and no-answer sentences come from msg (was: activity)', /The node tried, but the setting did not change\./.test(t) && /No answer after 75 seconds/.test(t));
    check('messages: chain message of the target is shown (was: activity)', /DK5EN-1: The connection check got no answer/.test(P.text('rm_chain')));
    check('raw table lists the sent entries', P.el('rm_msgs').querySelectorAll('tr').length === 3);
    check('heard chips: call, hardware, age; null rssi prints no "null"', (() => { const c = P.el('rm_nodes').textContent; return /DK5EN-1/.test(c) && /HELTEC_V3/.test(c) && /2 min ago, -95 dBm/.test(c) && /OE1ABC-5/.test(c) && !/null/.test(c); })(), P.el('rm_nodes').textContent);
    P.w.rmPageLeave();
  }

  // ---- W1d: Messages card, lock label, toggle, clear confirm, Radio card, Advanced open, no fixed 10 s ----
  {
    const srv = mkServer();
    srv.nodes[0] = { slot: 0, used: 1, call: 'DK5EN-1' };
    srv.status = srv.mkStatus({ sent: [ent('DK5EN-1', 'status', '', { ctr: 7, ago: 4, st: 'waiting' })] });
    const P = await mkPage(srv);
    await P.init();
    const row0 = () => P.el('rm_msgs').querySelectorAll('tr')[0];
    const r1 = row0();
    check('W1d messages: waiting row is state "sent"', r1 && /sent/.test(r1.children[2].textContent) && r1.children[0].textContent === '4 s ago', r1 && r1.textContent);
    await P.setPoll({ sent: [ent('DK5EN-1', 'status', STATUS_NEW, { ctr: 7, ago: 14, st: 'ok' })] });
    check('W1d messages: sent -> verified ok, same DOM node', row0() === r1 && r1.children[2].textContent === 'verified' && /Connected/.test(r1.children[3].textContent) && r1.children[2].className === 'rmok');
    await P.setPoll({ sent: [ent('DK5EN-1', 'gps off', 'err rejected', { ctr: 7, ago: 24, st: 'err', msg: 'The node tried, but the setting did not change.' })] });
    check('W1d messages: verified err is marked, rows are keyed', P.el('rm_msgs').querySelectorAll('tr').length === 1 && row0().children[2].className === 'rmbad' && /did not change/.test(row0().children[3].textContent));
    await P.setPoll({ sent: [ent('DK5EN-1', 'sendpos', '', { ctr: 8, ago: 5, st: 'noanswer', msg: 'No answer after 75 seconds.' }), ent('DK5EN-1', 'led on', 'ok led=on', { ctr: 9, ago: 3, ver: 0, st: 'unverified', msg: 'Not verified.' })] });
    const rs = P.el('rm_msgs').querySelectorAll('tr');
    check('W1d messages: no answer and unverified badges, newest first', rs.length === 2 && rs[0].children[2].textContent === 'unverified' && rs[1].children[2].textContent === 'no answer', [...rs].map((x) => x.children[2].textContent).join());
    await P.typeCall('DK5EN-1');
    const before = P.sends().length;
    await P.tap(rs[1].querySelector('button'));
    await P.tap(rs[1].querySelector('button'));   // sendpos is not a read: two-tap confirm
    const sd = P.sends();
    check('W1d run again sends the same cmd to the same target', sd.length === before + 1 && /cmd=sendpos/.test(sd[sd.length - 1].body || '') && /DK5EN-1|slot=0/.test(sd[sd.length - 1].body || ''), JSON.stringify(sd[sd.length - 1]));
    check('W1d run again is disabled while the node is in cooldown', [...P.el('rm_msgs').querySelectorAll('button')].every((b) => b.disabled));
    check('W1d old ids are gone, TX control lives in the Node settings Radio sub-card (UI-06)', !['rm_act', 'rm_laststatus', 'rm_sent', 'rm_txval', 'rm_txdn', 'rm_txup', 'rm_txapply', 'rm_radio', 'rm_rs', 'rm_test', 'rm_sync', 'rm_lockline', 'rm_pinon', 'rm_pinoff'].some((i) => P.el(i)) && P.el('rm_card_radio').contains(P.el('rm_rtxval')) && P.el('rm_card_radio').contains(P.el('rm_rtxapply')) && ![...P.el('rm_page').querySelectorAll('.cardlabel')].some((l) => l.textContent === 'Radio'));
    check('UI-03/04 Advanced is a collapsablecard, closed by default, no <details>', P.el('rm_adv').classList.contains('collapsablecard') && !P.el('rm_adv').classList.contains('cardopen') && !P.el('rm_page').querySelector('details'));
    P.w.rmPageLeave();
  }
  {
    const hdr = HTML + JS;
    check('W1d no fixed "10 s" in the emitted page text', !/10 s/.test(hdr) && !/10 seconds/.test(hdr));
    for (const [pw, on] of [[0, 0], [0, 1], [1, 0], [1, 1]]) {
      const srv = mkServer(); srv.status = srv.mkStatus({ pw, on, lock: pw && !on ? 1 : 0, lockS: 42 });
      const P = await mkPage(srv); await P.init();
      const vis = P.el('rm_on').style.display !== 'none' && P.el('rm_onlbl').style.display !== 'none';
      check('W1d toggle visible for pw=' + pw + ' on=' + on, vis === !!(pw || on));
      const lt = P.text('rm_lockstate');
      check('W1d lock label ' + (pw && !on ? 'shows seconds' : 'empty') + ' pw=' + pw + ' on=' + on, pw && !on ? /^A sender is blocked for 42 s after wrong attempts$/.test(lt) : lt === '', lt);
      check('W1d lock label never says "not locked"', !/not locked/.test(lt));
      P.w.rmPageLeave();
    }
    const srv = mkServer(); const P = await mkPage(srv); await P.init();
    const n0 = P.calls.length;
    await P.tap(P.el('rm_selfclear'));
    check('W1d clear asks first, nothing sent', P.text('rm_selfmsg') === 'Clear the password? Remote management will be switched off.' && P.calls.length === n0, P.text('rm_selfmsg'));
    await P.w.rmDisarm(); await flush();
    check('W1d cancel (disarm) sends nothing', P.calls.length === n0 && P.text('rm_selfmsg') === '');
    await P.tap(P.el('rm_selfclear')); await P.tap(P.el('rm_selfclear'));
    check('W1d confirmed clear shows the new message', P.text('rm_selfmsg') === 'Password cleared. Remote management is off.', P.text('rm_selfmsg'));
    P.w.rmPageLeave();
  }

  // ---- advisor rework: Run again goes to the row's node, dedupe, confirm, lock label -------------------
  {
    const srv = mkServer();
    srv.nodes[0] = { slot: 0, used: 1, call: 'DK5EN-1' };
    srv.nodes[1] = { slot: 1, used: 1, call: 'DK5EN-2' };
    srv.status = srv.mkStatus({ sent: [
      ent('DK5EN-1', 'reboot', 'ok', { ago: 10, ctr: 1 }),
      ent('DK5EN-1', 'status', STATUS_NEW, { ago: 20, ctr: 2 }),
      ent('DK5EN-3', 'status', STATUS_NEW, { ago: 30, ctr: 3 }),
    ] });
    const P = await mkPage(srv);
    await P.init();
    await P.tap(P.tile('DK5EN-2'));
    const row = (n) => Array.from(P.el('rm_msgs').querySelectorAll('tr')).find((r) => r.children[1].textContent.indexOf(n) === 0);
    const rb = (n) => row(n).querySelector('button');
    const sel = () => P.w.rmSel.call + '/' + P.w.rmSel.slot + '/' + P.el('rm_call').value;
    await P.tap(rb('DK5EN-1 ' + P.w.rmLabel('status')));
    check('Run again (read) goes to the ROW node by its slot while another node is selected', P.sends().length === 1 && /^slot=0&cmd=status/.test(P.sends()[0].body), JSON.stringify(P.sends()));
    check('Run again leaves the selection unchanged and sends no password', sel() === 'DK5EN-2/1/DK5EN-2' && !new URLSearchParams(P.sends()[0].body).has('pw') && P.el('rm_pw').value === '', sel());
    await P.T.advance(10100);
    await P.tap(rb('DK5EN-1 Restart'));
    check('Run again of reboot: first tap arms, names the ROW node, sends nothing', P.sends().length === 1 && rb('DK5EN-1 Restart').textContent === 'Really? tap again' && /DK5EN-1/.test(P.text('rm_msg')) && !/DK5EN-2/.test(P.text('rm_msg')), P.text('rm_msg'));
    await P.tap(rb('DK5EN-1 Restart'));
    check('Run again of reboot: second tap sends to the ROW node slot', P.sends().length === 2 && /^slot=0&cmd=reboot/.test(P.sends()[1].body), JSON.stringify(P.sends().map((x) => x.body)));
    check('Run again of reboot leaves the selection unchanged', sel() === 'DK5EN-2/1/DK5EN-2', sel());
    check('360 px layout: Run again sits inside the reply cell, rows have 4 cells, header has 4 columns', row('DK5EN-1') && row('DK5EN-1').children.length === 4 && !!row('DK5EN-1').children[3].querySelector('button') && P.el('rm_msgs').parentNode.querySelectorAll('th').length === 4 && /@media \(max-width:600px\)\{\.rmtab thead\{display:none;\}/.test(HTML) && /\.rmtab,#rm_msgs,#rm_log\{display:block;\}/.test(HTML) && !/(#rm_msgs|#rm_log) thead/.test(HTML) && /#rm_log td:nth-child\(3\)::before\{content:"ctr ";\}/.test(HTML));
    check('a row whose node has no saved slot has no Run again button', rb('DK5EN-3') === null && !row('DK5EN-3').querySelector('button'), row('DK5EN-3').innerHTML);
    P.w.rmPageLeave();
  }
  {
    const srv = mkServer();
    srv.nodes[0] = { slot: 0, used: 1, call: 'DK5EN-1' };
    srv.status = srv.mkStatus({ sent: [ent('DK5EN-1', 'txpower 5', 'ok', { ago: 10 })] });
    const P = await mkPage(srv);
    await P.init();
    const b = () => P.el('rm_msgs').querySelector('button');
    await P.tap(b());
    check('Run again of txpower 5 needs two taps (first arms)', P.sends().length === 0 && b().textContent === 'Really? tap again');
    await P.tap(b());
    check('Run again of txpower 5 sends on the second tap', P.sends().length === 1 && /slot=0&cmd=txpower&args=5/.test(P.sends()[0].body), JSON.stringify(P.sends()));
    P.w.rmPageLeave();
  }
  {
    const srv = mkServer();
    srv.nodes[0] = { slot: 0, used: 1, call: 'DK5EN-1' };
    srv.status = srv.mkStatus({ sent: [
      ent('DK5EN-1', 'sync', '', { ctr: 0, ago: 90, st: 'noanswer', msg: 'No answer after 75 seconds. Check the password.' }),
      ent('DK5EN-1', 'sync', '', { ctr: 0, ago: 3, st: 'waiting' }),
    ] });
    const P = await mkPage(srv);
    await P.init();
    const rows = P.el('rm_msgs').querySelectorAll('tr');
    check('two syncs to one node: one row, the newest (waiting) owns it: badge "sent"', rows.length === 1 && rows[0].children[2].textContent === 'sent' && /^3 s ago|^3/.test(rows[0].children[0].textContent), P.text('rm_msgs'));
    P.w.rmPageLeave();
  }
  // ---- W2-E part 1: error sentences, forced attempt, Run again call= ----------------------------
  {
    const srv = mkServer();
    srv.nodes[0] = { slot: 0, used: 1, call: 'DK5EN-1' };
    srv.status = srv.mkStatus({ sent: [ent('DK5EN-1', 'txpower 5', 'ok', { ago: 10 })] });
    const P = await mkPage(srv);
    await P.init();
    await P.tap(P.el('rm_msgs').querySelector('button'));
    await P.tap(P.el('rm_msgs').querySelector('button'));
    const s0 = P.sends().map((c) => c.body);
    check('W2E Run again posts call=<row dst> with the slot', s0.length === 1 && /^slot=0&cmd=txpower&args=5&call=DK5EN-1$/.test(s0[0]), JSON.stringify(s0));
    P.w.rmPageLeave();
  }
  {
    const srv = mkServer();
    srv.nodes[0] = { slot: 0, used: 1, call: 'DK5EN-1' };
    const P = await mkPage(srv);
    await P.init();
    P.w.rmPick('DK5EN-1');
    for (const tok of ['range', 'text', 'unknown', 'unsupported', 'end', 'gps', 'hidden', 'busy', 'failed']) {
      await P.T.advance(12000);
      srv.sendReply = refuse(tok);
      await P.tap(P.btn('Refresh status', P.el('rm_info')));
      check('W2E error token ' + tok + ' shows the server msg verbatim', !!SRVMSG[tok] && P.text('rm_msg') === SRVMSG[tok], P.text('rm_msg'));
    }
    // verified err reply: the Messages row shows the sentence (cell and title)
    await P.setPoll({ sent: [ent('DK5EN-1', 'name Martin', 'err text', { ctr: 5, ago: 3, st: 'err', msg: SRVMSG.text })] });
    const r = P.el('rm_msgs').querySelector('tr');
    check('W2E Messages row shows the sentence for err text, also as title', /characters the node will not accept/.test(r.children[3].textContent) && /characters the node will not accept/.test(r.title), r.textContent);
    // 108-character reply row stays one row, text only
    const long = 'ok ' + 'n=' + 'A'.repeat(105);
    await P.setPoll({ sent: [ent('DK5EN-1', 'name Martin', long, { ctr: 6, ago: 2, st: 'ok', msg: long.substring(3) })] });
    const rr = P.el('rm_msgs').querySelectorAll('tr');
    check('W2E 108-character reply renders as one row via textContent', rr.length === 1 && rr[0].children.length === 4 && rr[0].children[3].textContent.length > 100 && rr[0].querySelector('img') === null, rr.length);
    // forced attempt
    await P.T.advance(12000);
    srv.sendReply = refuse('limit', { canForce: 1, retry: 30 });
    await P.tap(P.btn('Refresh status', P.el('rm_info')));
    const fb = () => P.btn('Try once more', P.el('rm_force'));
    check('W2E refusal with canForce shows "Try once more"', !!fb() && /still unanswered/.test(P.text('rm_msg')), P.text('rm_force'));
    const n0 = P.sends().length;
    srv.sendReply = null;
    await P.tap(fb());
    const sb = P.sends().slice(n0).map((c) => c.body);
    check('W2E Try once more repeats the command with force=1', sb.length === 1 && /cmd=status/.test(sb[0]) && /&force=1$/.test(sb[0]) && !/force/.test(P.sends()[n0 - 1].body), JSON.stringify(sb));
    check('W2E the button is gone after use', P.el('rm_force').textContent === '', P.el('rm_force').textContent);
    await P.T.advance(12000);
    srv.sendReply = refuse('limit', { canForce: 0 });
    await P.tap(P.btn('Refresh status', P.el('rm_info')));
    check('W2E canForce 0 shows no button', P.el('rm_force').textContent === '' && /still unanswered/.test(P.text('rm_msg')));
    P.w.rmPageLeave();
  }
  // ---- W2-E part 1: generic cards, capability gate ----------------------------------------------
  {
    const srv = mkServer();
    srv.nodes[0] = { slot: 0, used: 1, call: 'DK5EN-1' };
    srv.status = srv.mkStatus({ targets: [{ dst: 'DK5EN-1', pending: 0, retry: 0, locked: 0, canForce: 0, cap: 0 }] });
    const P = await mkPage(srv);
    await P.init();
    P.w.rmPick('DK5EN-1');
    await P.setPoll({ targets: [{ dst: 'DK5EN-1', pending: 0, retry: 0, locked: 0, canForce: 0, cap: 0 }] });
    const note = () => P.el('rm_cards').querySelector('.rmcapnote');
    const rd = () => P.btn('Read', P.el('rm_card_radio'));
    check('W2E gate unknown (cap 0): note and disabled Read radio', note() && /not reported support.*Press Re-sync counter/.test(note().textContent) && rd().disabled, note() && note().textContent);
    const n0 = P.sends().length;
    await P.tap(P.btn('Re-sync counter', P.el('rm_info')));
    check('W2E Re-sync counter sends sync', P.sends().length === n0 + 1 && /cmd=sync/.test(P.sends()[n0].body), JSON.stringify(P.sends().slice(n0)));
    await P.T.advance(12000);
    await P.setPoll({ sent: [ent('DK5EN-1', 'sync', 'ok', { ctr: 1, ago: 3, st: 'ok' })], targets: [{ dst: 'DK5EN-1', pending: 0, retry: 0, locked: 0, canForce: 0, cap: 0 }] });
    check('W2E gate old firmware (sync ok, cap 0): older-firmware note', note() && /older firmware: only the basic commands work/.test(note().textContent) && rd().disabled, note() && note().textContent);
    await P.setPoll({ sent: [ent('DK5EN-1', 'radio', 'ok f=433.175 sf=11 cr=5 bw=250 p=10/22', { ctr: 2, ago: 3, st: 'ok' }), ent('DK5EN-1', 'sens', 'ok t=21.4 h=<img/src=x> p=- t2=-', { ctr: 3, ago: 2, st: 'ok' })], targets: [{ dst: 'DK5EN-1', pending: 0, retry: 0, locked: 0, canForce: 0, cap: 2 }] });
    const T = (k) => (P.el('rm_v_' + k) || { textContent: '' }).textContent;
    check('W2E gate cap 2: no note, Read radio enabled', !note() && !rd().disabled);
    check('W2E radio reply fills the card (MHz, SF, 4/x, kHz, p cur/max)', T('radio_f') === 'Frequency: 433.175 MHz' && T('radio_sf') === 'Spreading factor: 11' && T('radio_cr') === 'Coding rate: 4/5' && T('radio_bw') === 'Bandwidth: 250 kHz' && /10\/22 dBm/.test(T('radio_p')), [T('radio_f'), T('radio_cr'), T('radio_p')].join('|'));
    check('W2E sens: absent marker shows "not present", hostile text lands as text only', T('sens_p') === 'Pressure: not present' && T('sens_t2') === 'Second temperature: not present' && T('sens_h') === 'Humidity: <img/src=x> %' && P.el('rm_cards').querySelector('img') === null, T('sens_h'));
    const m0 = P.sends().length;
    await P.tap(rd());
    check('W2E Read radio sends cmd=radio', P.sends().length === m0 + 1 && /^slot=0&cmd=radio&args=&call=DK5EN-1$/.test(P.sends()[m0].body), JSON.stringify(P.sends().slice(m0)));
    P.w.rmPageLeave();
  }
  {
    const srv = mkServer();
    srv.nodes[0] = { slot: 0, used: 1, call: 'DK5EN-1' };
    const tg = [{ dst: 'DK5EN-1', pending: 0, retry: 0, locked: 0, canForce: 0, cap: 2 }];
    srv.status = srv.mkStatus({ targets: tg });
    srv.nodes[0] = { slot: 0, used: 1, call: 'DK5EN-1' };
    const P = await mkPage(srv); await P.init(); P.w.rmPick('DK5EN-1');
    const X = (id) => P.el(id), tx = (id) => (X(id) ? X(id).textContent : '(none)');
    const E = (cmd, rep, c) => ent('DK5EN-1', cmd, 'ok ' + rep, { ctr: c, ago: 1, st: 'ok' });
    const poll = (l) => P.setPoll({ sent: l, targets: tg });
    const typeIn = async (id, v) => { X(id).value = v; X(id).dispatchEvent(new P.w.Event('input', { bubbles: true })); await flush(); };
    await poll([]);
    // parser: examples, worst cases, absent markers, free text
    await poll([E('radio', 'f=999.999 sf=99 cr=99 bw=999.99 p=-99/-99', 1), E('sens', 't=-99.9 h=100 p=1099.9 t2=-99.9', 2), E('pos', '-89.99999 -179.99999 40000 nofix', 3)]);
    check('RMX worst cases parse (radio, sens, pos nofix)', tx('rm_v_radio_f') === 'Frequency: 999.999 MHz' && tx('rm_v_sens_h') === 'Humidity: 100 %' && X('rm_f_pos_lat').value === '-89.99999' && tx('rm_v_pos_src') === 'Source: GPS on, no fix' && X('rm_f_pos_alt').value === '40000', tx('rm_v_radio_f') + '|' + tx('rm_v_pos_src'));
    await poll([E('pos', '48.40760 11.73850 482 nofix', 3)]);
    check('3.2 D5 position inputs are the INPUT elements (ids are unique, rm_f_ is the input)', ['lat', 'lon', 'alt'].every((k) => X('rm_f_pos_' + k) && X('rm_f_pos_' + k).tagName === 'INPUT') && X('rm_f_pos_lat').value === '48.40760' && X('rm_f_pos_lon').value === '11.73850' && X('rm_f_pos_alt').value === '482', ['lat', 'lon', 'alt'].map((k) => X('rm_f_pos_' + k) && X('rm_f_pos_' + k).tagName).join());
    check('3.2 D5 verified pos read: no false error, Set enabled', tx('rm_f_pos_hint') === '' && X('rm_f_pos_set') && !X('rm_f_pos_set').disabled, tx('rm_f_pos_hint') + ' set.disabled=' + (X('rm_f_pos_set') && X('rm_f_pos_set').disabled));
    await typeIn('rm_f_pos_lat', '48.4076');
    check('3.2 D5 typing 48.4076 keeps Set enabled and the hint empty', tx('rm_f_pos_hint') === '' && !X('rm_f_pos_set').disabled, tx('rm_f_pos_hint'));
    await typeIn('rm_f_pos_lat', '91');
    check('3.2 D5 a really bad latitude is still refused', X('rm_f_pos_set').disabled && /Latitude must be/.test(tx('rm_f_pos_hint')), tx('rm_f_pos_hint'));
    check('3.2 D5 position keeps a single read-only line (Source), no lat/lon/alt copies', !X('rm_v_pos_lat') && !X('rm_v_pos_lon') && !X('rm_v_pos_alt') && tx('rm_v_pos_src') === 'Source: GPS on, no fix', tx('rm_v_pos_src'));
    P.w.rmRenderCards();
    check('3.2 no duplicate id in #rm_page after rmRenderCards() (radio, sens, pos, name, atxt)', dupIds(P).length === 0, dupIds(P).join());
    check('3.2 id namespace: every rm_v_ element is read-only text, every input of the cards is rm_f_', [...P.el('rm_cards').querySelectorAll('[id^="rm_v_"]')].every((e) => e.tagName === 'DIV') && [...P.el('rm_cards').querySelectorAll('input')].every((e) => /^rm_f_/.test(e.id)) && !!X('rm_v_pos_src'), 'rm_v_pos_src=' + !!X('rm_v_pos_src'));
    await poll([E('sens', 't=21.4 h=45 p=1013.2 t2=-', 2), E('atxt', 'a=MeshCom Garten', 3), E('name', 'n=<img src=x onerror=1>', 4)]);
    check('RMX sens absent marker, name/atxt free text with spaces/capitals, hostile name stays text', tx('rm_v_sens_t2') === 'Second temperature: not present' && X('rm_f_atxt_v').value === 'MeshCom Garten' && X('rm_f_name_v').value === '<img src=x onerror=1>' && !P.el('rm_cards').querySelector('img'), X('rm_f_atxt_v').value);
    for (const [r, s] of [['48.40812 11.73812 492 gps', 'Source: from GPS'], ['1.5 2.5 3 set', 'Source: set by hand']]) {
      await poll([E('pos', r, 5)]);
      check('RMX pos reply "' + r + '" renders', tx('rm_v_pos_src') === s && X('rm_f_pos_lon').value === r.split(' ')[1], tx('rm_v_pos_src'));
    }
    await poll([E('name', 'n=-', 6)]);
    check('RMX name n=- (empty) leaves the input empty', X('rm_f_name_v').value === '');
    // identity
    const nm = 'rm_f_name_v', nset = () => X('rm_f_name_set');
    await typeIn(nm, 'Martin');
    check('RMX name counter and Set enabled', tx('rm_f_name_cnt') === '6/' + HDR.RM_NAME_MAX && !nset().disabled && tx('rm_f_name_note') === 'Stored exactly as typed.', tx('rm_f_name_cnt'));
    // DRY-03: the page checks the length only (against rmLen, from the header); characters, spaces and the word none
    // are the node's and the server's business (rmTextAllowed is not mirrored).
    for (const v of ['a<b', 'a=b', ' ab', 'a  b', 'NoNe']) {
      await typeIn(nm, v);
      check('DRY-03 name "' + v + '" is not judged by the page: Set stays enabled, no hint', !nset().disabled && tx('rm_f_name_hint') === '', tx('rm_f_name_hint') + '|' + nset().disabled);
    }
    for (const [v, ok] of [['a'.repeat(HDR.RM_NAME_MAX), true], ['a'.repeat(HDR.RM_NAME_MAX + 1), false]]) {
      await typeIn(nm, v);
      check('DRY-04 name of ' + v.length + ' chars: ' + (ok ? 'accepted' : 'refused "Too long."') + ' (limit ' + HDR.RM_NAME_MAX + ' from the header)', nset().disabled === !ok && tx('rm_f_name_hint') === (ok ? '' : 'Too long.'), tx('rm_f_name_hint') + '|' + nset().disabled);
    }
    for (const [v, ok] of [['a'.repeat(HDR.RM_ATXT_MAX), true], ['a'.repeat(HDR.RM_ATXT_MAX + 1), false]]) {
      await typeIn('rm_f_atxt_v', v);
      check('DRY-04 APRS text of ' + v.length + ' chars: ' + (ok ? 'accepted' : 'refused') + ' (limit ' + HDR.RM_ATXT_MAX + ' from the header)', X('rm_f_atxt_set').disabled === !ok && tx('rm_f_atxt_cnt') === v.length + '/' + HDR.RM_ATXT_MAX, tx('rm_f_atxt_cnt') + '|' + X('rm_f_atxt_set').disabled);
    }
    P.w.rmClrIn('atxt');
    await typeIn(nm, 'Martin');
    const s0 = P.sends().length;
    await P.tap(nset());
    check('RMX name Set needs two taps (first sends nothing)', P.sends().length === s0, P.sends().length);
    await P.tap(nset());
    check('RMX name Set posts the exact capitalised text', P.sends().length === s0 + 1 && /cmd=name&args=Martin(&|$)/.test(P.sends()[s0].body), JSON.stringify(P.sends().slice(s0)));
    await P.T.advance(12000);
    // fill vs focus (atxt, never typed)
    await poll([E('atxt', 'a=MeshCom Garten', 8)]);
    X('rm_f_atxt_v').focus();
    await poll([E('atxt', 'a=Other', 9)]);
    check('RMX focused, unedited input is not overwritten by a reply', X('rm_f_atxt_v').value === 'MeshCom Garten' && P.el('rm_f_atxt_v') === P.d.activeElement, X('rm_f_atxt_v').value);
    // position
    for (const [id, v, ok] of [['rm_f_pos_lat', '90.1', 0], ['rm_f_pos_lon', '-180.5', 0], ['rm_f_pos_alt', '40001', 0], ['rm_f_pos_alt', '1e2', 0], ['rm_f_pos_lat', '1,5', 0], ['rm_f_pos_lat', '48.40812', 1]]) {
      await typeIn('rm_f_pos_lat', id == 'rm_f_pos_lat' ? v : '48.4'); await typeIn('rm_f_pos_lon', id == 'rm_f_pos_lon' ? v : '11.7'); await typeIn('rm_f_pos_alt', id == 'rm_f_pos_alt' ? v : '492');
      check('RMX pos "' + v + '" ' + (ok ? 'accepted' : 'refused with hint'), X('rm_f_pos_set').disabled === !ok && (ok || tx('rm_f_pos_hint').length > 5), tx('rm_f_pos_hint'));
    }
    await typeIn('rm_f_pos_lat', '48.40812'); await typeIn('rm_f_pos_lon', '-11.7'); await typeIn('rm_f_pos_alt', '492');
    const p0 = P.sends().length;
    await P.tap(X('rm_f_pos_set')); const p1 = P.sends().length; await P.tap(X('rm_f_pos_set'));
    check('RMX pos Set: two taps, exact body', p1 === p0 && P.sends().length === p0 + 1 && /cmd=pos&args=48\.40812%20-11\.7%20492(&|$)/.test(P.sends()[p0].body), JSON.stringify(P.sends().slice(p0)));
    await P.T.advance(12000);
    for (const [t, re] of [['hidden', /does not send its position/], ['gps', /controlled by GPS/]]) {
      await poll([ent('DK5EN-1', 'pos 1 2 3', 'err ' + t, { ctr: 12, ago: 1, st: 'err', msg: SRVMSG[t] })]);
      check('RMX err ' + t + ' shows its sentence', re.test(P.text('rm_msgs')), P.text('rm_msgs').slice(0, 120));
    }
    // radio stepper bound follows a radio reply
    await poll([E('radio', 'f=433.175 sf=11 cr=5 bw=250 p=10/22', 13)]);
    check('RMX radio reply sets the TX power stepper bound (max 22)', P.w.rmCap(P.w.rmKn()) === 22, P.w.rmCap(P.w.rmKn()));
    P.w.rmPageLeave();
  }
  {
    // RMN: Network card - txq / mbox / maxhop rows, heard-list driver, details
    const srv = mkServer();
    srv.nodes[0] = { slot: 0, used: 1, call: 'DK5EN-1' };
    const TG = (r) => [{ dst: 'DK5EN-1', pending: 0, retry: r, locked: 0, canForce: 0, cap: 2 }, { dst: 'DK5EN-3', pending: 0, retry: 0, locked: 0, canForce: 0, cap: 2 }];
    srv.status = srv.mkStatus({ targets: TG(0) });
    const P = await mkPage(srv); await P.init(); P.w.rmPick('DK5EN-1');
    const X = (id) => P.el(id), tx = (id) => (X(id) ? X(id).textContent : '(none)');
    const E = (cmd, rep, o) => ent('DK5EN-1', cmd, rep, Object.assign({ ctr: /^mh /.test(cmd) ? srv.lastCtr : 30, ago: 1, st: 'ok' }, o || {}));
    const poll = (l, r) => P.setPoll({ sent: l, targets: TG(r || 0) });
    const nS = () => P.sends().length, lastBody = () => P.sends().length ? P.sends()[P.sends().length - 1].body : '';
    await poll([]);
    await poll([E('txq', 'ok q=65535/65535 bp=qrt tx=4294M rt=4294M dr=4294M u=100'), E('mbox', 'ok m=heard u=65535/65535 b=999999 a=65535/65535 st=4294M dl=4294M ak=4294M dr=4294M bl=4294M nt=4294M'), E('maxhop', 'ok t=99 p=99')]);
    check('RMN worst cases txq/mbox/maxhop parse (a= is plain key, hold in words)', tx('rm_v_txq_bp') === 'State: hold' && tx('rm_v_txq_q') === 'Queued (now/capacity): 65535/65535' && tx('rm_v_txq_u') === 'Channel use: 100 %' && tx('rm_v_mbox_a') === 'Actions last hour (done/limit): 65535/65535' && tx('rm_v_mbox_nt') === 'Notified: 4294M' && tx('rm_v_mbox_m') === 'Mode: heard' && tx('rm_v_maxhop_t') === 'Text messages: 99', tx('rm_v_mbox_a') + '|' + tx('rm_v_txq_bp'));
    await poll([E('txq', 'ok q=3/20 bp=qrs tx=- rt=1 dr=0 u=5'), E('mbox', 'err unsupported', { st: 'err', ctr: 31 })]);
    check('RMN txq state in words, absent marker; mbox unsupported sentence', tx('rm_v_txq_bp') === 'State: slow down' && tx('rm_v_txq_tx') === 'Sent: not present' && /This node has no mailbox\./.test(tx('rm_card_mbox')), tx('rm_card_mbox'));
    // driver: double press, three pages, spacing
    P.w.rmMhStart(); P.w.rmMhStart(); await flush();
    check('RMN double press starts one chain, first page asks mh 0', nS() === 1 && /cmd=mh&args=0/.test(lastBody()), nS() + ' ' + lastBody());
    await poll([E('mh 0', 'ok 999 107 DK5EN-001 180 <b>X 7 DL1ABC-1 20')], 30);
    check('RMN page 1: rows in order, hostile call is text, progress; no 2nd send while retry>0', nS() === 1 && /DK5EN-001.*180 min ago.*<b>X.*DL1ABC-1/.test(tx('rm_mh_tab')) && !X('rm_mh_tab').querySelector('b') && /Read 3 of 999/.test(tx('rm_mh_prog')), nS() + ' ' + tx('rm_mh_tab') + ' ' + tx('rm_mh_prog'));
    await poll([E('mh 0', 'ok 999 107 DK5EN-001 180 <b>X 7 DL1ABC-1 20')], 0);
    check('RMN 2nd request (mh 107) goes out once retry is 0, one in flight', nS() === 2 && /args=107/.test(lastBody()), nS() + ' ' + lastBody());
    await poll([E('mh 0', 'ok 999 107 DK5EN-001 180 <b>X 7 DL1ABC-1 20'), E('mh 107', 'ok 999 207 DL2ZZ-1 4')], 30);
    check('RMN page 2 appended, no 3rd send while retry>0', nS() === 2 && /DL2ZZ-1.*4 min ago/.test(tx('rm_mh_tab')) && /Read 4 of 999/.test(tx('rm_mh_prog')), nS() + ' ' + tx('rm_mh_prog'));
    await poll([E('mh 0', 'ok 999 107 DK5EN-001 180 <b>X 7 DL1ABC-1 20'), E('mh 107', 'ok 999 207 DL2ZZ-1 4')], 0);
    check('RMN 3rd request (mh 207) after retry 0', nS() === 3 && /args=207/.test(lastBody()), nS() + ' ' + lastBody());
    await poll([E('mh 207', 'ok 999 - DL9X-1 2')], 0);
    check('RMN next "-" ends the list cleanly, nothing more sent, button free again', nS() === 3 && /Read 5 of 999/.test(tx('rm_mh_prog')) && P.w.rmMh.on === 0, nS() + ' ' + tx('rm_mh_prog'));
    P.w.rmMhStart(); await flush(); await poll([E('mh 0', 'err end', { ago: 0, st: 'err' })], 0);
    check('RMN err end ends cleanly with its sentence', nS() === 4 && /End of the list/.test(tx('rm_mh_prog')) && P.w.rmMh.on === 0, tx('rm_mh_prog'));
    P.w.rmMhStart(); await flush(); P.w.rmMhStop(); await poll([E('mh 0', 'ok 9 12 AA1AA-1 1', { ago: 0 })], 0);
    check('RMN Stop ends at once and sends nothing further', nS() === 5 && P.w.rmMh.on === 0 && /Stopped/.test(tx('rm_mh_prog')), nS() + ' ' + tx('rm_mh_prog'));
    await poll([], 0); P.w.rmMhStart(); await flush(); await poll([E('mh 0', 'ok 9 12 AA1AA-1 1', { ago: 0 })], 30); P.w.rmPick('DK5EN-3'); await flush(); await poll([], 0);
    check('RMN node change stops the driver and clears the list', nS() === 6 && P.w.rmMh.on === 0 && P.w.rmMh.rows.length === 0 && !X('rm_mh_tab').children.length, nS() + ' ' + P.w.rmMh.rows.length + JSON.stringify(P.sends().map((s) => s.body)));
    P.w.rmPick('DK5EN-1');
    srv.sendReply = refuse('busy', { retry: 4 });
    P.w.rmMhStart(); await flush();
    check('RMN spacing refusal is not an error: no sentence, driver still on', nS() === 7 && P.w.rmMh.on === 1 && !/Two tries|refused/.test(tx('rm_msg') + tx('rm_mh_prog')), nS() + tx('rm_msg') + tx('rm_mh_prog'));
    srv.sendReply = null; await poll([], 0);
    check('RMN refused request is retried once the target allows it', nS() === 8, nS());
    P.w.rmMhStop();
    // details
    const det = async (c, rep) => { P.w.rmMhDet(c); await flush(); await poll([E('mh ' + c, rep, { ago: 0 })], 0); return tx('rm_mh_det'); };
    let d = await det('DL1ABC-1', 'ok d g=1 m=0 r=-95 s=8 la=48.4231 lo=11.7871 di=4.0 a=499 n=15 x=14 h=18 t=0');
    check('RMN details direct in words', /Gateway: yes/.test(d) && /Mesh: no/.test(d) && /RSSI: -95 dBm/.test(d) && /SNR: 8 dB/.test(d) && /Position: 48.4231, 11.7871/.test(d) && /Distance: 4.0 km/.test(d) && /Altitude: 499 m/.test(d) && /Its neighbours: 15/.test(d) && /Only it hears: 14/.test(d) && /It hears: 18/.test(d) && /Heard: 0 min ago/.test(d), d);
    d = await det('DL1ABC-2', 'ok d g=1 m=1 r=-140 s=-20 la=-90.0000 lo=-180.0000 di=9999.9 a=40000 n=255 x=255 h=255 t=65535');
    check('RMN details direct worst case', /Position: -90.0000, -180.0000/.test(d) && /Heard: 65535 min ago/.test(d) && /Distance: 9999.9 km/.test(d), d);
    d = await det('DL1ABC-3', 'ok d g=1 m=- r=- s=- la=- lo=- di=- a=- n=- x=- h=- t=-');
    check('RMN details direct absent markers', /Mesh: not present/.test(d) && /RSSI: not present/.test(d) && /Position: not present/.test(d) && /Heard: not present/.test(d), d);
    d = await det('DL1ABC-4', 'ok r h=3 k=2 g=0 m=- rc=17.8@DL2JA-2 t=3 v=DK5EN-98,DL2JA-2');
    check('RMN details routed in words, via chain with spaces', /Hops: 3/.test(d) && /Routes: 2/.test(d) && /Via gateway: no/.test(d) && /Relay: 17.8@DL2JA-2/.test(d) && /Age: 3 min/.test(d) && /Via: DK5EN-98, DL2JA-2/.test(d), d);
    d = await det('DL1ABC-5', 'ok r h=255 k=255 g=1 m=1 rc=- t=65535 v=DK5EN-001,DK5EN-002,DK5EN-003,DK5EN-004,DK5EN-005');
    check('RMN details routed worst case, unknown relay', /Relay: not known/.test(d) && /Via: DK5EN-001, DK5EN-002, DK5EN-003, DK5EN-004, DK5EN-005/.test(d), d);
    await det('DL1ABC-6', 'err unknown');
    check('RMN err unknown sentence', /That node is not known there\./.test(tx('rm_mh_prog')), tx('rm_mh_prog'));
    P.w.rmMhStart(); await flush(); await poll([E('mh 0', 'ok 999 107 DL1AB-11 180 DL1AB-12 180 DL1AB-13 180 DL1AB-14 180 DL1AB-15 180 DL1AB-16 180 DL1AB-17 180', { ago: 0 })], 30);
    check('RMN worst-case page parses', /Read 7 of 999/.test(tx('rm_mh_prog')) && /DL1AB-17180 min ago|DL1AB-17.*180 min ago/.test(tx('rm_mh_tab')), tx('rm_mh_prog'));
    // details through the real buttons; other errors, no answer and the row cap stop the driver
    P.w.rmMhStop(); await flush(); await poll([], 0);
    let n0 = nS();
    await P.tap(X('rm_mh_tab').querySelector('[data-call="DL1AB-11"]'));
    check('RMN row Details button sends mh <CALL> once', nS() === n0 + 1 && /cmd=mh&args=DL1AB-11(&|$)/.test(lastBody()), nS() + ' ' + lastBody() + ' msg=' + tx('rm_msg'));
    await poll([E('mh DL1AB-11', 'ok r h=1 k=1 g=0 m=- rc=- t=1 v=<b>X,DL2JA-2', { ago: 0 })], 0);
    check('RMN hostile via chain is text only', /Via: <b>X, DL2JA-2/.test(tx('rm_mh_det')) && !X('rm_mh_det').querySelector('b') && /Relay: not known/.test(tx('rm_mh_det')), tx('rm_mh_det'));
    P.w.rmRenderCards();
    check('3.2 no duplicate id in #rm_page after rmRenderCards() (txq, mbox, maxhop, heard list with details)', dupIds(P).length === 0 && !!X('rm_mh_det'), dupIds(P).join());
    n0 = nS();
    X('rm_f_mh_other').value = 'dl1abc-9'; X('rm_f_mh_other').dispatchEvent(new P.w.Event('input', { bubbles: true }));
    await P.tap(X('rm_card_mh').querySelector('[data-act="mhother"]'));
    check('RMN Other node input is upper-cased and sends mh <CALL>', nS() === n0 + 1 && /cmd=mh&args=DL1ABC-9(&|$)/.test(lastBody()), nS() + ' ' + lastBody());
    await poll([E('mh DL1ABC-9', 'err unknown', { ago: 0, st: 'err' })], 0);
    n0 = nS();
    await poll([], 0); P.w.rmMhStart(); await flush(); await poll([E('mh 0', 'err auth', { ago: 0, st: 'err' })], 0);
    check('RMN other error stops the driver, shows a sentence, sends nothing more', nS() === n0 + 1 && P.w.rmMh.on === 0 && tx('rm_mh_prog').length > 0 && !/End of the list/.test(tx('rm_mh_prog')), nS() + ' ' + tx('rm_mh_prog'));
    n0 = nS();
    await poll([], 0); P.w.rmMhStart(); await flush(); await poll([E('mh 0', '', { ago: 0, st: 'noanswer' })], 0);
    check('RMN no answer stops the driver with its sentence', nS() === n0 + 1 && P.w.rmMh.on === 0 && /no answer/.test(tx('rm_mh_prog')), nS() + ' ' + tx('rm_mh_prog'));
    n0 = nS();
    await poll([], 0); P.w.rmMhStart(); await flush();
    P.w.rmMh.rows = Array.from({ length: 125 }, (_, i) => ({ c: 'AA' + i, m: '1' }));
    await poll([E('mh 0', 'ok 999 500 B1 1 B2 1 B3 1 B4 1 B5 1 B6 1 B7 1', { ago: 0 })], 0);
    check('RMN list is capped at 128 rows and ends, nothing more sent', P.w.rmMh.rows.length === 128 && P.w.rmMh.on === 0 && X('rm_mh_tab').children.length === 128 && nS() === n0 + 1, P.w.rmMh.rows.length + ' ' + X('rm_mh_tab').children.length + ' ' + nS());
    // item 0: typed text must not follow to another node
    X('rm_f_mh_other').value = 'ZZ1ZZ-1'; X('rm_f_mh_other').dispatchEvent(new P.w.Event('input', { bubbles: true }));
    P.w.rmPick('DK5EN-3'); await flush();
    check('RMN typed input does not follow to another node (rmIn cleared)', X('rm_f_mh_other').value === '' && Object.keys(P.w.rmIn).length === 0, X('rm_f_mh_other').value);
    // password (dst) form: the password goes out with the chain, the field is emptied, a new run asks again
    n0 = nS(); X('rm_pw').value = 'secret123';
    await poll([], 0); P.w.rmMhStart(); await flush();
    check('RMN dst form sends dst=&pw= and empties the password field', nS() === n0 + 1 && /^dst=DK5EN-3&pw=secret123&cmd=mh&args=0$/.test(lastBody()) && X('rm_pw').value === '', nS() + ' ' + lastBody());
    await poll([ent('DK5EN-3', 'mh 0', 'ok 3 - AA1AA-1 1', { ago: 0, ctr: srv.lastCtr, st: 'ok' })], 0);
    n0 = nS(); P.w.rmMhStart(); await flush();
    check('RMN dst form: a new run without a password sends nothing and asks for it', nS() === n0 && P.w.rmMh.on === 0 && /Type the password of DK5EN-3/.test(tx('rm_msg')), nS() + ' ' + tx('rm_msg'));
    P.w.rmPageLeave();
  }
  {
    const srv = mkServer(); srv.status = srv.mkStatus({ pw: 1, on: 0, lock: 1, lockS: 0 });
    const P = await mkPage(srv); await P.init();
    check('lock true with lockS 0 shows "1 s", never "0 s"', /blocked for 1 s after/.test(P.text('rm_lockstate')), P.text('rm_lockstate'));
    P.w.rmPageLeave();
  }

  // ---- advisor rework: the forced retry and every slot send are bound to the node the page believes in ------
  {
    const srv = mkServer();
    srv.nodes[0] = { slot: 0, used: 1, call: 'DK5EN-1' };
    srv.nodes[1] = { slot: 1, used: 1, call: 'DK5EN-2' };
    const TF = (f) => [{ dst: 'DK5EN-1', pending: 0, retry: 0, locked: 0, canForce: f, cap: 2 }, { dst: 'DK5EN-2', pending: 0, retry: 0, locked: 0, canForce: f, cap: 2 }];
    srv.status = srv.mkStatus({ targets: TF(1) });
    const P = await mkPage(srv); await P.init(); P.w.rmPick('DK5EN-1');
    const X = (id) => P.el(id), tx = (id) => (X(id) ? X(id).textContent : '(none)');
    const nS = () => P.sends().length, last = () => (nS() ? P.sends()[nS() - 1].body : '');
    const fb = () => P.btn('Try once more', P.el('rm_force'));
    await P.T.advance(12000);
    srv.sendReply = refuse('limit', { canForce: 1, retry: 30 });
    await P.tap(P.btn('Refresh status', P.el('rm_info')));
    check('ADV a: refused send on DK5EN-1 offers "Try once more"', !!fb() && P.w.rmForce && P.w.rmForce.d === 'DK5EN-1' && P.w.rmForce.s === 0, JSON.stringify(P.w.rmForce));
    srv.sendReply = null;
    await P.setPoll({ targets: TF(1) });
    const forced = nS();
    P.w.rmPick('DK5EN-2');
    const stale = fb();
    check('ADV a: after a node change the button is gone and the force state cleared', !stale && P.w.rmForce === null && tx('rm_force') === '', tx('rm_force'));
    if (stale) await P.tap(stale);
    P.w.rmForceBtn(); P.w.rmPick('DK5EN-1'); await flush();
    check('ADV a: selecting the original node again does not resurrect it; nothing was sent', !fb() && nS() === forced, nS() + ' ' + forced);
    await P.T.advance(12000);
    srv.sendReply = refuse('limit', { canForce: 1, retry: 30 });
    await P.tap(P.btn('Refresh status', P.el('rm_info')));
    srv.sendReply = null;
    await P.tap(fb());
    check('ADV a: forced send carries call=<original node> and force=1', last() === 'slot=0&cmd=status&args=&call=DK5EN-1&force=1', last());
    await P.T.advance(12000);
    P.w.rmPick('DK5EN-2'); await P.T.advance(12000);
    srv.sendReply = refuse('limit', { canForce: 1, retry: 30 });
    await P.tap(P.btn('Refresh status', P.el('rm_info')));
    const fo = P.w.rmForce; P.w.rmPick('DK5EN-1'); P.w.rmForce = fo; srv.sendReply = null; const n1 = nS();
    P.w.rmForceBtn(); if (fb()) await P.tap(fb());
    check('ADV a: a force record of another node is neither shown nor sent', !fb() && nS() === n1, nS() + ' ' + n1);
    // b: call= on every slot send
    await P.T.advance(12000); P.w.rmPick('DK5EN-1'); const b0 = nS();
    P.w.rmSendCmd('radio', ''); await flush(); await P.T.advance(12000);
    P.w.rmSendCmd('txpower', '5'); await flush(); await P.T.advance(12000);
    P.w.rmSendCmd('ping', ''); await flush(); await P.T.advance(12000);
    P.w.rmSendCmd('status', ''); await flush();
    const bb = P.sends().slice(b0).map((c) => c.body);
    check('ADV b: card Read/Set, TX power, Check connection, refresh carry call= (exact bodies)', bb.length === 4 && bb[0] === 'slot=0&cmd=radio&args=&call=DK5EN-1' && bb[1] === 'slot=0&cmd=txpower&args=5&call=DK5EN-1' && /^slot=0&cmd=[a-z]+&args=&call=DK5EN-1$/.test(bb[2]) && bb[3] === 'slot=0&cmd=status&args=&call=DK5EN-1', JSON.stringify(bb));
    await P.T.advance(12000); const d0 = nS();
    P.w.rmMhStart(); await flush();
    const first = last();
    await P.setPoll({ targets: TF(0), sent: [ent('DK5EN-1', 'mh 0', 'ok 999 107 AA1AA-1 1', { ago: 0, ctr: srv.lastCtr })] });
    check('ADV b: first driver request and the follow-up page carry call=', nS() === d0 + 2 && first === 'slot=0&call=DK5EN-1&cmd=mh&args=0' && last() === 'slot=0&call=DK5EN-1&cmd=mh&args=107', first + ' | ' + last());
    // c: leaving the page stops the run, re-opening does not resume it
    P.w.rmMhStop(); await P.T.advance(12000);
    srv.sendReply = refuse('busy', { retry: 20 });
    P.w.rmMhStart(); await flush(); const c0 = nS();
    P.w.rmPageLeave(); srv.sendReply = null; await P.T.advance(60000);
    check('ADV c: leaving the page during a retry wait ends the run, nothing is sent', P.w.rmMh.on === 0 && nS() === c0 && P.w.rmMh.pre === '', nS() + ' ' + c0);
    P.w.rmPageInit(); await flush(); await P.T.advance(60000);
    check('ADV c: re-opening the page does not resume the run', P.w.rmMh.on === 0 && nS() === c0, nS() + ' ' + c0);
    P.w.rmPick('DK5EN-1');
    // d: stale identical entry with another ctr
    await P.T.advance(12000); srv.sendReply = null;
    P.w.rmMhStart(); await flush(); const d1 = nS();
    await P.setPoll({ targets: TF(0), sent: [ent('DK5EN-1', 'mh 0', 'ok 3 - ZZ1ZZ-1 1', { ago: 0, ctr: srv.lastCtr + 50 })] });
    check('ADV d: a stale identical "mh 0" entry with another ctr is not taken as the answer', P.w.rmMh.on === 1 && P.w.rmMh.rows.length === 0 && nS() === d1, P.w.rmMh.rows.length + ' ' + nS());
    await P.setPoll({ targets: TF(0), sent: [ent('DK5EN-1', 'mh 0', 'ok 3 - ZZ1ZZ-1 1', { ago: 0, ctr: srv.lastCtr })] });
    check('ADV d: the entry with the request ctr is taken', P.w.rmMh.on === 0 && P.w.rmMh.rows.length === 1, P.w.rmMh.on + ' ' + P.w.rmMh.rows.length);
    // e: the driver never hangs, retries only the spacing refusal
    P.w.rmMhStart(); await flush();
    await P.setPoll({ targets: TF(0), sent: [ent('DK5EN-1', 'mh 0', 'ok 3 - ZZ1ZZ-1 1', { ago: 0, ctr: srv.lastCtr, ver: 0 })] });
    check('ADV e: an unverified entry ends the run with the no-answer sentence', P.w.rmMh.on === 0 && /no answer/.test(tx('rm_mh_prog')), tx('rm_mh_prog'));
    P.w.rmMhStart(); await flush();
    for (let i = 0; i < 14; i++) await P.setPoll({ targets: TF(0), sent: [] });
    check('ADV e: 120 s without a matching entry ends the run', P.w.rmMh.on === 0 && /no answer/.test(tx('rm_mh_prog')), tx('rm_mh_prog'));
    for (const tk of ['limit', 'locked', 'auth']) {
      await P.T.advance(12000); srv.sendReply = { ok: false, err: tk, retry: 5, canForce: 0 };
      const e0 = nS(); P.w.rmMhStart(); await flush(); await P.T.advance(30000);
      check('ADV e: refusal "' + tk + '" ends the run, no later send', P.w.rmMh.on === 0 && nS() === e0 + 1, tk + ' ' + nS() + ' ' + e0);
    }
    // g: err slot
    srv.sendReply = refuse('slot'); await P.T.advance(12000);
    await P.tap(P.btn('Refresh status', P.el('rm_info')));
    check('ADV g: err slot shows its sentence (the server msg)', tx('rm_msg') === SRVMSG.slot, tx('rm_msg'));
    // f: position grammar
    const bad = [['lat', '0090'], ['lon', '000001'], ['lat', '.5'], ['lat', '5.'], ['alt', '000001'], ['alt', '40001'], ['lat', '90.1']];
    const good = [['lat', '-0'], ['lat', '90.0'], ['lon', '-179.123456'], ['alt', '0'], ['alt', '40000']];
    check('ADV f: position grammar refuses leading-zero runs, ".5", "5."', bad.every((q) => P.w.rmChk(q[0], q[1]) !== false), JSON.stringify(bad.filter((q) => P.w.rmChk(q[0], q[1]) === false)));
    check('ADV f: valid positions still pass', good.every((q) => P.w.rmChk(q[0], q[1]) === false), JSON.stringify(good.filter((q) => P.w.rmChk(q[0], q[1]) !== false)));
    P.w.rmPageLeave();
  }

  // ---- D6 pin switch, UI-07 single message line --------------------------------------------------
  {
    const P = await savedNodePage(STATUS_NEW);
    const pin = P.el('rm_pinsw');
    check('D6 Pin is one switch (no Pin on / Pin off buttons), starts indeterminate', pin.type === 'checkbox' && pin.getAttribute('role') === 'switch' && pin.indeterminate === true && !P.btn('Pin on') && !P.btn('Pin off') && !pin.disabled);
    P.el('rm_pin').value = 'b3'; P.el('rm_pin').dispatchEvent(new P.w.Event('change', { bubbles: true })); await flush();
    await P.tap(pin);
    check('D6 flipping the pin switch sends "setout b3 on"', P.sends().length === 1 && /cmd=setout&args=b3(%20|\+)on/.test(P.sends()[0].body), JSON.stringify(P.sends().map((x) => x.body)));
    check('D6 the switch then shows the last value sent from this page (on, no longer indeterminate)', P.el('rm_pinsw').checked === true && P.el('rm_pinsw').indeterminate === false);
    P.el('rm_pin').value = 'a1'; P.el('rm_pin').dispatchEvent(new P.w.Event('change', { bubbles: true })); await flush();
    check('D6 another pin starts indeterminate again', P.el('rm_pinsw').indeterminate === true && P.el('rm_pinsw').checked === false);
    P.w.rmPageLeave();
  }
  {
    const P = await savedNodePage(STATUS_NEW);   // mesh on
    await P.tap(P.tiles().mesh.el);
    check('UI-07 confirm hint goes to the one message line, in the confirm sentence', /^Tap again within 4 seconds to confirm: Mesh off on DK5EN-1\.$/.test(P.text('rm_msg')) && /rmconf/.test(P.el('rm_msg').className), P.text('rm_msg'));
    await P.T.advance(4100);
    check('UI-07 the hint disappears with the arming', P.text('rm_msg') === '', P.text('rm_msg'));
    await P.tap(P.btn('Send position now', P.el('rm_info')));
    check('UI-07 lock countdown shares the same line', /Next command possible in 10 s/.test(P.text('rm_msg')) && !P.el('rm_lockline'), P.text('rm_msg'));
    P.w.rmPageLeave();
  }
  {
    const P = await savedNodePage(STATUS_NEW);
    await P.tap(P.btn('Send position now', P.el('rm_info')));
    const n0 = P.sends().length;
    P.click(P.tiles().gps.el); await flush();
    check('UI-01 a switch is disabled and snaps back while the 10 s lock is on', P.sends().length === n0 && P.tiles().gps.el.disabled && P.tiles().gps.state === 'on', P.tiles().gps.state);
    P.w.rmPageLeave();
  }

  console.log(failures ? '\n' + failures + ' FAILED' : '\nall passed');
  process.exit(failures ? 1 : 0);
})().catch((e) => { console.log('FAIL harness error: ' + (e && e.stack || e)); process.exit(2); });

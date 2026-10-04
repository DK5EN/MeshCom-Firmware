// Web GUI delivery-tick check against a live node (docs/webgui-ack-ticks-verdict-20261004.md,
// Findings 1-2, plan B3/B4).
//
// Loads the real scaffold from the node, opens the Messages page through
// loadPage() the way a browser does, then drives mcProcessMessages() and
// mcRenderHistory() with crafted fragments and asserts that a delivery tick
// (<span class="mctick" data-st="N">) the browser already holds is never
// dropped or downgraded when the server re-sends the same message id without
// it (own_msg_id ring churn) or with a lower-ranked one, and that a received
// bubble never carries a tick.
//
// Needs a firmware image with the B1/B3/B4 change. Against the old firmware the
// old mcMergeEntry replaces blindly, so the "kept", "stays", "history" and
// "reload" checks FAIL there (and "own bubbles use the mctick span" fails as
// soon as the ring holds any ticked own message).
//
// Prerequisites (not part of the repo):
//   npm i jsdom@24           (anywhere; run from that directory or set NODE_PATH)
// Run:
//   node --insecure-http-parser tools/webgui_tick_test.js http://dk5en-1.local/
// The flag is needed because the node's web server ends its status line with
// a bare LF, which Node's strict HTTP parser rejects (curl and browsers
// tolerate it). The test writes nothing to the node except two reads per
// session; the synthetic messages never leave the browser.
const { TextEncoder, TextDecoder } = require('util');
const { JSDOM, VirtualConsole } = require('jsdom');

const HOST = process.argv[2] || 'http://dk5en-1.local/';
let failures = 0;
function check(name, cond, extra) {
  console.log((cond ? 'PASS ' : 'FAIL ') + name + (extra !== undefined ? '  [' + extra + ']' : ''));
  if (!cond) failures++;
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const GLYPH = { 1: '&#x2713;', 2: '&#x2611;', 3: '&#x2717;', 4: '&#x2709;' };

// One bubble the way sub_content_messages() renders it; st 0 = no tick.
function frag(id, cls, st, ts) {
  const tick = st ? '<span class="mctick" data-st="' + st + '">' + GLYPH[st] + '&nbsp;</span>' : '';
  return (
    '<div class="message ' + cls + '" data-id="' + id + '" data-dst="TEST" data-ts="' + ts + '"><div>' +
    '<p class="font-small font-bold">' + tick + '<a>X</a>&gt;TEST</p><p class="font-small font-bold">t</p><p class="font-normal">hello</p></div></div>'
  );
}

function tickOf(win, id) {
  const el = win.document.querySelector('#messages_panel .message[data-id="' + id + '"]');
  if (!el) return 'missing';
  const t = el.querySelector('.mctick');
  return t ? parseInt(t.getAttribute('data-st')) : null;
}

function histTick(win, id) {
  const e = win.mcHistory.find((m) => m.id == id);
  if (!e) return 'missing';
  const t = new win.DOMParser().parseFromString(e.html, 'text/html').querySelector('.mctick');
  return t ? parseInt(t.getAttribute('data-st')) : null;
}

async function openMessages(win) {
  // loadPage needs a sender element with a classList
  const btn = win.document.querySelector('.nav_button') || win.document.createElement('button');
  win.loadPage('messages', btn, false);
  for (let i = 0; i < 100; i++) {
    await sleep(100);
    if (win.document.getElementById('messages_panel')) break;
  }
  await sleep(300);
}

async function load() {
  const vc = new VirtualConsole();
  vc.on('jsdomError', (e) => console.log('jsdomError:', e.message));
  const dom = await JSDOM.fromURL(HOST, {
    runScripts: 'dangerously',
    resources: 'usable',
    pretendToBeVisual: true,
    virtualConsole: vc,
    beforeParse(window) {
      // jsdom's window has no TextEncoder/TextDecoder (see webgui_badge_test.js)
      window.TextEncoder = TextEncoder;
      window.TextDecoder = TextDecoder;
    },
  });
  await sleep(500);
  return dom;
}

(async () => {
  const dom = await load();
  const win = dom.window;
  check('scaffold defines mcProcessMessages/mcRenderHistory', typeof win.mcProcessMessages === 'function' && typeof win.mcRenderHistory === 'function');
  await openMessages(win);
  const panel = win.document.getElementById('messages_panel');
  check('messages page opened', !!panel);

  // B3/B1 on the live render: received bubbles carry no tick, own ticks use the machine-readable span
  const rxTicks = panel.querySelectorAll('.message-received .mctick').length;
  check('server-rendered received bubbles have no mctick', rxTicks === 0, rxTicks + ' found');
  const rxGlyph = Array.from(panel.querySelectorAll('.message-received p:first-child')).filter((p) => /[✓☑✗✉]/.test(p.textContent)).length;
  check('server-rendered received bubbles show no tick glyph (B3)', rxGlyph === 0, rxGlyph + ' found');
  const sendBubbles = Array.from(panel.querySelectorAll('.message-send'));
  const bare = sendBubbles.filter((m) => /[✓☑✗✉]/.test(m.querySelector('p').textContent) && !m.querySelector('.mctick'));
  check('server-rendered own ticks use the mctick span', bare.length === 0, sendBubbles.length + ' own bubbles, ' + bare.length + ' bare glyphs');

  const now = Math.floor(Date.now() / 1000);

  // (a) ACK tick, then the same id re-sent by the server without a tick -> tick kept
  win.mcProcessMessages(frag(910001, 'message-send', 2, now + 1), panel);
  check('(a) own bubble with ACK tick shows data-st=2', tickOf(win, 910001) === 2, tickOf(win, 910001));
  win.mcProcessMessages(frag(910001, 'message-send', 0, now + 1), panel);
  check('(a) tick kept when the server re-sends the bubble without one', tickOf(win, 910001) === 2, tickOf(win, 910001));
  check('(a) history entry keeps the tick', histTick(win, 910001) === 2, histTick(win, 910001));
  check('(a) kept bubble is still shown once', win.document.querySelectorAll('#messages_panel .message[data-id="910001"]').length === 1);

  // (b) heard -> ACK upgrades
  win.mcProcessMessages(frag(910002, 'message-send', 1, now + 2), panel);
  check('(b) heard tick shown (data-st=1)', tickOf(win, 910002) === 1, tickOf(win, 910002));
  win.mcProcessMessages(frag(910002, 'message-send', 2, now + 2), panel);
  check('(b) heard -> ACK upgrades to data-st=2', tickOf(win, 910002) === 2 && histTick(win, 910002) === 2, tickOf(win, 910002) + '/' + histTick(win, 910002));

  // (c) ACK -> heard stays ACK
  win.mcProcessMessages(frag(910003, 'message-send', 2, now + 3), panel);
  win.mcProcessMessages(frag(910003, 'message-send', 1, now + 3), panel);
  check('(c) ACK then heard stays data-st=2', tickOf(win, 910003) === 2 && histTick(win, 910003) === 2, tickOf(win, 910003) + '/' + histTick(win, 910003));
  // rank order heard < held < failed < ACK: failed beats held, held does not beat failed
  win.mcProcessMessages(frag(910004, 'message-send', 3, now + 4), panel);
  win.mcProcessMessages(frag(910004, 'message-send', 4, now + 4), panel);
  check('(c) failed then held stays data-st=3', tickOf(win, 910004) === 3, tickOf(win, 910004));
  win.mcProcessMessages(frag(910005, 'message-send', 4, now + 5), panel);
  win.mcProcessMessages(frag(910005, 'message-send', 3, now + 5), panel);
  check('(c) held then failed upgrades to data-st=3', tickOf(win, 910005) === 3, tickOf(win, 910005));

  // (d) received bubbles never gain a tick from this logic
  win.mcProcessMessages(frag(910006, 'message-received', 0, now + 6), panel);
  win.mcProcessMessages(frag(910006, 'message-received', 0, now + 6), panel);
  check('(d) received bubble stays without tick after a re-send', tickOf(win, 910006) === null, tickOf(win, 910006));
  // even if an own bubble with a tick had that id once (id collision), a received bubble must not inherit it
  win.mcProcessMessages(frag(910007, 'message-send', 2, now + 7), panel);
  win.mcProcessMessages(frag(910007, 'message-received', 0, now + 7), panel);
  check('(d) received bubble does not inherit a tick of an earlier same-id entry', tickOf(win, 910007) === null && histTick(win, 910007) === null, tickOf(win, 910007) + '/' + histTick(win, 910007));

  // (e) mcRenderHistory (page re-injected by loadPage) still shows the kept tick
  win.mcRenderHistory();
  check('(e) mcRenderHistory keeps the tick of (a)', tickOf(win, 910001) === 2, tickOf(win, 910001));
  check('(e) mcRenderHistory keeps the upgraded tick of (b)', tickOf(win, 910002) === 2, tickOf(win, 910002));

  // (f) page reload from the server: the freshly injected panel holds a bubble without tick,
  // mcRenderHistory merges it into the remembered entry -> tick survives
  panel.querySelector('.message[data-id="910001"]').outerHTML = frag(910001, 'message-send', 0, now + 1);
  check('(f) setup: panel bubble 910001 is bare', tickOf(win, 910001) === null, tickOf(win, 910001));
  win.mcRenderHistory();
  check('(f) mcRenderHistory merge of a bare server bubble keeps the remembered tick', tickOf(win, 910001) === 2 && histTick(win, 910001) === 2, tickOf(win, 910001) + '/' + histTick(win, 910001));

  dom.window.close();
  console.log(failures === 0 ? 'ALL PASS' : failures + ' FAILURES');
  process.exit(failures === 0 ? 0 : 1);
})().catch((e) => {
  console.log('HARNESS ERROR', e);
  process.exit(2);
});

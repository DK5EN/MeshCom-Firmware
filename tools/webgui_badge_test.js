// Web GUI unread-badge check against a live node (changelog item 195).
//
// Loads the real scaffold from the node, opens the Messages page through
// loadPage() the way a browser does, then drives mcProcessMessages() with
// crafted inbound fragments and asserts the badge state per tab, including
// the hidden-window rule, All-clears-everything, the persisted watermark
// across a simulated reload, and a corrupt localStorage value.
//
// Prerequisites (not part of the repo):
//   npm i jsdom@24           (anywhere; run from that directory or set NODE_PATH)
// Run:
//   node --insecure-http-parser tools/webgui_badge_test.js http://dk5en-98.local/
// The flag is needed because the node's web server ends its status line with
// a bare LF, which Node's strict HTTP parser rejects (curl and browsers
// tolerate it). The test writes nothing to the node except two reads per
// session; the synthetic messages never leave the browser.
const { JSDOM, VirtualConsole } = require('jsdom');

const HOST = process.argv[2] || 'http://dk5en-98.local/';
let failures = 0;
function check(name, cond, extra) {
  console.log((cond ? 'PASS ' : 'FAIL ') + name + (extra !== undefined ? '  [' + extra + ']' : ''));
  if (!cond) failures++;
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function badges(win) {
  const out = {};
  win.document.querySelectorAll('#mctabs .mctab').forEach((b) => {
    const badge = b.querySelector('.mcbadge');
    out[b.getAttribute('data-tab')] = {
      n: badge ? parseInt(badge.textContent) : 0,
      green: b.classList.contains('mctab-new'),
      on: b.classList.contains('mctab-on'),
    };
  });
  return out;
}

function frag(id, dst, ts, cls) {
  return (
    '<div class="message ' + cls + '" data-id="' + id + '" data-dst="' + dst + '" data-ts="' + ts + '"><div>' +
    '<p class="font-small font-bold"><a>X</a>&gt;' + dst + '</p><p class="font-small font-bold">t</p><p class="font-normal">hello</p></div></div>'
  );
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

async function load(seedStorage) {
  const vc = new VirtualConsole();
  vc.on('jsdomError', (e) => console.log('jsdomError:', e.message));
  const dom = await JSDOM.fromURL(HOST, {
    runScripts: 'dangerously',
    resources: 'usable',
    pretendToBeVisual: true,
    virtualConsole: vc,
    beforeParse(window) {
      if (seedStorage) for (const k of Object.keys(seedStorage)) window.localStorage.setItem(k, seedStorage[k]);
    },
  });
  await sleep(500);
  return dom;
}

(async () => {
  // ---------- session 1: fresh browser ----------
  let dom = await load(null);
  let win = dom.window;
  check('scaffold defines mcApplyTab/mcUnreadCount', typeof win.mcApplyTab === 'function' && typeof win.mcUnreadCount === 'function');
  await openMessages(win);
  let panel = win.document.getElementById('messages_panel');
  check('messages page opened', !!panel);
  const serverMsgs = panel.querySelectorAll('.message[data-id]');
  const withTs = panel.querySelectorAll('.message[data-ts]');
  check('server-rendered messages carry data-ts', serverMsgs.length === withTs.length, serverMsgs.length + ' msgs, ' + withTs.length + ' with ts');
  if (withTs.length) check('data-ts is a plausible unix time', parseInt(withTs[0].getAttribute('data-ts')) > 1700000000, withTs[0].getAttribute('data-ts'));

  let b = badges(win);
  check('first visit: no badge anywhere', Object.values(b).every((x) => x.n === 0 && !x.green), JSON.stringify(b));
  check('watermark persisted after seed', !!win.localStorage.getItem('mcWm'), win.localStorage.getItem('mcWm'));
  const tabs = Object.keys(b);
  const groups = tabs.filter((k) => /^[0-9]+$/.test(k));
  check('at least two group tabs on this node', groups.length >= 2, groups.join(','));
  const g1 = groups[0], g2 = groups[1];

  // select '*' so that group traffic is not on the visible tab
  win.mcTab(win.document.querySelector('#mctabs .mctab[data-tab="*"]'));
  const now = Math.floor(Date.now() / 1000);

  // inbound message on g1 while looking at '*'
  win.mcProcessMessages(frag(900001, g1, now + 1, 'message-received'), panel);
  b = badges(win);
  check('inbound on ' + g1 + ' -> badge 1 on ' + g1, b[g1].n === 1 && b[g1].green, JSON.stringify(b[g1]));
  check('inbound on ' + g1 + ' -> badge 1 on All', b.all.n === 1 && b.all.green);
  check('no badge on * (active) or ' + g2, b['*'].n === 0 && b[g2].n === 0);

  // own message on g2 must not count
  win.mcProcessMessages(frag(900002, g2, now + 2, 'message-send'), panel);
  b = badges(win);
  check('own message on ' + g2 + ' does not count', b[g2].n === 0 && b.all.n === 1);

  // ack update of the same id (re-render) must not count twice
  win.mcProcessMessages(frag(900001, g1, now + 1, 'message-received'), panel);
  b = badges(win);
  check('re-delivery of same id does not double count', b[g1].n === 1 && b.all.n === 1);

  // DM to us keys on source call (non-numeric dst)
  win.mcProcessMessages(frag(900003, 'OE1XYZ-9', now + 3, 'message-received'), panel);
  b = badges(win);
  check('DM -> badge on dm and All=2', b.dm.n === 1 && b.all.n === 2, JSON.stringify({ dm: b.dm, all: b.all }));

  // message to a group we are not in: counted under All only
  win.mcProcessMessages(frag(900004, '222', now + 4, 'message-received'), panel);
  b = badges(win);
  check('foreign group 222 -> All only (3)', b.all.n === 3 && b[g1].n === 1 && b.dm.n === 1);

  // click g1 -> its badge clears, All drops by one
  win.mcTab(win.document.querySelector('#mctabs .mctab[data-tab="' + g1 + '"]'));
  b = badges(win);
  check('click ' + g1 + ' -> ' + g1 + ' cleared, blue', b[g1].n === 0 && !b[g1].green && b[g1].on);
  check('All now 2', b.all.n === 2, b.all.n);
  check('sendcall follows group tab', win.document.getElementById('sendcall').value === g1);

  // sendMessage() with a stubbed XHR: a group destination must survive the
  // "sendmessage ok" clear, a DM call sign must not (nothing reaches the node)
  {
    const RealXHR = win.XMLHttpRequest;
    win.XMLHttpRequest = function () {
      this.readyState = 0; this.status = 0; this.responseText = '';
      this.open = function () {};
      this.send = function () { this.readyState = 4; this.status = 200; this.responseText = 'sendmessage ok'; this.onreadystatechange(); };
    };
    const sc = win.document.getElementById('sendcall');
    const mt = win.document.getElementById('messagetext');
    mt.value = 'x'; win.sendMessage();
    check('send to group keeps sendcall', sc.value === g1 && mt.value === '', JSON.stringify({ sc: sc.value, mt: mt.value }));
    sc.value = 'OE1XYZ-9'; mt.value = 'x'; win.sendMessage();
    check('send to DM call clears sendcall', sc.value === '' && mt.value === '');
    win.XMLHttpRequest = RealXHR;
    sc.value = g1;
  }

  // hidden window: message on the ACTIVE tab must stay unread
  Object.defineProperty(win.document, 'visibilityState', { configurable: true, get: () => 'hidden' });
  win.mcProcessMessages(frag(900005, g1, now + 5, 'message-received'), panel);
  b = badges(win);
  check('hidden window: inbound on active tab stays unread', b[g1].n === 1 && b[g1].green, JSON.stringify(b[g1]));
  Object.defineProperty(win.document, 'visibilityState', { configurable: true, get: () => 'visible' });
  win.document.dispatchEvent(new win.Event('visibilitychange'));
  b = badges(win);
  check('visible again -> active tab read', b[g1].n === 0 && !b[g1].green);

  // inbound on g2, then click All -> everything cleared
  win.mcProcessMessages(frag(900006, g2, now + 6, 'message-received'), panel);
  b = badges(win);
  check('inbound on ' + g2 + ' -> badge', b[g2].n === 1 && b.all.n === 3, JSON.stringify({ g2: b[g2], all: b.all }));
  win.mcTab(win.document.querySelector('#mctabs .mctab[data-tab="all"]'));
  b = badges(win);
  check('click All -> every badge cleared', Object.values(b).every((x) => x.n === 0 && !x.green), JSON.stringify(b));
  const wm = JSON.parse(win.localStorage.getItem('mcWm'));
  check('All raised every watermark to newest ts', tabs.every((k) => wm[k] === now + 6), JSON.stringify(wm));

  // now: view g2 only (so All is not touched), a message on g1 arrives, then "reload"
  win.mcTab(win.document.querySelector('#mctabs .mctab[data-tab="' + g2 + '"]'));
  win.mcProcessMessages(frag(900007, g1, now + 7, 'message-received'), panel);
  b = badges(win);
  check('pre-reload: badge on ' + g1 + ' and All', b[g1].n === 1 && b.all.n === 1);
  const stored = { mcWm: win.localStorage.getItem('mcWm'), mcTab: win.localStorage.getItem('mcTab') };
  dom.window.close();

  // ---------- session 2: reload with persisted watermarks ----------
  // The ring on the node does not hold our synthetic messages, so on reload the
  // page only shows the real ring. Verify the watermark logic on a synthetic
  // fragment whose ts sits between the g1 watermark (now+6) and "now+7".
  dom = await load(stored);
  win = dom.window;
  await openMessages(win);
  panel = win.document.getElementById('messages_panel');
  b = badges(win);
  check('reload: real ring content (older than watermark) shows no badge', Object.values(b).every((x) => x.n === 0), JSON.stringify(b));
  check('reload: selected tab restored to ' + g2, b[g2].on);
  win.mcProcessMessages(frag(900007, g1, now + 7, 'message-received'), panel);
  b = badges(win);
  check('reload: message newer than watermark is unread on ' + g1 + ' and All', b[g1].n === 1 && b.all.n === 1, JSON.stringify({ g1: b[g1], all: b.all }));
  win.mcProcessMessages(frag(900008, g1, now + 6, 'message-received'), panel);
  b = badges(win);
  check('reload: message at or below watermark is read (strict >)', b[g1].n === 1);
  dom.window.close();

  // ---------- session 3: corrupted localStorage must not break the page ----------
  dom = await load({ mcWm: '42' });
  win = dom.window;
  await openMessages(win);
  b = badges(win);
  check('corrupt mcWm -> page still renders tabs, no badge', Object.keys(b).length > 0 && Object.values(b).every((x) => x.n === 0), JSON.stringify(b));
  dom.window.close();

  console.log(failures === 0 ? 'ALL PASS' : failures + ' FAILURES');
  process.exit(failures === 0 ? 0 : 1);
})().catch((e) => {
  console.log('HARNESS ERROR', e);
  process.exit(2);
});

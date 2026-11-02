// LoRa Queue card wording check (docs/lora-qrt-dk5en98-db0taw14-20261018-verdict.md, Finding 11).
//
// Offline: takes mcRenderQueue() from /mc.js (the gunzipped blob of
// src/web_functions/web_assets.h, made from web/mc.js), runs it in jsdom against a fixture #mcq
// element and asserts the back-pressure state is worded for what it is (it only
// reacts to the node's own typed messages) and that the ring condition line
// (queued, drops, TX/RX share) is shown. Needs no node.
//
// Prerequisite: npm i jsdom@24 (anywhere; set NODE_PATH). Run:
//   node tools/webgui_queue_test.js
const { JSDOM } = require('jsdom');

const { loadAssets } = require('./webgui_assets');

const MC_JS = loadAssets().MC_JS;
const a = MC_JS.indexOf('var mcQueueOpen=true;');
if (a < 0) { console.log('FAIL cannot locate queue script in mc.js'); process.exit(1); }
const script = MC_JS.slice(a) + '\n';   // the queue block is the tail of mc.js

let failures = 0;
function check(name, cond, extra) {
  console.log((cond ? 'PASS ' : 'FAIL ') + name + (extra !== undefined ? '  [' + extra + ']' : ''));
  if (!cond) failures++;
}

let lastDoc = null;
function render(bp, bpname, extra) {
  let attrs = '<body><button id="mcqtogglebtn"></button><div id="mcq" data-ring="20" data-p0="19" data-p1="0" data-p2="5" data-p3="10" data-p4="4" data-p5="0" ' +
    'data-bp="' + bp + '" data-bpname="' + bpname + '" data-qrs="10" data-qrsf="12" data-qrt="18" data-win="1" data-rx="130000" data-tx="0" data-int="300" ' +
    'data-newid="50" data-dup="9" data-dedup="70" data-dwin="44" data-age="20" data-drop="7"' + (extra || '') + '></div></body>';
  // first duplicate attribute wins in HTML: let `extra` override the p1 default
  if (extra && extra.includes('data-p1=')) attrs = attrs.replace('data-p1="0" ', '');
  const dom = new JSDOM(attrs, { runScripts: 'outside-only' });
  dom.window.eval(script + '\nmcRenderQueue();');
  lastDoc = dom.window.document;
  return dom.window.document.getElementById('mcq').textContent.replace(/\s+/g, ' ');
}

const RING = 'Queue: 19/20 queued, 7 dropped in the last 5 min, TX 0.0 % / RX 43.3 %';

let t = render(0, 'QUIET');
check('no "Back-pressure:" label', !t.includes('Back-pressure:'), t.slice(0, 120));
check('QUIET -> "Your messages: accepted"', t.includes('Your messages: accepted'));
check('ring condition line', t.includes(RING));
check('no "waiting for ACK" when w=0', !t.includes('waiting for ACK'));
check('no outlined cell when w=0', lastDoc.querySelectorAll('.mcq-cell-wait').length === 0);
check('title is "TX queue"', t.includes('TX queue') && !t.includes('TX ring'));

t = render(0, 'QUIET', ' data-p1="3" data-w0="2" data-w1="2" data-w2="0"');
check('prio-1: 1 solid + 2 outlined', lastDoc.querySelectorAll('.mcq-cell').length === 1 + 5 + 10 + 4 &&
  lastDoc.querySelectorAll('.mcq-cell-wait').length === 2);
check('legend "2 waiting for ACK"', t.includes('2 waiting for ACK'));
check('no space before the waiting-for-ACK comma', !t.includes(' , '), t.slice(0, 200));

t = render(1, 'QRS');
check('QRS -> "slow down"', t.includes('Your messages: slow down'));
t = render(2, 'QRT');
check('QRT -> "refused"', t.includes('Your messages: refused'));
check('QRT keeps ring line', t.includes(RING));

console.log(failures ? failures + ' FAILED' : 'all passed');
process.exit(failures ? 1 : 0);

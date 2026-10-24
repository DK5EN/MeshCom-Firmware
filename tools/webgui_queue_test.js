// LoRa Queue card wording check (docs/lora-qrt-dk5en98-db0taw14-20261018-verdict.md, Finding 11).
//
// Offline: extracts mcRenderQueue() from the println() literals in
// src/web_functions/web_functions.cpp, runs it in jsdom against a fixture #mcq
// element and asserts the back-pressure state is worded for what it is (it only
// reacts to the node's own typed messages) and that the ring condition line
// (queued, drops, TX/RX share) is shown. Needs no node.
//
// Prerequisite: npm i jsdom@24 (anywhere; set NODE_PATH). Run:
//   node tools/webgui_queue_test.js
const fs = require('fs');
const path = require('path');
const { JSDOM } = require('jsdom');

const SRC = path.join(__dirname, '..', 'src', 'web_functions', 'web_functions.cpp');
const lines = fs.readFileSync(SRC, 'utf8').split('\n');
const a = lines.findIndex((l) => l.includes('web_client.println("var mcQueueOpen=true;");'));
let b = -1;
for (let i = a; i < lines.length; i++) if (lines[i].includes('println("</script>')) { b = i; break; }
if (a < 0 || b < 0) { console.log('FAIL cannot locate queue script in source'); process.exit(1); }
let script = '';
for (const l of lines.slice(a, b)) {
  const m = l.trim().match(/^web_client\.println\("(.*)"\);$/);
  if (m) script += m[1].replace(/\\(["\\n])/g, (_, c) => (c === 'n' ? '\n' : c)) + '\n';
}

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

t = render(1, 'QRS');
check('QRS -> "slow down"', t.includes('Your messages: slow down'));
t = render(2, 'QRT');
check('QRT -> "refused"', t.includes('Your messages: refused'));
check('QRT keeps ring line', t.includes(RING));

console.log(failures ? failures + ' FAILED' : 'all passed');
process.exit(failures ? 1 : 0);

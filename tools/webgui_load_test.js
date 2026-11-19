// WEB-LOAD-01: the scaffold loads mc.css, mc.js, mcau.js and mcrm.js through a retry hook, offline jsdom test.
// The RAK4631 (W5100S, 4 sockets) refuses part of the parallel asset requests; a refused <script>/<link> must be
// re-inserted (mcRe) until it loads, and what the page missed while it was missing must be caught up (mcLate).
// Source: WEBLOAD-JS-BEGIN/END and deliver_scaffold() in src/web_functions/web_functions.cpp.
// Run:   NODE_PATH=<dir>/node_modules node tools/webgui_load_test.js
const fs = require('fs');
const path = require('path');
const { JSDOM, ResourceLoader, VirtualConsole } = require('jsdom');

const WF = fs.readFileSync(path.join(__dirname, '..', 'src', 'web_functions', 'web_functions.cpp'), 'utf8');
let failures = 0;
function check(name, cond, detail) {
  console.log((cond ? 'PASS ' : 'FAIL ') + name + (detail !== undefined && !cond ? '  [' + detail + ']' : ''));
  if (!cond) failures++;
}
const lit = (s) => s.replace(/\\(["\\])/g, '$1').replace(/\\n/g, '\n');

// ---- static: every asset tag of the scaffold carries the hook ---------------------------------------------------
const sc = WF.slice(WF.indexOf('void deliver_scaffold('), WF.indexOf('</head>', WF.indexOf('void deliver_scaffold(')));
for (const a of ['mc.css', 'mc.js', 'mcau.js', 'mcrm.js']) {
  const line = sc.split('\n').find((l) => l.includes('/' + a + '?v='));
  check('scaffold tag ' + a + ' present', !!line);
  check('scaffold tag ' + a + ' has onerror=mcRe(this)', !!line && line.includes('onerror=\\"mcRe(this)\\"'), line);
}
const a = WF.indexOf('WEBLOAD-JS-BEGIN'), b = WF.indexOf('WEBLOAD-JS-END');
check('WEBLOAD-JS block present', a >= 0 && b > a);
if (a < 0 || b < a) { console.log('FAILED ' + failures); process.exit(1); }
const block = WF.slice(a, b);
const loadJs = [...block.matchAll(/"((?:[^"\\]|\\.)*)"/g)].map((m) => lit(m[1])).join('');
check('scaffold emits the hook before the first asset', sc.indexOf('WEB_LOAD_JS') >= 0 && sc.indexOf('WEB_LOAD_JS') < sc.indexOf('/mc.css?v='));

// ---- behaviour: refuse the first N requests of an asset, then serve it ------------------------------------------
const STUB = {
  '/mc.css': 'body{color:red}',
  '/mc.js': 'window.mcjsRuns=(window.mcjsRuns||0)+1;var cpage="info";function mcClockTick(){window.tick=(window.tick||0)+1;}',
  '/mcau.js': 'function auPageInit(){window.auInit=(window.auInit||0)+1;}',
  '/mcrm.js': 'window.rm=1;',
};
function run(refuse, label) {
  return new Promise((resolve) => {
    const seen = {};
    class Loader extends ResourceLoader {
      fetch(url) {
        const p = new URL(url).pathname;
        seen[p] = (seen[p] || 0) + 1;
        if (seen[p] <= (refuse[p] || 0)) return Promise.reject(new Error('ECONNRESET ' + p));
        return Promise.resolve(Buffer.from(STUB[p] || ''));
      }
    }
    const html = '<!DOCTYPE html><html><head><script>' + loadJs + '</script>' +
      '<link rel="stylesheet" href="/mc.css?v=1" onerror="mcRe(this)">' +
      '<script src="/mc.js?v=1" onerror="mcRe(this)"></script>' +
      '<script src="/mcau.js?v=1" onerror="mcRe(this)"></script>' +
      '<script src="/mcrm.js?v=1" onerror="mcRe(this)"></script></head><body></body></html>';
    const dom = new JSDOM(html, { url: 'http://node.test/', runScripts: 'dangerously', resources: new Loader(),
      pretendToBeVisual: true, virtualConsole: new VirtualConsole() });
    setTimeout(() => {
      const w = dom.window;
      resolve({ w: { mcjsRuns: w.mcjsRuns, tick: w.tick, auInit: w.auInit, rm: w.rm, links: w.document.querySelectorAll('link[rel=stylesheet]').length }, seen });
      w.close();
    }, 3500);
  });
}

(async () => {
  let r = await run({}, 'clean');
  check('clean load: mc.js runs once', r.w.mcjsRuns === 1, r.w.mcjsRuns);
  check('clean load: no retry', r.seen['/mc.js'] === 1 && r.seen['/mc.css'] === 1, JSON.stringify(r.seen));

  r = await run({ '/mc.js': 2, '/mc.css': 1, '/mcau.js': 1, '/mcrm.js': 3 }, 'refused');
  check('refused mc.js is retried and runs once', r.w.mcjsRuns === 1 && r.seen['/mc.js'] === 3, JSON.stringify(r.seen) + ' runs=' + r.w.mcjsRuns);
  check('refused mc.css is retried', r.seen['/mc.css'] === 2 && r.w.links === 1, JSON.stringify(r.seen));
  check('refused mcrm.js is retried', r.w.rm === 1 && r.seen['/mcrm.js'] === 4, JSON.stringify(r.seen));
  check('late mc.js: clock tick caught up', r.w.tick >= 1, r.w.tick);
  // every late load runs the catch-ups, auPageInit() is idempotent (starts with auPageLeave)
  check('late mcau.js on the info page: auPageInit caught up', r.w.auInit >= 1, r.w.auInit);

  r = await run({ '/mc.js': 99 }, 'dead');
  check('gives up after 8 retries', r.seen['/mc.js'] <= 9, JSON.stringify(r.seen));

  console.log(failures ? 'FAILED ' + failures : 'ALL PASS');
  process.exit(failures ? 1 : 0);
})();

// The GUI's static CSS/JS as the firmware ships them: gunzips the blobs of src/web_functions/web_assets.h
// (made by tools/gen_web_assets.py). Used by the jsdom tests. Keys: MC_CSS, MC_JS, MC_AU_JS, MC_RM_JS.
// WEB_ASSETS_H: read another copy of the header.
const fs = require('fs');
const path = require('path');
const zlib = require('zlib');

const HEADER = process.env.WEB_ASSETS_H || path.join(__dirname, '..', 'src', 'web_functions', 'web_assets.h');

function loadAssets(file) {
  const text = fs.readFileSync(file || HEADER, 'utf8');
  const out = {};
  const re = /#define WEB_(\w+)_TAG "(\w+)"\nstatic const uint8_t web_\w+_gz\[\] = \{([\s\S]*?)\};/g;
  for (const m of text.matchAll(re)) {
    const blob = Buffer.from([...m[3].matchAll(/0x([0-9a-f]{2})/g)].map((x) => parseInt(x[1], 16)));
    out[m[1]] = zlib.gunzipSync(blob).toString('utf8');
  }
  return out;
}

module.exports = { loadAssets };

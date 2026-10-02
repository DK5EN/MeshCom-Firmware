nrfutil-web.js -- Web Serial DFU for nRF52 (RAK4631 etc.), used by ../flasher.js

Contents: one ESM file exporting performDfu and enterDfuMode.
  nrfutil-web 1.0.0  (BSD-3-Clause, https://github.com/takkaO/nrfutil-web)  LICENSE-nrfutil-web
  jszip       3.10.1 (MIT option, inlined dependency)                       LICENSE-jszip

Rebuild (any scratch directory, never commit node_modules):

  npm init -y
  npm install nrfutil-web@1.0.0 esbuild
  echo 'export { performDfu, enterDfuMode } from "nrfutil-web";' > entry.js
  npx esbuild entry.js --bundle --format=esm --minify --target=es2020 \
      --legal-comments=none --outfile=pages/flash/nrfutil/nrfutil-web.js
  cp node_modules/nrfutil-web/LICENSE pages/flash/nrfutil/LICENSE-nrfutil-web
  cp node_modules/jszip/LICENSE.markdown pages/flash/nrfutil/LICENSE-jszip

// Builds the self-contained checker page: web/editor.html with swcc.wasm (base64) and the profiles inlined.
//   node wasm/mkpage.mjs build/swcc.wasm OUT.html
import { readFileSync, writeFileSync } from 'node:fs';

const [wasm, out] = process.argv.slice(2);
const profiles = {};
for (const k of ['swos9697', 'openswos']) profiles[k] = readFileSync(`profiles/${k}.json`, 'utf8');
const page = readFileSync('web/editor.html', 'utf8')
  .replace('"__SWCC_WASM__"', () => JSON.stringify(readFileSync(wasm).toString('base64')))
  .replace('= __PROFILES__;', () => '= ' + JSON.stringify(profiles).replace(/</g, '\\u003c') + ';');
writeFileSync(out, page);
console.log(`${out}: ${page.length} bytes`);

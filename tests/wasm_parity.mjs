// swcc.wasm = native swcc: the same verdict, the same diagnostics (byte for byte) and, for a valid spec, the same
// compiled bytes, for every spec given.
//   node tests/wasm_parity.mjs build/swcc build/swcc.wasm [--profile P.json] SPEC.json|DIR ...
// A DIR is read recursively (*.json). --profile applies to the arguments after it (default: profiles/swos9697.json).
import { spawnSync } from 'node:child_process';
import { readFileSync, readdirSync, statSync, mkdtempSync, rmSync } from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';

const [native, wasmPath, ...rest] = process.argv.slice(2);
if (!native || !wasmPath || !rest.length) {
  console.error('usage: node tests/wasm_parity.mjs SWCC SWCC.wasm [--profile P.json] SPEC.json|DIR ...');
  process.exit(2);
}

const { instance } = await WebAssembly.instantiate(readFileSync(wasmPath), {});
const x = instance.exports;
const enc = new TextEncoder(), dec = new TextDecoder();

function put(bytes, nul) {
  const p = x.swcc_alloc(bytes.length + (nul ? 1 : 0));
  const m = new Uint8Array(x.memory.buffer);
  m.set(bytes, p);
  if (nul) m[p + bytes.length] = 0;
  return p;
}

function cstr(p) {
  const m = new Uint8Array(x.memory.buffer);
  let e = p;
  while (m[e]) e++;
  return dec.decode(m.subarray(p, e));
}

function wasmRun(name, spec, profile) {
  x.swcc_begin();
  const n = put(enc.encode(name), true), s = put(spec, true), p = put(profile, true);
  const errors = x.swcc_run(s, spec.length, n, p, profile.length, 1);
  const bin = errors ? null : new Uint8Array(x.memory.buffer, x.swcc_bin(), x.swcc_blen()).slice();
  return { errors, diag: cstr(x.swcc_diag()), bin };
}

function nativeRun(file, profile, out) {
  const r = spawnSync(native, ['compile', file, '--profile', profile, '-o', out]);
  if (r.status !== 0 && r.status !== 1) throw new Error(`${native} ${file}: exit ${r.status}\n${r.stderr}`);
  const err = r.stderr.toString();
  if (r.status === 0) return { ok: true, diag: err, bin: readFileSync(out) };
  return { ok: false, diag: err.slice(0, err.lastIndexOf('\n', err.length - 2) + 1), bin: null };  // less "N error(s)"
}

function specs(path) {
  if (!statSync(path).isDirectory()) return [path];
  return readdirSync(path).sort().flatMap(f => {
    const p = join(path, f);
    return statSync(p).isDirectory() ? specs(p) : f.endsWith('.json') ? [p] : [];
  });
}

const tmp = mkdtempSync(join(tmpdir(), 'swcc-wasm-'));
let profile = 'profiles/swos9697.json', n = 0, valid = 0, fail = 0;
for (let i = 0; i < rest.length; i++) {
  if (rest[i] === '--profile') { profile = rest[++i]; continue; }
  const ptext = readFileSync(profile);
  for (const f of specs(rest[i])) {
    const a = nativeRun(f, profile, join(tmp, 'out.swc')), b = wasmRun(f, readFileSync(f), ptext);
    n++;
    if (a.ok) valid++;
    const why = a.ok !== !b.errors ? `verdict: native ${a.ok ? 'valid' : 'refused'}, wasm ${b.errors} error(s)`
      : a.diag !== b.diag ? `diagnostics differ:\n--- native\n${a.diag}--- wasm\n${b.diag}`
      : a.ok && Buffer.compare(a.bin, Buffer.from(b.bin)) ? `compiled bytes differ (${a.bin.length} / ${b.bin.length})`
      : '';
    if (why) { fail++; console.log(`FAIL ${f} [${profile}]: ${why}`); }
  }
}
rmSync(tmp, { recursive: true, force: true });
console.log(`wasm parity: ${n - fail}/${n} specs identical (${valid} valid, ${n - valid} refused)`);
process.exit(fail ? 1 : 0);

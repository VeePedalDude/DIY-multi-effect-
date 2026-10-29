// tests/parity/parity.mjs - run the dump.cpp scenario through build/rig.wasm in Node,
// compare against the native output, check the self-description, and time it.
import fs from 'fs';
const bytes = fs.readFileSync('build/rig.wasm');
const mod = new WebAssembly.Module(bytes);
const imports = WebAssembly.Module.imports(mod);
const X = new WebAssembly.Instance(mod, {}).exports; X._initialize?.();
let fails = 0;
const check = (ok, msg) => { console.log(`  [${ok ? 'PASS' : 'FAIL'}] ${msg}`); if (!ok) fails++; };

check(imports.length === 0, `no imports (${imports.map((i) => i.module + '.' + i.name).join(', ') || 'none'}) · ${(bytes.length / 1024).toFixed(0)} KB`);
X.rig_init(48000);
const u = new Uint8Array(X.memory.buffer, X.rig_describe()); let n = 0; while (u[n]) n++;
const desc = JSON.parse(new TextDecoder().decode(u.subarray(0, n)));
check(desc.modules.length > 0 && desc.sources.length > 0, `describe(): ${desc.modules.map((m) => m.id).join(' ')} | ${desc.sources.map((s) => s.id).join(' ')}`);

const inp = new Float32Array(fs.readFileSync('build/in.f32').buffer.slice(0));
const nat = new Float32Array(fs.readFileSync('build/out_native.f32').buffer.slice(0));
X.rig_mod(1, 0, 1, 0.22); X.rig_mod(3, 0, 0, 0.22); X.rig_mod(3, 1, 3, 0.22); X.rig_mod(4, 1, 2, 0.22);
const view = (p) => new Float32Array(X.memory.buffer, p, 128);
const out = new Float32Array(inp.length * 2);
const t0 = performance.now();
for (let i = 0; i < inp.length; i += 128) {
  if (i === 48000 * 5) { X.rig_param(3, 0, 0.8); X.rig_bypass(1, 1); X.rig_tempo(90); }
  if (i === 48000 * 8) { X.rig_bypass(1, 0); X.rig_ext(5, 0.7); X.rig_mod(2, 1, 5, 0.3); }
  view(X.rig_in()).set(inp.subarray(i, i + 128)); X.rig_process(128);
  const L = view(X.rig_out_l()), R = view(X.rig_out_r());
  for (let k = 0; k < 128; k++) { out[2 * (i + k)] = L[k]; out[2 * (i + k) + 1] = R[k]; }
}
const ms = performance.now() - t0, secs = inp.length / 48000;
let maxd = 0, sd = 0, ss = 0;
for (let i = 0; i < out.length; i++) { const d = Math.abs(out[i] - nat[i]); maxd = Math.max(maxd, d); sd += d * d; ss += nat[i] * nat[i]; }
const diffDb = 10 * Math.log10(sd / ss);
check(diffDb < -70, `vs native: diff energy ${diffDb.toFixed(1)} dB below signal (max |diff| ${maxd.toExponential(2)}; math-library rounding)`);
const M = new Float32Array(X.memory.buffer, X.rig_meters(), X.rig_meters_len()), L = desc.meters;
const adsr = desc.sources.findIndex((s) => s.id === 'adsr'), holo = desc.modules.findIndex((m) => m.id === 'holo');
console.log(`      wasm render: ${ms.toFixed(0)} ms for ${secs.toFixed(1)} s audio -> ${(100 * ms / 1000 / secs).toFixed(2)}% of one core`);
console.log(`      meters: onsets ${M[L.srcm + adsr * L.perSrc]}, nan resets ${M[L.nan]}, delay ${M[L.mod + holo * L.perMod].toFixed(1)} ms`);
if (fails) { console.log(`${fails} parity check(s) FAILED`); process.exit(1); }

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
const view = (p) => new Float32Array(X.memory.buffer, p, 128);       // re-made each use: loading files can grow memory
const out = new Float32Array(inp.length * 2);
const t0 = performance.now();
for (let i = 0; i < inp.length; i += 128) {
  if (i === 48000 * 5) { X.rig_param(3, 0, 0.8); X.rig_bypass(1, 1); X.rig_tempo(90); }
  if (i === 48000 * 8) { X.rig_bypass(1, 0); X.rig_ext(5, 0.7); X.rig_mod(2, 1, 5, 0.3); }
  if (i === 48000 * 10) { X.rig_src_param(0, 2, 0.875); X.rig_src_param(0, 4, 0.25); X.rig_mod_tap(3, 0, 90, 1.5); }   // LFO: SMOOTH RANDOM, 8 STEPS; HOLOGRAM's patch at 90 degrees, 3/2
  if (i === 48000 * 11) { X.rig_src_param(3, 1, 4 / 11); X.rig_src_param(3, 4, 1); X.rig_src_param(3, 2, 1 / 3); }   // THREE-BODY: MOTH I, synced, BODY X
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

// Scenario 2: files (see dump.cpp)
const nat2 = new Float32Array(fs.readFileSync('build/out_native2.f32').buffer.slice(0));
const cstr = (p) => { const b = new Uint8Array(X.memory.buffer, p); let k = 0; while (b[k]) k++; return new TextDecoder().decode(b.subarray(0, k)); };
// rig_asset_buf is called before reading X.memory.buffer: reserving room can grow (and detach) memory
const load = (m, file) => { const b = fs.readFileSync(file); const t = performance.now(); const p = X.rig_asset_buf(m, b.length); new Uint8Array(X.memory.buffer, p, b.length).set(b); const st = X.rig_asset_commit(m, b.length); return { st, ms: performance.now() - t }; };
X.rig_init(48000); X.rig_param(2, 1, 0.35);
const la = load(1, 'tests/models/A2.nam'), li = load(2, 'build/ir_test.rir');
check(la.st === 0 && li.st === 0, `wasm loads A2.nam in ${la.ms.toFixed(0)} ms (${cstr(X.rig_asset_status(1))}) and a 3 s stereo IR in ${li.ms.toFixed(0)} ms`);
const out2 = new Float32Array(inp.length * 2); let lstm = null;
const t2 = performance.now();
for (let i = 0; i < inp.length; i += 128) {
  if (i === 48000 * 5) lstm = load(1, 'tests/models/lstm.nam');
  if (i === 48000 * 8) X.rig_asset_clear(2);
  view(X.rig_in()).set(inp.subarray(i, i + 128));
  X.rig_process(128);
  const L = view(X.rig_out_l()), R = view(X.rig_out_r());
  for (let k = 0; k < 128; k++) { out2[2 * (i + k)] = L[k]; out2[2 * (i + k) + 1] = R[k]; }
}
const ms2 = performance.now() - t2 - (lstm ? lstm.ms : 0);
let sd2 = 0, ss2 = 0, maxd2 = 0;
for (let i = 0; i < out2.length; i++) { const d = Math.abs(out2[i] - nat2[i]); maxd2 = Math.max(maxd2, d); sd2 += d * d; ss2 += nat2[i] * nat2[i]; }
const diff2 = 10 * Math.log10(sd2 / ss2);
check(diff2 < -60, `with files vs native: diff energy ${diff2.toFixed(1)} dB below signal (max |diff| ${maxd2.toExponential(2)}; SIMD vs scalar maths in NAM)`);
console.log(`      wasm render with A2 + 3 s stereo IR: ${(100 * ms2 / 1000 / secs).toFixed(1)}% of one core`);

// Scenario 3: GEO PULSE, every block's value and meters must match native exactly (see dump.cpp)
{
  const natG = new Float32Array(fs.readFileSync('build/geo_native.f32').buffer.slice(0));
  const gt = desc.sourceTypes.find((t) => t.type === 'geo'), si = (s) => gt.settings.findIndex((q) => q.symbol === s), ki = (s) => gt.params.findIndex((q) => q.symbol === s), ci = (s) => gt.table.cols.findIndex((q) => q.symbol === s);
  X.rig_init(48000); const tx = new Uint8Array(X.memory.buffer, X.rig_text(), 64); tx.fill(0); tx.set(new TextEncoder().encode('geo')); const gk = X.rig_src_add();
  const cfg = (s, real) => { const q = gt.settings[si(s)]; X.rig_src_cfg(gk, si(s), (real - q.min) / (q.max - q.min)); };
  cfg('shape', 3); cfg('verts', 9); cfg('dir', 3); cfg('seed', 11); cfg('out', 2);
  X.rig_src_param(gk, ki('prob'), 0.7); X.rig_src_param(gk, ki('warp'), 0.5); X.rig_src_param(gk, ki('curve'), 0.8); X.rig_src_param(gk, ki('timing'), 1);
  X.rig_src_cell(gk, 4, ci('on'), 0); X.rig_src_cell(gk, 2, ci('accent'), 1); X.rig_mod(3, 0, gk, 0.3);
  const tr = new Float32Array(natG.length); let w = 0;
  for (let i = 0; i < inp.length; i += 128) {
    if (i === 48000 * 4) { cfg('shape', 7); cfg('dir', 2); X.rig_src_param(gk, ki('depth'), 0.6); }
    if (i === 48000 * 9) { cfg('shape', 1); cfg('verts', 12); cfg('skip', 4); cfg('cycles', 1); cfg('swing', 30); X.rig_ext(gk, 2); }
    view(X.rig_in()).set(inp.subarray(i, i + 128)); X.rig_process(128);
    const M = new Float32Array(X.memory.buffer, X.rig_meters(), X.rig_meters_len()), L = desc.meters;
    tr[w++] = M[L.src + gk]; tr[w++] = M[L.srcm + gk * L.perSrc]; tr[w++] = M[L.srcm + gk * L.perSrc + 1];
  }
  let same = 0, firstDiff = -1, worst = 0; for (let i = 0; i < natG.length; i++) { if (tr[i] === natG[i]) same++; else { if (firstDiff < 0) firstDiff = i; worst = Math.max(worst, Math.abs(tr[i] - natG[i])); } }
  const events = Math.floor(natG[natG.length - 1] / 32);
  check(same === natG.length || (worst < 1e-5 && natG.every((x, i) => i % 3 === 0 || x === tr[i])), `GEO PULSE (hyperbolic, random, probability, 3 shape changes, a reset): ${natG.length / 3} blocks, ${events} events; ${same === natG.length ? 'every value and meter identical' : `meters identical, values within ${worst.toExponential(1)} (first difference at block ${Math.floor(firstDiff / 3)})`}`);
}

// What files cost in WebAssembly (the browser): one file loaded, the riff played through the chain.
// "99.9%" = 99.9% of 128-sample blocks finish within this share of their 2.67 ms (this machine).
const irFile = (secs, ch) => {
  const n = Math.round(secs * 48000), b = new ArrayBuffer(16 + ch * n * 4), h = new DataView(b);
  h.setInt32(0, 0x31524952, true); h.setInt32(4, ch, true); h.setInt32(8, n, true); h.setFloat32(12, 48000, true);
  let x = 7; for (let c = 0; c < ch; c++) { const f = new Float32Array(b, 16 + c * n * 4, n); for (let i = 0; i < n; i++) { x = (x * 1664525 + 1013904223) >>> 0; f[i] = (x / 2147483648 - 1) * Math.pow(10, -3 * i / (0.9 * n)); } }
  const p = `build/ir_${secs}s_${ch}.rir`; fs.writeFileSync(p, new Uint8Array(b)); return p;
};
console.log('      file loaded (wasm)            load    CPU     99.9% of blocks within');
for (const [label, m, file] of [['built-in amp + cab', -1, null], ['A1 standard WaveNet', 1, 'tests/models/wavenet_a1_standard.nam'], ['A2 (slimmable, full)', 1, 'tests/models/A2.nam'],
  ['LSTM', 1, 'tests/models/lstm.nam'], ['IR 1 s mono', 2, irFile(1, 1)], ['IR 5 s stereo', 2, irFile(5, 2)], ['IR 20 s stereo', 2, irFile(20, 2)]]) {
  X.rig_init(48000); const l = file ? load(m, file) : { st: 0, ms: 0 };
  for (let i = 0; i < 64; i++) X.rig_process(128);                       // finish the swap
  const t = [];
  for (let i = 0; i + 128 <= inp.length; i += 128) { view(X.rig_in()).set(inp.subarray(i, i + 128)); const b0 = performance.now(); X.rig_process(128); t.push(performance.now() - b0); }
  const total = t.reduce((a, b) => a + b, 0); t.sort((a, b) => a - b);
  console.log(`      ${label.padEnd(26)} ${String(l.ms.toFixed(0)).padStart(5)} ms ${(100 * total / 1000 / secs).toFixed(1).padStart(5)}%   ${(100 * t[Math.floor(t.length * 0.999)] / (128 / 48)).toFixed(0).padStart(4)}%${l.st ? '  LOAD FAILED' : ''}`);
  if (l.st) fails++;
}
if (fails) { console.log(`${fails} parity check(s) FAILED`); process.exit(1); }

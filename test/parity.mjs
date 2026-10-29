// parity.mjs - run the same scenario through rig.wasm in Node; compare with native and time it
import fs from 'fs';
const bytes = fs.readFileSync('build/rig.wasm');
const { instance } = await WebAssembly.instantiate(bytes, {});
const X = instance.exports; X._initialize?.();
const inp = new Float32Array(fs.readFileSync('build/in.f32').buffer.slice(0));
const nat = new Float32Array(fs.readFileSync('build/out_native.f32').buffer.slice(0));
console.log('imports:', WebAssembly.Module.imports(new WebAssembly.Module(bytes)).length);
X.rig_init(48000);
X.rig_mod(1,0,1,0.22); X.rig_mod(3,0,0,0.22); X.rig_mod(3,1,3,0.22); X.rig_mod(4,1,2,0.22);
const view = p => new Float32Array(X.memory.buffer, p, 128);
const out = new Float32Array(inp.length * 2);
const t0 = performance.now();
for (let i = 0; i < inp.length; i += 128) {
  if (i === 48000*5) { X.rig_param(3,0,0.8); X.rig_bypass(1,1); X.rig_tempo(90); }
  if (i === 48000*8) { X.rig_bypass(1,0); X.rig_ext(5,0.7); X.rig_mod(2,1,5,0.3); }
  view(X.rig_in()).set(inp.subarray(i, i + 128)); X.rig_process(128);
  const L = view(X.rig_out_l()), R = view(X.rig_out_r());
  for (let k = 0; k < 128; k++) { out[2*(i+k)] = L[k]; out[2*(i+k)+1] = R[k]; }
}
const ms = performance.now() - t0, secs = inp.length / 48000;
let maxd = 0, sd = 0, ss = 0, ident = 0;
for (let i = 0; i < out.length; i++) { const d = Math.abs(out[i]-nat[i]); maxd = Math.max(maxd, d); sd += d*d; ss += nat[i]*nat[i]; if (out[i] === nat[i]) ident++; }
console.log(`wasm render: ${ms.toFixed(0)} ms for ${secs.toFixed(1)} s audio -> ${(100*ms/1000/secs).toFixed(2)}% of one core`);
console.log(`vs native: max |diff| ${maxd.toExponential(2)}, diff energy ${(10*Math.log10(sd/ss)).toFixed(1)} dB below signal, ${(100*ident/out.length).toFixed(1)}% bit-identical samples`);
const m = new Float32Array(X.memory.buffer, X.rig_meters(), 32);
console.log(`meters: onsets ${m[18]}, nan resets ${m[17]}, delay ${m[20].toFixed(1)} ms`);

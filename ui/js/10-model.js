// 10-model.js - builds MODS / SRC / PARAMS from the engine's self-description (rig_describe).
// The page instantiates the same wasm on the main thread once, just to read it, so the
// stage can be drawn before audio starts. Nothing here is specific to any one effect.
let DESC = null, LAYOUT = null;
let MODS = [], SRC = [], PARAMS = {}, SRCMAP = {};
const WASM_BYTES = b64ToBytes(RIG_WASM_B64);

function cStringMain(mem, ptr) { const u = new Uint8Array(mem.buffer, ptr); let n = 0; while (u[n]) n++; return new TextDecoder().decode(u.subarray(0, n)); }

function formatter(q) {
  const unit = q.unit ? (q.unit === '%' ? '%' : ' ' + q.unit) : '';
  const real = (v) => q.curve === 'log' ? q.min * Math.pow(q.max / q.min, v) : q.min + (q.max - q.min) * v;
  return { real, fmt: (v) => { const r = real(v); return (q.sign && r >= 0 ? '+' : '') + r.toFixed(q.decimals) + unit; } };
}

async function loadModel() {
  const { instance } = await WebAssembly.instantiate(WASM_BYTES, {});
  const X = instance.exports; if (X._initialize) X._initialize();
  X.rig_init(48000);
  DESC = JSON.parse(cStringMain(X.memory, X.rig_describe()));
  LAYOUT = DESC.meters;
  MODS = DESC.modules.map((d, mi) => {
    const m = { id: d.id, inst: mi, idx: mi, mi, name: d.name, kind: d.kind, meterNames: d.meters, asset: d.asset || null, params: [] };
    m.params = d.params.map((q, pi) => {
      const f = formatter(q);
      return { id: `${d.id}.${q.symbol}`, sym: q.symbol, label: q.label, v: q.def, def: q.def, desc: q, real: f.real, fmt: f.fmt, mod: m, mi, pi };
    });
    return m;
  });
  PARAMS = {}; MODS.forEach((m) => m.params.forEach((p) => { PARAMS[p.id] = p; }));
  let fb = 0;
  SRC = DESC.sources.map((d, i) => {
    const s = { id: d.id, name: d.name, sub: d.sub, ext: d.ext, meterNames: d.meters, idx: i, color: SOURCE_COLORS[d.id] || FALLBACK_COLORS[fb++ % FALLBACK_COLORS.length], params: [] };
    s.params = (d.params || []).map((q, pi) => {           // the source's own knobs (src_set), same shape as module params
      const f = formatter(q);
      return { id: `${d.id}.${q.symbol}`, sym: q.symbol, label: q.label, v: q.def, def: q.def, desc: q, real: f.real, fmt: f.fmt, src: s, pi };
    });
    return s;
  });
  SRCMAP = Object.fromEntries(SRC.map((s) => [s.id, s]));
  Object.keys(state.conns).forEach((pid) => { if (!PARAMS[pid] || !SRCMAP[state.conns[pid]]) delete state.conns[pid]; });
  state.focus = (MODS.find((m) => m.id === 'holo') || MODS[0]).id;
}

// Meter accessors (offsets come from the engine, never hard-coded).
const effOf = (p) => meters ? meters.m[LAYOUT.eff + p.mi * DESC.maxpar + p.pi] : p.v;
const srcVal = (i) => meters ? meters.m[LAYOUT.src + i] : 0;
function modMeter(m, name, fallback) { const k = m.meterNames.indexOf(name); return meters && k >= 0 ? meters.m[LAYOUT.mod + m.mi * LAYOUT.perMod + k] : fallback; }
function srcMeter(s, name, fallback) { const k = s.meterNames.indexOf(name); return meters && k >= 0 ? meters.m[LAYOUT.srcm + s.idx * LAYOUT.perSrc + k] : fallback; }

// 10-model.js - builds MODS / SRC / PARAMS from the engine's self-description (rig_describe).
// The page instantiates the same wasm on the main thread once, just to read it, so the
// stage can be drawn before audio starts. Nothing here is specific to any one effect or source.
//
// Sources live in a rack (engine/mod_rack.h): TYPES are the kinds the engine knows, SRC the
// instances in the rack now (several of one type is fine: "lfo", "lfo2"). Anything can be a
// patch target: a module's knob (PARAMS, "holo.feedback") or a source's knob (SPARAMS, "lfo2.rate");
// ids never clash because the engine refuses source ids that are module ids.
let DESC = null, LAYOUT = null;
let MODS = [], SRC = [], PARAMS = {}, SRCMAP = {}, TYPES = {}, TYPE_ORDER = [], SPARAMS = {};
const RACK_HOOKS = [];                    // called after the rack changes (the stage rebuilds its orbs)
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
  TYPE_ORDER = DESC.sourceTypes.map((t) => t.type);
  TYPES = Object.fromEntries(DESC.sourceTypes.map((t, i) => [t.type, { ...t, index: i }]));
  SRC = []; SRCMAP = {};
  setRack(DESC.sources);
  state.focus = (MODS.find((m) => m.id === 'holo') || MODS[0]).id;
}

// ------------------------------------------------------------------ the rack
// Display name of an instance: the type's name, numbered after the first ("LFO", "LFO 2"); a
// chosen id that isn't type + number shows as itself.
function sourceNameFor(id, type) {
  const t = TYPES[type]; if (id === type) return t.name;
  const m = id.startsWith(type) && /^\d+$/.test(id.slice(type.length)); return m ? `${t.name} ${id.slice(type.length)}` : id.toUpperCase();
}
function makeSource(id, type, slot) {
  const d = TYPES[type];
  const s = { id, type, name: sourceNameFor(id, type), sub: d.sub, ext: d.ext, meterNames: d.meters, slot: slot ?? null, idx: 0, color: null, params: [], hist: [] };
  s.params = (d.params || []).map((q, pi) => {           // the source's own knobs (src_set), same shape as module params
    const f = formatter(q);
    return { id: `${id}.${q.symbol}`, sym: q.symbol, label: q.label, v: q.def, def: q.def, desc: q, real: f.real, fmt: f.fmt, src: s, pi };
  });
  return s;
}
// The rack in order: [{ id, type, slot? }]. Instances that stay keep their knob settings.
function setRack(list) {
  const old = SRCMAP;
  SRC = list.filter((e) => TYPES[e.type]).map((e) => {
    const s = old[e.id] && old[e.id].type === e.type ? old[e.id] : makeSource(e.id, e.type, e.slot);
    if (e.slot !== undefined) s.slot = e.slot;
    return s;
  });
  reindexSources();
}
function reindexSources() {
  const seen = {};
  SRC.forEach((s, i) => { s.idx = i; seen[s.type] = (seen[s.type] || 0) + 1; s.n = seen[s.type]; s.name = sourceNameFor(s.id, s.type); s.color = instanceColor(SKIN, s.type, s.n, TYPES[s.type].index); });
  SRCMAP = Object.fromEntries(SRC.map((s) => [s.id, s]));
  SPARAMS = {}; SRC.forEach((s) => s.params.forEach((q) => { SPARAMS[q.id] = q; }));
  // patches whose source or target left the rack go with it
  Object.keys(state.conns).forEach((pid) => { if (!knobOf(pid) || !SRCMAP[state.conns[pid]]) { delete state.conns[pid]; delete state.depths[pid]; } });
  RACK_HOOKS.forEach((f) => f());
}
// The next free id for a type: "lfo", "lfo2", "lfo3"... (never a module's id)
function nextSourceId(type) {
  for (let n = 1; n < 100; n++) { const id = n === 1 ? type : type + n; if (!SRCMAP[id] && !MODS.some((m) => m.id === id)) return id; }
  return null;
}
// Any patch target by id: a module knob or a source knob.
const knobOf = (pid) => PARAMS[pid] || SPARAMS[pid];
const ownerName = (k) => (k.mod ? k.mod.name : k.src.name);
const targetLabel = (pid) => { const k = knobOf(pid); return k ? `${ownerName(k)} · ${k.label}` : pid; };

// Meter accessors (offsets come from the engine, never hard-coded). Sources by rack slot.
const effOf = (p) => meters ? meters.m[LAYOUT.eff + p.mi * DESC.maxpar + p.pi] : p.v;
const srcVal = (s) => meters && s && s.slot !== null ? meters.m[LAYOUT.src + s.slot] : 0;
const srcEff = (q) => meters && q.src.slot !== null ? meters.m[LAYOUT.seff + q.src.slot * DESC.maxpar + q.pi] : q.v;   // a source knob after cross-modulation
const knobEff = (k) => (k.mod ? effOf(k) : srcEff(k));
function modMeter(m, name, fallback) { const k = m.meterNames.indexOf(name); return meters && k >= 0 ? meters.m[LAYOUT.mod + m.mi * LAYOUT.perMod + k] : fallback; }
function srcMeter(s, name, fallback) { const k = s.meterNames.indexOf(name); return meters && k >= 0 && s.slot !== null ? meters.m[LAYOUT.srcm + s.slot * LAYOUT.perSrc + k] : fallback; }

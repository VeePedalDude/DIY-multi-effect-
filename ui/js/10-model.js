// 10-model.js - builds MODS / SRC / PARAMS from the engine's self-description (rig_describe).
// The page instantiates the same wasm on the main thread once, just to read it, so the
// stage can be drawn before audio starts. Nothing here is specific to any one effect or source.
//
// The effect chain is dynamic too (engine/fx_chain.h): MTYPES are the kinds of effect, MODS the
// instances in the chain now, in order ("holo", "holo2"). Commands address a module by its id.
// Sources live in a rack (engine/mod_rack.h): TYPES are the kinds the engine knows, SRC the
// instances in the rack now (several of one type is fine: "lfo", "lfo2"). Anything can be a
// patch target: a module's knob (PARAMS, "holo.feedback") or a source's knob (SPARAMS, "lfo2.rate");
// ids never clash because the engine refuses source ids that are module ids.
let DESC = null, LAYOUT = null;
let MODS = [], SRC = [], PARAMS = {}, SRCMAP = {}, TYPES = {}, TYPE_ORDER = [], SPARAMS = {}, MTYPES = {}, MTYPE_ORDER = [];
const RACK_HOOKS = [];                    // called after the rack changes (the stage rebuilds its orbs)
const CHAIN_HOOKS = [];                   // called after the chain changes (the stage rebuilds its cards)
const WASM_BYTES = b64ToBytes(RIG_WASM_B64);

function cStringMain(mem, ptr) { const u = new Uint8Array(mem.buffer, ptr); let n = 0; while (u[n]) n++; return new TextDecoder().decode(u.subarray(0, n)); }

// A knob's real value, its readout and, for a knob with named stops (an LFO's RATE "1/4", WAVE "SAW UP"),
// snap(v): the nearest stop, which turning it by hand lands on. A whole-number knob without a unit is a
// selector (the engine rounds it), so its readout names the position it's on; others show a stop's
// name only when they sit on it (cross-modulation glides between).
function formatter(q) {
  const unit = q.unit ? (q.unit === '%' ? '%' : ' ' + q.unit) : '';
  const real = (v) => q.curve === 'log' ? q.min * Math.pow(q.max / q.min, v) : q.min + (q.max - q.min) * v;
  const norm = (r) => q.curve === 'log' ? Math.log(r / q.min) / Math.log(q.max / q.min) : (r - q.min) / (q.max - q.min);
  const stops = q.stops || [], selector = !q.unit && q.decimals === 0;
  const num = (r) => (q.sign && r >= 0 ? '+' : '') + r.toFixed(q.decimals) + unit;
  const fmt = (v) => {
    const r = real(v); if (!stops.length) return num(r);
    if (selector) { const w = Math.round(r), s = stops.find((x) => x[0] === w); return s ? s[1] : String(w); }
    const s = stops.find((x) => Math.abs(x[0] - r) <= Math.abs(x[0]) * 0.005); return s ? s[1] : num(r);
  };
  const snap = (v) => { if (!stops.length) return v; const r = real(v); let b = stops[0];
    for (const s of stops) if (Math.abs(norm(s[0]) - norm(r)) < Math.abs(norm(b[0]) - norm(r))) b = s; return clamp(norm(b[0]), 0, 1); };
  return { real, fmt, snap };
}

async function loadModel() {
  const { instance } = await WebAssembly.instantiate(WASM_BYTES, {});
  const X = instance.exports; if (X._initialize) X._initialize();
  X.rig_init(48000);
  DESC = JSON.parse(cStringMain(X.memory, X.rig_describe()));
  LAYOUT = DESC.meters;
  MTYPE_ORDER = DESC.moduleTypes.map((t) => t.type);
  MTYPES = Object.fromEntries(DESC.moduleTypes.map((t, i) => [t.type, { ...t, index: i }]));
  MODS = []; setChain(DESC.modules);
  TYPE_ORDER = DESC.sourceTypes.map((t) => t.type);
  TYPES = Object.fromEntries(DESC.sourceTypes.map((t, i) => [t.type, { ...t, index: i }]));
  SRC = []; SRCMAP = {};
  setRack(DESC.sources);
  state.focus = (MODS.find((m) => m.id === 'holo') || MODS[0]).id;
}

// ------------------------------------------------------------------ the chain
// Display name of a module instance: the type's name, numbered after the first ("HOLOGRAM 2").
function moduleNameFor(id, type) {
  const t = MTYPES[type]; if (id === type) return t.name;
  return id.startsWith(type) && /^\d+$/.test(id.slice(type.length)) ? `${t.name} ${id.slice(type.length)}` : id.toUpperCase();
}
function makeModule(id, type, slot) {
  const d = MTYPES[type];
  // inst is what commands use to address it: its id (the engine maps ids to slots)
  const m = { id, type, inst: id, slot: slot ?? null, idx: 0, mi: 0, name: moduleNameFor(id, type), kind: d.kind, meterNames: d.meters, asset: d.asset || null, params: [] };
  m.params = d.params.map((q, pi) => {
    const f = formatter(q);
    return { id: `${id}.${q.symbol}`, sym: q.symbol, label: q.label, v: q.def, def: q.def, desc: q, real: f.real, fmt: f.fmt, snap: f.snap, mod: m, mi: 0, pi };
  });
  return m;
}
// The chain in order: [{ id, type, slot? }]. Modules that stay keep their settings.
function setChain(list) {
  const old = Object.fromEntries(MODS.map((m) => [m.id, m]));
  MODS = list.filter((e) => MTYPES[e.type]).map((e) => {
    const m = old[e.id] && old[e.id].type === e.type ? old[e.id] : makeModule(e.id, e.type, e.slot);
    if (e.slot !== undefined) m.slot = e.slot;
    return m;
  });
  reindexModules();
}
function reindexModules() {
  MODS.forEach((m, i) => { m.idx = i; m.mi = i; m.name = moduleNameFor(m.id, m.type); m.params.forEach((p) => { p.mi = i; }); });
  PARAMS = {}; MODS.forEach((m) => m.params.forEach((p) => { PARAMS[p.id] = p; }));
  Object.keys(state.conns).forEach((pid) => { if (SRC.length && (!knobOf(pid) || !SRCMAP[state.conns[pid]])) { delete state.conns[pid]; delete state.depths[pid]; delete state.taps[pid]; } });
  CHAIN_HOOKS.forEach((f) => f());
}
// The next free id for a type: "holo", "holo2"... (never a source's id)
function nextModuleId(type) {
  for (let n = 1; n < 100; n++) { const id = n === 1 ? type : type + n; if (!MODS.some((m) => m.id === id) && !SRCMAP[id]) return id; }
  return null;
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
    return { id: `${id}.${q.symbol}`, sym: q.symbol, label: q.label, v: q.def, def: q.def, desc: q, real: f.real, fmt: f.fmt, snap: f.snap, src: s, pi };
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
  Object.keys(state.conns).forEach((pid) => { if (!knobOf(pid) || !SRCMAP[state.conns[pid]]) { delete state.conns[pid]; delete state.depths[pid]; delete state.taps[pid]; } });
  RACK_HOOKS.forEach((f) => f());
}
// The next free id for a type: "lfo", "lfo2", "lfo3"... (never a module's id)
function nextSourceId(type) {
  for (let n = 1; n < 100; n++) { const id = n === 1 ? type : type + n; if (!SRCMAP[id] && !MODS.some((m) => m.id === id)) return id; }
  return null;
}
// Any patch target by id: a module knob or a source knob.
const knobOf = (pid) => PARAMS[pid] || SPARAMS[pid];
// Does the patch on this knob come from a source that taps (its own PHASE and DIV per patch)?
const tapsAt = (pid) => { const s = SRCMAP[state.conns[pid]]; return !!(s && TYPES[s.type].taps); };
const divName = (d) => { const e = (DESC.tapDivs || []).find((x) => Math.abs(x[0] - d) < 1e-3); return e ? e[1] : String(d); };
const ownerName = (k) => (k.mod ? k.mod.name : k.src.name);
const targetLabel = (pid) => { const k = knobOf(pid); return k ? `${ownerName(k)} · ${k.label}` : pid; };

// Meter accessors (offsets come from the engine, never hard-coded). Sources by rack slot.
const effOf = (p) => meters && p.mod.slot !== null ? meters.m[LAYOUT.eff + p.mod.slot * DESC.maxpar + p.pi] : p.v;
const srcVal = (s) => meters && s && s.slot !== null ? meters.m[LAYOUT.src + s.slot] : 0;
const srcEff = (q) => meters && q.src.slot !== null ? meters.m[LAYOUT.seff + q.src.slot * DESC.maxpar + q.pi] : q.v;   // a source knob after cross-modulation
const knobEff = (k) => (k.mod ? effOf(k) : srcEff(k));
function modMeter(m, name, fallback) { const k = m.meterNames.indexOf(name); return meters && k >= 0 && m.slot !== null ? meters.m[LAYOUT.mod + m.slot * LAYOUT.perMod + k] : fallback; }
function srcMeter(s, name, fallback) { const k = s.meterNames.indexOf(name); return meters && k >= 0 && s.slot !== null ? meters.m[LAYOUT.srcm + s.slot * LAYOUT.perSrc + k] : fallback; }

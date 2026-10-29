// 72-preset-format.js - the .rig.json preset file: versioned, keyed by module id and knob symbol,
// values in real units (ms, dB, Hz, %) so a knob's range can change without changing the sound.
// Pure functions over MODS/PARAMS/SRCMAP/state (unit-tested in Node).
//
//   { "format": "rig-preset", "version": 3, "name": "...", "saved": "<ISO date>",
//     "chain":  [ { "id": "holo", "bypass": false, "params": { "feedback": 42.75, "halo": 60 },
//                   "asset": { "kind": "ir", "name": "V30.wav", "size": 8236, "hash": "1a2b3c4d" } } ],
//     "sources": [ { "id": "lfo", "type": "lfo", "params": { "rate": 8, "shape": 0 } },        the rack, in order (version 3)
//                  { "id": "lfo2", "type": "lfo", "params": { "rate": 0.5 } } ],
//     "mods":   [ { "target": "holo.feedback", "source": "lfo", "depth": 0.22 },              a module knob
//                 { "target": "lfo.rate", "source": "env", "depth": 0.3 } ],                 a source knob (cross-modulation)
//     "tempo": 120, "master": -12,
//     "scenes": { "A": { "chain": [...without assets], "mods": [...], "sources": [ { "id": "lfo", "params": {...} } ], "tempo": 120 } } }
// The rack belongs to the preset; scenes change knobs and patches of the instances in it.
const PRESET_FORMAT = 'rig-preset', PRESET_VERSION = 3;
// When a module, source type or knob is renamed, add the old name here so older files still load:
//   modules: { oldId: 'newId' },  params: { 'oldId.oldSymbol': 'newId.newSymbol' },
//   sourceTypes: { oldType: 'newType' },  sourceKnobs: { 'type.oldSymbol': 'newSymbol' }
const PRESET_RENAMES = { modules: {}, params: {}, sourceTypes: {}, sourceKnobs: {} };
// When the file layout itself changes, bump PRESET_VERSION and add PRESET_MIGRATIONS[old] = (json) => upgraded json.
const PRESET_MIGRATIONS = {
  // 1 -> 2: modulation sources gained knobs. Version 1 files were made with the fixed sources, which
  // are exactly today's defaults, so they get an empty "sources" list (= defaults).
  1: (j) => ({ ...j, version: 2, sources: [], scenes: Object.fromEntries(Object.entries(j.scenes || {}).map(([k, s]) => [k, { ...s, sources: [] }])) }),
  // 2 -> 3: sources became a rack you can add to. Version 2 files were made with the six fixed
  // sources (one of each, ids = types), so that is their rack; their knob settings carry over.
  2: (j) => {
    const V2_RACK = ['lfo', 'env', 'adsr', 'orbit', 'heart', 'ctrl'], old = Array.isArray(j.sources) ? j.sources : [];
    const entry = (id) => old.find((e) => e && e.id === id);
    const rack = V2_RACK.map((t) => ({ id: t, type: t, params: (entry(t) && entry(t).params) || {} }))
      .concat(old.filter((e) => e && typeof e.id === 'string' && !V2_RACK.includes(e.id)).map((e) => ({ ...e, type: e.id })));
    return { ...j, version: 3, sources: rack };
  },
};
const SOURCE_ID = /^[a-z][a-z0-9_]{0,14}$/;

const round6 = (x) => Number(Number(x).toPrecision(6));
function toNorm(q, r) { return q.curve === 'log' ? Math.log(r / q.min) / Math.log(q.max / q.min) : (r - q.min) / (q.max - q.min); }
const unitText = (q, r) => `${round6(r)}${q.unit ? (q.unit === '%' ? '%' : ' ' + q.unit) : ''}`;

// Normalised in-memory snapshot: { params: {pid: 0..1}, bypass: {id: bool}, conns: {pid: sid}, depths: {pid: d},
// srcParams: {"lfo.rate": 0..1}, bpm }
function currentSnapshot() {
  return { params: Object.fromEntries(Object.values(PARAMS).map((p) => [p.id, p.v])), bypass: { ...state.bypass },
    conns: { ...state.conns }, depths: { ...state.depths }, bpm: state.bpm,
    srcParams: Object.fromEntries(SRC.flatMap((s) => s.params.map((q) => [q.id, q.v]))) };
}
// top: the preset's own section (sources carry their type: that's the rack); scenes list knob values only.
function sectionFromSnapshot(s, top) {
  return {
    chain: MODS.map((m) => ({ id: m.id, bypass: !!s.bypass[m.id], params: Object.fromEntries(m.params.map((p) => [p.sym, round6(p.real(s.params[p.id] ?? p.v))])) })),
    mods: Object.entries(s.conns).filter(([pid, sid]) => knobOf(pid) && SRCMAP[sid]).map(([pid, sid]) => ({ target: pid, source: sid, depth: round6((s.depths || {})[pid] ?? DEPTH) })),
    sources: SRC.filter((src) => top || src.params.length).map((src) => ({ id: src.id, ...(top ? { type: src.type } : {}), params: Object.fromEntries(src.params.map((q) => [q.sym, round6(q.real((s.srcParams || {})[q.id] ?? q.def))])) })),
    tempo: s.bpm,
  };
}
function buildPreset(name) {
  const sec = sectionFromSnapshot(currentSnapshot(), true);
  sec.chain.forEach((c) => { const a = state.assets[c.id]; if (a) c.asset = { kind: a.kind, name: a.name, size: a.size, hash: a.hash }; });
  return {
    format: PRESET_FORMAT, version: PRESET_VERSION, name, saved: new Date().toISOString(),
    chain: sec.chain.concat(state.presetExtras), mods: sec.mods, sources: sec.sources.concat(state.sourceExtras || []), tempo: sec.tempo, master: state.master,
    scenes: Object.fromEntries(Object.entries(state.scenes).map(([k, s]) => { const x = sectionFromSnapshot(s, false); return [k, { chain: x.chain, mods: x.mods, sources: x.sources, tempo: x.tempo }]; })),
  };
}
// The preset's rack: [{ id, type }] for the types this build has; entries for unknown types are kept
// aside (re-saving writes them back) with a note.
function readRack(list, renames, warn) {
  const rack = [], extras = [];
  for (const e of Array.isArray(list) ? list : []) {
    if (!e || typeof e.id !== 'string' || !SOURCE_ID.test(e.id)) { warn(`source ${e && e.id}: not a usable id; skipped`); continue; }
    if (rack.some((r) => r.id === e.id)) { warn(`source ${e.id}: listed twice; the first is used`); continue; }
    if (MODS.some((m) => m.id === e.id)) { warn(`source ${e.id}: clashes with a module's id; skipped`); continue; }
    const type = (renames.sourceTypes || {})[e.type] || e.type;
    if (!TYPES[type]) { extras.push(e); warn(`source ${e.id}: this build has no “${e.type}” source; its settings are kept in the file`); continue; }
    rack.push({ id: e.id, type });
  }
  return { rack, extras };
}
// Patch targets in a preset: module knobs, or knobs of the preset's own source instances.
function presetKnob(pid, rackTypes, renames) {
  const r = renames.params[pid] || pid;
  if (PARAMS[r]) return { pid: r, desc: PARAMS[r].desc };
  const dot = pid.lastIndexOf('.'), id = pid.slice(0, dot), type = rackTypes[id];
  if (!type) return null;
  const sym = (renames.sourceKnobs || {})[`${type}.${pid.slice(dot + 1)}`] || pid.slice(dot + 1), q = TYPES[type].params.find((x) => x.symbol === sym);
  return q ? { pid: `${id}.${sym}`, desc: q } : null;
}
function readSection(sec, renames, warn, rackTypes, extraIds) {
  const snap = { params: {}, bypass: {}, conns: {}, depths: {}, srcParams: {}, bpm: undefined }, assets = {}, extras = [];
  for (const entry of Array.isArray(sec.chain) ? sec.chain : []) {
    const id = renames.modules[entry.id] || entry.id, m = MODS.find((x) => x.id === id);
    if (!m) { extras.push(entry); warn(`${entry.id}: this build has no such module; its settings are kept in the file`); continue; }
    snap.bypass[m.id] = !!entry.bypass;
    for (const [sym, r] of Object.entries(entry.params || {})) {
      const pid = renames.params[`${entry.id}.${sym}`] || `${m.id}.${sym}`, p = PARAMS[pid];
      if (!p) { warn(`${m.id}: no knob called “${sym}”; ignored`); continue; }
      if (typeof r !== 'number' || !isFinite(r) || (p.desc.curve === 'log' && r <= 0)) { warn(`${pid}: “${r}” isn't a usable value; kept the current setting`); continue; }
      const v = toNorm(p.desc, r);
      if (v < -1e-6 || v > 1 + 1e-6) warn(`${pid}: ${unitText(p.desc, r)} is outside ${unitText(p.desc, p.desc.min)}–${unitText(p.desc, p.desc.max)}; set to the nearest end`);
      snap.params[pid] = clamp(v, 0, 1);
    }
    if (entry.asset && m.asset) assets[m.id] = entry.asset;
  }
  for (const c of Array.isArray(sec.mods) ? sec.mods : []) {
    const k = typeof c.target === 'string' ? presetKnob(c.target, rackTypes, renames) : null;
    if (!k || !rackTypes[c.source]) { if (!extraIds.has(c.source) && !extraIds.has(String(c.target).split('.')[0])) warn(`modulation ${c.source} → ${c.target}: not available in this build; skipped`); continue; }
    snap.conns[k.pid] = c.source;
    if (typeof c.depth === 'number' && c.depth >= 0 && c.depth <= 1) snap.depths[k.pid] = c.depth;
  }
  for (const entry of Array.isArray(sec.sources) ? sec.sources : []) {      // knobs left out stay at their defaults
    const id = entry && entry.id, type = rackTypes[id];
    if (!type) { if (!extraIds.has(id)) warn(`source ${id}: not in this preset's rack; its settings were skipped`); continue; }
    for (const [sym0, r] of Object.entries(entry.params || {})) {
      const sym = (renames.sourceKnobs || {})[`${type}.${sym0}`] || sym0, q = TYPES[type].params.find((x) => x.symbol === sym), qid = `${id}.${sym}`;
      if (!q) { warn(`${id}: no knob called “${sym0}”; ignored`); continue; }
      if (typeof r !== 'number' || !isFinite(r) || (q.curve === 'log' && r <= 0)) { warn(`${qid}: “${r}” isn't a usable value; kept the default`); continue; }
      const v = toNorm(q, r);
      if (v < -1e-6 || v > 1 + 1e-6) warn(`${qid}: ${unitText(q, r)} is outside ${unitText(q, q.min)}–${unitText(q, q.max)}; set to the nearest end`);
      snap.srcParams[qid] = clamp(v, 0, 1);
    }
  }
  if (typeof sec.tempo === 'number' && sec.tempo >= 30 && sec.tempo <= 300) snap.bpm = sec.tempo;
  return { snap, assets, extras };
}
// -> { name, snap, assets, extras, rack, sourceExtras, scenes, master, warnings }  or  { error }
function readPreset(json, renames = PRESET_RENAMES) {
  renames = { modules: {}, params: {}, sourceTypes: {}, sourceKnobs: {}, ...renames };
  if (!json || typeof json !== 'object' || json.format !== PRESET_FORMAT) return { error: 'not a RIG preset file' };
  const warnings = [], warn = (w) => { if (!warnings.includes(w)) warnings.push(w); };
  let v = Number.isInteger(json.version) ? json.version : 0;
  if (v > PRESET_VERSION) warn(`saved by a newer version (format ${v}); loading what this build understands`);
  while (v < PRESET_VERSION) { if (!PRESET_MIGRATIONS[v]) { warn(`no upgrade path from format ${v}`); break; } json = PRESET_MIGRATIONS[v](json); v++; }
  const { rack, extras: sourceExtras } = readRack(json.sources, renames, warn);
  const rackTypes = Object.fromEntries(rack.map((r) => [r.id, r.type])), extraIds = new Set(sourceExtras.map((e) => e.id));
  const main = readSection(json, renames, warn, rackTypes, extraIds), scenes = {};
  for (const [k, s] of Object.entries(json.scenes || {})) if (/^[A-D]$/.test(k) && s && typeof s === 'object') scenes[k] = readSection(s, renames, (w) => warn(`scene ${k}: ${w}`), rackTypes, extraIds).snap;
  const master = typeof json.master === 'number' && json.master >= -60 && json.master <= 6 ? json.master : undefined;
  return { name: typeof json.name === 'string' ? json.name : '', snap: main.snap, assets: main.assets, extras: main.extras, rack, sourceExtras, scenes, master, warnings };
}

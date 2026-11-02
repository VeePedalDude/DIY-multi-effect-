// 72-preset-format.js - the .rig.json preset file: versioned, keyed by module id and knob symbol,
// values in real units (ms, dB, Hz, %) so a knob's range can change without changing the sound.
// Pure functions over MODS/PARAMS/SRCMAP/state (unit-tested in Node).
//
//   { "format": "rig-preset", "version": 1, "name": "...", "saved": "<ISO date>",
//     "chain":  [ { "id": "holo", "bypass": false, "params": { "feedback": 42.75, "halo": 60 },
//                   "asset": { "kind": "ir", "name": "V30.wav", "size": 8236, "hash": "1a2b3c4d" } } ],
//     "mods":   [ { "target": "holo.feedback", "source": "lfo", "depth": 0.22 } ],
//     "tempo": 120, "master": -12,
//     "scenes": { "A": { "chain": [...without assets], "mods": [...], "tempo": 120 } } }
const PRESET_FORMAT = 'rig-preset', PRESET_VERSION = 1;
// When a module or knob is renamed, add the old name here so older files still load:
//   modules: { oldId: 'newId' },  params: { 'oldId.oldSymbol': 'newId.newSymbol' }
const PRESET_RENAMES = { modules: {}, params: {} };
// When the file layout itself changes, bump PRESET_VERSION and add PRESET_MIGRATIONS[old] = (json) => upgraded json.
const PRESET_MIGRATIONS = {};

const round6 = (x) => Number(Number(x).toPrecision(6));
function toNorm(q, r) { return q.curve === 'log' ? Math.log(r / q.min) / Math.log(q.max / q.min) : (r - q.min) / (q.max - q.min); }
const unitText = (q, r) => `${round6(r)}${q.unit ? (q.unit === '%' ? '%' : ' ' + q.unit) : ''}`;

// Normalised in-memory snapshot: { params: {pid: 0..1}, bypass: {id: bool}, conns: {pid: sid}, depths: {pid: d}, bpm }
function currentSnapshot() {
  return { params: Object.fromEntries(Object.values(PARAMS).map((p) => [p.id, p.v])), bypass: { ...state.bypass },
    conns: { ...state.conns }, depths: { ...state.depths }, bpm: state.bpm };
}
function sectionFromSnapshot(s) {
  return {
    chain: MODS.map((m) => ({ id: m.id, bypass: !!s.bypass[m.id], params: Object.fromEntries(m.params.map((p) => [p.sym, round6(p.real(s.params[p.id] ?? p.v))])) })),
    mods: Object.entries(s.conns).map(([pid, sid]) => ({ target: pid, source: sid, depth: round6((s.depths || {})[pid] ?? DEPTH) })),
    tempo: s.bpm,
  };
}
function buildPreset(name) {
  const sec = sectionFromSnapshot(currentSnapshot());
  sec.chain.forEach((c) => { const a = state.assets[c.id]; if (a) c.asset = { kind: a.kind, name: a.name, size: a.size, hash: a.hash }; });
  return {
    format: PRESET_FORMAT, version: PRESET_VERSION, name, saved: new Date().toISOString(),
    chain: sec.chain.concat(state.presetExtras), mods: sec.mods, tempo: sec.tempo, master: state.master,
    scenes: Object.fromEntries(Object.entries(state.scenes).map(([k, s]) => { const x = sectionFromSnapshot(s); return [k, { chain: x.chain, mods: x.mods, tempo: x.tempo }]; })),
  };
}
function readSection(sec, renames, warn) {
  const snap = { params: {}, bypass: {}, conns: {}, depths: {}, bpm: undefined }, assets = {}, extras = [];
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
    const pid = renames.params[c.target] || c.target;
    if (!PARAMS[pid] || !SRCMAP[c.source]) { warn(`modulation ${c.source} → ${c.target}: not available in this build; skipped`); continue; }
    snap.conns[pid] = c.source;
    if (typeof c.depth === 'number' && c.depth >= 0 && c.depth <= 1) snap.depths[pid] = c.depth;
  }
  if (typeof sec.tempo === 'number' && sec.tempo >= 30 && sec.tempo <= 300) snap.bpm = sec.tempo;
  return { snap, assets, extras };
}
// -> { name, snap, assets, extras, scenes, master, warnings }  or  { error }
function readPreset(json, renames = PRESET_RENAMES) {
  if (!json || typeof json !== 'object' || json.format !== PRESET_FORMAT) return { error: 'not a RIG preset file' };
  const warnings = [], warn = (w) => { if (!warnings.includes(w)) warnings.push(w); };
  let v = Number.isInteger(json.version) ? json.version : 0;
  if (v > PRESET_VERSION) warn(`saved by a newer version (format ${v}); loading what this build understands`);
  while (v < PRESET_VERSION) { if (!PRESET_MIGRATIONS[v]) { warn(`no upgrade path from format ${v}`); break; } json = PRESET_MIGRATIONS[v](json); v++; }
  const main = readSection(json, renames, warn), scenes = {};
  for (const [k, s] of Object.entries(json.scenes || {})) if (/^[A-D]$/.test(k) && s && typeof s === 'object') scenes[k] = readSection(s, renames, (w) => warn(`scene ${k}: ${w}`)).snap;
  const master = typeof json.master === 'number' && json.master >= -60 && json.master <= 6 ? json.master : undefined;
  return { name: typeof json.name === 'string' ? json.name : '', snap: main.snap, assets: main.assets, extras: main.extras, scenes, master, warnings };
}

// 22-rack.js - the modulation rack from the page's side: adding, removing and duplicating source
// instances, and keeping the page's list in step with the engine's.
// The page decides what should be in the rack (and re-sends that when audio starts); the engine
// decides which slot each instance lives in, and says so in a {type:'rack'} message after every
// change. Patches to or from an instance that leaves the rack go with it (reindexSources).
const rackFull = () => SRC.length >= DESC.maxSources;

function addSource(type, id) {
  if (!TYPES[type]) return null;
  if (rackFull()) { logLine(`the rack is full: ${DESC.maxSources} sources at most`, true); return null; }
  id = id || nextSourceId(type);
  const s = makeSource(id, type, null); SRC.push(s); reindexSources();
  rig.cmd(`src_add ${type} ${id}`);
  logLine(`added ${s.name} (${id})`);
  return s;
}
function removeSource(s) {
  rig.cmd(`src_remove ${s.id}`);                       // the engine unpatches everything it touched
  SRC = SRC.filter((x) => x !== s);
  if (state.carrying === s.id) state.carrying = null;
  if (state.focus === 'src:' + s.id) state.focus = (SRC[Math.min(s.idx, SRC.length - 1)] ? 'src:' + SRC[Math.min(s.idx, SRC.length - 1)].id : MODS[0].id);
  reindexSources();
  logLine(`removed ${s.name} (${s.id})`);
}
// Another of the same kind, with the same knobs, settings and table (not its patches).
function duplicateSource(s) {
  const d = addSource(s.type); if (!d) return null;
  s.params.forEach((q, i) => setSrcParam(d.params[i], q.v));
  s.settings.forEach((q, i) => setSrcSetting(d.settings[i], q.v));
  if (s.table) s.table.v.forEach((row, r) => row.forEach((v, j) => { if (v !== d.table.v[r][j]) setSrcCell(d, r, j, v); }));
  return d;
}
// The rack a preset asks for, in its order: [{ id, type }]. Keeps instances that match (id and type).
function applyRack(list) {
  const want = list.filter((e) => TYPES[e.type]);
  SRC.filter((s) => !want.some((e) => e.id === s.id && e.type === s.type)).forEach((s) => rig.cmd(`src_remove ${s.id}`));
  want.filter((e) => !SRCMAP[e.id] || SRCMAP[e.id].type !== e.type).forEach((e) => rig.cmd(`src_add ${e.type} ${e.id}`));
  if (state.carrying && !want.some((e) => e.id === state.carrying)) state.carrying = null;
  setRack(want);
  if (typeof state.focus === 'string' && state.focus.startsWith('src:') && !SRCMAP[state.focus.slice(4)]) state.focus = MODS[0].id;
}
// The engine's view: which slot each instance is in.
function onEngineRack(list) {
  const slot = Object.fromEntries(list.map((e) => [e.id, e.slot]));
  SRC.forEach((s) => { s.slot = s.id in slot ? slot[s.id] : null; s.hist = []; });
}
// The engine refused a src_add (rack full, id taken): take the instance back out of the page.
function rackRefused(line, status) {
  const id = line.split(/\s+/)[2], s = SRCMAP[id]; if (!s) return;
  SRC = SRC.filter((x) => x !== s); reindexSources(); update();
  logLine(`couldn't add ${id}: ${status === -3 ? 'the rack is full' : status === -2 ? 'that id is taken or not allowed' : 'unknown source type'}`, true);
}
// When audio starts the engine has a fresh rack (one of each type, ids = types): make it the page's.
function syncRack() {
  const fresh = Object.fromEntries(DESC.sources.map((e) => [e.id, e.type]));
  Object.keys(fresh).forEach((id) => { if (!SRCMAP[id] || SRCMAP[id].type !== fresh[id]) rig.cmd(`src_remove ${id}`); });
  SRC.forEach((s) => { if (fresh[s.id] !== s.type) rig.cmd(`src_add ${s.type} ${s.id}`); });
}
rig.rack = { add: addSource, remove: (id) => SRCMAP[id] && removeSource(SRCMAP[id]), duplicate: (id) => SRCMAP[id] && duplicateSource(SRCMAP[id]),
  list: () => SRC.map((s) => ({ id: s.id, type: s.type, slot: s.slot, name: s.name })) };

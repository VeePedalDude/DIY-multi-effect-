// 24-chain.js - the effect chain from the page's side: adding, removing, duplicating and moving
// modules, and keeping the page's list in step with the engine's.
// The page decides what's in the chain and in which order (and re-sends that when audio starts);
// the engine decides which slot each instance lives in, and says so in a {type:'chain'} message after
// every change. A new module fades in from dry, a removed one fades out, a move dips the sound for
// 10 ms (engine.h), so all of this can happen while you play.
const chainFull = () => MODS.length >= DESC.maxModules;

// Add a module of `type` at chain position `pos` (default: the end). Returns it, or null.
function addModule(type, pos, id) {
  if (!MTYPES[type]) return null;
  if (chainFull()) { logLine(`the chain is full: ${DESC.maxModules} modules at most`, true); return null; }
  id = id || nextModuleId(type); pos = pos === undefined || pos < 0 || pos > MODS.length ? MODS.length : pos;
  const m = makeModule(id, type, null);
  MODS.splice(pos, 0, m); reindexModules();
  rig.cmd(`fx_add ${type} ${id} ${pos}`);
  logLine(`added ${m.name} (${id}) at position ${pos + 1}`);
  return m;
}
function removeModule(m) {
  rig.cmd(`fx_remove ${m.id}`);                          // the engine fades it out and frees it
  const i = MODS.indexOf(m);
  MODS = MODS.filter((x) => x !== m);
  delete state.bypass[m.id]; delete state.assets[m.id]; delete MODDATA[m.id];
  if (state.focus === m.id) state.focus = MODS.length ? MODS[Math.min(i, MODS.length - 1)].id : 'addfx';
  reindexModules();                                      // drops patches to its knobs
  logLine(`removed ${m.name} (${m.id})`);
}
// Another of the same kind right after it, with the same knobs, bypass and file.
function duplicateModule(m) {
  const d = addModule(m.type, MODS.indexOf(m) + 1); if (!d) return null;
  m.params.forEach((p, i) => setParam(d.params[i], p.v));
  if (state.bypass[m.id]) { state.bypass[d.id] = true; rig.cmd(`bypass ${d.id} 1`); }
  if (state.assets[m.id] && !state.assets[m.id].missing && typeof selectAsset === 'function') selectAsset(d, state.assets[m.id].name);
  return d;
}
// Move a module by `delta` places (or to position `to`).
function moveModule(m, delta, to) {
  const i = MODS.indexOf(m), j = to !== undefined ? to : clamp(i + delta, 0, MODS.length - 1);
  if (i < 0 || j === i) return;
  MODS.splice(i, 1); MODS.splice(j, 0, m); reindexModules();
  rig.cmd(`fx_move ${m.id} ${j}`);
}
// The chain a preset asks for, in its order: [{ id, type }]. Keeps instances that match (id and type).
function applyChain(list) {
  const want = list.filter((e) => MTYPES[e.type]);
  MODS.filter((m) => !want.some((e) => e.id === m.id && e.type === m.type)).forEach((m) => {
    rig.cmd(`fx_remove ${m.id}`); delete state.bypass[m.id]; delete state.assets[m.id]; delete MODDATA[m.id];
  });
  const kept = new Set(MODS.filter((m) => want.some((e) => e.id === m.id && e.type === m.type)).map((m) => m.id));
  want.forEach((e, i) => { if (!kept.has(e.id)) rig.cmd(`fx_add ${e.type} ${e.id} ${i}`); else rig.cmd(`fx_move ${e.id} ${i}`); });
  setChain(want);
  if (!MODS.some((m) => m.id === state.focus) && !String(state.focus).startsWith('src:')) state.focus = MODS.length ? MODS[0].id : 'addfx';
}
// The engine's view: which slot each instance is in.
function onEngineChain(list) {
  const slot = Object.fromEntries(list.map((e) => [e.id, e.slot]));
  MODS.forEach((m) => { m.slot = m.id in slot ? slot[m.id] : null; });
}
// The engine refused an fx_add (chain full, id taken): take it back out of the page.
function chainRefused(line, status) {
  const id = line.split(/\s+/)[2], m = MODS.find((x) => x.id === id); if (!m) return;
  MODS = MODS.filter((x) => x !== m); reindexModules(); update();
  logLine(`couldn't add ${id}: ${status === -3 ? 'the chain is full' : status === -2 ? 'that id is taken or not allowed' : 'unknown module type'}`, true);
}
// When audio starts the engine has a fresh chain (one of each type, ids = types): make it the page's.
function syncChain() {
  const fresh = Object.fromEntries(DESC.modules.map((e) => [e.id, e.type]));
  Object.keys(fresh).forEach((id) => { if (!MODS.some((m) => m.id === id && m.type === fresh[id])) rig.cmd(`fx_remove ${id}`); });
  MODS.forEach((m, i) => rig.cmd(fresh[m.id] === m.type ? `fx_move ${m.id} ${i}` : `fx_add ${m.type} ${m.id} ${i}`));
}
rig.chain = { add: (type, pos) => addModule(type, pos), remove: (id) => { const m = MODS.find((x) => x.id === id); if (m) removeModule(m); },
  duplicate: (id) => { const m = MODS.find((x) => x.id === id); return m ? duplicateModule(m) : null; },
  move: (id, to) => { const m = MODS.find((x) => x.id === id); if (m) moveModule(m, 0, to); },
  list: () => MODS.map((m) => ({ id: m.id, type: m.type, slot: m.slot, name: m.name })) };

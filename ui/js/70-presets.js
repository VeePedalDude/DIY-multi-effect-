// 70-presets.js - scenes A-D (in memory, saved inside presets) and preset files in the library's presets folder.
function snapshot() { return currentSnapshot(); }
function applySnapshot(s) {
  MODS.forEach((m) => m.params.forEach((p) => { if (s.params && s.params[p.id] !== undefined) p.v = s.params[p.id]; }));
  state.bypass = { ...(s.bypass || {}) };
  state.conns = Object.fromEntries(Object.entries(s.conns || {}).filter(([pid, sid]) => knobOf(pid) && SRCMAP[sid]));
  state.depths = { ...(s.depths || {}) };
  state.taps = { ...(s.taps || {}) };
  SRC.forEach((src) => src.params.forEach((q) => { q.v = s.srcParams && s.srcParams[q.id] !== undefined ? s.srcParams[q.id] : q.def; }));
  SRC.forEach((src) => src.settings.forEach((q) => { q.v = s.srcCfg && s.srcCfg[q.id] !== undefined ? s.srcCfg[q.id] : q.def; }));
  SRC.forEach((src) => { if (!src.table) return; const rows = s.srcCells && s.srcCells[src.id];
    src.table.v = Array.from({ length: src.table.rows }, (_, r) => src.table.cols.map((c, j) => (rows && rows[r] && rows[r][j] !== undefined ? rows[r][j] : c.def))); });
  if (s.bpm) { state.bpm = s.bpm; $('bpm').textContent = Math.round(s.bpm); }
  sendAll(); syncCables(); update();
}
function setMaster(db) { state.master = db; $('master').value = db; $('masterVal').textContent = db + ' dB'; rig.cmd(`master ${db}`); }
function refreshPresetList() {
  const sel = $('presetSel'), keep = sel.value; sel.innerHTML = '';
  (library.entries.presets || []).forEach((e) => { const o = document.createElement('option'); o.value = e.name; o.textContent = (e.meta && e.meta.name) || e.name.replace(/\.rig\.json$/i, ''); sel.appendChild(o); });
  if (keep && [...sel.options].some((o) => o.value === keep)) sel.value = keep;
  $('presetLoad').disabled = $('presetDelete').disabled = !sel.options.length;
}
function presetNote(text, warn) { const n = $('presetNote'); n.textContent = text; n.className = 'note' + (warn ? ' warnline' : ''); }

async function savePreset() {
  if (!library.core) return;
  const name = $('presetInput').value.trim() || 'Untitled', file = safeFileName(name) + '.rig.json';
  const existed = (library.entries.presets || []).some((e) => e.name === file);
  await library.core.write('presets', file, JSON.stringify(buildPreset(name), null, 2));
  await refreshLibrary(); $('presetSel').value = file; $('presetName').textContent = name;
  presetNote(`${existed ? 'Replaced' : 'Saved'} presets/${file}`); logLine(`preset saved: presets/${file}`);
}
async function loadPresetFile(file) {
  let json; try { json = JSON.parse(await (await library.core.file('presets', file)).text()); } catch (e) { presetNote(`Couldn't read ${file}: ${e.message}`, true); return null; }
  const r = readPreset(json);
  if (r.error) { presetNote(`${file}: ${r.error}`, true); return r; }
  applyChain(r.chain); applyRack(r.rack); state.sourceExtras = r.sourceExtras;
  applySnapshot(r.snap);
  if (r.master !== undefined) setMaster(r.master);
  state.scenes = r.scenes; state.scene = null; state.presetExtras = r.extras;
  document.querySelectorAll('.scene').forEach((x) => { x.classList.toggle('stored', !!state.scenes[x.dataset.scene]); x.setAttribute('aria-pressed', 'false'); });
  for (const m of MODS) {
    if (!m.asset) continue;
    const ref = r.assets[m.id];
    if (!ref) { if (state.assets[m.id]) clearAsset(m); continue; }
    const found = await library.core.findRef(ref);
    if (found) { await selectAsset(m, found.name); if (found.name !== ref.name) r.warnings.push(`${m.id}: ${ref.name} was found renamed as ${found.name}`); }
    else {                                    // keep the reference so re-saving doesn't lose it; play built-in meanwhile
      r.warnings.push(`${m.id}: ${ref.kind}/${ref.name} isn't in the library; using built-in until you add it`);
      if (rig.node) rig.node.port.postMessage({ type: 'asset_clear', inst: m.inst });
      state.assets[m.id] = { kind: ref.kind, name: ref.name, size: ref.size, hash: ref.hash, missing: true };
      const d = MODDATA[m.id] || (MODDATA[m.id] = {}); d.assetName = null; d.irResp = null; d.irDecay = null; d.namMeta = null; d.engineNote = "";
      assetStatus[m.id] = ' · missing from library (kept in the preset)';
    }
  }
  const name = r.name || file.replace(/\.rig\.json$/i, '');
  $('presetName').textContent = name; $('presetInput').value = name;
  r.warnings.forEach((w) => logLine('preset: ' + w, true));
  presetNote(r.warnings.length ? `Loaded with ${r.warnings.length} note${r.warnings.length > 1 ? 's' : ''}: ${r.warnings[0]}${r.warnings.length > 1 ? ' (rest in Engine log)' : ''}` : `Loaded presets/${file}`, r.warnings.length > 0);
  logLine(`preset loaded: presets/${file}`);
  return r;
}
let deleteArmed = 0;
async function deletePreset() {
  const file = $('presetSel').value; if (!file) return;
  if (!deleteArmed) { deleteArmed = setTimeout(() => { deleteArmed = 0; $('presetDelete').textContent = 'Delete'; }, 4000); $('presetDelete').textContent = 'Confirm delete'; return; }
  clearTimeout(deleteArmed); deleteArmed = 0; $('presetDelete').textContent = 'Delete';
  await library.core.remove('presets', file); await refreshLibrary(); presetNote(`Deleted presets/${file}`); logLine(`preset deleted: presets/${file}`);
}
function wirePresets() {
  $('presetSave').onclick = savePreset;
  $('presetLoad').onclick = () => { const f = $('presetSel').value; if (f) loadPresetFile(f); };
  $('presetDelete').onclick = deletePreset;
  refreshPresetList();
}

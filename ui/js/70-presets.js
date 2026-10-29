// 70-presets.js - snapshots (scenes A-D) and presets saved in this browser, plus JSON export/import.
const PKEY = 'rig01.presets';
function snapshot() { return { params: Object.fromEntries(Object.values(PARAMS).map((p) => [p.id, p.v])), bypass: { ...state.bypass }, conns: { ...state.conns }, bpm: state.bpm }; }
function applySnapshot(s) {
  Object.entries(s.params || {}).forEach(([id, v]) => { if (PARAMS[id]) PARAMS[id].v = v; });
  state.bypass = { ...(s.bypass || {}) };
  state.conns = Object.fromEntries(Object.entries(s.conns || {}).filter(([pid, sid]) => PARAMS[pid] && SRCMAP[sid]));
  if (s.bpm) { state.bpm = s.bpm; $('bpm').textContent = Math.round(s.bpm); }
  if (s.master !== undefined) { state.master = s.master; $('master').value = s.master; $('masterVal').textContent = s.master + ' dB'; }
  sendAll(); syncCables(); update();
}
function readPresets() { try { return JSON.parse(localStorage.getItem(PKEY) || '{}'); } catch (e) { return {}; } }
function writePresets(p) { try { localStorage.setItem(PKEY, JSON.stringify(p)); } catch (e) { logLine('presets could not be saved in this browser', true); } }
function refreshPresetList() { const p = readPresets(), sel = $('presetSel'); sel.innerHTML = ''; Object.keys(p).forEach((k) => { const o = document.createElement('option'); o.value = k; o.textContent = k; sel.appendChild(o); }); }
function wirePresets() {
  $('presetSave').onclick = () => { const name = $('presetInput').value.trim() || 'Untitled'; const p = readPresets(); p[name] = { ...snapshot(), master: state.master, scenes: state.scenes }; writePresets(p); refreshPresetList(); $('presetSel').value = name; $('presetName').textContent = name; logLine('preset saved: ' + name); };
  $('presetLoad').onclick = () => { const name = $('presetSel').value, p = readPresets()[name]; if (!p) return; state.scenes = p.scenes || {}; document.querySelectorAll('.scene').forEach((x) => x.classList.toggle('stored', !!state.scenes[x.dataset.scene])); applySnapshot(p); $('presetName').textContent = name; $('presetInput').value = name; logLine('preset loaded: ' + name); };
  $('presetExport').onclick = () => { const name = $('presetInput').value.trim() || 'preset'; const blob = new Blob([JSON.stringify({ name, ...snapshot(), master: state.master, scenes: state.scenes }, null, 2)], { type: 'application/json' }); const a = document.createElement('a'); a.href = URL.createObjectURL(blob); a.download = name.replace(/[^\w-]+/g, '_') + '.rig.json'; a.click(); };
  $('presetImport').onclick = () => $('presetFile').click();
  $('presetFile').onchange = async (e) => { const f = e.target.files[0]; if (!f) return; try { const p = JSON.parse(await f.text()); const all = readPresets(); all[p.name || f.name] = p; writePresets(all); refreshPresetList(); logLine('imported ' + (p.name || f.name)); } catch (err) { logLine('not a preset file: ' + err.message, true); } };
  refreshPresetList();
}

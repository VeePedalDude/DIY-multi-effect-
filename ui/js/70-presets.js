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
// ---- preset files: a window in the middle of the page (like the file browser), opened from the preset
// name in the top bar. What's loaded is presetState.file; its signature (the preset as it would be saved,
// without its name and date) says whether the rig has been edited since it was loaded or saved.
const presetState = { file: null, sig: null, armed: null, armT: 0, renaming: null, opener: null };
rig.presetState = presetState;
const presetSig = () => { const p = buildPreset(''); delete p.saved; delete p.name; return JSON.stringify(p); };
const presetEdited = () => presetState.sig !== null && presetSig() !== presetState.sig;
const presetEntry = (file) => (library.entries.presets || []).find((e) => e.name.toLowerCase() === String(file).toLowerCase());
const presetLabel = (e) => (e.meta && e.meta.name) || e.name.replace(/\.rig\.json$/i, '');
const escHtml = (t) => String(t).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/"/g, '&quot;');
function presetNote(text, warn) { const n = $('presetNote'); if (n) { n.textContent = text; n.className = 'note' + (warn ? ' warnline' : ''); } presetState.lastNote = [text, !!warn]; }
// A second click within 4 s confirms (replace, discard changes, delete).
function armed(key, message) {
  if (presetState.armed === key) { clearTimeout(presetState.armT); presetState.armed = null; return true; }
  presetState.armed = key; clearTimeout(presetState.armT);
  presetState.armT = setTimeout(() => { presetState.armed = null; renderPresetDlg(); }, 4000);
  presetNote(message, true); renderPresetDlg(); return false;
}
function setCurrentPreset(file, name) {
  presetState.file = file; presetState.sig = presetSig();
  $('presetName').textContent = name; updatePresetEdited(); saveSession();
}
// Where you left off: the preset that's loaded, and the rig as it is now (unsaved changes included), kept
// in this browser so a restart (of the page, the browser or rig-host) comes back to it. The preset files
// themselves stay the only saved copies; this is only the working state.
const SESSION_KEY = 'rig01.session';
let sessionSig = null;
function saveSession() {
  if (presetState.sig === null) return;
  const name = $('presetName').textContent, now = presetSig(); if (now === sessionSig) return;
  try { localStorage.setItem(SESSION_KEY, JSON.stringify({ file: presetState.file, name, base: presetState.sig, rig: buildPreset(name) })); sessionSig = now; } catch (e) { /* storage full or blocked: nothing to restore next time */ }
}
async function restoreSession() {
  let s = null; try { s = JSON.parse(localStorage.getItem(SESSION_KEY) || 'null'); } catch (e) { return; }
  if (!s || !s.rig) return;
  const r = await applyPreset(s.rig, s.file || 'last session'); if (!r || r.error) return;
  const file = s.file && presetEntry(s.file) ? presetEntry(s.file).name : null;
  presetState.file = file; $('presetName').textContent = s.name || r.name || 'Untitled';
  // edited is measured against the preset file; a rig never saved starts clean; one whose file has gone is unsaved
  presetState.sig = file ? s.base : s.file ? '' : presetSig();
  sessionSig = presetSig(); updatePresetEdited();
  const ed = presetEdited();
  logLine(`picked up where you left off: “${$('presetName').textContent}”${ed ? ', with unsaved changes' : ''}${s.file && !file ? ` (presets/${s.file} is no longer in the library)` : ''}`);
  presetNote(`Picked up where you left off${ed ? ', with your unsaved changes' : ''}.`, false);
}
function updatePresetEdited() { const ed = presetEdited(); $('presetEdited').hidden = !ed; $('presetBtn').setAttribute('aria-label', `Presets: ${$('presetName').textContent}${ed ? ', edited' : ''}`); }

async function writePreset(file, name) {
  await library.core.write('presets', file, JSON.stringify(buildPreset(name), null, 2));
  await refreshLibrary(); setCurrentPreset(file, name);
  logLine(`preset saved: presets/${file}`);
}
// Save: over the preset that's loaded.
async function savePreset() {
  if (!library.core) return;
  if (!presetState.file) { savePresetAs(); return; }            // nothing saved yet: Save needs a name
  const e = presetEntry(presetState.file), name = e ? presetLabel(e) : $('presetName').textContent;
  try { await writePreset(presetState.file, name); presetNote(`Saved “${name}”`); }
  catch (err) { presetNote(`Couldn't save: ${err.message}`, true); }
  renderPresetDlg();
}
// Save as new: a name of its own; an existing name is only replaced on a second click.
async function savePresetAs() {
  if (!library.core) return;
  const name = $('presetInput').value.trim();
  if (!name) { presetNote(presetState.file ? 'Type a name for the new preset first.' : 'Type a name for this preset below, then Save.', true); $('presetInput').focus(); return; }
  const file = safeFileName(name) + '.rig.json', had = presetEntry(file);
  if (had && !armed('new:' + file.toLowerCase(), `“${presetLabel(had)}” already exists. Click Save as new again to replace it.`)) return;
  try { await writePreset(had ? had.name : file, name); $('presetInput').value = ''; presetNote(`Saved “${name}”`); }
  catch (err) { presetNote(`Couldn't save: ${err.message}`, true); }
  renderPresetDlg();
}
async function loadPresetFile(file) {
  let json; try { json = JSON.parse(await (await library.core.file('presets', file)).text()); } catch (e) { presetNote(`Couldn't read ${file}: ${e.message}`, true); return null; }
  const r = await applyPreset(json, file); if (!r || r.error) return r;
  const name = r.name || file.replace(/\.rig\.json$/i, '');
  setCurrentPreset(file, name);
  r.warnings.forEach((w) => logLine('preset: ' + w, true));
  presetNote(r.warnings.length ? `Loaded “${name}” with ${r.warnings.length} note${r.warnings.length > 1 ? 's' : ''}: ${r.warnings[0]}${r.warnings.length > 1 ? ' (rest in Engine log)' : ''}` : `Loaded “${name}”`, r.warnings.length > 0);
  logLine(`preset loaded: presets/${file}`);
  renderPresetDlg();
  return r;
}
// Put a preset's contents on the rig (from a file, or the last session). `file` only names it in messages.
async function applyPreset(json, file) {
  const r = readPreset(json);
  if (r.error) { presetNote(`${file}: ${r.error}`, true); return r; }
  try {
    applyChain(r.chain); applyRack(r.rack); state.sourceExtras = r.sourceExtras;
    // CONTROLLER bindings: a preset that names any sets them; one that names none keeps yours
    if (Object.keys(r.inputs || {}).length) { Object.keys(state.inputs).forEach((id) => bindInput(id, null)); Object.entries(r.inputs).forEach(([id, b]) => bindInput(id, b)); }
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
  } catch (e) { presetNote(`Couldn't load ${file}: ${e.message}. The rig may be part way between presets.`, true); logLine(`preset: ${e.stack || e.message}`, true); renderPresetDlg(); return null; }
  return r;
}
// Load from the list: unsaved edits are only thrown away on a second click.
async function pickPreset(file) {
  if (presetEdited() && !armed('load:' + file, `“${$('presetName').textContent}” has unsaved changes. Click “${presetLabel(presetEntry(file) || { name: file })}” again to load it anyway, or Save first.`)) return;
  await loadPresetFile(file);
}
async function renamePreset(file, newName) {
  const e = presetEntry(file); newName = newName.trim();
  if (!e || !newName) { presetState.renaming = null; renderPresetDlg(); return; }
  const target = safeFileName(newName) + '.rig.json', clash = presetEntry(target);
  if (clash && clash.name !== e.name) { presetNote(`“${presetLabel(clash)}” already exists. Pick another name.`, true); return; }
  try {
    const json = JSON.parse(await (await library.core.file('presets', e.name)).text()); json.name = newName;
    await library.core.write('presets', target, JSON.stringify(json, null, 2));
    if (target !== e.name) await library.core.remove('presets', e.name);
    presetState.renaming = null; await refreshLibrary();
    if (presetState.file === e.name) { presetState.file = target; $('presetName').textContent = newName; updatePresetEdited(); sessionSig = null; saveSession(); }
    presetNote(`Renamed to “${newName}”`); logLine(`preset renamed: presets/${e.name} -> presets/${target}`);
  } catch (err) { presetNote(`Couldn't rename: ${err.message}`, true); }
  renderPresetDlg();
}
async function deletePreset(file) {
  const e = presetEntry(file); if (!e) return;
  if (!armed('del:' + file, `Click Delete again to delete “${presetLabel(e)}”.`)) return;
  try {
    await library.core.remove('presets', e.name); await refreshLibrary();
    if (presetState.file === e.name) { presetState.file = null; presetState.sig = ''; updatePresetEdited(); sessionSig = null; saveSession(); }
    presetNote(`Deleted “${presetLabel(e)}”`); logLine(`preset deleted: presets/${e.name}`);
  } catch (err) { presetNote(`Couldn't delete: ${err.message}`, true); }
  renderPresetDlg();
}

// ---- the window
function refreshPresetList() { renderPresetDlg(); }
function openPresetDlg(opener) {
  presetState.opener = opener || document.activeElement; presetState.renaming = null;
  $('presetDlg').hidden = false; renderPresetDlg();
  const first = $('presetList').querySelector('[aria-current="true"]') || $('presetInput'); first.focus();
}
function closePresetDlg() {
  if ($('presetDlg').hidden) return;
  $('presetDlg').hidden = true; presetState.renaming = null;
  const o = presetState.opener; presetState.opener = null; if (o && o.isConnected && o.focus) o.focus();
}
function renderPresetDlg() {
  if (!$('presetDlg') || $('presetDlg').hidden) return;
  const list = library.entries.presets || [], ed = presetEdited(), cur = presetState.file, A = presetState.armed;
  $('presetCount').textContent = `${list.length} saved · in the library's presets folder`;
  $('presetCur').innerHTML = `<b>${escHtml($('presetName').textContent)}</b><span class="muted">${cur ? `presets/${escHtml(cur)}` : 'not saved as a preset yet'}${ed ? ' · <span class="edited">edited</span>' : ''}</span>`;
  $('presetSave').title = cur ? `Save over “${$('presetName').textContent}”` : 'Save under the name typed below';
  const ul = $('presetList'), top = ul.scrollTop, sorted = [...list].sort((a, b) => presetLabel(a).localeCompare(presetLabel(b), undefined, { sensitivity: 'base', numeric: true }));
  ul.innerHTML = sorted.length ? sorted.map((e) => {
    const f = encodeURIComponent(e.name), here = e.name === cur;
    if (presetState.renaming === e.name) return `<li class="filerow"><input data-preset-rename-input="${f}" value="${escHtml(presetLabel(e))}" aria-label="New name for ${escHtml(presetLabel(e))}" autocomplete="off">
      <button class="btn mini" data-preset-rename-ok="${f}">Rename</button><button class="btn mini" data-preset-rename-cancel>Cancel</button></li>`;
    return `<li class="filerow"><button data-preset-load="${f}" aria-current="${here}"><span>${escHtml(presetLabel(e))}${here && ed ? ' <span class="edited">· edited</span>' : ''}</span>
        <span class="meta">${new Date(e.mtime).toLocaleString([], { dateStyle: 'medium', timeStyle: 'short' })}${A === 'load:' + e.name ? ' · click again to load' : ''}</span></button>
      <button class="btn mini" data-preset-rename="${f}" aria-label="Rename ${escHtml(presetLabel(e))}">Rename</button>
      <button class="btn mini" data-preset-del="${f}" aria-label="Delete ${escHtml(presetLabel(e))}">${A === 'del:' + e.name ? 'Confirm' : 'Delete'}</button></li>`;
  }).join('') : '<li class="muted">No presets yet. Name the rig below and press Save as new.</li>';
  ul.scrollTop = top;
  const ri = ul.querySelector('[data-preset-rename-input]'); if (ri && document.activeElement !== ri) { ri.focus(); ri.select(); }
  if (presetState.lastNote) presetNote(...presetState.lastNote);
}
function wirePresets() {
  $('presetBtn').onclick = () => openPresetDlg($('presetBtn'));
  $('presetSave').onclick = savePreset;
  $('presetSaveNew').onclick = savePresetAs;
  $('presetInput').addEventListener('keydown', (e) => { if (e.key === 'Enter') { e.preventDefault(); savePresetAs(); } });
  const dlg = $('presetDlg');
  dlg.addEventListener('click', (e) => {
    if (e.target === dlg || e.target.closest('[data-act="preset-close"]')) { closePresetDlg(); return; }
    const b = e.target.closest('[data-preset-load],[data-preset-rename],[data-preset-del],[data-preset-rename-ok],[data-preset-rename-cancel]'); if (!b) return;
    const d = b.dataset;
    if (d.presetLoad) pickPreset(decodeURIComponent(d.presetLoad));
    else if (d.presetRename) { presetState.renaming = decodeURIComponent(d.presetRename); renderPresetDlg(); }
    else if (d.presetDel) deletePreset(decodeURIComponent(d.presetDel));
    else if (d.presetRenameOk) { const f = decodeURIComponent(d.presetRenameOk); renamePreset(f, dlg.querySelector('[data-preset-rename-input]').value); }
    else { presetState.renaming = null; renderPresetDlg(); }
  });
  dlg.addEventListener('keydown', (e) => {
    const ri = e.target.dataset && e.target.dataset.presetRenameInput;
    if (ri && e.key === 'Enter') { e.preventDefault(); renamePreset(decodeURIComponent(ri), e.target.value); return; }
    if (ri && e.key === 'Escape') { e.preventDefault(); e.stopPropagation(); presetState.renaming = null; renderPresetDlg(); return; }
    if (e.key !== 'Tab') return;                                 // keep Tab inside the window
    const f = [...$('presetDlgCard').querySelectorAll('button,input')].filter((x) => !x.disabled && x.offsetParent);
    if (!f.length) return;
    if (e.shiftKey && document.activeElement === f[0]) { e.preventDefault(); f[f.length - 1].focus(); }
    else if (!e.shiftKey && document.activeElement === f[f.length - 1]) { e.preventDefault(); f[0].focus(); }
  });
  window.addEventListener('keydown', (e) => { if (e.key === 'Escape' && !$('presetDlg').hidden) { e.preventDefault(); closePresetDlg(); } });
  // the "edited" mark, checked now and then (cheap: the preset is a few KB)
  setInterval(() => {
    if (presetState.sig === null) { if (rig.meters) presetState.sig = presetSig(); return; }
    const ed = presetEdited(); if (ed !== !$('presetEdited').hidden) { updatePresetEdited(); renderPresetDlg(); }
    saveSession();
  }, 700);
  refreshPresetList();
}

rig.buildPreset = buildPreset;                                     // tests and the console
rig.loadPresetJson = async (json, name = 'Console.rig.json') => { await library.core.write('presets', name, json); return loadPresetFile(name); };

// 70-presets.js - the pedalboard: scenes (banks) of presets, in the bottom-left corner.
//
// The board is presets/board.json in the library: scenes in order, each with its presets in order. Each
// preset is its own .rig.json file in presets/ (the format of 72-preset-format.js), written by SAVE. A
// slot never saved has no file, and selecting it puts the basic rig on (gate, amp and cab at their
// defaults, no modulation). Edits stay with each preset while you move around the board, until the page
// restarts; SAVE writes the one you're on. The tree (Tree button) adds, renames, moves and removes
// scenes and presets.
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
// Put a preset's contents on the rig. `label` only names it in messages. Old presets' A-D snapshots
// (`scenes` in the file) are read but not used: each preset on the board is one sound.
async function applyPreset(json, label) {
  const r = readPreset(json);
  if (r.error) { boardNote(`${label}: ${r.error}`, true); return r; }
  try {
    applyChain(r.chain); applyRack(r.rack); state.sourceExtras = r.sourceExtras;
    // CONTROLLER bindings: a preset that names any sets them; one that names none keeps yours
    if (Object.keys(r.inputs || {}).length) { Object.keys(state.inputs).forEach((id) => bindInput(id, null)); Object.entries(r.inputs).forEach(([id, b]) => bindInput(id, b)); }
    applySnapshot(r.snap);
    if (r.master !== undefined) setMaster(r.master);
    state.presetExtras = r.extras;
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
  } catch (e) { boardNote(`couldn't load ${label}: ${e.message}. The rig may be part way between presets.`, true); logLine(`preset: ${e.stack || e.message}`, true); return null; }
  return r;
}

// ---- the board
const BLANK_PRESET = () => ({ format: 'rig-preset', version: PRESET_VERSION, name: '',
  chain: [{ id: 'gate', type: 'gate' }, { id: 'amp', type: 'amp' }, { id: 'cab', type: 'cab' }], sources: [], mods: [] });
const board = { scenes: [], s: 0, p: 0, work: {}, base: null, savedName: '', renaming: null, armed: null, armT: 0, ready: false };
rig.board = board;
const uid = () => 'p' + Date.now().toString(36) + Math.random().toString(36).slice(2, 6);
const escHtml = (t) => String(t).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/"/g, '&quot;');
const curScene = () => board.scenes[board.s];
const curSlot = () => curScene() && curScene().presets[board.p];
const presetSig = () => { const p = buildPreset(''); delete p.saved; delete p.name; return JSON.stringify(p); };
const presetEdited = () => board.base !== null && (presetSig() !== board.base || $('presetName').value.trim() !== board.savedName);
const newSlot = () => ({ id: uid(), file: null, name: '' });

function boardNote(text, warn) { logLine('presets: ' + text, !!warn); const n = $('boardNote'); if (n) { n.textContent = text; n.classList.toggle('warnline', !!warn); } }
async function writeBoard() {
  if (!library.core) return;
  const data = { format: 'rig-board', version: 1, current: { scene: board.s, preset: board.p },
    scenes: board.scenes.map((sc) => ({ id: sc.id, name: sc.name, presets: sc.presets.map((q) => ({ id: q.id, file: q.file, name: q.name })) })) };
  try { await library.core.write('presets', 'board.json', JSON.stringify(data, null, 2)); } catch (e) { boardNote(`couldn't write board.json: ${e.message}`, true); }
}
// After the library is read: the board, and the preset that was selected last.
async function initBoard() {
  let data = null;
  try { data = JSON.parse(await (await library.core.file('presets', 'board.json')).text()); } catch (e) { /* no board yet */ }
  if (data && Array.isArray(data.scenes) && data.scenes.length) {
    board.scenes = data.scenes.map((sc, i) => ({ id: sc.id || uid(), name: String(sc.name || `Scene ${i + 1}`),
      presets: (Array.isArray(sc.presets) && sc.presets.length ? sc.presets : [{}]).map((q) => ({ id: q.id || uid(), file: q.file || null, name: String(q.name || '') })) }));
    const c = data.current || {};
    board.s = Math.min(Math.max(0, c.scene | 0), board.scenes.length - 1);
    board.p = Math.min(Math.max(0, c.preset | 0), curScene().presets.length - 1);
    board.ready = true;
    await selectSlot(board.s, board.p, true);
  } else {
    // a new board: one scene, one preset, holding the rig as it is (not saved yet)
    board.scenes = [{ id: uid(), name: 'Scene 1', presets: [newSlot()] }]; board.s = 0; board.p = 0;
    board.base = null; board.savedName = ''; $('presetName').value = ''; board.ready = true;
  }
  renderBoard();
}
// Move to a slot: keep this one's edits, then put the other one on (its edits, its file, or blank).
async function selectSlot(s, p, first) {
  const was = curSlot();
  if (!first && was && board.base !== null && presetEdited()) board.work[was.id] = { json: buildPreset($('presetName').value.trim()), base: board.base, savedName: board.savedName };
  board.s = s; board.p = p;
  const q = curSlot(), w = board.work[q.id];
  let r = null;
  if (w) { r = await applyPreset(w.json, q.name || 'preset'); board.base = w.base; board.savedName = w.savedName; $('presetName').value = w.json.name || ''; }
  else if (q.file) {
    let json = null;
    try { json = JSON.parse(await (await library.core.file('presets', q.file)).text()); } catch (e) { boardNote(`couldn't read presets/${q.file}: ${e.message}`, true); }
    if (json) { r = await applyPreset(json, q.file); board.savedName = q.name; $('presetName').value = q.name; board.base = presetSig(); }
  } else {
    r = await applyPreset(BLANK_PRESET(), 'blank');
    MODS.forEach((m) => m.params.forEach((q) => { q.v = q.def; })); state.bypass = {}; sendAll(); update();   // knobs a file doesn't list keep their value; blank means defaults
    board.savedName = ''; $('presetName').value = ''; board.base = presetSig();
  }
  if (r && r.warnings && r.warnings.length) { r.warnings.forEach((x) => logLine('preset: ' + x, true)); boardNote(`${q.name || 'preset'}: ${r.warnings[0]}${r.warnings.length > 1 ? ' (rest in Engine log)' : ''}`, true); }
  if (!first) writeBoard();
  renderBoard(); renderTree();
}
// SAVE: the preset you're on, under the name in the box.
async function saveCurrent() {
  if (!library.core) return;
  const q = curSlot(), sc = curScene(); if (!q) return;
  const name = $('presetName').value.trim() || `${sc.name} ${board.p + 1}`;
  const file = q.file || `${q.id}.rig.json`;
  try {
    await library.core.write('presets', file, JSON.stringify(buildPreset(name), null, 2));
    q.file = file; q.name = name; $('presetName').value = name;
    delete board.work[q.id]; board.base = presetSig(); board.savedName = name;
    await writeBoard(); await refreshLibrary();
    boardNote(`saved “${name}” (presets/${file})`);
  } catch (e) { boardNote(`couldn't save: ${e.message}`, true); }
  renderBoard(); renderTree();
}
const stepScene = (d) => { const n = board.scenes.length; if (n > 1) selectSlot((board.s + d + n) % n, 0); };
async function addPreset(si = board.s) {
  board.scenes[si].presets.push(newSlot());
  await selectSlot(si, board.scenes[si].presets.length - 1);
}
async function addScene() {
  board.scenes.push({ id: uid(), name: `Scene ${board.scenes.length + 1}`, presets: [newSlot()] });
  await selectSlot(board.scenes.length - 1, 0);
}
// A second click within 4 s confirms a delete.
function armed(key) {
  if (board.armed === key) { clearTimeout(board.armT); board.armed = null; return true; }
  board.armed = key; clearTimeout(board.armT); board.armT = setTimeout(() => { board.armed = null; renderTree(); }, 4000); renderTree(); return false;
}
async function removePreset(si, pi) {
  const sc = board.scenes[si], q = sc.presets[pi];
  if (!armed('p:' + q.id)) return;
  if (q.file) { try { await library.core.remove('presets', q.file); } catch (e) { /* already gone */ } }
  delete board.work[q.id]; sc.presets.splice(pi, 1);
  if (!sc.presets.length) sc.presets.push(newSlot());                 // a scene always has a footswitch
  const here = si === board.s && pi === board.p;
  if (si === board.s && pi < board.p) board.p--;
  board.p = Math.min(board.p, curScene().presets.length - 1);
  if (here) { board.base = null; await selectSlot(board.s, board.p, true); }
  await writeBoard(); await refreshLibrary(); renderBoard(); renderTree();
}
async function removeScene(si) {
  const sc = board.scenes[si];
  if (!armed('s:' + sc.id)) return;
  for (const q of sc.presets) { delete board.work[q.id]; if (q.file) { try { await library.core.remove('presets', q.file); } catch (e) { /* gone */ } } }
  board.scenes.splice(si, 1);
  if (!board.scenes.length) board.scenes.push({ id: uid(), name: 'Scene 1', presets: [newSlot()] });
  const here = si === board.s;
  if (si < board.s) board.s--;
  board.s = Math.min(board.s, board.scenes.length - 1);
  if (here) { board.p = 0; board.base = null; await selectSlot(board.s, 0, true); }
  await writeBoard(); await refreshLibrary(); renderBoard(); renderTree();
}
function move(list, i, d) { const j = i + d; if (j < 0 || j >= list.length) return false; [list[i], list[j]] = [list[j], list[i]]; return true; }
async function moveScene(si, d) {
  const cur = curScene(); if (!move(board.scenes, si, d)) return;
  board.s = board.scenes.indexOf(cur); await writeBoard(); renderBoard(); renderTree();
}
async function movePreset(si, pi, d) {
  const list = board.scenes[si].presets, cur = curSlot(); if (!move(list, pi, d)) return;
  board.p = curScene().presets.indexOf(cur); await writeBoard(); renderBoard(); renderTree();
}
async function renameItem(key, name) {
  name = name.trim(); board.renaming = null;
  if (name) {
    const [kind, id] = key.split(':');
    if (kind === 's') { const sc = board.scenes.find((x) => x.id === id); if (sc) sc.name = name; }
    else {
      const q = board.scenes.flatMap((x) => x.presets).find((x) => x.id === id);
      if (q) {
        if (q === curSlot()) { $('presetName').value = name; }
        if (q.file) {                                                       // the name inside the file follows
          try { const j = JSON.parse(await (await library.core.file('presets', q.file)).text()); j.name = name; await library.core.write('presets', q.file, JSON.stringify(j, null, 2)); } catch (e) { boardNote(`couldn't rename the file: ${e.message}`, true); }
          if (q === curSlot()) board.savedName = name;
        }
        q.name = name;
      }
    }
    await writeBoard();
  }
  renderBoard(); renderTree();
}

// ---- drawing
const slotLabel = (q) => q.name || (q.file ? 'Untitled' : '—');
function renderBoard() {
  if (!board.ready) return;
  const sc = curScene(); if (!sc) return;
  $('sceneName').textContent = sc.name;
  $('sceneName').title = `${sc.name}: scene ${board.s + 1} of ${board.scenes.length}`;
  $('sceneCount').textContent = `${board.s + 1}/${board.scenes.length} · ${sc.presets.length} preset${sc.presets.length === 1 ? '' : 's'}`;
  $('boardSwitches').innerHTML = sc.presets.map((q, i) => {
    const on = i === board.p, ed = on ? presetEdited() : !!board.work[q.id];
    return `<button class="fsw${on ? ' on' : ''}${ed ? ' ed' : ''}${q.file ? '' : ' empty'}" data-slot="${i}" aria-pressed="${on}" aria-label="Preset ${i + 1}: ${escHtml(slotLabel(q))}${ed ? ', edited' : ''}">
      <span class="led"></span><b>${i + 1}</b><small>${escHtml(slotLabel(q))}</small></button>`;
  }).join('') + '<button class="fsw add" id="boardAdd" aria-label="Add a preset to this scene" title="Add a preset to this scene">+</button>';
  updateEdited();
}
function updateEdited() { const ed = presetEdited(); $('presetEdited').hidden = !ed; const b = document.querySelector('.fsw.on'); if (b) b.classList.toggle('ed', ed); }
function renderTree() {
  const t = $('boardTree'); if (!t || t.hidden) return;
  const nP = board.scenes.reduce((a, sc) => a + sc.presets.length, 0), A = board.armed;
  const ren = (key, val, label) => `<input class="tren" data-tree-rename-input="${key}" value="${escHtml(val)}" aria-label="New name for ${escHtml(label)}" autocomplete="off">`;
  $('treeCount').textContent = `${board.scenes.length} scene${board.scenes.length === 1 ? '' : 's'} · ${nP} preset${nP === 1 ? '' : 's'}`;
  $('treeList').innerHTML = board.scenes.map((sc, si) => `<li class="tscene${si === board.s ? ' here' : ''}">
      <div class="trow">${board.renaming === 's:' + sc.id ? ren('s:' + sc.id, sc.name, sc.name) : `<span class="tname"><b>${escHtml(sc.name)}</b> <small>${sc.presets.length} preset${sc.presets.length === 1 ? '' : 's'}</small></span>`}
        <button class="btn mini" data-tree="s-up" data-s="${si}" aria-label="Move ${escHtml(sc.name)} up" ${si ? '' : 'disabled'}>↑</button>
        <button class="btn mini" data-tree="s-down" data-s="${si}" aria-label="Move ${escHtml(sc.name)} down" ${si < board.scenes.length - 1 ? '' : 'disabled'}>↓</button>
        <button class="btn mini" data-tree="s-rename" data-s="${si}">Rename</button>
        <button class="btn mini" data-tree="s-add" data-s="${si}">+ Preset</button>
        <button class="btn mini" data-tree="s-del" data-s="${si}" aria-label="Delete ${escHtml(sc.name)}">${A === 's:' + sc.id ? 'Confirm' : 'Delete'}</button></div>
      <ol>${sc.presets.map((q, pi) => `<li class="tpreset${si === board.s && pi === board.p ? ' here' : ''}">
        ${board.renaming === 'p:' + q.id ? ren('p:' + q.id, q.name, slotLabel(q)) : `<button class="tpick" data-tree="p-pick" data-s="${si}" data-p="${pi}" aria-current="${si === board.s && pi === board.p}"><b>${pi + 1}</b> ${escHtml(slotLabel(q))}${board.work[q.id] || (si === board.s && pi === board.p && presetEdited()) ? ' <span class="edited">· edited</span>' : ''}${q.file ? '' : ' <small>(not saved)</small>'}</button>`}
        <button class="btn mini" data-tree="p-up" data-s="${si}" data-p="${pi}" aria-label="Move up" ${pi ? '' : 'disabled'}>↑</button>
        <button class="btn mini" data-tree="p-down" data-s="${si}" data-p="${pi}" aria-label="Move down" ${pi < sc.presets.length - 1 ? '' : 'disabled'}>↓</button>
        <button class="btn mini" data-tree="p-rename" data-s="${si}" data-p="${pi}">Rename</button>
        <button class="btn mini" data-tree="p-del" data-s="${si}" data-p="${pi}" aria-label="Delete preset ${pi + 1}">${A === 'p:' + q.id ? 'Confirm' : 'Delete'}</button></li>`).join('')}</ol></li>`).join('');
  const ri = t.querySelector('[data-tree-rename-input]'); if (ri && document.activeElement !== ri) { ri.focus(); ri.select(); }
}
function refreshPresetList() { renderTree(); }                         // the library calls this after a rescan

function wirePresets() {
  $('presetSave').onclick = saveCurrent;
  $('presetName').addEventListener('keydown', (e) => { if (e.key === 'Enter') { e.preventDefault(); saveCurrent(); } });
  $('presetName').addEventListener('input', () => { updateEdited(); });
  $('sceneUp').onclick = () => stepScene(1);
  $('sceneDown').onclick = () => stepScene(-1);
  $('boardSwitches').addEventListener('click', (e) => {
    if (e.target.closest('#boardAdd')) { addPreset(); return; }
    const b = e.target.closest('[data-slot]'); if (b && Number(b.dataset.slot) !== board.p) selectSlot(board.s, Number(b.dataset.slot));
  });
  const tree = $('boardTree');
  $('boardTreeBtn').onclick = () => { tree.style.bottom = Math.round(innerHeight - $('board').getBoundingClientRect().top + 10) + 'px'; tree.hidden = !tree.hidden; $('boardTreeBtn').setAttribute('aria-expanded', String(!tree.hidden)); board.renaming = null; renderTree(); };
  tree.addEventListener('click', (e) => {
    if (e.target.closest('[data-act="tree-close"]')) { tree.hidden = true; $('boardTreeBtn').setAttribute('aria-expanded', 'false'); return; }
    if (e.target.closest('[data-act="tree-add-scene"]')) { addScene(); return; }
    const b = e.target.closest('[data-tree]'); if (!b) return;
    const si = Number(b.dataset.s), pi = Number(b.dataset.p), sc = board.scenes[si];
    switch (b.dataset.tree) {
      case 'p-pick': if (si !== board.s || pi !== board.p) selectSlot(si, pi); break;
      case 's-up': moveScene(si, -1); break;
      case 's-down': moveScene(si, 1); break;
      case 'p-up': movePreset(si, pi, -1); break;
      case 'p-down': movePreset(si, pi, 1); break;
      case 's-add': addPreset(si); break;
      case 's-del': removeScene(si); break;
      case 'p-del': removePreset(si, pi); break;
      case 's-rename': board.renaming = 's:' + sc.id; renderTree(); break;
      case 'p-rename': board.renaming = 'p:' + sc.presets[pi].id; renderTree(); break;
    }
  });
  tree.addEventListener('keydown', (e) => {
    const k = e.target.dataset && e.target.dataset.treeRenameInput; if (!k) return;
    if (e.key === 'Enter') { e.preventDefault(); renameItem(k, e.target.value); }
    if (e.key === 'Escape') { e.preventDefault(); e.stopPropagation(); board.renaming = null; renderTree(); }
  });
  tree.addEventListener('focusout', (e) => { const k = e.target.dataset && e.target.dataset.treeRenameInput; if (k && board.renaming === k) renameItem(k, e.target.value); });
  setInterval(() => { if (board.ready && board.base === null && rig.meters) board.base = presetSig(); if (board.ready) updateEdited(); }, 700);
}

rig.buildPreset = buildPreset;                                     // tests and the console
// Put a preset's JSON on the slot you're on, as unsaved edits (tests and the console).
rig.loadPresetJson = async (json) => { const r = await applyPreset(typeof json === 'string' ? JSON.parse(json) : json, 'console'); updateEdited(); renderBoard(); return r; };

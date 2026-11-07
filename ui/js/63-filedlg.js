// 63-filedlg.js - the file browser: a window in the middle of the page for choosing a module's NAM
// capture or IR. It opens from the file strip on the module's card (40-stage.js) or the side panel's
// Browse button. Clicking a file loads it straight away and the window stays open, so captures can be
// auditioned one after another; Done, Escape or a click outside closes it. Search, tag chips, favorites,
// the current file's tags and note, import, drop, "Use built-in" and remove all work here as they did in
// the side panel (56-asset-ui.js and 62-tags.js handle the clicks).
const fileDlg = { id: null, opener: null };
rig.fileDlg = fileDlg;

function openFileDlg(m, opener) {
  if (!m || !m.asset) return;
  const el = $('fileDlg'), card = $('fileDlgCard'), a = m.asset;
  fileDlg.id = m.id; fileDlg.opener = opener || document.activeElement;
  card.dataset.assetMod = m.id;
  card.innerHTML = `<div class="fdhead"><div><div class="eyebrow" id="fdEyebrow"></div><h2 id="fileDlgTitle"></h2></div>
      <button class="btn mini" data-act="fd-close" aria-label="Close the file browser">✕</button></div>
    <div id="fdCur" class="fdcur"></div>
    <input type="search" id="as-${m.id}" data-asset-search="${m.id}" placeholder="Search ${kindLabel(a.kind)}, tags, notes…" aria-label="Search ${kindLabel(a.kind)}" value="${(assetQuery[m.id] || '').replace(/"/g, '&quot;')}" autocomplete="off">
    <div id="fdChips"></div>
    <ul id="al-${m.id}" class="fdlist" aria-label="${a.label} files"></ul>
    <div id="fdFoot"></div>`;
  el.hidden = false;
  renderFileDlg();
  $('as-' + m.id).focus();
}
function closeFileDlg() {
  const el = $('fileDlg'); if (el.hidden) return;
  el.hidden = true; $('fileDlgCard').innerHTML = ''; fileDlg.id = null;
  const o = fileDlg.opener; fileDlg.opener = null;
  if (o && o.isConnected && o.focus) o.focus();
}
// Redrawn with the side panel (renderPanel calls it). The search box is left alone so typing never
// loses its place, and so is the tag editor while it's being typed in.
function renderFileDlg() {
  if (!fileDlg.id) return;
  const m = MODS.find((x) => x.id === fileDlg.id);
  if (!m || !m.asset) { closeFileDlg(); return; }
  const a = m.asset, cur = state.assets[m.id], total = (library.entries[a.kind] || []).length;
  $('fdEyebrow').textContent = `${m.name} · ${a.label} · ${total} in library`;
  $('fileDlgTitle').textContent = `Choose ${a.kind === 'nam' ? 'a NAM capture' : a.kind === 'ir' ? 'an IR' : 'a file'}`;
  const curEl = $('fdCur');
  if (!curEl.contains(document.activeElement)) {
    curEl.innerHTML = `<div class="cur">${cur ? `<b>${cur.name.replace(/</g, '&lt;')}</b>` : '<b>Built-in</b>'}<span class="muted">${assetStatus[m.id] || ''}</span></div>
      ${a.ready ? '' : `<p class="note warnline">This build can’t load these files yet. They are stored, searchable and saved in presets.</p>`}
      ${cur && !cur.missing ? tagEditorHtml(a.kind, cur.name) : ''}`;
  }
  $('fdChips').innerHTML = chipsHtml('file:' + a.kind, library.entries[a.kind] || []);
  const list = $('al-' + m.id), top = list.scrollTop;
  list.innerHTML = assetListHtml(m); list.scrollTop = top;
  $('fdFoot').innerHTML = `<div class="drop" data-drop="${m.id}">Drop ${a.exts} files here to add them, or
      <button class="btn mini" data-act="asset-import" data-mod="${m.id}">Import…</button></div>
    <div class="row fdbtns">
      ${cur ? `<button class="btn" data-act="asset-clear" data-mod="${m.id}">Use built-in</button>` : ''}
      ${cur && !cur.missing ? `<button class="btn" data-act="asset-remove" data-mod="${m.id}">${removeArmed[m.id] ? 'Confirm remove' : 'Remove file'}</button>` : ''}
      <span class="spacer"></span><button class="btn primary" data-act="fd-close">Done</button></div>`;
}
function wireFileDlg() {
  const el = $('fileDlg');
  el.addEventListener('click', (e) => {
    if (e.target === el || e.target.closest('[data-act="fd-close"]')) closeFileDlg();
  });
  window.addEventListener('keydown', (e) => { if (e.key === 'Escape' && fileDlg.id) { e.preventDefault(); closeFileDlg(); } });
  // Keep Tab inside the window while it's open.
  el.addEventListener('keydown', (e) => {
    if (e.key !== 'Tab') return;
    const f = [...$('fileDlgCard').querySelectorAll('button,input,select,[tabindex]')].filter((x) => !x.disabled && x.offsetParent);
    if (!f.length) return;
    const first = f[0], last = f[f.length - 1];
    if (e.shiftKey && document.activeElement === first) { e.preventDefault(); last.focus(); }
    else if (!e.shiftKey && document.activeElement === last) { e.preventDefault(); first.focus(); }
  });
}

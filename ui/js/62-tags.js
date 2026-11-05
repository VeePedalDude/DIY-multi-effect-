// 62-tags.js - tags, favorites and notes for the library's files (NAM captures, IRs, presets, plugins)
// and favorites for every kind of effect and source, kept in library.json beside the library's folders
// (54-library-core.js: the files themselves are never touched, and the tags travel with the folder).
//
// Every file and type also has automatic tags from what it is (autoTags: a capture's gear and tone, an
// IR's length, a plugin's own tags). Each list (a module's file panel, the add panels) gets a row of
// chips: ★ for favorites and the tags used in that list, most used first; chosen chips narrow the list
// (a file must have every chosen tag). The search box matches tags and notes too.
const tagState = { ix: { format: 'rig-library', version: 1, files: {}, types: {} }, filter: {}, saveT: null };
rig.tags = tagState;

async function loadTagIndex() { if (library.core) tagState.ix = await library.core.readIndex(); decorateEntries(); }
function saveTagIndex() {
  clearTimeout(tagState.saveT);
  tagState.saveT = setTimeout(() => { if (library.core) library.core.writeIndex(tagState.ix).catch((e) => logLine('library.json: ' + e.message, true)); }, 250);
}
const fileKey = (kind, name) => `${kind}/${name}`;
// Library entries get .auto, .own, .tags (both), .fav and .note.
function decorateEntries() {
  for (const k in library.entries) for (const e of library.entries[k]) {
    const r = tagState.ix.files[fileKey(k, e.name)] || {};
    e.auto = autoTags(k, e); e.own = r.tags || []; e.tags = allTags(e.auto, e.own); e.fav = !!r.fav; e.note = r.note || '';
  }
}
function setFileTags(kind, name, change) {
  const key = fileKey(kind, name), r = { ...(tagState.ix.files[key] || {}), ...change };
  if (!r.tags || !r.tags.length) delete r.tags; if (!r.fav) delete r.fav; if (!r.note) delete r.note;
  if (Object.keys(r).length) tagState.ix.files[key] = r; else delete tagState.ix.files[key];
  saveTagIndex(); decorateEntries();
}
// Types: 'fx:holo', 'src:lfo'. Automatic tags: built-in or plugin, and a plugin's own tags.
function typeTags(scope, type, d) {
  const r = tagState.ix.types[`${scope}:${type}`] || {}, p = d.plugin;
  return { tags: allTags([p ? 'plugin' : 'built-in', ...((p && p.tags) || []).map((t) => String(t).toLowerCase())], r.tags), fav: !!r.fav };
}
function setTypeFav(scope, type, fav) {
  const key = `${scope}:${type}`, r = { ...(tagState.ix.types[key] || {}) };
  if (fav) r.fav = true; else delete r.fav;
  if (Object.keys(r).length) tagState.ix.types[key] = r; else delete tagState.ix.types[key];
  saveTagIndex();
}

// ---- the chips and the filter, per list (scope: 'file:nam', 'pick:fx'...)
function tagFilter(scope) { return tagState.filter[scope] || (tagState.filter[scope] = { fav: false, tags: new Set() }); }
function passesTags(scope, item) {
  const f = tagFilter(scope);
  if (f.fav && !item.fav) return false;
  for (const t of f.tags) if (!(item.tags || []).includes(t)) return false;
  return true;
}
function chipsHtml(scope, items, max = 14) {
  const f = tagFilter(scope), counts = tagCounts(items).slice(0, max);
  for (const t of f.tags) if (!counts.some(([x]) => x === t)) counts.push([t, 0]);          // a chosen tag stays visible
  const esc = (t) => String(t).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/"/g, '&quot;');
  const favN = items.filter((i) => i.fav).length;
  return `<div class="chips" data-chip-scope="${scope}" role="group" aria-label="Filter by tag">
    <button class="btn mini chip" data-chip-fav="${scope}" aria-pressed="${f.fav}" title="Favorites only">★ ${favN}</button>
    ${counts.map(([t, n]) => `<button class="btn mini chip" data-chip="${esc(t)}" data-chip-in="${scope}" aria-pressed="${f.tags.has(t)}">${esc(t)} <small>${n}</small></button>`).join('')}
  </div>`;
}
const starHtml = (on, attr, label) => `<button class="star" ${attr} aria-pressed="${on}" aria-label="${on ? 'Remove from' : 'Add to'} favorites: ${label.replace(/"/g, '&quot;')}" title="Favorite">${on ? '★' : '☆'}</button>`;
const tagLineHtml = (e) => (e.tags && e.tags.length ? `<span class="taglist">${e.tags.map((t) => `<span class="${(e.own || []).includes(t) ? 'own' : ''}">${String(t).replace(/</g, '&lt;')}</span>`).join('')}</span>` : '');

// The current file's tags and note, under a module's file panel.
function tagEditorHtml(kind, name) {
  const e = (library.entries[kind] || []).find((x) => x.name === name); if (!e) return '';
  const v = (s) => String(s).replace(/&/g, '&amp;').replace(/"/g, '&quot;');
  return `<div class="tagedit"><label>Your tags <input data-tags-file="${encodeURIComponent(kind + '/' + name)}" value="${v((e.own || []).join(', '))}" placeholder="comma, separated" autocomplete="off"></label>
    <label>Note <input data-note-file="${encodeURIComponent(kind + '/' + name)}" value="${v(e.note || '')}" placeholder="what it's good for" autocomplete="off"></label>
    <span class="meta">Automatic: ${(e.auto || []).join(', ') || 'none'}</span></div>`;
}

function wireTags() {
  const redraw = (scope) => {
    if (scope.startsWith('pick:')) pickerRefresh(scope.slice(5));
    else renderPanel();
  };
  for (const root of [panel, $('fileDlg')]) {
  root.addEventListener('click', (e) => {
    const fav = e.target.closest('[data-chip-fav]');
    if (fav) { const f = tagFilter(fav.dataset.chipFav); f.fav = !f.fav; redraw(fav.dataset.chipFav); return; }
    const chip = e.target.closest('[data-chip]');
    if (chip) { const f = tagFilter(chip.dataset.chipIn), t = chip.dataset.chip; if (f.tags.has(t)) f.tags.delete(t); else f.tags.add(t); redraw(chip.dataset.chipIn); return; }
    const sf = e.target.closest('[data-fav-file]');
    if (sf) { e.stopPropagation(); const [kind, ...rest] = decodeURIComponent(sf.dataset.favFile).split('/'), name = rest.join('/'), en = (library.entries[kind] || []).find((x) => x.name === name);
      setFileTags(kind, name, { fav: !(en && en.fav) }); renderPanel(); return; }
    const st = e.target.closest('[data-fav-type]');
    if (st) { e.stopPropagation(); const [scope, type] = st.dataset.favType.split(':'), d = scope === 'fx' ? MTYPES[type] : TYPES[type];
      setTypeFav(scope, type, !typeTags(scope, type, d).fav); pickerRefresh(scope); return; }
  }, true);
  root.addEventListener('change', (e) => {
    const t = e.target.dataset && (e.target.dataset.tagsFile || e.target.dataset.noteFile); if (!t) return;
    const [kind, ...rest] = decodeURIComponent(t).split('/'), name = rest.join('/');
    if (e.target.dataset.tagsFile) setFileTags(kind, name, { tags: parseTagText(e.target.value) });
    else setFileTags(kind, name, { note: e.target.value.trim().slice(0, 300) });
    e.target.blur(); renderPanel();
  });
  }
}

// 49-picker.js - the add panels' picker: a tab for each category the engine describes
// (engine/modules/categories.txt, engine/sources/categories.txt), a list of the kinds in that tab, and a
// search across every tab. Used by "Add an effect" (50-hud.js) and "Add a source" (51-source-panel.js).
// Nothing here knows what the categories or kinds are: a new folder or a new file in one shows up by
// itself. A tab with nothing in it yet says so, and where new ones go. The chosen tab is remembered
// per browser.
//
// PICKERS[kind] = { cats(): [{ id, label, blurb }], items(): [{ type, name, sub, detail, category, have, color }],
//                   full(), attr (data attribute of the Add buttons), noun, folder, doc }
const PICKERS = {};
const pickState = {};
function pickGet(kind) {
  if (!pickState[kind]) { let tab = null; try { tab = localStorage.getItem('rig01.pick.' + kind); } catch (e) { /* not remembered */ } pickState[kind] = { tab, q: '' }; }
  return pickState[kind];
}
const pickEsc = (t) => String(t).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/"/g, '&quot;');
// The tab to show: the remembered one if it still exists, else the first with something in it.
function pickTab(kind) {
  const P = PICKERS[kind], st = pickGet(kind), cats = P.cats(), items = P.items();
  if (st.tab && cats.some((c) => c.id === st.tab)) return st.tab;
  const first = cats.find((c) => items.some((i) => i.category === c.id)) || cats[0];
  return first ? first.id : null;
}
const pickMatch = (i, q) => !q || [i.name, i.type, i.sub, i.detail, (i.tags || []).join(' ')].join(' ').toLowerCase().includes(q.toLowerCase().trim());
// Items with their tags and favorite mark (62-tags.js), filtered by the chosen chips.
function pickItems(kind) { return PICKERS[kind].items().map((i) => ({ ...i, ...typeTags(kind, i.type, i.d || {}) })); }

function pickerTabsHtml(kind) {
  const P = PICKERS[kind], st = pickGet(kind), tab = pickTab(kind), items = pickItems(kind), searching = !!st.q.trim();
  return P.cats().map((c) => {
    const n = items.filter((i) => i.category === c.id && pickMatch(i, st.q) && passesTags('pick:' + kind, i)).length, on = !searching && c.id === tab;
    return `<button role="tab" class="tab${n ? '' : ' empty'}" id="tab-${kind}-${c.id}" data-pick-tab="${kind}:${c.id}" aria-selected="${on}" aria-controls="pick-${kind}" tabindex="${on || (searching && c === P.cats()[0]) ? 0 : -1}" title="${pickEsc(c.blurb)}">${pickEsc(c.label)} <small>${n}</small></button>`;
  }).join('');
}
function pickerListHtml(kind) {
  const P = PICKERS[kind], st = pickGet(kind), tab = pickTab(kind), full = P.full(), cats = P.cats();
  const searching = !!st.q.trim(), catOf = (id) => cats.find((c) => c.id === id) || { id, label: id, blurb: '' };
  const filtering = tagFilter('pick:' + kind).fav || tagFilter('pick:' + kind).tags.size;
  const items = pickItems(kind).filter((i) => passesTags('pick:' + kind, i) && (searching ? pickMatch(i, st.q) : filtering ? true : i.category === tab));
  const row = (i) => `<div class="patch"><div class="prow"><span>${starHtml(i.fav, `data-fav-type="${kind}:${i.type}"`, i.name)}<b${i.color ? ` style="color:${i.color}"` : ''}>${pickEsc(i.name)}</b> <small class="muted">${pickEsc(i.sub)}${i.have ? ` · ${i.have} in use` : ''}${searching || filtering ? ` · ${pickEsc(catOf(i.category).label)}` : ''}${i.d && i.d.plugin ? ` · plugin ${pickEsc(i.d.plugin.file || '')}` : ''}</small></span>
      <button class="btn mini" ${P.attr}="${i.type}" ${full ? 'disabled' : ''} aria-label="Add ${pickEsc(i.name)}">Add</button></div>
      <small class="muted">${pickEsc(i.detail)}</small></div>`;
  if (items.length) return items.map(row).join('');
  if (searching || filtering) return `<p class="note">No ${P.noun} matches${searching ? ` “${pickEsc(st.q.trim())}”` : ''}${filtering ? ' with the chosen tags' : ''}.</p>`;
  const c = catOf(tab);
  return `<p class="note">Nothing in <b>${pickEsc(c.label)}</b> yet (${pickEsc(c.blurb)}). New ones go in <span class="mono">${P.folder}/${c.id}/</span>: see <span class="mono">${P.doc}</span>.</p>`;
}
// The whole picker: search, tabs, list.
function pickerHtml(kind) {
  const P = PICKERS[kind], st = pickGet(kind), tab = pickTab(kind);
  return `<label class="pick-search"><span class="sr-only">Search ${P.noun}s</span><input type="search" data-pick-q="${kind}" placeholder="Search every ${P.noun}…" value="${pickEsc(st.q)}" autocomplete="off"></label>
  <div data-pick-chips="${kind}">${chipsHtml('pick:' + kind, pickItems(kind), 12)}</div>
  <div class="tabs" role="tablist" aria-label="Kinds of ${P.noun}" data-pick-tabs="${kind}">${pickerTabsHtml(kind)}</div>
  <div class="types" role="tabpanel" id="pick-${kind}" aria-labelledby="tab-${kind}-${tab}" data-pick-list="${kind}">${pickerListHtml(kind)}</div>
  <div class="pick-load"><button class="btn" data-pick-load="${kind}">Load ${P.noun} file (.rigfx)…</button>
    <span class="note" data-pick-note="${kind}">${pickEsc(st.note || `${P.noun === 'effect' ? 'An' : 'A'} ${P.noun} someone made as a file (a plugin): it’s kept in your library and added here.`)}</span></div>`;
}
// Redraw tabs and list in place (the search box keeps focus while you type).
function pickerRefresh(kind) {
  const t = document.querySelector(`[data-pick-tabs="${kind}"]`), l = document.querySelector(`[data-pick-list="${kind}"]`);
  if (t) t.innerHTML = pickerTabsHtml(kind);
  const ch = document.querySelector(`[data-pick-chips="${kind}"]`); if (ch) ch.innerHTML = chipsHtml('pick:' + kind, pickItems(kind), 12);
  if (l) { l.innerHTML = pickerListHtml(kind); l.setAttribute('aria-labelledby', `tab-${kind}-${pickTab(kind)}`); }
}
function pickSetTab(kind, tab, focus) {
  const st = pickGet(kind); st.tab = tab; st.q = '';
  try { localStorage.setItem('rig01.pick.' + kind, tab); } catch (e) { /* not remembered */ }
  const q = document.querySelector(`[data-pick-q="${kind}"]`); if (q) q.value = '';
  pickerRefresh(kind);
  if (focus) { const b = document.getElementById(`tab-${kind}-${tab}`); if (b) b.focus(); }
}
// "Load … file": choose a .rigfx, and it's in the library, loaded, and in the chain (or rack) at once.
async function pickLoadFiles(kind, files) {
  const st = pickGet(kind), say = (t) => { st.note = t; const n = document.querySelector(`[data-pick-note="${kind}"]`); if (n) n.textContent = t; };
  for (const f of files) {
    say(`Loading ${f.name}…`);
    const r = await pluginFromFile(f);
    if (!r.type) { say(r.note); flashHint(r.note); continue; }
    const want = kind === 'fx' ? 'effect' : 'source';
    if (r.kind !== want) { say(r.kind === 'source' ? `${r.name} is a modulation source: it’s in Add a source now.` : `${r.name} is an effect: it’s in Add an effect now.`); continue; }
    say(`${r.name} loaded; it’s in this list from now on.`);
    if (PICKERS[kind].full()) { say(`${r.name} loaded; the ${kind === 'fx' ? 'chain' : 'rack'} is full, so remove something to add it.`); continue; }
    if (kind === 'fx') { const m = addModule(r.type); if (m) { state.focus = m.id; update(); flyTo(m); } }
    else { const s = addSource(r.type); if (s) focusSource(s); }
  }
}
function wirePicker() {
  const input = $('pluginFile');
  input.addEventListener('change', async () => { const files = [...input.files], kind = input.dataset.kind; input.value = ''; if (files.length) await pickLoadFiles(kind, files); });
  panel.addEventListener('click', (e) => {
    const ld = e.target.closest('[data-pick-load]');
    if (ld) { input.dataset.kind = ld.dataset.pickLoad; input.click(); return; }
    const b = e.target.closest('[data-pick-tab]'); if (!b) return;
    const [kind, tab] = b.dataset.pickTab.split(':'); pickSetTab(kind, tab);
  });
  panel.addEventListener('input', (e) => {
    const k = e.target.dataset && e.target.dataset.pickQ; if (!k) return;
    pickGet(k).q = e.target.value; pickerRefresh(k);
  });
  panel.addEventListener('keydown', (e) => {                // arrow keys move between tabs
    const b = e.target.closest && e.target.closest('[data-pick-tab]'); if (!b || !['ArrowLeft', 'ArrowRight', 'Home', 'End'].includes(e.key)) return;
    const [kind] = b.dataset.pickTab.split(':'), ids = PICKERS[kind].cats().map((c) => c.id), i = ids.indexOf(b.dataset.pickTab.split(':')[1]);
    const j = e.key === 'Home' ? 0 : e.key === 'End' ? ids.length - 1 : (i + (e.key === 'ArrowRight' ? 1 : -1) + ids.length) % ids.length;
    e.preventDefault(); pickSetTab(kind, ids[j], true);
  });
}
rig.picker = { tab: (kind) => pickTab(kind), setTab: (kind, tab) => pickSetTab(kind, tab), search: (kind, q) => { pickGet(kind).q = q; pickerRefresh(kind); } };

// 57-skins.js - puts a skin on the running page, and the Skins menu (settings drawer).
// Skins come built in (ui/js/04-skin-core.js) or as .rigskin files in the library's skins/
// folder (drop one on the stage, or Import). The viewer marks favourites; K steps through them.
// The choice is remembered per browser. Export writes the skin in use out as a complete
// template: every setting spelled out, plus its images, ready to edit into a new skin.
const BUILTIN_SKINS = [
  { id: 'builtin:astral', json: DEFAULT_SKIN },
  { id: 'builtin:classic', json: CLASSIC_SKIN },
];
const skinState = { id: 'builtin:astral', json: DEFAULT_SKIN, files: null, urls: [], warnings: [], busy: false };
const lsGet = (k, d) => { try { const v = localStorage.getItem(k); return v === null ? d : v; } catch (e) { return d; } };
const lsSet = (k, v) => { try { localStorage.setItem(k, v); } catch (e) { /* not remembered in this browser */ } };
let skinFavs = new Set(); try { skinFavs = new Set(JSON.parse(lsGet('rig01.skinFavs', '[]'))); } catch (e) { /* none */ }

function dataUrlBytes(u) { return b64ToBytes(u.slice(u.indexOf(',') + 1)); }
const MIME = { webp: 'image/webp', png: 'image/png', jpg: 'image/jpeg', jpeg: 'image/jpeg', gif: 'image/gif', svg: 'image/svg+xml', avif: 'image/avif', css: 'text/css', woff2: 'font/woff2', woff: 'font/woff', ttf: 'font/ttf', otf: 'font/otf' };
const mimeOf = (n) => MIME[n.split('.').pop().toLowerCase()] || 'application/octet-stream';

// Every skin the viewer can choose: built-ins, then the library's skins/ folder.
function skinChoices() {
  const lib = (library.entries.skins || []).map((e) => ({ id: 'lib:' + e.name, name: (e.meta && e.meta.name) || e.name.replace(/\.rigskin$/i, ''), author: e.meta && e.meta.author, file: e.name }));
  return BUILTIN_SKINS.map((b) => ({ id: b.id, name: b.json.name, author: b.json.author, builtIn: true })).concat(lib);
}
// Read a skin by id -> { json, skin (validated), files: Map | null, warnings } or { error }.
async function loadSkin(id) {
  const b = BUILTIN_SKINS.find((x) => x.id === id);
  if (b) { const r = readSkinJson(b.json, (n) => typeof UI_ASSETS !== 'undefined' && n in UI_ASSETS); return { json: b.json, skin: r.skin, files: null, warnings: r.warnings }; }
  if (!id.startsWith('lib:') || !library.core) return { error: 'not found' };
  try { return await readSkinPacket(new Uint8Array(await (await library.core.file('skins', id.slice(4))).arrayBuffer())); }
  catch (e) { return { error: e.message }; }
}
// Put a skin on: by id. Returns true if it was applied.
async function useSkin(id) {
  if (skinState.busy) return false;
  skinState.busy = true;
  try {
    const r = await loadSkin(id);
    if (r.error) { skinNote(`Couldn't use that skin: ${r.error}`, true); logLine(`skin ${id}: ${r.error}`, true); return false; }
    await applySkin(mergeSkin(DEFAULT_SKIN, r.skin), r.files);
    Object.assign(skinState, { id, json: r.json, files: r.files, warnings: r.warnings });
    lsSet('rig01.skin', id);
    skinNote(r.warnings.length ? `${SKIN.name}: ${r.warnings.length} setting${r.warnings.length > 1 ? 's' : ''} skipped (${r.warnings[0]}${r.warnings.length > 1 ? '; rest in Engine log' : ''})` : `${SKIN.name}${SKIN.author ? ' by ' + SKIN.author : ''}`, r.warnings.length > 0);
    r.warnings.forEach((w) => logLine('skin: ' + w, true));
    logLine(`skin: ${SKIN.name}`);
    renderSkinList();
    return true;
  } finally { skinState.busy = false; }
}
// Apply a fully merged skin to the page, the 3D stage and every face.
async function applySkin(S, files) {
  skinState.urls.forEach((u) => URL.revokeObjectURL(u)); skinState.urls = [];
  const urlOf = (name) => {
    if (!name) return null;
    if (files) { const b = files.get(name); if (!b) return null; const u = URL.createObjectURL(new Blob([b], { type: mimeOf(name) })); skinState.urls.push(u); return u; }
    return typeof UI_ASSETS !== 'undefined' ? UI_ASSETS[name] || null : null;
  };
  SKIN = S; ACC = S.screen.accent;
  // menus, panels, tables, bars: CSS variables
  const root = document.documentElement.style;
  for (const [k, v] of Object.entries(S.ui)) root.setProperty(k === 'fontDisplay' ? '--display' : k === 'fontMono' ? '--mono' : '--' + k, v);
  root.setProperty('--stageBg', S.stage.backgroundColor);
  // fonts and the optional stylesheet that come with the skin
  for (const f of S.fonts || []) {
    const b = files && files.get(f.file); if (!b) continue;
    try { const ff = new FontFace(f.family, b, { weight: f.weight, style: f.style }); await ff.load(); document.fonts.add(ff); }
    catch (e) { logLine(`skin font ${f.file}: ${e.message}`, true); }
  }
  let css = document.getElementById('skinCss'); if (!css) { css = document.createElement('style'); css.id = 'skinCss'; document.head.appendChild(css); }
  css.textContent = S.css && files && files.get(S.css) ? new TextDecoder().decode(files.get(S.css)) : '';
  // the stage
  const bg = urlOf(S.stage.background); $('backdrop').style.backgroundImage = bg ? `url("${bg}")` : 'none';
  scene.fog.color.set(S.stage.fog); scene.fog.density = S.stage.fogDensity;
  flowTube.material.color.set(S.stage.flow); flowMat.color.set(S.stage.flow);
  MODS.forEach((m) => { const st = styleOf(m); m.slab.material.color.set(S.stage.slab); m.halo.material.color.set(st.glow); m.glow.material.color.set(st.glow); });
  SRC.forEach((s, i) => { s.color = sourceColor(s.id, i); [s.orb, s.ring, s.halo].forEach((o) => o.material.color.set(s.color)); });
  syncCables();
  // card images, decoded before the faces are redrawn
  Object.keys(SKIN_IMG).forEach((k) => delete SKIN_IMG[k]);
  await Promise.all(Object.values(S.tiles).map((t) => t.image).filter((n, i, a) => n && a.indexOf(n) === i).map(async (n) => {
    const u = urlOf(n); if (!u) return;
    const im = new Image(); im.src = u;
    try { await im.decode(); SKIN_IMG[n] = im; } catch (e) { logLine(`skin image ${n} couldn't be decoded`, true); }
  }));
  drawPill(inLbl); drawPill(outLbl); MODS.forEach(drawFace); SRC.forEach(drawLabel);
  update();
}

// ------------------------------------------------------------------ the Skins menu
function skinNote(text, warn) { const n = $('skinNote'); if (n) { n.textContent = text; n.className = 'note' + (warn ? ' warnline' : ''); } }
function renderSkinList() {
  const el = $('skinList'); if (!el) return;
  const all = skinChoices().sort((a, b) => (skinFavs.has(b.id) - skinFavs.has(a.id)));
  el.innerHTML = all.map((s) => {
    const on = s.id === skinState.id, fav = skinFavs.has(s.id), nm = String(s.name).replace(/</g, '&lt;');
    return `<li class="skinrow${on ? ' on' : ''}">
      <button class="star" data-skin-fav="${encodeURIComponent(s.id)}" aria-pressed="${fav}" aria-label="${fav ? 'Remove' : 'Add'} ${nm} ${fav ? 'from' : 'to'} favourites" title="Favourite">${fav ? '★' : '☆'}</button>
      <span class="grow"><b>${nm}</b><small>${s.builtIn ? 'built in' : 'skins/' + String(s.file).replace(/</g, '&lt;')}${s.author ? ' · ' + String(s.author).replace(/</g, '&lt;') : ''}</small></span>
      ${on ? '<span class="inuse">In use</span>' : `<button class="btn mini" data-skin-use="${encodeURIComponent(s.id)}">Use</button>`}</li>`;
  }).join('');
}
// K: the next favourite (or the next skin, with no favourites)
function cycleSkin() {
  const all = skinChoices(), pool = all.filter((s) => skinFavs.has(s.id)), list = pool.length ? pool : all;
  if (!list.length) return;
  const i = list.findIndex((s) => s.id === skinState.id);
  useSkin(list[(i + 1) % list.length].id);
}
// The skin in use, written out as a complete, editable template.
function exportSkin() {
  const full = JSON.parse(JSON.stringify(SKIN));
  full.name = SKIN.name + ' (template)'; full.description = `Starting point made from “${SKIN.name}”. Edit skin.json and the images, zip the folder, rename it .rigskin.`;
  const files = [['skin.json', new TextEncoder().encode(JSON.stringify(full, null, 2))],
    ['SKIN-README.txt', new TextEncoder().encode('A RIG/01 skin. Change colours, images and fonts in skin.json; every setting is listed.\n' +
      'Images are referenced by file name. Card frames are drawn at 518 x 754 (keep that shape, with transparent corners).\n' +
      'Zip this folder (skin.json at the top or in one folder), rename the .zip to .rigskin, and drop it on the RIG/01 stage.\n' +
      'Full reference: docs/SKINS.md in the project.\n')]];
  for (const n of skinFiles(full)) {
    const b = skinState.files ? skinState.files.get(n) : (typeof UI_ASSETS !== 'undefined' && UI_ASSETS[n] ? dataUrlBytes(UI_ASSETS[n]) : null);
    if (b) files.push([n, b]);
  }
  const blob = new Blob([zipFiles(files)], { type: 'application/zip' }), a = document.createElement('a');
  a.href = URL.createObjectURL(blob); a.download = safeFileName(SKIN.name) + ' template.rigskin';
  document.body.appendChild(a); a.click(); a.remove(); setTimeout(() => URL.revokeObjectURL(a.href), 4000);
  rig.lastExport = { name: a.download, bytes: blob.size, files: files.map((f) => f[0]) };
  skinNote(`Exported ${a.download} (${files.length} files)`); logLine(`skin exported: ${a.download}`);
}
function wireSkins() {
  $('skinList').addEventListener('click', (e) => {
    const u = e.target.closest('[data-skin-use]'), f = e.target.closest('[data-skin-fav]');
    if (u) useSkin(decodeURIComponent(u.dataset.skinUse));
    if (f) { const id = decodeURIComponent(f.dataset.skinFav); if (skinFavs.has(id)) skinFavs.delete(id); else skinFavs.add(id); lsSet('rig01.skinFavs', JSON.stringify([...skinFavs])); renderSkinList(); }
  });
  $('skinImport').onclick = () => $('skinFiles').click();
  $('skinFiles').onchange = async (e) => { const files = [...e.target.files]; e.target.value = ''; if (files.length) await importFiles(files, null); };
  $('skinExport').onclick = exportSkin;
  window.addEventListener('keydown', (e) => {
    if (e.target.closest && e.target.closest('input,textarea,select')) return;
    if ((e.key === 'k' || e.key === 'K') && !e.ctrlKey && !e.metaKey && !e.altKey) cycleSkin();
  });
  renderSkinList();
}
// At start: the remembered skin (a library skin waits for the library to open).
async function restoreSkin(libraryReady) {
  const id = lsGet('rig01.skin', 'builtin:astral');
  if (id.startsWith('lib:')) { await libraryReady; if (await useSkin(id)) return; }
  await useSkin(BUILTIN_SKINS.some((b) => b.id === id) ? id : 'builtin:astral');
}
rig.useSkin = useSkin; rig.skinState = skinState; rig.exportSkin = exportSkin;
rig.skinTools = { unzip, zipFiles };          // for tests and the console

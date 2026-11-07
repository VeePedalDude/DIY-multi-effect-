// 55-library.js - picks the library's storage (your folder, else browser storage, else memory),
// keeps the file lists fresh, and runs imports (file picker, whole folders, drag and drop).
const library = { core: null, backend: '', rootName: '', kinds: {}, entries: {}, pendingFolder: null };
rig.library = library;
rig.importFiles = (files) => importFiles(files, null);        // tests and the console

function libraryKinds() {
  const k = {};
  MODS.forEach((m) => { if (!m.asset) return; const list = k[m.asset.kind] || (k[m.asset.kind] = []);
    m.asset.exts.split(',').map((e) => e.trim().toLowerCase()).forEach((e) => { if (e && !list.includes(e)) list.push(e); }); });
  k.presets = ['.rig.json'];
  k.skins = ['.rigskin'];
  k.plugins = ['.rigfx', '.clap'];                             // effects and sources as files (61-plugins.js)
  return k;
}
const kindLabel = (k) => ({ ir: 'IRs', nam: 'NAM captures', presets: 'presets', skins: 'skins', plugins: 'plugins' }[k] || k);

async function initLibrary() {
  library.kinds = libraryKinds();
  if (HOST) {                                   // the rig-host's folder in Documents: no choosing, no permission prompts
    try { const root = await hostLibraryRoot(); await bringBrowserLibrary(root); library.hostSig = hostLibSig(await root.lib.list()); return useLibraryRoot(root, 'host'); }
    catch (e) { logLine(`library: the rig-host's folder isn't available (${e.message}); using browser storage`, true); }
  }
  const saved = await savedFolderHandle();
  if (saved) {
    let perm = 'prompt';
    try { perm = await saved.queryPermission({ mode: 'readwrite' }); } catch (e) { /* treat as needing a click */ }
    if (perm === 'granted') return useLibraryRoot(saved, 'folder');
    library.pendingFolder = saved;              // re-allowing a folder needs a click, so offer a button
    $('libReconnect').hidden = false;
  }
  try { await useLibraryRoot(new KvDir(idbStore(await idbOpen()), '', 'browser storage'), 'browser'); }
  catch (e) { await useLibraryRoot(new KvDir(memStore(), '', 'memory'), 'memory'); }
}
// Once, the first time the rig-host's folder is used: anything imported into this browser's storage before
// (NAMs, IRs, plugins, skins, presets and the pedalboard, tags) is copied into it. Duplicates are skipped.
async function bringBrowserLibrary(root) {
  try { if (localStorage.getItem('rig01.lib.brought')) return; } catch (e) { return; }
  let old; try { old = new KvDir(idbStore(await idbOpen()), '', 'browser storage'); } catch (e) { return; }
  const from = new LibraryCore(old, library.kinds), to = new LibraryCore(root, library.kinds);
  let copied = 0;
  for (const k in library.kinds) for (const e of await from.list(k).catch(() => [])) {
    const r = await to.importFile(await from.file(k, e.name), k).catch(() => null); if (r && (r.status === 'added' || r.status === 'renamed')) copied++;
  }
  for (const [kind, name] of [['presets', 'board.json'], ['', 'library.json']]) {   // only if the folder has none yet
    try {
      const src = kind ? await (await old.getDirectoryHandle(kind)).getFileHandle(name) : await old.getFileHandle(name);
      const dst = kind ? await root.getDirectoryHandle(kind) : root;
      if (await dst.getFileHandle(name).then(() => true, () => false)) continue;
      const w = await (await dst.getFileHandle(name, { create: true })).createWritable(); await w.write(await (await src.getFile()).arrayBuffer()); await w.close(); copied++;
    } catch (e) { /* nothing there */ }
  }
  if (copied) logLine(`library: copied ${copied} file(s) from this browser into ${root.name}`);
  try { localStorage.setItem('rig01.lib.brought', '1'); } catch (e) { /* asks again next time; duplicates are skipped */ }
}
async function useLibraryRoot(root, backend) {
  library.core = new LibraryCore(root, library.kinds); library.backend = backend; library.rootName = root.name;
  await library.core.ensure();
  await refreshLibrary();
  await initBoard();                            // the pedalboard lives in this library (70-presets.js)
}
async function refreshLibrary() {
  if (!library.core) return;
  for (const k in library.kinds) {
    try { library.entries[k] = await library.core.list(k); } catch (e) { library.entries[k] = []; logLine(`library: couldn't read ${k}/: ${e.message}`, true); }
  }
  const where = {
    folder: `Folder “${library.rootName}”. Files you put in its ir, nam, presets, skins and plugins folders show up after Rescan.`,
    host: `Folder ${library.rootName}. Put files in its NAM, IR, Effects, Mod Sources, Skins and Presets folders: they show up when you come back to this page.`,
    browser: 'Stored in this browser. Choose a folder to keep the library as normal files you can open, back up and share.',
    memory: 'Temporary: this page can’t store files where it’s open, so imports are lost on reload.',
  }[library.backend];
  $('libWhere').textContent = library.pendingFolder && library.backend !== 'folder' ? `Your folder “${library.pendingFolder.name}” needs permission again. Until then: ${where}` : where;
  $('libChoose').hidden = library.backend === 'host'; $('libOpen').hidden = library.backend !== 'host';
  $('libCounts').textContent = Object.keys(library.kinds).map((k) => `${(library.entries[k] || []).length} ${kindLabel(k)}`).join(' · ');
  await knowLibraryPlugins();
  await loadTagIndex();                                       // library.json: tags, favorites, notes (62-tags.js)
  renderPluginList();
  refreshPresetList();
  renderSkinList();
  renderPanel();
}
// Back on the page after adding files in Explorer: read the folder again if anything changed, and hand
// any new plugin to the engine so it can be added straight away.
const hostLibSig = (l) => l.files.map((f) => `${f.k}:${f.s}:${f.m}`).join('|');
async function rescanHostLibrary() {
  if (library.backend !== 'host') return;
  let now; try { library.core.root.lib.changed(); now = hostLibSig(await library.core.root.lib.list()); } catch (e) { return; }
  if (now === library.hostSig) return;
  const had = new Set((library.entries.plugins || []).map((e) => e.name));
  library.hostSig = now; await refreshLibrary();
  if (rig.node) for (const e of library.entries.plugins || []) if (!had.has(e.name)) {
    try { await loadPlugin(e.name, new Uint8Array(await (await library.core.file('plugins', e.name)).arrayBuffer())); } catch (err) { logLine(`plugin ${e.name}: ${err.message}`, true); }
  }
}
async function chooseLibraryFolder() {
  if (!window.showDirectoryPicker) { logLine('This browser can’t open a folder. Use Chrome or Edge on a desktop.', true); return; }
  let h; try { h = await showDirectoryPicker({ id: 'rig-library', mode: 'readwrite' }); } catch (e) { if (e.name !== 'AbortError') logLine('folder: ' + e.message, true); return; }
  const old = library.core, oldBackend = library.backend;
  await rememberFolderHandle(h); library.pendingFolder = null; $('libReconnect').hidden = true;
  await useLibraryRoot(h, 'folder');
  if (old && oldBackend !== 'folder') {                // bring along anything imported before the folder was chosen
    let copied = 0;
    for (const k in library.kinds) for (const e of await old.list(k)) { const r = await library.core.importFile(await old.file(k, e.name), k); if (r.status === 'added' || r.status === 'renamed') copied++; }
    if (copied) { logLine(`library: copied ${copied} file(s) from browser storage into “${h.name}”`); await refreshLibrary(); }
  }
  logLine(`library: using folder “${h.name}”`);
}
async function reconnectLibraryFolder() {
  const h = library.pendingFolder; if (!h) return;
  try { if ((await h.requestPermission({ mode: 'readwrite' })) === 'granted') { library.pendingFolder = null; $('libReconnect').hidden = true; await useLibraryRoot(h, 'folder'); logLine(`library: reconnected “${h.name}”`); } }
  catch (e) { logLine('folder: ' + e.message, true); }
}
// Import files; if `target` (a module) is given, its slot's kind is required and the first good file is loaded into it.
async function importFiles(files, target) {
  if (!library.core) return [];
  const want = target && target.asset ? target.asset.kind : null, results = [];
  for (const f of files) {
    try { results.push(await library.core.importFile(f, want)); } catch (e) { results.push({ status: 'skipped', name: f.name, reason: e.message }); }
  }
  results.forEach((r) => logLine(r.status === 'skipped' ? `skipped ${r.name}: ${r.reason}` : `${r.status === 'duplicate' ? 'already in library' : r.status === 'renamed' ? 'imported as' : 'imported'} ${r.kind}/${r.name}`, r.status === 'skipped'));
  await refreshLibrary();
  for (const r of results) if (r.kind === 'plugins' && (r.status === 'added' || r.status === 'renamed') && rig.node) {   // a new plugin is ready straight away
    await loadPlugin(r.name, new Uint8Array(await (await library.core.file('plugins', r.name)).arrayBuffer()));
  }
  const skins = results.filter((r) => r.kind === 'skins' && r.status !== 'skipped');
  if (skins.length) await useSkin('lib:' + skins[skins.length - 1].name);   // a dropped skin is put on straight away
  const ok = results.filter((r) => r.status !== 'skipped');
  if (target && ok.length) await selectAsset(target, ok[0].name);
  const skipped = results.length - ok.length;
  flashHint(`${ok.length} file${ok.length === 1 ? '' : 's'} in library${skipped ? ` · ${skipped} skipped (see Engine log)` : ''}${target && ok.length ? ` · loaded into ${target.name}` : ''}`);
  return results;
}
async function importFolderTree() {
  if (!window.showDirectoryPicker) { logLine('This browser can’t open a folder. Use Chrome or Edge on a desktop.', true); return; }
  let src; try { src = await showDirectoryPicker({ id: 'rig-import', mode: 'read' }); } catch (e) { return; }
  const files = [];
  async function walk(dir, depth) {
    for await (const h of dir.values()) {
      if (h.kind === 'file' && kindForName(h.name, library.kinds)) files.push(await h.getFile());
      else if (h.kind === 'directory' && depth < 4) await walk(h, depth + 1);
    }
  }
  await walk(src, 0);
  logLine(`importing ${files.length} file(s) from “${src.name}”…`);
  await importFiles(files, null);
}
let hintTimer = 0;
function flashHint(text) { const el = $('dropHint'); el.textContent = text; el.hidden = false; clearTimeout(hintTimer); hintTimer = setTimeout(() => { el.hidden = true; }, 3500); }

function wireLibrary() {
  $('libChoose').onclick = chooseLibraryFolder;
  $('libOpen').onclick = () => hostLibReq('POST', 'open').catch((e) => logLine(e.message, true));
  addEventListener('focus', rescanHostLibrary);
  $('libReconnect').onclick = reconnectLibraryFolder;
  $('libRescan').onclick = async () => { if (library.backend === 'host') { library.hostSig = null; await rescanHostLibrary(); } else await refreshLibrary(); logLine('library rescanned'); };
  $('libImport').onclick = () => { $('libFiles').dataset.target = ''; $('libFiles').accept = ''; $('libFiles').click(); };   // every kind (a module's Import narrows it)
  $('libImportDir').onclick = importFolderTree;
  $('libFiles').onchange = async (e) => {
    const files = [...e.target.files], target = MODS.find((m) => m.id === e.target.dataset.target) || null;
    e.target.value = ''; if (files.length) await importFiles(files, target);
  };
}

// The settings drawer's list of plugin files, and what became of each.
function renderPluginList() {
  const el = $('libPlugins'); if (!el) return;
  const list = library.entries.plugins || [];
  if (!list.length) { el.innerHTML = `No plugins yet: put <span class="mono">.rigfx</span> bundles in the library\u2019s ${library.backend === 'host' ? '<span class="mono">Effects</span> or <span class="mono">Mod Sources</span> folder' : '<span class="mono">plugins</span> folder'}, or use Load effect file in Add an effect (docs/PLUGINS.md).`; return; }
  const esc = (t) => String(t).replace(/&/g, '&amp;').replace(/</g, '&lt;');
  el.innerHTML = list.map((e) => {
    const m = e.meta || {}, r = plugins.results[e.name];
    const what = m.error ? `not usable: ${m.error}` : m.native ? `native CLAP (${m.native}): the rig-host runs it` : `${m.name || ''} · ${m.kind || ''}${m.author ? ' · ' + m.author : ''}`;
    const state = r ? (r.status >= 0 ? ' · loaded' : ` · not loaded: ${r.note}`) : '';
    return `<div><b>${esc(e.name)}</b> <span class="muted">${esc(what + state)}</span></div>`;
  }).join('');
}

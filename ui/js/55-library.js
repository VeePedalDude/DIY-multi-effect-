// 55-library.js - picks the library's storage (your folder, else browser storage, else memory),
// keeps the file lists fresh, and runs imports (file picker, whole folders, drag and drop).
const library = { core: null, backend: '', rootName: '', kinds: {}, entries: {}, pendingFolder: null };
rig.library = library;

function libraryKinds() {
  const k = {};
  MODS.forEach((m) => { if (!m.asset) return; const list = k[m.asset.kind] || (k[m.asset.kind] = []);
    m.asset.exts.split(',').map((e) => e.trim().toLowerCase()).forEach((e) => { if (e && !list.includes(e)) list.push(e); }); });
  k.presets = ['.rig.json'];
  k.skins = ['.rigskin'];
  return k;
}
const kindLabel = (k) => ({ ir: 'IRs', nam: 'NAM captures', presets: 'presets', skins: 'skins' }[k] || k);

async function initLibrary() {
  library.kinds = libraryKinds();
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
async function useLibraryRoot(root, backend) {
  library.core = new LibraryCore(root, library.kinds); library.backend = backend; library.rootName = root.name;
  await library.core.ensure();
  await refreshLibrary();
}
async function refreshLibrary() {
  if (!library.core) return;
  for (const k in library.kinds) {
    try { library.entries[k] = await library.core.list(k); } catch (e) { library.entries[k] = []; logLine(`library: couldn't read ${k}/: ${e.message}`, true); }
  }
  const where = {
    folder: `Folder “${library.rootName}”. Files you put in its ir, nam, presets and skins folders show up after Rescan.`,
    browser: 'Stored in this browser. Choose a folder to keep the library as normal files you can open, back up and share.',
    memory: 'Temporary: this page can’t store files where it’s open, so imports are lost on reload.',
  }[library.backend];
  $('libWhere').textContent = library.pendingFolder && library.backend !== 'folder' ? `Your folder “${library.pendingFolder.name}” needs permission again. Until then: ${where}` : where;
  $('libCounts').textContent = Object.keys(library.kinds).map((k) => `${(library.entries[k] || []).length} ${kindLabel(k)}`).join(' · ');
  refreshPresetList();
  renderSkinList();
  renderPanel();
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
  $('libReconnect').onclick = reconnectLibraryFolder;
  $('libRescan').onclick = async () => { await refreshLibrary(); logLine('library rescanned'); };
  $('libImport').onclick = () => { $('libFiles').dataset.target = ''; $('libFiles').click(); };
  $('libImportDir').onclick = importFolderTree;
  $('libFiles').onchange = async (e) => {
    const files = [...e.target.files], target = MODS.find((m) => m.id === e.target.dataset.target) || null;
    e.target.value = ''; if (files.length) await importFiles(files, target);
  };
}

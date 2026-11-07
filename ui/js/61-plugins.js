// 61-plugins.js - effects and modulation sources that come as files: .rigfx bundles (built with
// tools/build_plugin.py, docs/PLUGINS.md) in the library's plugins/ folder, and, in the rig-host, plain
// .clap effects too. Nothing is rebuilt: a plugin is read here and handed to the engine, which adds it
// as a new type (engine/plugins.h).
//
//   - In the browser the bundle's module.wclap (WebAssembly) goes to the audio worklet
//     (ui/worklet/wclap-host.js). A plain .clap is native code, which only the rig-host can run.
//   - Served by rig-host, the whole file goes over the socket and the host loads its own platform's
//     .clap from the bundle (or the .clap itself), off the audio thread.
// The bundle's manifest carries the plugin's full description, so the add panels and presets know a
// library plugin as soon as the library is read, before the engine has it; plugins load before the rig
// is sent to the engine at Start. A bundle's screen.js, if it has one, draws its face.
// The library decides (syncPlugins, after every read of it): a file that's gone takes its type out of the
// add panels, a new or changed one is loaded, and loading a newer version of a type swaps the copies
// already in the rig onto it (upgradeInstances), knobs and patches kept.
const plugins = { known: {}, results: {}, loaded: {}, waiting: new Map(), screens: new Set(), upgrades: [], descSeq: 0 };
rig.plugins = plugins;
rig.typesKnown = () => ({ modules: MTYPE_ORDER.slice(), sources: TYPE_ORDER.slice() });   // tests and the console

// .rigfx bytes -> { manifest, wclap, screen } (throws if it isn't one)
async function readRigfx(bytes) {
  const files = await unzip(bytes);
  const m = files.get('manifest.json'); if (!m) throw new Error('no manifest.json inside');
  const manifest = JSON.parse(new TextDecoder().decode(m));
  if (manifest.format !== 'rigfx' || !manifest.description) throw new Error('not a RIG plugin bundle');
  return { manifest, wclap: files.get('module.wclap') || null, screen: files.has('screen.js') ? new TextDecoder().decode(files.get('screen.js')) : '' };
}
// A plugin's description (sdk/rig_clap_ext.h) as the engine's describe() lists a type.
function pluginTypeEntry(d, file) {
  const plugin = { file, author: d.author || '', version: d.version || '', tags: d.tags || [] };
  const params = (d.params || []).map((q) => ({ ...q, stops: q.stops && q.stops.length ? q.stops : undefined }));
  return d.kind === 'source'
    ? { type: d.id, category: d.category, name: d.name, sub: d.sub || '', ext: null, taps: false, params, settings: [], table: { rows: 0, cols: [] }, meters: d.meters || [], plugin }
    : { type: d.id, category: d.category, name: d.name, kind: d.sub || '', params, meters: d.meters || [], asset: null, plugin };
}
// Types the page knows: from a describe() (the engine has them) or a manifest (it will at Start). A plugin
// type that's there already takes the newer description (a built-in never changes).
function mergeTypes(desc) {
  let added = 0;
  const put = (list, MAP, ORDER, t) => {
    const had = MAP[t.type];
    if (had && !had.plugin) return;
    const i = list.findIndex((x) => x.type === t.type); if (i >= 0) list[i] = t; else list.push(t);
    if (!ORDER.includes(t.type)) { ORDER.push(t.type); added++; }
    MAP[t.type] = { ...t, index: had ? had.index : ORDER.length - 1 };
  };
  for (const t of desc.moduleTypes || []) put(DESC.moduleTypes, MTYPES, MTYPE_ORDER, t);
  for (const t of desc.sourceTypes || []) put(DESC.sourceTypes, TYPES, TYPE_ORDER, t);
  return added;
}
// A plugin type whose file left the library: out of the add panels (copies in the rig keep their entry).
function dropPluginType(id) {
  const m = MTYPE_ORDER.indexOf(id), s = TYPE_ORDER.indexOf(id);
  if (m >= 0 && MTYPES[id].plugin) MTYPE_ORDER.splice(m, 1);
  if (s >= 0 && TYPES[id].plugin) TYPE_ORDER.splice(s, 1);
}
// Library plugins' manifests -> known types (called whenever the library is read).
async function knowLibraryPlugins() {
  for (const e of library.entries.plugins || []) {
    const d = e.meta && e.meta.description; if (!d || plugins.known[e.name] === e.key) continue;
    plugins.known[e.name] = e.key;
    mergeTypes(d.kind === 'source' ? { sourceTypes: [pluginTypeEntry(d, e.name)] } : { moduleTypes: [pluginTypeEntry(d, e.name)] });
  }
}
const libId = (e) => (e.meta && e.meta.description && e.meta.description.id) || null;
// The library against what the engine has: run after every read of the library.
// One at a time: each call waits for the one before, then reads the library as it is by then.
function syncPlugins() { return (plugins.sync = (plugins.sync || Promise.resolve()).then(syncPluginsNow, syncPluginsNow)); }
async function syncPluginsNow() {
  const files = library.entries.plugins || [], names = new Set(files.map((e) => e.name));
  const provided = (id) => files.some((e) => libId(e) === id);
  for (const name of Object.keys(plugins.known)) if (!names.has(name)) delete plugins.known[name];
  for (const [name, L] of Object.entries(plugins.loaded)) {   // removed from the library
    if (names.has(name)) continue;
    delete plugins.loaded[name]; delete plugins.results[name];
    if (L.id && !provided(L.id) && !Object.values(plugins.loaded).some((x) => x.id === L.id)) { dropPluginType(L.id); rig.cmd(`plugin_remove ${L.id}`); logLine(`plugin ${name} removed: ${L.id} left the add panels`); }
  }
  for (const t of [...MTYPE_ORDER.filter((t) => MTYPES[t].plugin), ...TYPE_ORDER.filter((t) => TYPES[t].plugin)]) {   // known only from a manifest, not loaded
    if (!provided(t) && !Object.values(plugins.loaded).some((x) => x.id === t)) dropPluginType(t);
  }
  if (rig.node) for (const e of files) {                       // new or changed: load it
    if (!HOST && /\.clap$/i.test(e.name)) continue;
    const L = plugins.loaded[e.name]; if (L && L.key === e.key) continue;
    if (!L && plugins.results[e.name] && plugins.results[e.name].status < 0 && plugins.results[e.name].key === e.key) continue;   // failed before, unchanged
    try { await loadPlugin(e.name, new Uint8Array(await (await library.core.file('plugins', e.name)).arrayBuffer())); } catch (err) { logLine(`plugin ${e.name}: ${err.message}`, true); }
  }
  renderPluginList(); if (typeof renderPanel === 'function') renderPanel();
}
// A newer version of a type replaced the old one in the engine: the copies in the rig still run the old
// code, so each is taken out and put back as the new one, same id and place, with its knobs (by name),
// bypass, settings and patches.
function upgradeInstances(id) {
  let n = 0;
  if (MTYPES[id] && MTYPES[id].plugin) MODS.forEach((m, pos) => {
    if (m.type !== id) return;
    const vals = Object.fromEntries(m.params.map((p) => [p.sym, p.v]));
    rig.cmd(`fx_remove ${m.id}`); rig.cmd(`fx_add ${id} ${m.id} ${pos}`);
    const u = makeModule(m.id, id, null); u.params.forEach((p) => { if (p.sym in vals) p.v = vals[p.sym]; });
    delete MODDATA[m.id]; MODS[pos] = u; n++;
  });
  if (TYPES[id] && TYPES[id].plugin) SRC.forEach((s, i) => {
    if (s.type !== id) return;
    const pv = Object.fromEntries(s.params.map((q) => [q.sym, q.v])), sv = Object.fromEntries(s.settings.map((q) => [q.sym, q.v]));
    rig.cmd(`src_remove ${s.id}`); rig.cmd(`src_add ${id} ${s.id}`);
    const u = makeSource(s.id, id, null);
    u.params.forEach((q) => { if (q.sym in pv) q.v = pv[q.sym]; }); u.settings.forEach((q) => { if (q.sym in sv) q.v = sv[q.sym]; });
    if (s.table && u.table && s.table.rows === u.table.rows && s.table.cols.length === u.table.cols.length) u.table.v = s.table.v.map((r) => r.slice());
    SRC[i] = u; n++;
  });
  if (!n) return;
  reindexModules(); reindexSources(); sendAll(); update();
  logLine(`${(MTYPES[id] || TYPES[id]).name}: ${n} in the rig now run${n === 1 ? 's' : ''} the new version`);
}
// Called when a describe arrives: upgrades waiting for the engine's new description.
function runUpgrades() {
  const ready = plugins.upgrades.filter((u) => plugins.descSeq > u.seq); plugins.upgrades = plugins.upgrades.filter((u) => plugins.descSeq <= u.seq);
  ready.forEach((u) => upgradeInstances(u.id));
}
// Hand one plugin file to the engine; resolves with the engine's answer { status, note }.
async function loadPlugin(name, bytes) {
  if (!rig.node) return { status: -1, note: 'the engine isn’t running' };
  const isClap = /\.clap$/i.test(name);
  let parts = null;
  if (!isClap) { try { parts = await readRigfx(bytes); } catch (e) { return { status: -1, note: e.message }; } }
  const answer = new Promise((resolve) => plugins.waiting.set(name, resolve)), seq = plugins.descSeq;
  if (HOST) hostSend({ type: 'plugin', ref: name, bytes: bytes.byteLength }, bytes);
  else if (isClap) { plugins.waiting.delete(name); return { status: -1, note: 'a native CLAP plugin: the rig-host runs those (the browser runs .rigfx bundles)' }; }
  else if (!parts.wclap) { plugins.waiting.delete(name); return { status: -1, note: 'the bundle has no module.wclap for the browser' }; }
  else rig.node.port.postMessage({ type: 'plugin', file: name, bytes: parts.wclap.buffer.slice(0) });
  const r = await Promise.race([answer, new Promise((res) => setTimeout(() => res({ status: -1, note: 'no answer from the engine' }), 15000))]);
  plugins.waiting.delete(name);
  const e = (library.entries.plugins || []).find((x) => x.name === name), key = e ? e.key : null;
  r.key = key; plugins.results[name] = r;
  const id = (/^(?:effect|source) (\S+) /.exec(r.note || '') || [])[1];
  if (r.status >= 0 && id) {
    plugins.loaded[name] = { key, id };
    if (parts && parts.screen) installPluginScreen(id, parts.screen);       // a newer version's face replaces the old
    if (/\) replaced$/.test(r.note)) { plugins.upgrades.push({ id, seq }); runUpgrades(); }
  }
  return r;
}
// The engine's answer to a plugin (worklet or host): { type:'plugin', file, status, note }
function onPluginMsg(d) {
  plugins.results[d.file] = d;
  logLine(d.status >= 0 ? `plugin ${d.file}: ${d.note}` : `plugin ${d.file}: not loaded: ${d.note}`, d.status < 0);
  const w = plugins.waiting.get(d.file); if (w) w(d);
  renderPluginList();
}
// A bundle's face screen: its screen(...) call registers like ui/screens/<type>.js.
function installPluginScreen(id, src) {
  try { new Function('screen', 'NOTES', 'h', 'clamp', src)(screen, NOTES, undefined, clamp); plugins.screens.add(id); }
  catch (e) { logLine(`plugin ${id}: its screen didn’t load: ${e.message}`, true); }
}
// Every plugin in the library, before the rig is sent to the engine (Start, and rig-host reconnects).
// A fresh engine (or a fresh page on the rig-host's) has none of them from this page: all are loaded; the
// rig-host's engine keeps a file it already has ("already loaded").
async function loadLibraryPlugins() {
  plugins.loaded = {};
  await syncPlugins();
}

// The add panels' "Load … file" button (49-picker.js): put the file in the library, load it into the
// engine (as importing does), and find the type it added. { type, kind: 'effect' | 'source', name } or
// { note } saying what went wrong.
async function pluginFromFile(f) {
  if (!/\.(rigfx|clap)$/i.test(f.name)) return { note: `${f.name} isn’t an effect or source file (.rigfx, or .clap for the rig-host)` };
  const [r] = await importFiles([f], null);
  if (!r || r.status === 'skipped') return { note: `${f.name}: ${r ? r.reason : 'couldn’t be read'}` };
  let res = plugins.results[r.name];
  if (!res || res.status < 0) {                                  // already in the library but not loaded (or loaded before the engine was)
    if (!rig.node) return { note: `${r.name} is in the library; it loads when you press Start` };
    res = await loadPlugin(r.name, new Uint8Array(await (await library.core.file('plugins', r.name)).arrayBuffer()));
  }
  if (!res || res.status < 0) return { note: `${r.name}: not loaded: ${res ? res.note : 'no answer from the engine'}` };
  for (let i = 0; i < 40; i++) {                                 // its type arrives with the engine's next description
    const fx = Object.keys(MTYPES).find((t) => MTYPES[t].plugin && MTYPES[t].plugin.file === r.name);
    if (fx) return { type: fx, kind: 'effect', name: MTYPES[fx].name };
    const sr = Object.keys(TYPES).find((t) => TYPES[t].plugin && TYPES[t].plugin.file === r.name);
    if (sr) return { type: sr, kind: 'source', name: TYPES[sr].name };
    await new Promise((ok) => setTimeout(ok, 50));
  }
  return { note: `${r.name} loaded, but the engine didn’t list it` };
}
rig.pluginFromFile = pluginFromFile;

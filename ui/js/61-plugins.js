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
const plugins = { known: {}, results: {}, waiting: new Map(), screens: new Set() };
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
// Types the page knows: from a describe() (the engine has them) or a manifest (it will at Start).
function mergeTypes(desc) {
  let added = 0;
  for (const t of desc.moduleTypes || []) if (!MTYPES[t.type]) { DESC.moduleTypes.push(t); MTYPE_ORDER.push(t.type); MTYPES[t.type] = { ...t, index: MTYPE_ORDER.length - 1 }; added++; }
  for (const t of desc.sourceTypes || []) if (!TYPES[t.type]) { DESC.sourceTypes.push(t); TYPE_ORDER.push(t.type); TYPES[t.type] = { ...t, index: TYPE_ORDER.length - 1 }; added++; }
  return added;
}
// Library plugins' manifests -> known types (called whenever the library is read).
async function knowLibraryPlugins() {
  for (const e of library.entries.plugins || []) {
    const d = e.meta && e.meta.description; if (!d || plugins.known[e.name]) continue;
    plugins.known[e.name] = d.id;
    mergeTypes(d.kind === 'source' ? { sourceTypes: [pluginTypeEntry(d, e.name)] } : { moduleTypes: [pluginTypeEntry(d, e.name)] });
  }
}
// Hand one plugin file to the engine; resolves with the engine's answer { status, note }.
async function loadPlugin(name, bytes) {
  if (!rig.node) return { status: -1, note: 'the engine isn’t running' };
  const isClap = /\.clap$/i.test(name);
  let parts = null;
  if (!isClap) { try { parts = await readRigfx(bytes); } catch (e) { return { status: -1, note: e.message }; } }
  const answer = new Promise((resolve) => plugins.waiting.set(name, resolve));
  if (HOST) hostSend({ type: 'plugin', ref: name, bytes: bytes.byteLength }, bytes);
  else if (isClap) { plugins.waiting.delete(name); return { status: -1, note: 'a native CLAP plugin: the rig-host runs those (the browser runs .rigfx bundles)' }; }
  else if (!parts.wclap) { plugins.waiting.delete(name); return { status: -1, note: 'the bundle has no module.wclap for the browser' }; }
  else rig.node.port.postMessage({ type: 'plugin', file: name, bytes: parts.wclap.buffer.slice(0) });
  const r = await Promise.race([answer, new Promise((res) => setTimeout(() => res({ status: -1, note: 'no answer from the engine' }), 15000))]);
  plugins.waiting.delete(name);
  if (r.status >= 0 && parts && parts.screen && !plugins.screens.has(parts.manifest.id)) installPluginScreen(parts.manifest.id, parts.screen);
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
async function loadLibraryPlugins() {
  const list = library.entries.plugins || [];
  for (const e of list) {
    if (!HOST && /\.clap$/i.test(e.name)) continue;            // native: rig-host only
    try {
      const bytes = new Uint8Array(await (await library.core.file('plugins', e.name)).arrayBuffer());
      await loadPlugin(e.name, bytes);
    } catch (err) { logLine(`plugin ${e.name}: ${err.message}`, true); }
  }
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

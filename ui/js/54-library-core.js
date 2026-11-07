// 54-library-core.js - the library over any root directory handle (see 52-vfs.js).
// Layout:  <root>/ir/*.wav   <root>/nam/*.nam   <root>/presets/*.rig.json   <root>/skins/*.rigskin   (one folder per kind)
// No DOM here, so it is unit-tested in Node against the in-memory backend.
class LibraryCore {
  constructor(root, kinds) { this.root = root; this.kinds = kinds; this.meta = new Map(); this.hashes = new Map(); }
  dir(kind) { return this.root.getDirectoryHandle(kind, { create: true }); }
  async ensure() { for (const k in this.kinds) await this.dir(k); }
  async readMeta(kind, file) {
    if (kind === 'ir') return parseWavInfo(await file.arrayBuffer());
    if (kind === 'nam') return parseNamMeta(await file.text());
    if (kind === 'skins') { const r = await readSkinPacket(new Uint8Array(await file.arrayBuffer())); return r.error ? null : { name: r.skin.name || '', author: r.skin.author || '', description: r.skin.description || '' }; }
    if (kind === 'plugins') return pluginMeta(file.name, new Uint8Array(await file.arrayBuffer()));
    if (kind === 'presets') { try { const j = JSON.parse(await file.text()); return j && j.format === 'rig-preset' ? { name: j.name || '', version: j.version } : null; } catch (e) { return null; } }
    return null;
  }
  // Files of one kind with their details, sorted by name. Details are cached per name+size+date.
  async list(kind) {
    const d = await this.dir(kind), out = [];
    for await (const h of d.values()) {
      if (h.kind !== 'file' || kindForName(h.name, this.kinds) !== kind) continue;
      const f = await h.getFile(), key = `${kind}/${h.name}:${f.size}:${f.lastModified}`;
      if (!this.meta.has(key)) this.meta.set(key, await this.readMeta(kind, f));
      out.push({ kind, name: h.name, size: f.size, mtime: f.lastModified, key, meta: this.meta.get(key) });
    }
    const base = (n) => n.slice(0, n.length - extOf(n).length);     // "x.wav" before "x (2).wav"
    return out.sort((a, b) => base(a.name).localeCompare(base(b.name), undefined, { numeric: true, sensitivity: 'base' }));
  }
  async file(kind, name) { return (await (await this.dir(kind)).getFileHandle(name)).getFile(); }
  async write(kind, name, data) {
    const h = await (await this.dir(kind)).getFileHandle(name, { create: true });
    const w = await h.createWritable(); await w.write(data); await w.close();
  }
  async remove(kind, name) { await (await this.dir(kind)).removeEntry(name); }
  async hash(kind, name) {
    const f = await this.file(kind, name), key = `${kind}/${name}:${f.size}:${f.lastModified}`;
    if (!this.hashes.has(key)) this.hashes.set(key, fnv1a(new Uint8Array(await f.arrayBuffer())));
    return this.hashes.get(key);
  }
  // Copy one file into the library. Returns { status: added | renamed | duplicate | skipped, kind, name, reason }.
  async importFile(file, wantKind) {
    const kind = kindForName(file.name, this.kinds);
    if (!kind) return { status: 'skipped', name: file.name, reason: 'not a file type the rig uses' };
    if (wantKind && kind !== wantKind) return { status: 'skipped', name: file.name, reason: `this slot takes ${this.kinds[wantKind].join(', ')} files` };
    const bytes = new Uint8Array(await file.arrayBuffer());
    let skinErr = '', pluginErr = '';
    const check = kind === 'ir' ? parseWavInfo(bytes.buffer) : kind === 'nam' ? parseNamMeta(new TextDecoder().decode(bytes))
      : kind === 'skins' ? await readSkinPacket(bytes).then((r) => { skinErr = r.error || ''; return !r.error; })
      : kind === 'plugins' ? await pluginMeta(file.name, bytes).then((m) => { pluginErr = m && m.error ? m.error : ''; return !!m && !m.error; })
      : (() => { try { return JSON.parse(new TextDecoder().decode(bytes)).format === 'rig-preset'; } catch (e) { return false; } })();
    if (!check) return { status: 'skipped', name: file.name, reason: kind === 'plugins' ? `not a usable plugin: ${pluginErr}` : kind === 'skins' ? `not a usable skin: ${skinErr}` : `couldn't read it as a ${kind === 'ir' ? 'WAV' : kind === 'nam' ? 'NAM capture' : 'RIG preset'}` };
    const h = fnv1a(bytes), existing = await this.list(kind);
    for (const e of existing) if (e.size === bytes.length && (await this.hash(kind, e.name)) === h) return { status: 'duplicate', kind, name: e.name };
    const name = uniqueName(new Set(existing.map((e) => e.name)), safeFileName(file.name));
    await this.write(kind, name, bytes);
    return { status: name === file.name ? 'added' : 'renamed', kind, name };
  }
  // library.json beside the folders: tags, favorites and notes the player gave files and effect /
  // source types (the files themselves are never changed). Missing or unreadable: an empty index.
  async readIndex() {
    const empty = { format: 'rig-library', version: 1, files: {}, types: {} };
    try {
      const f = await (await this.root.getFileHandle('library.json')).getFile(), j = JSON.parse(await f.text());
      return j && j.format === 'rig-library' ? { ...empty, ...j, files: j.files || {}, types: j.types || {} } : empty;
    } catch (e) { return empty; }
  }
  async writeIndex(ix) {
    const h = await this.root.getFileHandle('library.json', { create: true });
    const w = await h.createWritable(); await w.write(JSON.stringify(ix, null, 1)); await w.close();
  }
  // Find a file a preset refers to: by name first, then by content hash (survives renames).
  async findRef(ref) {
    const list = await this.list(ref.kind);
    const byName = list.find((e) => e.name === ref.name);
    if (byName && (!ref.hash || (await this.hash(ref.kind, byName.name)) === ref.hash)) return byName;
    if (ref.hash) for (const e of list) if ((!ref.size || e.size === ref.size) && (await this.hash(ref.kind, e.name)) === ref.hash) return e;
    return byName || null;
  }
}

// A plugin file's details: a .rigfx bundle's manifest (with the plugin's full description), or for a
// plain native .clap just that it is one (the rig-host reads the rest when it loads it).
async function pluginMeta(name, bytes) {
  if (/\.clap$/i.test(name)) {
    const pe = bytes[0] === 0x4d && bytes[1] === 0x5a, elf = bytes[0] === 0x7f && bytes[1] === 0x45 && bytes[2] === 0x4c && bytes[3] === 0x46;
    return pe || elf ? { name: name.replace(/\.clap$/i, ''), native: pe ? 'windows' : 'linux', kind: 'effect', tags: [] } : { error: 'not a CLAP plugin (neither a Windows nor a Linux library)' };
  }
  try {
    const files = await unzip(bytes), m = files.get('manifest.json');
    if (!m) return { error: 'no manifest.json inside' };
    const j = JSON.parse(new TextDecoder().decode(m));
    if (j.format !== 'rigfx' || !j.description || !j.description.id) return { error: 'not a RIG plugin bundle' };
    return { name: j.name, id: j.id, kind: j.kind, category: j.category, tags: j.tags || [], author: j.author || '', version: j.pluginVersion || '', sub: j.sub || '',
             browser: files.has('module.wclap'), description: j.description };
  } catch (e) { return { error: e.message }; }
}

// ---------------------------------------------------------------- tags (library.json, 62-tags.js)
// Tags a file or type gets without anyone typing them, from what it is: a NAM capture's gear, tone and
// architecture; an IR's length (a cab, a room or a reverb), channels and the gear named in its file
// name; a plugin's own tags and category. Lower case, so "Crunch" and "crunch" are one tag.
const IR_GEAR_WORDS = ['v30', 'greenback', 'creamback', 'alnico', 'jensen', 'celestion', 'eminence', 'sm57', 'sm7b', 'md421', 'r121', 'r-121', 'u87', 'e906', 'm160',
  '1x12', '2x12', '4x12', '1x10', '2x10', '4x10', '1x15', 'open', 'closed', 'mesa', 'marshall', 'fender', 'vox', 'orange', 'bogner', 'friedman', 'ribbon', 'condenser', 'dynamic'];
function autoTags(kind, e) {
  const m = (e && e.meta) || {}, t = new Set(), add = (x) => { if (x) String(x).toLowerCase().split(/[,/]/).map((w) => w.trim().replace(/_/g, ' ')).filter(Boolean).forEach((w) => t.add(w)); };
  if (kind === 'nam') {
    add(m.gearType); add(m.toneType); add(m.architecture && m.architecture.toLowerCase());
    if (m.gear) m.gear.split(/\s+/).slice(0, 1).forEach(add);                 // the maker
    if (m.modeledBy) add('by ' + m.modeledBy);
  } else if (kind === 'ir') {
    if (m.ms) add(m.ms <= 150 ? 'cab' : m.ms <= 800 ? 'room' : 'reverb');
    if (m.channels) add(m.channels > 1 ? 'stereo' : 'mono');
    const words = String(e.name || '').toLowerCase().split(/[^a-z0-9-]+/);
    for (const w of IR_GEAR_WORDS) if (words.includes(w)) add(w);
  } else if (kind === 'plugins') {
    (m.tags || []).forEach(add); add(m.category); add(m.kind); add(m.native ? 'rig-host only' : 'plugin');
  } else if (kind === 'presets' && m.name) {
    /* presets: only what the player tags */
  }
  return [...t];
}
// A file's or type's tags: the automatic ones plus the player's, without repeats.
function allTags(auto, own) { const s = new Set(auto); (own || []).forEach((x) => s.add(String(x).toLowerCase().trim())); s.delete(''); return [...s]; }
// "fuzz, Clean ,  hi gain" -> ['fuzz', 'clean', 'hi gain']
function parseTagText(text) { const s = new Set(); String(text || '').split(',').map((x) => x.trim().toLowerCase().replace(/\s+/g, ' ')).filter((x) => x && x.length <= 32).forEach((x) => s.add(x)); return [...s].slice(0, 24); }
// Counted tags over a list ([{ tags }]), most used first, for the filter chips.
function tagCounts(items) { const c = new Map(); items.forEach((i) => (i.tags || []).forEach((t) => c.set(t, (c.get(t) || 0) + 1))); return [...c.entries()].sort((a, b) => b[1] - a[1] || a[0].localeCompare(b[0])); }

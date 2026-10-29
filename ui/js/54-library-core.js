// 54-library-core.js - the library over any root directory handle (see 52-vfs.js).
// Layout:  <root>/ir/*.wav   <root>/nam/*.nam   <root>/presets/*.rig.json   (one folder per kind)
// No DOM here, so it is unit-tested in Node against the in-memory backend.
class LibraryCore {
  constructor(root, kinds) { this.root = root; this.kinds = kinds; this.meta = new Map(); this.hashes = new Map(); }
  dir(kind) { return this.root.getDirectoryHandle(kind, { create: true }); }
  async ensure() { for (const k in this.kinds) await this.dir(k); }
  async readMeta(kind, file) {
    if (kind === 'ir') return parseWavInfo(await file.arrayBuffer());
    if (kind === 'nam') return parseNamMeta(await file.text());
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
    const check = kind === 'ir' ? parseWavInfo(bytes.buffer) : kind === 'nam' ? parseNamMeta(new TextDecoder().decode(bytes))
      : (() => { try { return JSON.parse(new TextDecoder().decode(bytes)).format === 'rig-preset'; } catch (e) { return false; } })();
    if (!check) return { status: 'skipped', name: file.name, reason: `couldn't read it as a ${kind === 'ir' ? 'WAV' : kind === 'nam' ? 'NAM capture' : 'RIG preset'}` };
    const h = fnv1a(bytes), existing = await this.list(kind);
    for (const e of existing) if (e.size === bytes.length && (await this.hash(kind, e.name)) === h) return { status: 'duplicate', kind, name: e.name };
    const name = uniqueName(new Set(existing.map((e) => e.name)), safeFileName(file.name));
    await this.write(kind, name, bytes);
    return { status: name === file.name ? 'added' : 'renamed', kind, name };
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

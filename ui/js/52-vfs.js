// 52-vfs.js - storage backends for the library, all speaking the same small subset of the
// browser's File System Access API (directory and file handles):
//   dir.getDirectoryHandle(name, {create}) · dir.getFileHandle(name, {create}) · dir.values() · dir.removeEntry(name)
//   file.getFile() · file.createWritable() -> { write(data), close() }
// 1. A real folder the user picks (showDirectoryPicker): the files are ordinary files on disk.
// 2. IndexedDB, when no folder is chosen (works on pages opened straight from disk).
// 3. Memory, when the host blocks both (e.g. an in-app preview); lost on reload.
// 4. The rig-host's folder (Documents/RIG01), over HTTP to the program that served the page (hosts/rig-host/library.h).
const VFS_DB = 'rig01-library';

function idbOpen() {
  return new Promise((res, rej) => {
    const q = indexedDB.open(VFS_DB, 1);
    q.onupgradeneeded = () => { q.result.createObjectStore('files'); q.result.createObjectStore('handles'); };
    q.onsuccess = () => res(q.result); q.onerror = () => rej(q.error);
  });
}
function idbReq(db, store, mode, fn) {
  return new Promise((res, rej) => { const tx = db.transaction(store, mode), r = fn(tx.objectStore(store)); tx.oncomplete = () => res(r && r.result); tx.onerror = () => rej(tx.error); });
}
const notFound = (name) => new DOMException(`${name} not found`, 'NotFoundError');

// ---------------------------------------------------------------- key/value backed handles
// `store` = { get(key), put(key, rec), del(key), keys() } with rec = { blob, lastModified }
class KvFile {
  constructor(store, key, name) { this.kind = 'file'; this.store = store; this.key = key; this.name = name; }
  async getFile() { const r = await this.store.get(this.key); if (!r) throw notFound(this.name); return new File([r.blob], this.name, { lastModified: r.lastModified }); }
  async createWritable() {
    const parts = [], store = this.store, key = this.key;
    return { write: async (d) => { parts.push(d instanceof Blob ? d : new Blob([d])); }, close: async () => { await store.put(key, { blob: new Blob(parts), lastModified: Date.now() }); } };
  }
}
class KvDir {
  constructor(store, path, name) { this.kind = 'directory'; this.store = store; this.path = path; this.name = name; }
  async getDirectoryHandle(name) { return new KvDir(this.store, this.path + name + '/', name); }
  async getFileHandle(name, opts = {}) {
    const key = this.path + name;
    if (!(await this.store.get(key))) { if (!opts.create) throw notFound(name); await this.store.put(key, { blob: new Blob([]), lastModified: Date.now() }); }
    return new KvFile(this.store, key, name);
  }
  async removeEntry(name) { await this.store.del(this.path + name); }
  async *values() {
    const seen = new Set();
    for (const k of await this.store.keys()) {
      if (!k.startsWith(this.path)) continue;
      const rest = k.slice(this.path.length), slash = rest.indexOf('/');
      if (slash < 0) yield new KvFile(this.store, k, rest);
      else { const d = rest.slice(0, slash); if (!seen.has(d)) { seen.add(d); yield new KvDir(this.store, this.path + d + '/', d); } }
    }
  }
}
function memStore() {
  const m = new Map();
  return { get: async (k) => m.get(k), put: async (k, v) => { m.set(k, v); }, del: async (k) => { m.delete(k); }, keys: async () => [...m.keys()] };
}
function idbStore(db) {
  return {
    get: (k) => idbReq(db, 'files', 'readonly', (s) => s.get(k)),
    put: (k, v) => idbReq(db, 'files', 'readwrite', (s) => s.put(v, k)),
    del: (k) => idbReq(db, 'files', 'readwrite', (s) => s.delete(k)),
    keys: () => idbReq(db, 'files', 'readonly', (s) => s.getAllKeys()),
  };
}

// ---------------------------------------------------------------- the rig-host's library folder
// GET /lib/list -> {root, files:[{k, s, m}]}; GET|PUT|DELETE /lib/f/<key>; POST /lib/open. Keys as above ("nam/x.nam").
// A file's bytes are fetched only when read, so listing a big library costs one small request.
async function hostLibReq(method, path, body) {
  const r = await fetch('/lib/' + path, { method, body, headers: { 'X-Rig': '1' }, cache: 'no-store' });
  if (r.status === 404) throw notFound(decodeURIComponent(path.replace(/^f\//, '')));
  if (!r.ok) throw new Error(`rig-host library: ${r.status} ${await r.text()}`);
  return r;
}
class HostLib {
  constructor() { this.cache = null; }
  list() { if (!this.cache) { this.cache = hostLibReq('GET', 'list').then((r) => r.json()); setTimeout(() => { this.cache = null; }, 300); } return this.cache; }
  changed() { this.cache = null; }
  async info(key) { return (await this.list()).files.find((f) => f.k === key) || null; }
}
class HostFile {
  constructor(lib, key, name, info) { this.kind = 'file'; this.lib = lib; this.key = key; this.name = name; this.info = info; }
  async getFile() {
    const info = this.info || await this.lib.info(this.key); if (!info) throw notFound(this.name);
    const key = this.key; let bytes = null;
    const load = async () => bytes || (bytes = await (await hostLibReq('GET', 'f/' + encodeURIComponent(key))).arrayBuffer());
    return { name: this.name, size: info.s, lastModified: info.m, type: '', arrayBuffer: load, text: async () => new TextDecoder().decode(await load()) };
  }
  async createWritable() {
    const parts = [], lib = this.lib, key = this.key;
    return { write: async (d) => { parts.push(d && d.arrayBuffer && !(d instanceof Blob) ? await d.arrayBuffer() : d); },
      close: async () => { await hostLibReq('PUT', 'f/' + encodeURIComponent(key), new Blob(parts)); lib.changed(); } };
  }
}
class HostDir {
  constructor(lib, path, name) { this.kind = 'directory'; this.lib = lib; this.path = path; this.name = name; }
  async getDirectoryHandle(name) { return new HostDir(this.lib, this.path + name + '/', name); }
  async getFileHandle(name, opts = {}) {
    const key = this.path + name, info = await this.lib.info(key);
    if (!info && !opts.create) throw notFound(name);
    return new HostFile(this.lib, key, name, info);
  }
  async removeEntry(name) { await hostLibReq('DELETE', 'f/' + encodeURIComponent(this.path + name)); this.lib.changed(); }
  async *values() {
    for (const f of (await this.lib.list()).files) {
      if (!f.k.startsWith(this.path)) continue;
      const rest = f.k.slice(this.path.length); if (!rest.includes('/')) yield new HostFile(this.lib, f.k, rest, f);
    }
  }
}
// The root handle, named by the folder's full path; null if the host has no library folder.
async function hostLibraryRoot() {
  const lib = new HostLib(), l = await lib.list();
  return new HostDir(lib, '', l.root);
}

// ---------------------------------------------------------------- remembered folder (File System Access)
async function savedFolderHandle() {
  try { const db = await idbOpen(); return await idbReq(db, 'handles', 'readonly', (s) => s.get('root')); } catch (e) { return null; }
}
async function rememberFolderHandle(h) {
  try { const db = await idbOpen(); await idbReq(db, 'handles', 'readwrite', (s) => s.put(h, 'root')); } catch (e) { /* folder still works this session */ }
}
async function forgetFolderHandle() {
  try { const db = await idbOpen(); await idbReq(db, 'handles', 'readwrite', (s) => s.delete('root')); } catch (e) { /* nothing to forget */ }
}

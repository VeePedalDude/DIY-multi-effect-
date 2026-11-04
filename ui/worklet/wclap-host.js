// ui/worklet/wclap-host.js - runs WebCLAP plugins (a .rigfx bundle's module.wclap: a CLAP plugin
// compiled to WebAssembly, https://github.com/WebCLAP) inside the audio worklet, for the engine.
//
// The engine (rig.wasm) can't call into another WebAssembly module, so it imports five functions
// (engine/core/plugin_js.h) and this file answers them: it copies the block's audio and the knobs that
// moved into the plugin's own memory, calls the plugin's CLAP process() through its function table, and
// copies the result back. Everything a call needs is allocated in the plugin's memory when an instance
// is made, so a block allocates nothing.
//
// CLAP passes the plugin callbacks into the host (the event list's size() and get(), the host's
// get_extension()...). In WebAssembly those must be functions in the plugin's own table, so a tiny
// module is assembled here whose functions call back into this file, and they're added to that table.
// Struct layouts are CLAP's on wasm32 (measured with offsetof in a wasm32 build of clap.h).
// Only RIG plugins (sdk/rigfx.h: they describe themselves through rig01.describe/1) run here; the
// rig-host also runs any native CLAP effect.

const WCLAP_MAXBLOCK = 128, WCLAP_MAXPAR = 8, WCLAP_EV = 48;

// A minimal WASI for plugins: logging to the console, everything else politely refused.
function wclapWasi(getMem) {
  const ENOSYS = 52;
  return new Proxy({
    fd_write(fd, iovs, n, nwritten) {
      const m = getMem(), dv = new DataView(m.buffer); let total = 0, s = '';
      for (let i = 0; i < n; i++) { const p = dv.getUint32(iovs + 8 * i, true), l = dv.getUint32(iovs + 8 * i + 4, true); for (let k = 0; k < l; k++) s += String.fromCharCode(m[p + k]); total += l; }
      dv.setUint32(nwritten, total, true); if (s.trim()) console.log('[plugin] ' + s.trim()); return 0;
    },
    fd_close() { return 0; }, fd_seek() { return 0; }, fd_fdstat_get() { return 0; },
    proc_exit() { throw new Error('plugin exited'); },
    clock_time_get(id, prec, out) { new DataView(getMem().buffer).setBigUint64(out, BigInt(Math.round(Date.now() * 1e6)), true); return 0; },
    random_get(buf, len) { const m = getMem(); for (let i = 0; i < len; i++) m[buf + i] = (Math.random() * 256) | 0; return 0; },
    environ_sizes_get(a, b) { const dv = new DataView(getMem().buffer); dv.setUint32(a, 0, true); dv.setUint32(b, 0, true); return 0; },
    environ_get() { return 0; },
    args_sizes_get(a, b) { const dv = new DataView(getMem().buffer); dv.setUint32(a, 0, true); dv.setUint32(b, 0, true); return 0; },
    args_get() { return 0; },
  }, { get: (t, k) => (k in t ? t[k] : () => ENOSYS) });
}

// A module of wasm functions that call `fns` (JS), so they can go into the plugin's function table.
// sigs: [params, results] per function, all i32.
function wclapTrampolines(fns, sigs) {
  const b = [0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00];
  const vec = (items) => [items.length, ...items.flat()];
  const str = (s) => [s.length, ...[...s].map((c) => c.charCodeAt(0))];
  const section = (id, body) => { b.push(id, ...uleb(body.length), ...body); };
  const uleb = (n) => { const o = []; do { let x = n & 0x7f; n >>>= 7; if (n) x |= 0x80; o.push(x); } while (n); return o; };
  section(1, vec(sigs.map(([p, r]) => [0x60, ...vec(Array(p).fill([0x7f])), ...vec(Array(r).fill([0x7f]))])));            // types
  section(2, vec(sigs.map((_, i) => [...str('h'), ...str('f' + i), 0x00, i])));                                            // imports
  section(3, vec(sigs.map((_, i) => [i])));                                                                                 // functions
  section(7, vec(sigs.map((_, i) => [...str('w' + i), 0x00, sigs.length + i])));                                           // exports
  section(10, vec(sigs.map(([p], i) => { const body = [0x00]; for (let k = 0; k < p; k++) body.push(0x20, k); body.push(0x10, i, 0x0b); return [...uleb(body.length), ...body]; })));
  const mod = new WebAssembly.Module(new Uint8Array(b));
  return new WebAssembly.Instance(mod, { h: Object.fromEntries(fns.map((f, i) => ['f' + i, f])) }).exports;
}

class WclapLib {
  constructor(bytes) {
    const mod = new WebAssembly.Module(bytes);
    let mem = null; const getMem = () => new Uint8Array(mem.buffer);
    this.x = new WebAssembly.Instance(mod, { wasi_snapshot_preview1: wclapWasi(getMem) }).exports;
    mem = this.x.memory; this.mem = mem;
    if (this.x._initialize) this.x._initialize();
    if (!this.x.clap_entry || !this.x.__indirect_function_table || !this.x.malloc) throw new Error('not a WebCLAP plugin (no clap_entry, function table or malloc)');
    this.table = this.x.__indirect_function_table;
    this.insts = [null];                                 // instance numbers start at 1
    // Host callbacks into the table: get_extension(host, id), request_*(host), events size(list), get(list, i), try_push(list, ev)
    const T = wclapTrampolines([
      () => 0, () => {}, () => {}, () => {},
      (list) => { const s = this.byList.get(list); return s ? s.nev : 0; },
      (list, i) => { const s = this.byList.get(list); return s ? s.ev + i * WCLAP_EV : 0; },
      () => 1,
    ], [[2, 1], [1, 0], [1, 0], [1, 0], [1, 1], [2, 1], [2, 1]]);
    const base = this.table.grow(7);
    ['w0', 'w1', 'w2', 'w3', 'w4', 'w5', 'w6'].forEach((k, i) => this.table.set(base + i, T[k]));
    this.fn = { getExt: base, restart: base + 1, process: base + 2, callback: base + 3, size: base + 4, get: base + 5, push: base + 6 };
    this.byList = new Map();
    const e = this.x.clap_entry.value;                 // clap_entry: the address of the entry struct
    this.call(this.u32(e + 12), this.cstr('/'));       // init(path)
    this.factory = this.call(this.u32(e + 20), this.cstr('clap.plugin-factory'));
    if (!this.factory) throw new Error('the plugin has no plugin factory');
    if (this.call(this.u32(this.factory), this.factory) < 1) throw new Error('the file holds no plugin');
    const d = this.call(this.u32(this.factory + 4), this.factory, 0);
    this.idPtr = this.u32(d + 12);
    // the host struct, in the plugin's memory
    this.host = this.alloc(48);
    const name = this.cstr('RIG/01'), empty = this.cstr('');
    const dv = this.dv();
    dv.setUint32(this.host, 1, true); dv.setUint32(this.host + 4, 2, true); dv.setUint32(this.host + 8, 0, true);
    dv.setUint32(this.host + 12, 0, true); dv.setUint32(this.host + 16, name, true); dv.setUint32(this.host + 20, name, true);
    dv.setUint32(this.host + 24, empty, true); dv.setUint32(this.host + 28, empty, true);
    dv.setUint32(this.host + 32, this.fn.getExt, true); dv.setUint32(this.host + 36, this.fn.restart, true);
    dv.setUint32(this.host + 40, this.fn.process, true); dv.setUint32(this.host + 44, this.fn.callback, true);
    this.extRig = this.cstr('rig01.describe/1');
    // what it is: a probe instance's description
    const p = this.newPlugin();
    const rig = this.call(this.u32(p + 40), p, this.extRig);
    if (!rig) { this.call(this.u32(p + 12), p); throw new Error('a WebCLAP without RIG\'s description: the browser runs RIG plugins only (the rig-host runs any CLAP effect)'); }
    this.json = this.readStr(this.call(this.u32(rig), p));
    this.call(this.u32(p + 12), p);                   // destroy
  }
  dv() { return new DataView(this.mem.buffer); }
  u32(a) { return this.dv().getUint32(a, true); }
  call(idx, ...args) { return this.table.get(idx)(...args); }
  alloc(n) { const p = this.x.malloc(n); if (!p) throw new Error('the plugin is out of memory'); new Uint8Array(this.mem.buffer, p, n).fill(0); return p; }
  cstr(s) { const p = this.alloc(s.length + 1); const m = new Uint8Array(this.mem.buffer); for (let i = 0; i < s.length; i++) m[p + i] = s.charCodeAt(i) & 0x7f; m[p + s.length] = 0; return p; }
  readStr(p) {                                         // UTF-8 (worklets have no TextDecoder)
    const u = new Uint8Array(this.mem.buffer); let s = '', i = p;
    while (u[i]) {
      const c = u[i++];
      if (c < 0x80) s += String.fromCharCode(c);
      else if (c < 0xe0) s += String.fromCharCode(((c & 31) << 6) | (u[i++] & 63));
      else if (c < 0xf0) { const c2 = u[i++], c3 = u[i++]; s += String.fromCharCode(((c & 15) << 12) | ((c2 & 63) << 6) | (c3 & 63)); }
      else { const c2 = u[i++], c3 = u[i++], c4 = u[i++]; s += String.fromCodePoint(((c & 7) << 18) | ((c2 & 63) << 12) | ((c3 & 63) << 6) | (c4 & 63)); }
    }
    return s;
  }
  newPlugin() {
    const p = this.call(this.u32(this.factory + 8), this.factory, this.host, this.idPtr);
    if (!p || !this.call(this.u32(p + 8), p)) throw new Error('the plugin wouldn\'t start');
    return p;
  }
  // An activated instance with everything a block needs, laid out in the plugin's memory.
  create(sr) {
    const p = this.newPlugin();
    if (!this.call(this.u32(p + 16), p, sr, 1, WCLAP_MAXBLOCK) || !this.call(this.u32(p + 24), p)) { this.call(this.u32(p + 12), p); return 0; }
    const B = WCLAP_MAXBLOCK * 4;
    const s = { p, last: new Float32Array(WCLAP_MAXPAR).fill(-1), nev: 0 };
    s.bufs = this.alloc(4 * B);                          // inL inR outL outR
    s.ptrs = this.alloc(16);                             // float* in[2], out[2]
    s.ain = this.alloc(24); s.aout = this.alloc(24);     // clap_audio_buffer_t
    s.tr = this.alloc(104);                              // clap_event_transport_t
    s.ev = this.alloc(WCLAP_MAXPAR * WCLAP_EV);          // clap_event_param_value_t[]
    s.inEv = this.alloc(12); s.outEv = this.alloc(8);    // clap_input_events_t, clap_output_events_t
    s.proc = this.alloc(40);                             // clap_process_t
    s.met = this.alloc(16);
    const dv = this.dv();
    for (let c = 0; c < 4; c++) dv.setUint32(s.ptrs + 4 * c, s.bufs + c * B, true);
    dv.setUint32(s.ain, s.ptrs, true); dv.setUint32(s.ain + 8, 2, true);
    dv.setUint32(s.aout, s.ptrs + 8, true); dv.setUint32(s.aout + 8, 2, true);
    dv.setUint32(s.tr, 104, true); dv.setUint16(s.tr + 8, 0, true); dv.setUint16(s.tr + 10, 9, true);    // CLAP_EVENT_TRANSPORT
    dv.setUint32(s.tr + 16, 1, true);                    // CLAP_TRANSPORT_HAS_TEMPO
    dv.setUint32(s.inEv, s.inEv, true); dv.setUint32(s.inEv + 4, this.fn.size, true); dv.setUint32(s.inEv + 8, this.fn.get, true);
    dv.setUint32(s.outEv, s.outEv, true); dv.setUint32(s.outEv + 4, this.fn.push, true);
    dv.setBigInt64(s.proc, -1n, true); dv.setUint32(s.proc + 12, s.tr, true);
    dv.setUint32(s.proc + 16, s.ain, true); dv.setUint32(s.proc + 20, s.aout, true);
    dv.setUint32(s.proc + 24, 1, true); dv.setUint32(s.proc + 28, 1, true);
    dv.setUint32(s.proc + 32, s.inEv, true); dv.setUint32(s.proc + 36, s.outEv, true);
    s.rig = this.call(this.u32(p + 40), p, this.extRig);
    s.fProcess = this.u32(p + 36);
    s.fOut = s.rig ? this.u32(s.rig + 8) : 0; s.fMet = s.rig ? this.u32(s.rig + 4) : 0;
    this.byList.set(s.inEv, s);
    this.insts.push(s);
    return this.insts.length - 1;
  }
  destroy(h) {
    const s = this.insts[h]; if (!s) return;
    this.call(this.u32(s.p + 28), s.p); this.call(this.u32(s.p + 20), s.p); this.call(this.u32(s.p + 12), s.p);   // stop, deactivate, destroy
    this.byList.delete(s.inEv); this.insts[h] = null;
    for (const a of [s.bufs, s.ptrs, s.ain, s.aout, s.tr, s.ev, s.inEv, s.outEv, s.proc, s.met]) this.x.free && this.x.free(a);
  }
  // rigF32: a Float32Array over the engine's memory (rig.wasm); pointers are byte offsets in it.
  process(h, rigF32, inL, inR, oL, oR, n, norm, np, bpm) {
    const s = this.insts[h]; if (!s) return;
    const dv = this.dv(), f = new Float32Array(this.mem.buffer), B = WCLAP_MAXBLOCK, b0 = s.bufs >> 2;
    let nev = 0;
    for (let k = 0; k < np && k < WCLAP_MAXPAR; k++) {
      const v = rigF32[(norm >> 2) + k]; if (v === s.last[k]) continue;
      s.last[k] = v; const e = s.ev + nev * WCLAP_EV; nev++;
      dv.setUint32(e, WCLAP_EV, true); dv.setUint32(e + 4, 0, true); dv.setUint16(e + 8, 0, true); dv.setUint16(e + 10, 5, true); dv.setUint32(e + 12, 0, true);   // CLAP_EVENT_PARAM_VALUE
      dv.setUint32(e + 16, k, true); dv.setUint32(e + 20, 0, true); dv.setInt32(e + 24, -1, true); dv.setInt16(e + 28, -1, true); dv.setInt16(e + 30, -1, true); dv.setInt16(e + 32, -1, true);
      dv.setFloat64(e + 40, v, true);
    }
    s.nev = nev;
    f.set(rigF32.subarray(inL >> 2, (inL >> 2) + n), b0); f.set(rigF32.subarray(inR >> 2, (inR >> 2) + n), b0 + B);
    dv.setFloat64(s.tr + 40, bpm, true); dv.setUint32(s.proc + 8, n, true);
    this.table.get(s.fProcess)(s.p, s.proc);
    const g = new Float32Array(this.mem.buffer);           // (memory may have grown)
    rigF32.set(g.subarray(b0 + 2 * B, b0 + 2 * B + n), oL >> 2); rigF32.set(g.subarray(b0 + 3 * B, b0 + 3 * B + n), oR >> 2);
  }
  output(h) { const s = this.insts[h]; return s && s.fOut ? this.table.get(s.fOut)(s.p) : 0; }
  meters(h, rigF32, out, max) {
    const s = this.insts[h]; if (!s || !s.fMet) return 0;
    const n = this.table.get(s.fMet)(s.p, s.met, Math.min(max, 4));
    const g = new Float32Array(this.mem.buffer); for (let i = 0; i < n; i++) rigF32[(out >> 2) + i] = g[(s.met >> 2) + i];
    return n;
  }
}

// The engine's imports (engine/core/plugin_js.h), over a list of loaded plugins. getF32() gives a
// Float32Array over the engine's current memory.
function wclapImports(libs, getF32) {
  return {
    plugin_create: (lib, sr) => { try { return libs[lib] ? libs[lib].create(sr) : 0; } catch (e) { console.log('[plugin] ' + e.message); return 0; } },
    plugin_destroy: (lib, h) => { if (libs[lib]) libs[lib].destroy(h); },
    plugin_process: (lib, h, inL, inR, oL, oR, n, norm, np, bpm) => { if (libs[lib]) libs[lib].process(h, getF32(), inL, inR, oL, oR, n, norm, np, bpm); },
    plugin_output: (lib, h) => (libs[lib] ? libs[lib].output(h) : 0),
    plugin_meters: (lib, h, out, max) => (libs[lib] ? libs[lib].meters(h, getF32(), out, max) : 0),
  };
}

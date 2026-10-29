// ui/worklet/engine-worklet.js - runs rig.wasm inside an AudioWorklet.
// The protocol is built from the engine's own description (rig_describe), so new
// modules and sources need no changes here. Same text commands as the future Linux host:
//
//   param_set <inst> <symbol> <0..1>          bypass <inst> <0|1>
//   mod_connect <inst> <symbol> <source> <depth>
//   mod_disconnect <inst> <symbol>            set_bpm <bpm>
//   master <dB>                               ext <source> <value>
//   input_channel <0|1>
// File slots arrive as {type:'asset', inst, data, ref} messages (binary); the Linux host takes
// the text form `asset_load <inst> <kind>/<file name>` and reads the file from its library folder.
// <inst> is the chain index (mod-host style) or the module id ("holo").
// Replies: resp <status>  (0 ok, negatives follow mod-host error numbers)

const ERR = { INSTANCE_INVALID: -1, INVALID_PARAM_SYMBOL: -103, VALUE_OUT_OF_RANGE: -207, INVALID_OPERATION: -902 };

function cString(mem, ptr) {                 // UTF-8 C string from wasm memory (no TextDecoder in worklets)
  const u = new Uint8Array(mem.buffer, ptr); let s = '', i = 0;
  while (u[i]) {
    const c = u[i++];
    if (c < 0x80) s += String.fromCharCode(c);
    else if (c < 0xe0) s += String.fromCharCode(((c & 31) << 6) | (u[i++] & 63));
    else { const c2 = u[i++], c3 = u[i++]; s += String.fromCharCode(((c & 15) << 12) | ((c2 & 63) << 6) | (c3 & 63)); }
  }
  return s;
}

class RigEngine extends AudioWorkletProcessor {
  constructor(options) {
    super();
    const mod = new WebAssembly.Module(options.processorOptions.wasm);
    this.x = new WebAssembly.Instance(mod, {}).exports;
    if (this.x._initialize) this.x._initialize();
    this.x.rig_init(sampleRate);
    this.desc = JSON.parse(cString(this.x.memory, this.x.rig_describe()));
    this.mlen = this.x.rig_meters_len();
    this.inCh = 0;
    this.blocks = 0; this.busyMs = 0; this.wallStart = Date.now();
    this.inPk = 0; this.outPk = 0;
    this.port.onmessage = (e) => this.onMsg(e.data);
    this.port.postMessage({ type: 'describe', desc: this.desc });
  }
  views() {
    const b = this.x.memory.buffer;
    if (this._buf !== b) {
      this._buf = b;
      this.vIn = new Float32Array(b, this.x.rig_in(), 128);
      this.vL = new Float32Array(b, this.x.rig_out_l(), 128);
      this.vR = new Float32Array(b, this.x.rig_out_r(), 128);
      this.vM = new Float32Array(b, this.x.rig_meters(), this.mlen);
    }
  }
  inst(tok) {
    const mods = this.desc.modules, i = Number(tok);
    if (Number.isInteger(i)) return i >= 0 && i < mods.length ? i : -1;
    return mods.findIndex((m) => m.id === tok);
  }
  source(tok) { return this.desc.sources.findIndex((s) => s.id === tok); }
  exec(line) {
    const t = String(line).trim().split(/\s+/), c = t[0], X = this.x;
    if (c === 'param_set' || c === 'mod_connect' || c === 'mod_disconnect') {
      const m = this.inst(t[1]); if (m < 0) return ERR.INSTANCE_INVALID;
      const p = this.desc.modules[m].params.findIndex((q) => q.symbol === t[2]); if (p < 0) return ERR.INVALID_PARAM_SYMBOL;
      if (c === 'param_set') { const v = Number(t[3]); if (!(v >= 0 && v <= 1)) return ERR.VALUE_OUT_OF_RANGE; X.rig_param(m, p, v); return 0; }
      if (c === 'mod_disconnect') { X.rig_mod(m, p, -1, 0); return 0; }
      const s = this.source(t[3]), d = Number(t[4]);
      if (s < 0 || !(d >= 0 && d <= 1)) return ERR.VALUE_OUT_OF_RANGE;
      X.rig_mod(m, p, s, d); return 0;
    }
    if (c === 'bypass') { const m = this.inst(t[1]); if (m < 0) return ERR.INSTANCE_INVALID; X.rig_bypass(m, t[2] === '1' ? 1 : 0); return 0; }
    if (c === 'set_bpm') { const v = Number(t[1]); if (!(v >= 30 && v <= 300)) return ERR.VALUE_OUT_OF_RANGE; X.rig_tempo(v); return 0; }
    if (c === 'master') { const v = Number(t[1]); if (!(v >= -60 && v <= 6)) return ERR.VALUE_OUT_OF_RANGE; X.rig_master(v); return 0; }
    if (c === 'ext') { const s = this.source(t[1]), v = Number(t[2]); if (s < 0 || !Number.isFinite(v)) return ERR.VALUE_OUT_OF_RANGE; X.rig_ext(s, v); return 0; }
    if (c === 'input_channel') { this.inCh = t[1] === '1' ? 1 : 0; return 0; }
    return ERR.INVALID_OPERATION;
  }
  onMsg(d) {
    if (d.type === 'cmd') {
      this.port.postMessage({ type: 'resp', id: d.id, line: d.line, status: this.exec(d.line) });
    } else if (d.type === 'asset') {             // file slot: d.inst, d.data (ArrayBuffer), d.ref (library name, for the log)
      const m = this.inst(d.inst), max = m < 0 ? 0 : this.x.rig_asset_max(m), bytes = d.data ? d.data.byteLength : 0;
      let status = ERR.INSTANCE_INVALID;
      if (m >= 0) {
        if (bytes > max) status = ERR.VALUE_OUT_OF_RANGE;
        else { new Uint8Array(this.x.memory.buffer, this.x.rig_asset_buf(m), bytes).set(new Uint8Array(d.data)); status = this.x.rig_asset_commit(m, bytes); }
      }
      this.port.postMessage({ type: 'asset', inst: m, ref: d.ref, status, bytes, max });
    } else if (d.type === 'asset_clear') {
      const m = this.inst(d.inst); if (m >= 0) this.x.rig_asset_clear(m);
      this.port.postMessage({ type: 'asset', inst: m, ref: null, status: m >= 0 ? 0 : ERR.INSTANCE_INVALID, bytes: 0, max: 0 });
    }
  }
  process(inputs, outputs) {
    const t0 = Date.now();
    this.views();
    const inp = inputs[0], out = outputs[0];
    const src = inp && inp.length ? (inp[this.inCh] || inp[0]) : null;
    const n = out[0].length;
    if (src) { this.vIn.set(src); for (let i = 0; i < n; i++) { const a = Math.abs(src[i]); if (a > this.inPk) this.inPk = a; } }
    else this.vIn.fill(0);
    this.x.rig_process(n);
    out[0].set(this.vL.subarray(0, n));
    if (out[1]) out[1].set(this.vR.subarray(0, n));
    for (let i = 0; i < n; i++) { const a = Math.max(Math.abs(this.vL[i]), Math.abs(this.vR[i])); if (a > this.outPk) this.outPk = a; }
    this.busyMs += Date.now() - t0;          // 1 ms clock, but the average is unbiased over many blocks
    if (++this.blocks % 12 === 0) {           // ~31 Hz meter stream at 48 kHz
      const wall = Date.now() - this.wallStart;
      this.port.postMessage({ type: 'meters', m: Array.from(this.vM), inPk: this.inPk, outPk: this.outPk, load: wall > 0 ? this.busyMs / wall : 0 });
      this.inPk = 0; this.outPk = 0;
      if (wall > 2000) { this.busyMs = 0; this.wallStart = Date.now(); }
    }
    return true;
  }
}
registerProcessor('rig-engine', RigEngine);

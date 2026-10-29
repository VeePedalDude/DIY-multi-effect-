// ui/worklet/engine-worklet.js - runs rig.wasm inside an AudioWorklet.
// The protocol is built from the engine's own description (rig_describe), so new
// modules and sources need no changes here. Same text commands as the future Linux host:
//
//   param_set <inst> <symbol> <0..1>          bypass <inst> <0|1>
//   mod_connect <inst> <symbol> <source> <depth>
//   mod_disconnect <inst> <symbol>            set_bpm <bpm>
//   master <dB>                               ext <source> <value>
//   input_channel <0|1>                       src_set <source> <symbol> <0..1>
//   tuner <0|1> [mute 0|1]                    tuner_ref <A4 Hz, 400..480>
//   src_add <type> [id]                       src_remove <source>
//   src_mod <source> <symbol> <from source> <depth>
//   src_unmod <source> <symbol>
//   fx_add <type> [id] [position]             fx_remove <inst>             fx_move <inst> <position>
// Sources are rack instances, named by id ("lfo", "lfo2"); every change to the rack is answered
// with a {type:'rack'} message listing what's in it and in which slot. Modules are chain instances
// ("holo", "holo2"); every change to the chain is answered with a {type:'chain'} message.
// File slots arrive as {type:'asset', inst, data, ref} messages (binary: a .nam file's text, or an
// IR as RIR1 header + float samples, see engine/core/nam.h) and are answered with the engine's
// status line; the Linux host takes the text form `asset_load <inst> <kind>/<file name>` and reads
// the file from its library folder.
// <inst> is the module id ("holo") or its position in the chain (0 = first).
// Replies: resp <status>  (0 ok, negatives follow mod-host error numbers; src_add: -1 unknown type,
// -2 id taken or not allowed, -3 rack full; fx_add the same, for the chain)

const ERR = { INSTANCE_INVALID: -1, ID_TAKEN: -2, RACK_FULL: -3, INVALID_PARAM_SYMBOL: -103, VALUE_OUT_OF_RANGE: -207, INVALID_OPERATION: -902 };

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
    this.types = Object.fromEntries(this.desc.sourceTypes.map((t) => [t.type, t]));
    this.mtypes = Object.fromEntries(this.desc.moduleTypes.map((t) => [t.type, t]));
    this.readRack(); this.readChain();
    this.mlen = this.x.rig_meters_len();
    this.inCh = 0;
    this.blocks = 0; this.busyMs = 0; this.wallStart = Date.now();
    this.inPk = 0; this.outPk = 0;
    this.port.onmessage = (e) => this.onMsg(e.data);
    this.port.postMessage({ type: 'describe', desc: this.desc }); this.sendRack(); this.sendChain();
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
  readChain() { this.chain = JSON.parse(cString(this.x.memory, this.x.rig_chain())).modules; }
  sendChain() { this.port.postMessage({ type: 'chain', chain: this.chain }); }
  inst(tok) {                                  // engine slot of a module, by id or chain position
    const i = Number(tok);
    if (Number.isInteger(i)) return i >= 0 && i < this.chain.length ? this.chain[i].slot : -1;
    const m = this.chain.find((x) => x.id === tok); return m ? m.slot : -1;
  }
  modParams(slot) { const m = this.chain.find((x) => x.slot === slot); return m ? this.mtypes[m.type].params : []; }
  modId(slot) { const m = this.chain.find((x) => x.slot === slot); return m ? m.id : null; }
  writeText(line) { const buf = new Uint8Array(this.x.memory.buffer, this.x.rig_text(), 64); for (let i = 0; i < 64; i++) buf[i] = i < line.length ? line.charCodeAt(i) & 0x7f : 0; }
  readRack() { this.rack = JSON.parse(cString(this.x.memory, this.x.rig_rack())).sources; }
  sendRack() { this.port.postMessage({ type: 'rack', rack: this.rack }); }
  source(tok) { const r = this.rack.find((s) => s.id === tok); return r ? r.slot : -1; }      // rack slot of an instance id
  sourceKnob(slot, sym) { const r = this.rack.find((s) => s.slot === slot), t = r && this.types[r.type]; return t ? t.params.findIndex((q) => q.symbol === sym) : -1; }
  exec(line) {
    const t = String(line).trim().split(/\s+/), c = t[0], X = this.x;
    if (c === 'param_set' || c === 'mod_connect' || c === 'mod_disconnect') {
      const m = this.inst(t[1]); if (m < 0) return ERR.INSTANCE_INVALID;
      const p = this.modParams(m).findIndex((q) => q.symbol === t[2]); if (p < 0) return ERR.INVALID_PARAM_SYMBOL;
      if (c === 'param_set') { const v = Number(t[3]); if (!(v >= 0 && v <= 1)) return ERR.VALUE_OUT_OF_RANGE; X.rig_param(m, p, v); return 0; }
      if (c === 'mod_disconnect') { X.rig_mod(m, p, -1, 0); return 0; }
      const s = this.source(t[3]), d = Number(t[4]);
      if (s < 0 || !(d >= 0 && d <= 1)) return ERR.VALUE_OUT_OF_RANGE;
      X.rig_mod(m, p, s, d); return 0;
    }
    if (c === 'tuner') {                         // tuner <0|1> [mute 0|1]: strobe tuner on the dry input
      if (t[1] !== '0' && t[1] !== '1') return ERR.VALUE_OUT_OF_RANGE;
      X.rig_tuner(t[1] === '1' ? 1 : 0, t[2] === '0' ? 0 : 1); return 0;
    }
    if (c === 'tuner_ref') { const v = Number(t[1]); if (!(v >= 400 && v <= 480)) return ERR.VALUE_OUT_OF_RANGE; X.rig_tuner_ref(v); return 0; }
    if (c === 'fx_add') {                        // fx_add <type> [id] [position]
      if (!this.mtypes[t[1]]) return ERR.INSTANCE_INVALID;
      if (t[3] !== undefined && !/^\d+$/.test(t[3])) return ERR.VALUE_OUT_OF_RANGE;
      this.writeText(`${t[1]} ${t[2] || '-'} ${t[3] || ''}`.slice(0, 60));
      const k = X.rig_fx_add(); if (k < 0) return k === -2 ? ERR.RACK_FULL : k === -3 ? ERR.ID_TAKEN : ERR.INSTANCE_INVALID;
      this.readChain(); this.sendChain(); return 0;
    }
    if (c === 'fx_remove') { const m = this.inst(t[1]); if (m < 0) return ERR.INSTANCE_INVALID; X.rig_fx_remove(m); this.readChain(); this.sendChain(); return 0; }
    if (c === 'fx_move') {                       // fx_move <inst> <position>
      const m = this.inst(t[1]), pos = Number(t[2]); if (m < 0) return ERR.INSTANCE_INVALID;
      if (!Number.isInteger(pos) || pos < 0) return ERR.VALUE_OUT_OF_RANGE;
      X.rig_fx_move(m, pos); this.readChain(); this.sendChain(); return 0;
    }
    if (c === 'src_add') {                       // src_add <type> [id]
      if (!this.types[t[1]]) return ERR.INSTANCE_INVALID;
      this.writeText(`${t[1]} ${t[2] || ''}`.slice(0, 60));
      const k = X.rig_src_add(); if (k < 0) return k === -2 ? ERR.RACK_FULL : k === -3 ? ERR.ID_TAKEN : ERR.INSTANCE_INVALID;
      this.readRack(); this.sendRack(); return 0;
    }
    if (c === 'src_remove') { const k = this.source(t[1]); if (k < 0) return ERR.INSTANCE_INVALID; X.rig_src_remove(k); this.readRack(); this.sendRack(); return 0; }
    if (c === 'src_mod' || c === 'src_unmod') {  // src_mod <source> <symbol> <from source> <depth> | src_unmod <source> <symbol>
      const k = this.source(t[1]); if (k < 0) return ERR.INSTANCE_INVALID;
      const p = this.sourceKnob(k, t[2]); if (p < 0) return ERR.INVALID_PARAM_SYMBOL;
      if (c === 'src_unmod') { X.rig_src_mod(k, p, -1, 0); return 0; }
      const f = this.source(t[3]), d = Number(t[4]); if (f < 0 || !(d >= 0 && d <= 1)) return ERR.VALUE_OUT_OF_RANGE;
      return X.rig_src_mod(k, p, f, d) === 0 ? 0 : ERR.INSTANCE_INVALID;
    }
    if (c === 'src_set') {                       // src_set <source> <symbol> <0..1>
      const k = this.source(t[1]); if (k < 0) return ERR.INSTANCE_INVALID;
      const p = this.sourceKnob(k, t[2]); if (p < 0) return ERR.INVALID_PARAM_SYMBOL;
      const v = Number(t[3]); if (!(v >= 0 && v <= 1)) return ERR.VALUE_OUT_OF_RANGE;
      X.rig_src_param(k, p, v); return 0;
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
      // Loading parses and allocates here on the audio thread, so audio pauses briefly (a capture
      // takes ~0.1 s); the swap itself is a click-free crossfade inside rig_process.
      const m = this.inst(d.inst), max = m < 0 ? 0 : this.x.rig_asset_max(m), bytes = d.data ? d.data.byteLength : 0;
      let status = ERR.INSTANCE_INVALID, note = '';
      const t0 = Date.now();
      if (m >= 0) {
        if (!bytes || bytes > max) status = ERR.VALUE_OUT_OF_RANGE;
        else {
          const p = this.x.rig_asset_buf(m, bytes);              // reserve first: it can grow (and detach) memory
          new Uint8Array(this.x.memory.buffer, p, bytes).set(new Uint8Array(d.data));
          status = this.x.rig_asset_commit(m, bytes);
        }
        note = cString(this.x.memory, this.x.rig_asset_status(m));
      }
      this.port.postMessage({ type: 'asset', inst: m, id: this.modId(m), ref: d.ref, status, note, bytes, max, ms: Date.now() - t0 });
    } else if (d.type === 'asset_clear') {
      const m = this.inst(d.inst); if (m >= 0) this.x.rig_asset_clear(m);
      this.port.postMessage({ type: 'asset', inst: m, id: this.modId(m), ref: null, status: m >= 0 ? 0 : ERR.INSTANCE_INVALID, note: m >= 0 ? cString(this.x.memory, this.x.rig_asset_status(m)) : '', bytes: 0, max: 0 });
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
    if (++this.blocks % 12 === 0) {           // ~31 Hz meter stream at 48 kHz; and free modules that have faded out
      this.x.rig_collect();
      const wall = Date.now() - this.wallStart;
      this.port.postMessage({ type: 'meters', t: currentTime, m: Array.from(this.vM), inPk: this.inPk, outPk: this.outPk, load: wall > 0 ? this.busyMs / wall : 0 });
      this.inPk = 0; this.outPk = 0;
      if (wall > 2000) { this.busyMs = 0; this.wallStart = Date.now(); }
    }
    return true;
  }
}
registerProcessor('rig-engine', RigEngine);

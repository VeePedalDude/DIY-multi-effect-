// ui/worklet/engine-worklet.js - runs rig.wasm inside an AudioWorklet.
// The text protocol is parsed by the engine itself (rig_exec), the same code rig-host runs, so new
// modules and sources need no changes here. The commands:
//
//   param_set <inst> <symbol> <0..1>          bypass <inst> <0|1>
//   mod_connect <inst> <symbol> <source> <depth> [phase] [div]
//   mod_disconnect <inst> <symbol>            set_bpm <bpm>
//   master <dB>                               ext <source> <value>
//   input_channel <0|1>                       src_set <source> <symbol> <0..1>
//   tuner <0|1> [mute 0|1]                    tuner_ref <A4 Hz, 400..480>
//   src_add <type> [id]                       src_remove <source>
//   src_mod <source> <symbol> <from source> <depth> [phase] [div]
//     phase (degrees, default 0) and div (1/4 1/3 1/2 2/3 1 3/2 2 3 4 8, default 1) are the patch's own
//     offset and multiple of the source's cycle, for sources that tap (the LFO); others ignore them.
//   src_unmod <source> <symbol>
//   src_cfg <source> <symbol> <0..1>          a setting (not a patch target: a shape, a seed...)
//   src_cell <source> <row> <symbol> <0..1>   a cell of the source's table (per step / per vertex)
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
  modId(slot) { const m = this.chain.find((x) => x.slot === slot); return m ? m.id : null; }
  readRack() { this.rack = JSON.parse(cString(this.x.memory, this.x.rig_rack())).sources; }
  sendRack() { this.port.postMessage({ type: 'rack', rack: this.rack }); }
  // The protocol itself is in the engine (rig_exec, engine/api/rig_api.cpp), shared with rig-host.
  exec(line) {
    const X = this.x, max = X.rig_cmd_len() - 1, buf = new Uint8Array(X.memory.buffer, X.rig_cmd_buf(), max + 1);
    const str = String(line); let n = 0;
    for (let i = 0; i < str.length && n < max; i++) { const c = str.charCodeAt(i); buf[n++] = c < 0x80 ? c : 0x3f; }
    buf[n] = 0;
    const status = X.rig_exec(), changed = X.rig_exec_changed();
    if (changed & 1) { this.readRack(); this.sendRack(); }
    if (changed & 2) { this.readChain(); this.sendChain(); }
    this.inCh = X.rig_input_channel();
    return status;
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

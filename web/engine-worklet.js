// engine-worklet.js - runs rig.wasm inside an AudioWorklet.
// Control is a mod-host-style text protocol, so the same UI can later talk to the
// real rig (WebSocket -> control hub -> mod-host) by swapping only the transport.
//
//   param_set <inst> <symbol> <0..1>          bypass <inst> <0|1>
//   mod_connect <inst> <symbol> <source> <depth>
//   mod_disconnect <inst> <symbol>            set_bpm <bpm>
//   master <dB>                               ext <source> <value>
//   input_channel <0|1>
// Replies:  resp <status>   (0 ok, negatives follow mod-host error numbers)

const INSTANCES = [
  { name: 'gate', symbols: ['thresh', 'release'] },
  { name: 'amp',  symbols: ['input', 'output'] },
  { name: 'cab',  symbols: ['locut', 'mix'] },
  { name: 'holo', symbols: ['feedback', 'halo'] },
  { name: 'tess', symbols: ['size', 'fold'] },
];
const SOURCES = ['lfo', 'env', 'adsr', 'orbit', 'heart', 'ctrl'];
const ERR = { INSTANCE_INVALID: -1, INVALID_PARAM_SYMBOL: -103, VALUE_OUT_OF_RANGE: -207, INVALID_OPERATION: -902 };

class RigEngine extends AudioWorkletProcessor {
  constructor(options) {
    super();
    const mod = new WebAssembly.Module(options.processorOptions.wasm);
    this.x = new WebAssembly.Instance(mod, {}).exports;
    if (this.x._initialize) this.x._initialize();
    this.x.rig_init(sampleRate);
    this.inCh = 0;
    this.blocks = 0; this.busyMs = 0; this.wallStart = Date.now();
    this.inPk = 0; this.outPk = 0;
    this.port.onmessage = (e) => this.onMsg(e.data);
  }
  views() {
    const b = this.x.memory.buffer;
    if (this._buf !== b) {
      this._buf = b;
      this.vIn = new Float32Array(b, this.x.rig_in(), 128);
      this.vL = new Float32Array(b, this.x.rig_out_l(), 128);
      this.vR = new Float32Array(b, this.x.rig_out_r(), 128);
      this.vM = new Float32Array(b, this.x.rig_meters(), 32);
    }
  }
  inst(tok) { const i = Number(tok); return Number.isInteger(i) && i >= 0 && i < INSTANCES.length ? i : -1; }
  exec(line) {
    const t = String(line).trim().split(/\s+/), c = t[0];
    const X = this.x;
    if (c === 'param_set' || c === 'mod_connect' || c === 'mod_disconnect') {
      const m = this.inst(t[1]); if (m < 0) return ERR.INSTANCE_INVALID;
      const p = INSTANCES[m].symbols.indexOf(t[2]); if (p < 0) return ERR.INVALID_PARAM_SYMBOL;
      if (c === 'param_set') { const v = Number(t[3]); if (!(v >= 0 && v <= 1)) return ERR.VALUE_OUT_OF_RANGE; X.rig_param(m, p, v); return 0; }
      if (c === 'mod_disconnect') { X.rig_mod(m, p, -1, 0); return 0; }
      const s = SOURCES.indexOf(t[3]); const d = Number(t[4]);
      if (s < 0 || !(d >= 0 && d <= 1)) return ERR.VALUE_OUT_OF_RANGE;
      X.rig_mod(m, p, s, d); return 0;
    }
    if (c === 'bypass') { const m = this.inst(t[1]); if (m < 0) return ERR.INSTANCE_INVALID; X.rig_bypass(m, t[2] === '1' ? 1 : 0); return 0; }
    if (c === 'set_bpm') { const v = Number(t[1]); if (!(v >= 30 && v <= 300)) return ERR.VALUE_OUT_OF_RANGE; X.rig_tempo(v); return 0; }
    if (c === 'master') { const v = Number(t[1]); if (!(v >= -60 && v <= 6)) return ERR.VALUE_OUT_OF_RANGE; X.rig_master(v); return 0; }
    if (c === 'ext') { const s = SOURCES.indexOf(t[1]); const v = Number(t[2]); if (s < 0 || !Number.isFinite(v)) return ERR.VALUE_OUT_OF_RANGE; X.rig_ext(s, v); return 0; }
    if (c === 'input_channel') { this.inCh = t[1] === '1' ? 1 : 0; return 0; }
    return ERR.INVALID_OPERATION;
  }
  onMsg(d) {
    if (d.type === 'cmd') {
      const st = this.exec(d.line);
      this.port.postMessage({ type: 'resp', id: d.id, line: d.line, status: st });
    } else if (d.type === 'ir') {
      const max = this.x.rig_ir_max(), n = Math.min(max, d.data.length);
      new Float32Array(this.x.memory.buffer, this.x.rig_ir_buf(), max).set(d.data.subarray(0, n));
      this.x.rig_ir_commit(n);
      this.port.postMessage({ type: 'ir', taps: n, truncated: d.data.length > max });
    } else if (d.type === 'ir_default') {
      this.x.rig_ir_default();
      this.port.postMessage({ type: 'ir', taps: 0, truncated: false });
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
      this.port.postMessage({ type: 'meters', m: Array.from(this.vM), inPk: this.inPk, outPk: this.outPk,
        load: wall > 0 ? this.busyMs / wall : 0 });
      this.inPk = 0; this.outPk = 0;
      if (wall > 2000) { this.busyMs = 0; this.wallStart = Date.now(); }
    }
    return true;
  }
}
registerProcessor('rig-engine', RigEngine);

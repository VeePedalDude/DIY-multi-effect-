// 20-bridge.js - the only path from UI to engine: rig.cmd("text line").
// In the browser the transport is the AudioWorklet port; on the Linux rig it becomes a WebSocket.
const rig = { node: null, ctx: null, cmdId: 0, log: [], afterSend: [] };   // afterSend: features that resend their own state after sendAll()
window.__rig = rig;
rig.cmd = function (line) {
  rig.log.push(line);
  if (!rig.node) return;
  rig.node.port.postMessage({ type: 'cmd', id: ++rig.cmdId, line });
};
// Test and console helpers: read live engine values by name.
rig.eff = (modId, sym) => { const p = PARAMS[`${modId}.${sym}`]; return p ? effOf(p) : undefined; };
rig.src = (id) => { const s = SRCMAP[id]; return s ? srcVal(s) : undefined; };
rig.srcEff = (id, sym) => { const q = SPARAMS[`${id}.${sym}`]; return q ? srcEff(q) : undefined; };
rig.modMeter = (modId, name) => { const m = MODS.find((x) => x.id === modId); return m ? modMeter(m, name) : undefined; };
rig.srcParam = (id, sym) => { const s = SRCMAP[id], q = s && s.params.find((x) => x.sym === sym); return q ? q.real(q.v) : undefined; };
rig.srcMeter = (id, name) => { const s = SRCMAP[id]; return s ? srcMeter(s, name) : undefined; };
rig.state = state;
rig.nanResets = () => meters ? meters.m[LAYOUT.nan] : 0;

let quietLog = { last: '', t: 0 };
function onEngineMsg(d) {
  if (d.type === 'meters') {                     // ~31 per second: one point of each source's trace
    meters = d; rig.meters = d;
    SRC.forEach((s) => { if (!s.hist) return; s.hist.push(srcVal(s)); if (s.hist.length > 120) s.hist.shift(); });
    return;
  }
  if (d.type === 'resp') {
    const k = d.line.split(' ').slice(0, 3).join(' '), now = performance.now();
    if (d.status !== 0) { logLine(d.line + '  ->  resp ' + d.status, true); if (/^src_add /.test(d.line)) rackRefused(d.line, d.status); if (/^fx_add /.test(d.line)) chainRefused(d.line, d.status); }
    else if (k !== quietLog.last || now - quietLog.t > 400) { logLine(d.line + '  ->  resp 0'); quietLog = { last: k, t: now }; }
    rig.lastResp = d;
    return;
  }
  if (d.type === 'rack') { onEngineRack(d.rack); rig.engineRack = d.rack; return; }
  if (d.type === 'chain') { onEngineChain(d.chain); rig.engineChain = d.chain; return; }
  if (d.type === 'describe') { rig.engineDesc = d.desc; logLine(`engine: ${d.desc.modules.length} modules, ${d.desc.sources.length} sources @ ${d.desc.sr} Hz`); return; }
  if (d.type === 'asset') onAssetReply(d);
}
function sendAll() {
  MODS.forEach((m) => m.params.forEach((p) => rig.cmd(`param_set ${m.inst} ${p.sym} ${p.v.toFixed(4)}`)));
  MODS.forEach((m) => rig.cmd(`bypass ${m.inst} ${state.bypass[m.id] ? 1 : 0}`));
  SRC.forEach((s) => s.params.forEach((q) => rig.cmd(`src_set ${s.id} ${q.sym} ${q.v.toFixed(6)}`)));
  Object.keys(PARAMS).concat(Object.keys(SPARAMS)).forEach((pid) => sendPatch(pid));
  rig.cmd(`set_bpm ${state.bpm}`); rig.cmd(`master ${state.master}`);
  rig.afterSend.forEach((f) => f());
}
function setSrcParam(q, v) { q.v = clamp(v, 0, 1); rig.cmd(`src_set ${q.src.id} ${q.sym} ${q.v.toFixed(6)}`); }
// One patch, as the engine should have it: a module knob (mod_connect) or a source knob (src_mod).
// A patch from a tapping source (an LFO) also carries its PHASE and DIV when they aren't 0 degrees, 1x.
function sendPatch(pid) {
  const k = knobOf(pid), s = state.conns[pid]; if (!k) return;
  const t = tapOf(pid), tap = s && tapsAt(pid) && (t.phase || t.div !== 1) ? ` ${Math.round(t.phase)} ${divName(t.div)}` : '';
  if (k.mod) rig.cmd(s ? `mod_connect ${k.mod.inst} ${k.sym} ${s} ${depthOf(pid).toFixed(4)}${tap}` : `mod_disconnect ${k.mod.inst} ${k.sym}`);
  else rig.cmd(s ? `src_mod ${k.src.id} ${k.sym} ${s} ${depthOf(pid).toFixed(4)}${tap}` : `src_unmod ${k.src.id} ${k.sym}`);
}
function setTap(pid, phase, div) {             // a patch's own PHASE (0..360 degrees) and DIV
  if (!knobOf(pid) || !state.conns[pid]) return;
  const t = { phase: ((Math.round(phase) % 360) + 360) % 360, div };
  if (!t.phase && t.div === 1) delete state.taps[pid]; else state.taps[pid] = t;
  sendPatch(pid);
}
function setDepth(pid, d) {                     // swing of one patch, 0..1 either side of the knob
  if (!knobOf(pid) || !state.conns[pid]) return;
  state.depths[pid] = clamp(d, 0, 1); sendPatch(pid);
}
function setParam(p, v) { p.v = clamp(v, 0, 1); rig.cmd(`param_set ${p.mod.inst} ${p.sym} ${p.v.toFixed(4)}`); }
// Patch a knob (a module's or a source's) from a source, or unpatch it (sid null).
function connect(pid, sid) {
  if (!knobOf(pid)) return;
  if (state.conns[pid] !== sid) delete state.taps[pid];          // a new patch starts at 0 degrees, 1x
  if (sid) state.conns[pid] = sid; else { delete state.conns[pid]; delete state.depths[pid]; }
  sendPatch(pid); syncCables();
}

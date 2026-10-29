// 20-bridge.js - the only path from UI to engine: rig.cmd("text line").
// In the browser the transport is the AudioWorklet port; on the Linux rig it becomes a WebSocket.
const rig = { node: null, ctx: null, cmdId: 0, log: [] };
window.__rig = rig;
rig.cmd = function (line) {
  rig.log.push(line);
  if (!rig.node) return;
  rig.node.port.postMessage({ type: 'cmd', id: ++rig.cmdId, line });
};
// Test and console helpers: read live engine values by name.
rig.eff = (modId, sym) => { const p = PARAMS[`${modId}.${sym}`]; return p ? effOf(p) : undefined; };
rig.src = (id) => { const s = SRCMAP[id]; return s ? srcVal(s.idx) : undefined; };
rig.modMeter = (modId, name) => { const m = MODS.find((x) => x.id === modId); return m ? modMeter(m, name) : undefined; };
rig.srcParam = (id, sym) => { const s = SRCMAP[id], q = s && s.params.find((x) => x.sym === sym); return q ? q.real(q.v) : undefined; };
rig.srcMeter = (id, name) => { const s = SRCMAP[id]; return s ? srcMeter(s, name) : undefined; };
rig.state = state;
rig.nanResets = () => meters ? meters.m[LAYOUT.nan] : 0;

let quietLog = { last: '', t: 0 };
function onEngineMsg(d) {
  if (d.type === 'meters') {                     // ~31 per second: one point of each source's trace
    meters = d; rig.meters = d;
    SRC.forEach((s) => { if (!s.hist) return; s.hist.push(srcVal(s.idx)); if (s.hist.length > 120) s.hist.shift(); });
    return;
  }
  if (d.type === 'resp') {
    const k = d.line.split(' ').slice(0, 3).join(' '), now = performance.now();
    if (d.status !== 0) logLine(d.line + '  ->  resp ' + d.status, true);
    else if (k !== quietLog.last || now - quietLog.t > 400) { logLine(d.line + '  ->  resp 0'); quietLog = { last: k, t: now }; }
    rig.lastResp = d;
    return;
  }
  if (d.type === 'describe') { rig.engineDesc = d.desc; logLine(`engine: ${d.desc.modules.length} modules, ${d.desc.sources.length} sources @ ${d.desc.sr} Hz`); return; }
  if (d.type === 'asset') onAssetReply(d);
}
function sendAll() {
  MODS.forEach((m) => m.params.forEach((p) => rig.cmd(`param_set ${m.inst} ${p.sym} ${p.v.toFixed(4)}`)));
  MODS.forEach((m) => rig.cmd(`bypass ${m.inst} ${state.bypass[m.id] ? 1 : 0}`));
  Object.keys(PARAMS).forEach((pid) => {
    const p = PARAMS[pid], s = state.conns[pid];
    rig.cmd(s ? `mod_connect ${p.mod.inst} ${p.sym} ${s} ${depthOf(pid)}` : `mod_disconnect ${p.mod.inst} ${p.sym}`);
  });
  SRC.forEach((s) => s.params.forEach((q) => rig.cmd(`src_set ${s.id} ${q.sym} ${q.v.toFixed(6)}`)));
  rig.cmd(`set_bpm ${state.bpm}`); rig.cmd(`master ${state.master}`);
}
function setSrcParam(q, v) { q.v = clamp(v, 0, 1); rig.cmd(`src_set ${q.src.id} ${q.sym} ${q.v.toFixed(6)}`); }
function setDepth(pid, d) {                     // swing of one patch, 0..1 either side of the knob
  const p = PARAMS[pid], s = state.conns[pid]; if (!p || !s) return;
  state.depths[pid] = clamp(d, 0, 1); rig.cmd(`mod_connect ${p.mod.inst} ${p.sym} ${s} ${state.depths[pid].toFixed(4)}`);
}
function setParam(p, v) { p.v = clamp(v, 0, 1); rig.cmd(`param_set ${p.mod.inst} ${p.sym} ${p.v.toFixed(4)}`); }
function connect(pid, sid) {
  const p = PARAMS[pid];
  if (sid) { state.conns[pid] = sid; rig.cmd(`mod_connect ${p.mod.inst} ${p.sym} ${sid} ${depthOf(pid)}`); }
  else { delete state.conns[pid]; delete state.depths[pid]; rig.cmd(`mod_disconnect ${p.mod.inst} ${p.sym}`); }
  syncCables();
}

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
rig.srcMeter = (id, name) => { const s = SRCMAP[id]; return s ? srcMeter(s, name) : undefined; };
rig.nanResets = () => meters ? meters.m[LAYOUT.nan] : 0;

let quietLog = { last: '', t: 0 };
function onEngineMsg(d) {
  if (d.type === 'meters') { meters = d; rig.meters = d; return; }
  if (d.type === 'resp') {
    const k = d.line.split(' ').slice(0, 3).join(' '), now = performance.now();
    if (d.status !== 0) logLine(d.line + '  ->  resp ' + d.status, true);
    else if (k !== quietLog.last || now - quietLog.t > 400) { logLine(d.line + '  ->  resp 0'); quietLog = { last: k, t: now }; }
    rig.lastResp = d;
    return;
  }
  if (d.type === 'describe') { rig.engineDesc = d.desc; logLine(`engine: ${d.desc.modules.length} modules, ${d.desc.sources.length} sources @ ${d.desc.sr} Hz`); return; }
  if (d.type === 'ir') logLine(d.taps ? `ir loaded: ${d.taps} taps${d.truncated ? ' (truncated to 2048)' : ''}` : 'ir: built-in');
}
function sendAll() {
  MODS.forEach((m) => m.params.forEach((p) => rig.cmd(`param_set ${m.inst} ${p.sym} ${p.v.toFixed(4)}`)));
  MODS.forEach((m) => rig.cmd(`bypass ${m.inst} ${state.bypass[m.id] ? 1 : 0}`));
  Object.keys(PARAMS).forEach((pid) => {
    const p = PARAMS[pid], s = state.conns[pid];
    rig.cmd(s ? `mod_connect ${p.mod.inst} ${p.sym} ${s} ${DEPTH}` : `mod_disconnect ${p.mod.inst} ${p.sym}`);
  });
  rig.cmd(`set_bpm ${state.bpm}`); rig.cmd(`master ${state.master}`);
}
function setParam(p, v) { p.v = clamp(v, 0, 1); rig.cmd(`param_set ${p.mod.inst} ${p.sym} ${p.v.toFixed(4)}`); }
function connect(pid, sid) {
  const p = PARAMS[pid];
  if (sid) { state.conns[pid] = sid; rig.cmd(`mod_connect ${p.mod.inst} ${p.sym} ${sid} ${DEPTH}`); }
  else { delete state.conns[pid]; rig.cmd(`mod_disconnect ${p.mod.inst} ${p.sym}`); }
  syncCables();
}

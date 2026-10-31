// 31-host.js - when rig-host serves this page (window.RIG_HOST, set by hosts/rig-host), the engine runs
// natively on the audio interface instead of in the browser: "Start" opens a WebSocket to the host
// and everything else works unchanged. rig.node is a stand-in whose port carries the same messages the
// worklet's did (cmd, asset, asset_clear out; describe, rack, chain, resp, meters, asset back), plus
// the host's own: {type:'host'} (backend, buffers, devices) and {type:'latency'} (a measured round trip).
// Files are decoded here as before (an OfflineAudioContext: no audio of its own), then sent as bytes.
const HOST = typeof window !== 'undefined' && window.RIG_HOST ? window.RIG_HOST : null;
const host = { ws: null, info: null, latency: null, want: 'device', up: false, started: false };
rig.host = host;

function hostSend(msg, data) {
  if (!host.ws || host.ws.readyState !== 1) return;
  host.ws.send(JSON.stringify(msg)); if (data) host.ws.send(data);
}
// The worklet stand-in: same postMessage shape, over the WebSocket.
const hostPort = {
  postMessage(msg) {
    if (msg.type === 'asset') { hostSend({ type: 'asset', inst: msg.inst, ref: msg.ref, bytes: msg.data ? msg.data.byteLength : 0 }, msg.data); return; }
    hostSend(msg);
  },
};
function hostMsg(d) {
  if (d.type === 'host') { host.info = d; hostReadout(); return; }
  if (d.type === 'latency') { host.latency = d; hostReadout(); logLine(d.ok ? `round trip measured: ${d.ms.toFixed(2)} ms (${d.samples} samples at ${d.rate} Hz; runs ${d.runs.join(', ')})` : 'round trip: ' + d.note, !d.ok); $('latTest').disabled = false; return; }
  if (d.type === 'describe') { onEngineMsg({ type: 'describe', desc: d.raw }); if (!host.up) hostReady(); return; }
  if (d.type === 'rack') { onEngineMsg({ type: 'rack', rack: d.raw.sources }); return; }
  if (d.type === 'chain') { onEngineMsg({ type: 'chain', chain: d.raw.modules }); return; }
  onEngineMsg(d);
}
// Connected and described: hand the host this page's state (the page is the source of truth, as with the worklet).
function hostReady() {
  host.up = true; rig.node = { port: hostPort };
  syncChain(); syncRack(); sendAll(); pushAllAssets();
  rig.cmd(`host_input ${host.want === 'riff' ? 'riff' : 'interface'}`);
  $('statusDot').style.background = 'var(--ok)'; $('engineState').textContent = 'engine running · rig-host';
  logLine(`rig-host ${HOST.version}: engine running natively`);
}
function hostConnect() {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(`ws://${location.host}/ws`); ws.binaryType = 'arraybuffer'; host.ws = ws;
    let opened = false;
    ws.onopen = () => { opened = true; resolve(); };
    ws.onmessage = (e) => { try { hostMsg(JSON.parse(e.data)); } catch (err) { logLine('host message: ' + err.message, true); } };
    ws.onclose = () => {
      host.up = false; rig.node = null;
      $('statusDot').style.background = 'var(--bad)'; $('engineState').textContent = 'rig-host disconnected · retrying';
      if (!opened) reject(new Error('rig-host isn’t answering')); else setTimeout(() => hostConnect().catch(() => {}), 2000);
    };
  });
}
// Start (from the start overlay): no browser audio, just the host.
async function hostStart(source) {
  host.want = source; host.started = true;
  rig.ctx = new OfflineAudioContext(2, 1, 48000);            // for decoding IR files only
  await hostConnect();
  $('srcSel').value = source;
}
// What the drawer and top bar say about the host's audio.
function hostReadout() {
  const i = host.info; if (!i) return;
  const L = host.latency && host.latency.ok ? host.latency : null;
  $('rateLat').textContent = `${(i.rate / 1000).toFixed(1)} kHz · ${L ? L.ms.toFixed(1) + ' ms measured' : i.nominalMs.toFixed(1) + ' ms buffers'}`;
  $('hostInfo').textContent = `rig-host: ${i.backend}${i.backend === 'WASAPI' ? (i.exclusive ? ' exclusive' : ' shared') : ''} · ${i.rate} Hz · ${i.frames} frames × ${i.periods} buffers (${i.nominalMs.toFixed(2)} ms of buffering) · in: ${i.in} · out: ${i.out}`;
  $('latNote').textContent = L ? `Round trip measured through the interface: ${L.ms.toFixed(2)} ms (${L.samples} samples; buffers alone are ${i.nominalMs.toFixed(2)} ms, the rest is the converters and the driver).`
    : 'Round trip: connect an output to the input with a cable (a short guitar cable from Output 1 to Input 1, gain low), then Measure. Unplug it before playing.';
}
function wireHost() {
  if (!HOST) return;
  document.body.classList.add('host-mode');
  $('startTitle').textContent = 'RIG/01 · NATIVE ENGINE';
  $('startBtn').textContent = 'Start';
  ['inSelRow', 'outSelRow'].forEach((id) => { $(id).hidden = true; });
  $('srcSel').querySelector('option[value="file"]').disabled = true;
  $('hostInfo').hidden = false; $('latTest').hidden = false;
  $('latTest').onclick = () => { $('latTest').disabled = true; host.latency = null; $('latNote').textContent = 'Measuring: five clicks out and back…'; rig.cmd('latency_test'); setTimeout(() => { $('latTest').disabled = false; }, 6000); };
}

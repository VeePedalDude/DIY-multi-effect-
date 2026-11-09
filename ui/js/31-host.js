// 31-host.js - when rig-host serves this page (window.RIG_HOST, set by hosts/rig-host), the engine runs
// natively on the audio interface instead of in the browser: "Start" opens a WebSocket to the host
// and everything else works unchanged. rig.node is a stand-in whose port carries the same messages the
// worklet's did (cmd, asset, asset_clear out; describe, rack, chain, resp, meters, asset back), plus
// the host's own: {type:'host'} (backend, buffers, devices) and {type:'latency'} (a measured round trip).
// Files are decoded here as before (an OfflineAudioContext: no audio of its own), then sent as bytes.
const HOST = typeof window !== 'undefined' && window.RIG_HOST ? window.RIG_HOST : null;
const host = { ws: null, info: null, latency: null, want: 'device', up: false, started: false, devices: null };
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
  if (d.type === 'host') {
    if (d.note && (!host.info || host.info.note !== d.note)) logLine('rig-host: ' + d.note, true);   // dropouts, or devices that don't match
    host.info = d; hostReadout(); return;
  }
  if (d.type === 'latency') { host.latency = d; hostReadout(); logLine(d.ok ? `round trip measured: ${d.ms.toFixed(2)} ms (${d.samples} samples at ${d.rate} Hz; runs ${d.runs.join(', ')})` : 'round trip: ' + d.note, !d.ok); $('latTest').disabled = false; return; }
  if (d.type === 'devices') { host.devices = d; hostDeviceMenus(); return; }
  if (d.type === 'resp' && /^host_(device|frames) /.test(d.line)) {
    host.lastSwitch = d;
    ['hostDev', 'inSel', 'outSel', 'hostBuf'].forEach((id) => { $(id).disabled = false; });
    logLine(d.status === 0 ? 'rig-host: switched' : 'rig-host: that didn\u2019t start (or a file was loading); still on the previous device', d.status !== 0);
    hostDeviceMenus(); return;
  }
  if (d.type === 'resync') { syncChain(); syncRack(); sendAll(); pushAllAssets(); logLine('rig-host: new sample rate, rig sent again'); return; }
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
  rig.cmd('host_devices');
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
  $('hostInfo').textContent = `rig-host: ${i.backend}${i.backend === 'WASAPI' ? (i.exclusive ? ' exclusive' : ' shared') : ''} · ${i.rate} Hz · ${i.frames} frames × ${i.periods} buffers (${i.nominalMs.toFixed(2)} ms ${i.backend === 'ASIO' ? 'in + out, as the driver reports it' : 'of buffering'}) · in: ${i.in} · out: ${i.out}${i.drops ? ` · ${i.drops} dropouts` : ''}${i.note ? ` · ${i.note}` : ''}`;
  $('latNote').textContent = L ? `Round trip measured through the interface: ${L.ms.toFixed(2)} ms (${L.samples} samples; buffers alone are ${i.nominalMs.toFixed(2)} ms, the rest is the converters and the driver).`
    : 'Round trip: connect an output to the input with a cable (a short guitar cable from Output 1 to Input 1, gain low), then Measure. Unplug it before playing.';
}
// The Driver menu (like any audio program's): every ASIO driver, and the system's own path with an input
// and output menu; a buffer size; the ASIO driver's own settings window. The host remembers the choice.
function hostDeviceMenus() {
  const d = host.devices; if (!d) return;
  const opt = (v, t, sel) => { const o = document.createElement('option'); o.value = v; o.textContent = t; o.selected = !!sel; return o; };
  const dev = $('hostDev'); dev.replaceChildren();
  d.asio.forEach((a, i) => dev.append(opt('asio:' + i, `ASIO · ${a.name}${a.wrapper ? ' (wraps Windows audio)' : ''}`, d.asioOn && d.drv === i)));
  dev.append(opt('device', `${d.system === 'WASAPI' ? 'Windows audio (WASAPI)' : d.system} · choose input and output`, !d.asioOn));
  const fill = (sel, list, cur) => { sel.replaceChildren(); list.forEach((n, i) => sel.append(opt(String(i), n, i === cur))); };
  fill($('inSel'), d.inputs, d.in); fill($('outSel'), d.outputs, d.out);
  ['inSelRow', 'outSelRow'].forEach((id) => { $(id).hidden = d.asioOn; });
  const buf = $('hostBuf'); buf.replaceChildren();
  if (d.asioOn) buf.append(opt('0', `driver's setting (${d.frames})`, !d.fixed));
  [32, 64, 128, 256, 512, 1024].forEach((n) => buf.append(opt(String(n), `${n} samples (${(1000 * n / ((host.info && host.info.rate) || 48000)).toFixed(1)} ms)`, (d.fixed || !d.asioOn) && n === d.frames)));
  $('hostPanel').hidden = !d.asioOn;
}
function hostSwitch(line) {
  ['hostDev', 'inSel', 'outSel', 'hostBuf'].forEach((id) => { $(id).disabled = true; });
  logLine('rig-host: switching…');
  rig.cmd(line);                                 // answered by a resp (hostMsg) and a fresh {type:'devices'}
}
function wireHost() {
  if (!HOST) return;
  document.body.classList.add('host-mode');
  $('startTitle').textContent = 'RIG/01 · NATIVE ENGINE';
  $('startBtn').textContent = 'Start';
  ['inSelRow', 'outSelRow'].forEach((id) => { $(id).hidden = true; });
  $('srcSel').querySelector('option[value="file"]').disabled = true;
  $('hostInfo').hidden = false; $('latTest').hidden = false;
  ['hostDevRow', 'hostBufRow'].forEach((id) => { $(id).hidden = false; });
  $('hostDev').onchange = () => {
    const v = $('hostDev').value;
    if (v === 'device') { ['inSelRow', 'outSelRow'].forEach((id) => { $(id).hidden = false; }); hostSwitch(`host_device device ${$('inSel').value || -1} ${$('outSel').value || -1}`); }
    else hostSwitch(`host_device asio ${v.split(':')[1]}`);
  };
  const pair = () => hostSwitch(`host_device device ${$('inSel').value} ${$('outSel').value}`);
  $('inSel').addEventListener('change', (e) => { if (HOST) { e.stopImmediatePropagation(); pair(); } }, true);
  $('outSel').addEventListener('change', (e) => { if (HOST) { e.stopImmediatePropagation(); pair(); } }, true);
  $('hostBuf').onchange = () => hostSwitch(`host_frames ${$('hostBuf').value}`);
  $('hostPanel').onclick = () => rig.cmd('host_panel');
  $('latTest').onclick = () => { $('latTest').disabled = true; host.latency = null; $('latNote').textContent = 'Measuring: five clicks out and back…'; rig.cmd('latency_test'); setTimeout(() => { $('latTest').disabled = false; }, 6000); };
}

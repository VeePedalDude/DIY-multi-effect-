// 30-audio.js - AudioContext, engine worklet, input sources (interface, test riff, DI file), devices, latency.
let inputNode = null, mediaStream = null, loopSrc = null, riffBuf = null;

async function startAudio(source) {
  const ctx = new AudioContext({ latencyHint: 0 });
  rig.ctx = ctx;
  // A data: URL works when the page is opened straight from disk (file://), where blob: worklets are refused.
  const src = $('worklet-src').textContent;
  const url = 'data:text/javascript;base64,' + btoa(unescape(encodeURIComponent(src)));
  await ctx.audioWorklet.addModule(url);
  const wasm = WASM_BYTES.slice();
  const node = new AudioWorkletNode(ctx, 'rig-engine', {
    numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [2],
    channelCount: 2, channelCountMode: 'explicit', channelInterpretation: 'discrete',
    processorOptions: { wasm: wasm.buffer },
  });
  node.port.onmessage = (e) => onEngineMsg(e.data);
  node.connect(ctx.destination);
  rig.node = node;
  sendAll();
  pushAllAssets();
  await ctx.resume();
  await setSource(source);
  await listDevices();
  $('statusDot').style.background = 'var(--ok)'; $('engineState').textContent = 'engine running';
  setInterval(updateLatency, 1000); updateLatency();
}
function disconnectInput() {
  if (inputNode) { try { inputNode.disconnect(); } catch (e) {} inputNode = null; }
  if (loopSrc) { try { loopSrc.stop(); } catch (e) {} loopSrc = null; }
  if (mediaStream) { mediaStream.getTracks().forEach((t) => t.stop()); mediaStream = null; }
}
async function setSource(kind, deviceId) {
  const ctx = rig.ctx; disconnectInput();
  $('srcSel').value = kind;
  if (kind === 'device') {
    const audio = { echoCancellation: false, noiseSuppression: false, autoGainControl: false, channelCount: { ideal: 2 }, latency: 0 };
    if (deviceId) audio.deviceId = { exact: deviceId };
    mediaStream = await navigator.mediaDevices.getUserMedia({ audio });
    inputNode = ctx.createMediaStreamSource(mediaStream);
    inputNode.connect(rig.node);
    const tr = mediaStream.getAudioTracks()[0];
    logLine(`input: ${tr.label || 'device'} · ${tr.getSettings().channelCount || '?'} ch`);
  } else {
    let buf;
    if (kind === 'riff') buf = riffBuf || (riffBuf = makeRiff(ctx));
    else buf = kind;                       // an AudioBuffer from a DI file
    loopSrc = ctx.createBufferSource(); loopSrc.buffer = buf; loopSrc.loop = true;
    loopSrc.connect(rig.node); loopSrc.start(); inputNode = loopSrc;
    rig.cmd('input_channel 0'); $('chSel').value = '0';
    logLine(kind === 'riff' ? 'input: built-in test riff' : 'input: DI file loop');
  }
}
async function listDevices() {
  const devs = await navigator.mediaDevices.enumerateDevices();
  const fill = (sel, kind, cur) => {
    sel.innerHTML = '';
    devs.filter((d) => d.kind === kind).forEach((d, i) => { const o = document.createElement('option'); o.value = d.deviceId; o.textContent = d.label || `${kind} ${i + 1}`; sel.appendChild(o); });
    if (cur) sel.value = cur;
  };
  const curIn = mediaStream ? mediaStream.getAudioTracks()[0].getSettings().deviceId : undefined;
  fill($('inSel'), 'audioinput', curIn);
  fill($('outSel'), 'audiooutput');
  if (!rig.ctx.setSinkId) { $('outSel').disabled = true; $('outSel').title = 'This browser cannot choose an output device'; }
}
function updateLatency() {
  const ctx = rig.ctx; if (!ctx) return;
  let inLat = 0;
  if (mediaStream) { const s = mediaStream.getAudioTracks()[0].getSettings(); inLat = s.latency || 0; }
  const total = (inLat + (ctx.baseLatency || 0) + (ctx.outputLatency || 0)) * 1000;
  $('rateLat').textContent = `${(ctx.sampleRate / 1000).toFixed(1)} kHz · ${total.toFixed(1)} ms`;
  $('latNote').textContent = `Reported round trip ≈ ${total.toFixed(1)} ms (input ${(inLat * 1000).toFixed(1)}, processing ${((ctx.baseLatency || 0) * 1000).toFixed(1)}, output ${((ctx.outputLatency || 0) * 1000).toFixed(1)}). Browsers on Windows usually add 20–40 ms; the JACK rig on the iMac targets 5–10 ms.`;
}
// synthesized guitar loop (Karplus-Strong), peaks at -12 dBFS like a 2i2 on INST
function makeRiff(ctx) {
  const sr = ctx.sampleRate, secs = 8, N = Math.floor(sr * secs);
  const out = new Float32Array(N);
  const E2 = 82.41, A2 = 110, B2 = 123.47, D3 = 146.83;
  const notes = []; let t = 0.05;
  for (let bar = 0; bar < 2; bar++) {
    const r = bar ? A2 : E2;
    notes.push([t, r, 1, 0], [t + 0.008, r * 1.4983, 0.8, 0], [t + 0.016, r * 2, 0.6, 0]); t += 0.75;
    for (let k = 0; k < 4; k++) { notes.push([t, r, 0.7, 1]); t += 0.125; } notes.push([t, bar ? D3 : B2, 0.9, 0]); t += 0.75;
    notes.push([t, r * 2, .8, 0], [t + .25, r * 2 * 1.189, .7, 0], [t + .5, r * 2 * 1.335, .75, 0], [t + .75, r * 2 * 1.498, .8, 0]); t += 1.0;
  }
  let seed = 12345; const rnd = () => ((seed = (seed * 1103515245 + 12345) >>> 0) / 4294967296) * 2 - 1;
  for (const [nt, f, vel, mute] of notes) {
    const start = Math.floor(nt * sr), period = Math.round(sr / f), line = new Float32Array(period);
    for (let i = 0; i < period; i++) line[i] = vel * rnd();
    const damp = mute ? 0.93 : 0.996, dur = Math.floor((mute ? 0.18 : 2.0) * sr);
    let idx = 0;
    for (let i = 0; i < dur && start + i < N; i++) {
      const a = line[idx], b = line[(idx + 1) % period];
      line[idx] = 0.5 * (a + b) * damp; idx = (idx + 1) % period;
      const fade = i > dur - 480 ? (dur - i) / 480 : 1;
      out[start + i] += a * fade;
    }
  }
  let pk = 0; for (const v of out) pk = Math.max(pk, Math.abs(v));
  for (let i = 0; i < N; i++) out[i] *= 0.25 / pk;
  const buf = ctx.createBuffer(1, N, sr); buf.copyToChannel(out, 0); return buf;
}


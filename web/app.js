// app.js - RIG/01 test bench UI. Talks to the engine only through rig.cmd("text line").
(function () {
'use strict';
const ACC = '#4FE3F0';
const RM = matchMedia('(prefers-reduced-motion: reduce)').matches;
const clamp = (x, a, b) => Math.max(a, Math.min(b, x));
const $ = (id) => document.getElementById(id);

// ------------------------------------------------------------------ model (mirrors engine mappings in rig.h)
const SRC = [
  { id: 'lfo',   name: 'LFO',        sub: 'tempo synced · 8 beats', color: '#4FE3F0' },
  { id: 'env',   name: 'ENV FOLLOW', sub: 'pick dynamics',         color: '#FFB547' },
  { id: 'adsr',  name: 'PICK ADSR',  sub: 'per-note swell',        color: '#FF5FA2' },
  { id: 'orbit', name: 'THREE-BODY', sub: 'chaotic orbit',         color: '#A78BFA' },
  { id: 'heart', name: 'HEART RATE', sub: 'simulated 72 bpm',      color: '#FF7A6B' },
  { id: 'ctrl',  name: 'CONTROLLER', sub: 'gamepad stick / slider', color: '#9BE15D' },
];
const SRCMAP = Object.fromEntries(SRC.map((s) => [s.id, s]));
const pct = (v) => Math.round(v * 100) + '%';
const sdb = (d) => (d >= 0 ? '+' : '') + d.toFixed(1) + ' dB';
const MODS = [
  { id: 'gate', inst: 0, name: 'NOISE GATE', kind: 'utility', params: [
    { id: 'gate.th', sym: 'thresh', label: 'THRESH', v: .35, fmt: (v) => (-80 + 60 * v).toFixed(0) + ' dB' },
    { id: 'gate.rel', sym: 'release', label: 'RELEASE', v: .4, fmt: (v) => (20 + 480 * v).toFixed(0) + ' ms' }] },
  { id: 'amp', inst: 1, name: 'AMP', kind: 'stand-in · NAM A2 slot', params: [
    { id: 'amp.in', sym: 'input', label: 'INPUT', v: .6, fmt: (v) => sdb(-12 + 36 * v) },
    { id: 'amp.out', sym: 'output', label: 'OUTPUT', v: .5, fmt: (v) => sdb(-18 + 24 * v) }] },
  { id: 'cab', inst: 2, name: 'CAB IR', kind: 'built-in cab', params: [
    { id: 'cab.lo', sym: 'locut', label: 'LO CUT', v: .25, fmt: (v) => (20 * Math.pow(10, 1.3 * v)).toFixed(0) + ' Hz' },
    { id: 'cab.mix', sym: 'mix', label: 'MIX', v: .9, fmt: pct }] },
  { id: 'holo', inst: 3, name: 'HOLOGRAM', kind: 'delay · dotted 8th', params: [
    { id: 'holo.fb', sym: 'feedback', label: 'FEEDBACK', v: .45, fmt: (v) => Math.round(v * 95) + '%' },
    { id: 'holo.halo', sym: 'halo', label: 'HALO', v: .6, fmt: pct }] },
  { id: 'tess', inst: 4, name: 'TESSERACT', kind: 'reverb · 8-line FDN', params: [
    { id: 'tess.size', sym: 'size', label: 'SIZE', v: .7, fmt: (v) => 'RT ' + (0.5 + 7 * v).toFixed(1) + ' s' },
    { id: 'tess.fold', sym: 'fold', label: '4D FOLD', v: .3, fmt: (v) => v.toFixed(2) }] },
];
const PARAMS = {}; MODS.forEach((m, mi) => m.params.forEach((p, pi) => { p.mod = m; p.pi = pi; p.mi = mi; PARAMS[p.id] = p; }));
const DEPTH = 0.22;

const state = {
  carrying: null, focus: 'holo', view: 'stage', bpm: 120, master: -12,
  conns: { 'amp.in': 'env', 'holo.fb': 'lfo', 'holo.halo': 'orbit', 'tess.fold': 'heart' },
  bypass: {}, scenes: {}, scene: null, storing: false, ctrl: 0,
};
let meters = null;           // latest engine meter packet
const effOf = (p) => meters ? meters.m[7 + p.mi * 2 + p.pi] : p.v;
const srcVal = (i) => meters ? meters.m[1 + i] : 0;

// ------------------------------------------------------------------ engine bridge
const rig = { node: null, ctx: null, cmdId: 0, log: [] };
window.__rig = rig;
function logLine(text, err) {
  const el = $('log'); const d = document.createElement('div'); d.textContent = text; if (err) d.className = 'err';
  el.appendChild(d); while (el.childNodes.length > 200) el.removeChild(el.firstChild); el.scrollTop = el.scrollHeight;
}
rig.cmd = function (line) {
  rig.log.push(line);
  if (!rig.node) return;
  rig.node.port.postMessage({ type: 'cmd', id: ++rig.cmdId, line });
};
let quietLog = { last: '', t: 0 };
function onEngineMsg(d) {
  if (d.type === 'meters') { meters = d; rig.meters = d; return; }
  if (d.type === 'resp') {
    const k = d.line.split(' ').slice(0, 3).join(' ');
    const now = performance.now();
    if (d.status !== 0) logLine(d.line + '  ->  resp ' + d.status, true);
    else if (k !== quietLog.last || now - quietLog.t > 400) { logLine(d.line + '  ->  resp 0'); quietLog = { last: k, t: now }; }
    rig.lastResp = d;
    return;
  }
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

// ------------------------------------------------------------------ audio setup
let inputNode = null, mediaStream = null, loopSrc = null, riffBuf = null;
function b64ToBytes(b64) { const s = atob(b64); const u = new Uint8Array(s.length); for (let i = 0; i < s.length; i++) u[i] = s.charCodeAt(i); return u; }

async function startAudio(source) {
  const ctx = new AudioContext({ latencyHint: 0 });
  rig.ctx = ctx;
  // A data: URL works when the page is opened straight from disk (file://), where blob: worklets are refused.
  const src = $('worklet-src').textContent;
  const url = 'data:text/javascript;base64,' + btoa(unescape(encodeURIComponent(src)));
  await ctx.audioWorklet.addModule(url);
  const wasm = b64ToBytes(RIG_WASM_B64);
  const node = new AudioWorkletNode(ctx, 'rig-engine', {
    numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [2],
    channelCount: 2, channelCountMode: 'explicit', channelInterpretation: 'discrete',
    processorOptions: { wasm: wasm.buffer },
  });
  node.port.onmessage = (e) => onEngineMsg(e.data);
  node.connect(ctx.destination);
  rig.node = node;
  sendAll();
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

// ------------------------------------------------------------------ three.js scene
const canvas = $('stage');
const renderer = new THREE.WebGLRenderer({ canvas, antialias: true });
renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 1.5));
const scene = new THREE.Scene();
scene.background = new THREE.Color(0x05070c);
scene.fog = new THREE.FogExp2(0x05070c, 0.026);
const camera = new THREE.PerspectiveCamera(48, 1, 0.1, 300);
const grid = new THREE.GridHelper(140, 140, 0x1d6a73, 0x122f3a); grid.material.transparent = true; grid.material.opacity = .55; scene.add(grid);
{ const n = 700, pos = new Float32Array(n * 3);
  for (let i = 0; i < n; i++) { const r = 60 + Math.random() * 40, th = Math.random() * Math.PI * 2, ph = Math.random() * Math.PI * .5;
    pos[i * 3] = r * Math.cos(th) * Math.sin(ph); pos[i * 3 + 1] = r * Math.cos(ph) + 4; pos[i * 3 + 2] = r * Math.sin(th) * Math.sin(ph); }
  const g = new THREE.BufferGeometry(); g.setAttribute('position', new THREE.BufferAttribute(pos, 3));
  scene.add(new THREE.Points(g, new THREE.PointsMaterial({ color: 0x6f86b0, size: .18, fog: false }))); }
function rr(c, x, y, w, h, r) { c.beginPath(); c.moveTo(x + r, y); c.arcTo(x + w, y, x + w, y + h, r); c.arcTo(x + w, y + h, x, y + h, r); c.arcTo(x, y + h, x, y, r); c.arcTo(x, y, x + w, y, r); c.closePath(); }
function mkTex(cv) { const t = new THREE.CanvasTexture(cv); t.minFilter = THREE.LinearFilter; t.generateMipmaps = false; return t; }

const FW = 512, FH = 602, PW = 3.4, PH = 4.0, KY = 400, KX = [128, 384];
const modPos = [[-8, -1.6], [-4, .2], [0, .9], [4, .2], [8, -1.6]];
const pickables = [];
MODS.forEach((m, i) => {
  const g = new THREE.Group(); g.position.set(modPos[i][0], 2.8, modPos[i][1]); g.lookAt(0, 2.8, 22);
  const slab = new THREE.Mesh(new THREE.BoxGeometry(PW, PH, .18), new THREE.MeshBasicMaterial({ color: 0x0a0e17 })); g.add(slab);
  const edges = new THREE.LineSegments(new THREE.EdgesGeometry(slab.geometry), new THREE.LineBasicMaterial({ color: 0x2a3550 })); g.add(edges);
  const cv = document.createElement('canvas'); cv.width = FW; cv.height = FH; const tex = mkTex(cv);
  const face = new THREE.Mesh(new THREE.PlaneGeometry(PW, PH), new THREE.MeshBasicMaterial({ map: tex, transparent: true })); face.position.z = .1; g.add(face);
  const glow = new THREE.Mesh(new THREE.RingGeometry(1.2, 2.2, 48), new THREE.MeshBasicMaterial({ color: ACC, transparent: true, opacity: .08, side: THREE.DoubleSide, depthWrite: false }));
  glow.rotation.x = -Math.PI / 2; glow.position.set(modPos[i][0], .02, modPos[i][1]);
  scene.add(glow, g);
  Object.assign(m, { group: g, face, edges, glow, ctx: cv.getContext('2d'), tex, idx: i });
  face.userData = { type: 'face', mod: m }; pickables.push(face);
});
function knobWorld(p) { const i = p.pi; return p.mod.face.localToWorld(new THREE.Vector3((KX[i] / FW - .5) * PW, (.5 - KY / FH) * PH, .12)); }

const flowPts = [new THREE.Vector3(-12.5, 1.0, -3.2)];
MODS.forEach((m, i) => flowPts.push(new THREE.Vector3(modPos[i][0], .55, modPos[i][1] + .6)));
flowPts.push(new THREE.Vector3(12.5, 1.0, -3.2));
const flowCurve = new THREE.CatmullRomCurve3(flowPts);
scene.add(new THREE.Mesh(new THREE.TubeGeometry(flowCurve, 160, .05, 8, false), new THREE.MeshBasicMaterial({ color: ACC, transparent: true, opacity: .35 })));
const NP = 70, flowPos = new Float32Array(NP * 3), flowGeo = new THREE.BufferGeometry();
flowGeo.setAttribute('position', new THREE.BufferAttribute(flowPos, 3));
const flowMat = new THREE.PointsMaterial({ color: ACC, size: .22, transparent: true, opacity: .95 });
const flowDots = new THREE.Points(flowGeo, flowMat); flowDots.frustumCulled = false; scene.add(flowDots);
function pill(txt) { const cv = document.createElement('canvas'); cv.width = 128; cv.height = 64; const s = new THREE.Sprite(new THREE.SpriteMaterial({ map: mkTex(cv), transparent: true })); s.scale.set(1.4, .7, 1); return { s, cv, txt }; }
function drawPill(l) { const c = l.cv.getContext('2d'); c.clearRect(0, 0, 128, 64); rr(c, 4, 4, 120, 56, 28); c.fillStyle = 'rgba(15,21,36,.9)'; c.fill(); c.strokeStyle = '#2A3550'; c.lineWidth = 3; c.stroke(); c.fillStyle = '#E6ECF5'; c.font = '600 26px "IBM Plex Mono", monospace'; c.textAlign = 'center'; c.textBaseline = 'middle'; c.fillText(l.txt, 64, 33); l.s.material.map.needsUpdate = true; }
const inLbl = pill('IN'), outLbl = pill('OUT'); inLbl.s.position.set(-12.5, 1.9, -3.2); outLbl.s.position.set(12.5, 1.9, -3.2); scene.add(inLbl.s, outLbl.s);

const srcX = [-10, -6, -2, 2, 6, 10];
SRC.forEach((s, i) => {
  const col = new THREE.Color(s.color);
  const orb = new THREE.Mesh(new THREE.IcosahedronGeometry(.42, 1), new THREE.MeshBasicMaterial({ color: col })); orb.position.set(srcX[i], 7.6, -4.2);
  const ring = new THREE.Mesh(new THREE.TorusGeometry(.75, .03, 8, 48), new THREE.MeshBasicMaterial({ color: col, transparent: true, opacity: .7 })); ring.position.copy(orb.position);
  const halo = new THREE.Mesh(new THREE.SphereGeometry(.9, 16, 12), new THREE.MeshBasicMaterial({ color: col, transparent: true, opacity: .1, depthWrite: false })); halo.position.copy(orb.position);
  const cv = document.createElement('canvas'); cv.width = 256; cv.height = 128; const tex = mkTex(cv);
  const lbl = new THREE.Sprite(new THREE.SpriteMaterial({ map: tex, transparent: true })); lbl.scale.set(3.2, 1.6, 1); lbl.position.set(srcX[i], 9.3, -4.2);
  scene.add(orb, ring, halo, lbl);
  Object.assign(s, { orb, ring, halo, lbl, ctx: cv.getContext('2d'), tex, hist: [], idx: i });
  orb.userData = lbl.userData = halo.userData = { type: 'src', src: s }; pickables.push(orb, halo, lbl);
});

let cables = []; const CN = 48;
function bez(a, b) { return new THREE.CubicBezierCurve3(a, a.clone().add(new THREE.Vector3(0, -3.5, 1.5)), b.clone().add(new THREE.Vector3(0, 3, 2.5)), b); }
function syncCables() {
  cables.forEach((c) => { scene.remove(c.line); c.pulses.forEach((p) => scene.remove(p)); });
  cables = Object.keys(state.conns).map((pid) => {
    const s = SRCMAP[state.conns[pid]], col = new THREE.Color(s.color);
    const geo = new THREE.BufferGeometry(); geo.setAttribute('position', new THREE.BufferAttribute(new Float32Array(CN * 3), 3));
    const line = new THREE.Line(geo, new THREE.LineBasicMaterial({ color: col, transparent: true, opacity: .85 })); line.frustumCulled = false; scene.add(line);
    const pulses = [0, 1, 2].map(() => { const p = new THREE.Mesh(new THREE.SphereGeometry(.1, 10, 8), new THREE.MeshBasicMaterial({ color: col })); scene.add(p); return p; });
    return { pid, s, line, pulses, ph: 0 };
  });
}
const carryGeo = new THREE.BufferGeometry(); carryGeo.setAttribute('position', new THREE.BufferAttribute(new Float32Array(CN * 3), 3));
const carryLine = new THREE.Line(carryGeo, new THREE.LineDashedMaterial({ color: 0xffffff, dashSize: .25, gapSize: .18 })); carryLine.frustumCulled = false; carryLine.visible = false; scene.add(carryLine);
const pointerWorld = new THREE.Vector3(0, 4, 0);

// ------------------------------------------------------------------ face drawing
let irResp = null;           // log-spaced magnitude (dB) of a loaded IR, or null for built-in
function arcDeg(c, cx, cy, r, a0, a1, col, w) { c.beginPath(); c.arc(cx, cy, r, (a0 - 90) * Math.PI / 180, (Math.max(a1, a0 + .5) - 90) * Math.PI / 180); c.strokeStyle = col; c.lineWidth = w; c.lineCap = 'round'; c.stroke(); }
function drawKnob(c, cx, cy, p) {
  const R = 64, a = -135 + 270 * p.v, e = effOf(p), sid = state.conns[p.id], S = sid && SRCMAP[sid];
  if (state.carrying) { c.save(); c.setLineDash([10, 10]); c.beginPath(); c.arc(cx, cy, 100, 0, Math.PI * 2); c.strokeStyle = SRCMAP[state.carrying].color; c.lineWidth = 4; c.stroke(); c.restore(); }
  arcDeg(c, cx, cy, R, -135, 135, '#263149', 10);
  arcDeg(c, cx, cy, R, -135, a, ACC, 10);
  if (S) {
    const lo = clamp(p.v - DEPTH, 0, 1), hi = clamp(p.v + DEPTH, 0, 1);
    const ga = c.globalAlpha; c.globalAlpha = ga * .5; arcDeg(c, cx, cy, R + 19, -135 + 270 * lo, -135 + 270 * hi, S.color, 7); c.globalAlpha = ga;
    const ea = (-135 + 270 * e - 90) * Math.PI / 180; c.beginPath(); c.arc(cx + (R + 19) * Math.cos(ea), cy + (R + 19) * Math.sin(ea), 9, 0, Math.PI * 2); c.fillStyle = S.color; c.fill();
  }
  c.beginPath(); c.arc(cx, cy, 44, 0, Math.PI * 2); c.fillStyle = '#151C2E'; c.fill(); c.strokeStyle = '#2A3550'; c.lineWidth = 2; c.stroke();
  const pa = (-135 + 270 * e - 90) * Math.PI / 180;
  c.beginPath(); c.moveTo(cx + 14 * Math.cos(pa), cy + 14 * Math.sin(pa)); c.lineTo(cx + 38 * Math.cos(pa), cy + 38 * Math.sin(pa)); c.strokeStyle = '#E6ECF5'; c.lineWidth = 6; c.lineCap = 'round'; c.stroke();
  c.textAlign = 'center'; c.fillStyle = '#E6ECF5'; c.font = '700 25px "Chakra Petch", sans-serif'; c.fillText(p.label, cx, cy + 112);
  c.fillStyle = S ? S.color : '#94A3BD'; c.font = '500 21px "IBM Plex Mono", monospace'; c.fillText(p.fmt(e), cx, cy + 144); c.textAlign = 'left';
}
let T = 0;
function drawScreen(m, c) {
  const x = 34, y = 132, w = 444, h = 150;
  c.save(); rr(c, x, y, w, h, 14); c.fillStyle = '#070B14'; c.fill(); c.strokeStyle = '#1F2A40'; c.lineWidth = 2; c.stroke(); c.clip();
  const [p0, p1] = m.params, e0 = effOf(p0), e1 = effOf(p1);
  c.strokeStyle = ACC; c.fillStyle = ACC; c.lineWidth = 3; c.font = '500 18px "IBM Plex Mono", monospace';
  if (m.id === 'gate') {
    const inDb = meters ? 20 * Math.log10(meters.inPk + 1e-9) : -100, lvl = clamp((inDb + 80) / 80, 0, 1), th = e0;
    const open = meters && meters.m[0] > .5;
    c.fillStyle = '#1C2538'; c.fillRect(x + 20, y + 54, w - 40, 30);
    c.fillStyle = open ? ACC : '#3A4766'; c.fillRect(x + 20, y + 54, (w - 40) * lvl, 30);
    c.fillStyle = '#FFB547'; c.fillRect(x + 20 + (w - 40) * (th * 60 / 80) - 2, y + 36, 4, 66);
    c.fillStyle = open ? '#9BE15D' : '#94A3BD'; c.fillText(open ? 'OPEN' : 'CLOSED', x + 20, y + 128);
    c.fillStyle = '#94A3BD'; c.textAlign = 'right'; c.fillText(meters ? inDb.toFixed(0) + ' dBFS in' : 'no signal', x + w - 20, y + 128); c.textAlign = 'left';
  } else if (m.id === 'amp') {
    const k = Math.pow(10, (6 + 36 * e0) / 20) * 0.08;
    c.beginPath(); for (let i = 0; i <= 100; i++) { const u = i / 50 - 1, out = Math.tanh(k * u + .15) - Math.tanh(.15), py = y + h / 2 - out * (h / 2 - 16) * .9, px = x + 20 + (w - 40) * i / 100; i ? c.lineTo(px, py) : c.moveTo(px, py); } c.stroke();
    c.fillStyle = '#94A3BD'; c.fillText('NAM A2 drops in here', x + 20, y + 26);
  } else if (m.id === 'cab') {
    c.beginPath();
    for (let i = 0; i <= 63; i++) {
      const f = i / 63; let g;
      if (irResp) g = clamp((irResp[i] + 30) / 36, 0, 1);
      else { const hz = 40 * Math.pow(400, f); g = clamp((10 * Math.log10(1 / (1 + Math.pow(hz / 5500, 4))) + 4 * Math.exp(-Math.pow(Math.log(hz / 115), 2) * 4) + 4 * Math.exp(-Math.pow(Math.log(hz / 2600), 2) * 5) + 30) / 36, 0, 1); }
      const lc = 20 * Math.pow(10, 1.3 * e0), hz = 40 * Math.pow(400, f), hp = hz < lc ? Math.pow(hz / lc, 2) : 1;
      const px = x + 16 + (w - 32) * f, py = y + h - 14 - g * hp * (h - 40); i ? c.lineTo(px, py) : c.moveTo(px, py);
    }
    c.stroke(); c.fillStyle = '#94A3BD'; c.fillText(irResp ? 'loaded IR' : 'built-in cab', x + 20, y + 26);
  } else if (m.id === 'holo') {
    const n = 12, fb = e0 * .95;
    for (let k = 0; k < n; k++) { const amp = Math.pow(fb * .95 + .02, k); const sh = e1 * .25 * Math.sin(T * 4 + k); const bx = x + 18 + k * ((w - 36) / n); const bh = (h - 44) * amp * (1 + sh); c.globalAlpha = .35 + .65 * amp; c.fillRect(bx, y + h - 12 - bh, 16, bh); }
    c.globalAlpha = 1; c.fillStyle = '#94A3BD'; c.fillText((meters ? meters.m[20] : 375).toFixed(0) + ' ms', x + 20, y + 26);
  } else {
    const V = []; for (let i = 0; i < 16; i++) V.push([(i & 1) ? 1 : -1, (i & 2) ? 1 : -1, (i & 4) ? 1 : -1, (i & 8) ? 1 : -1]);
    const a = T * (.3 + e1 * .6), b = T * .4 + e1 * Math.PI, sc = 110 + 50 * e0;
    const P = V.map(([X, Y, Z, W]) => { const x1 = X * Math.cos(a) - W * Math.sin(a), w1 = X * Math.sin(a) + W * Math.cos(a); const y1 = Y * Math.cos(b) - Z * Math.sin(b), z1 = Y * Math.sin(b) + Z * Math.cos(b); const d4 = 1 / (2.6 - w1); const X3 = x1 * d4, Y3 = y1 * d4, Z3 = z1 * d4; const r = T * .5; const X4 = X3 * Math.cos(r) - Z3 * Math.sin(r), Z4 = X3 * Math.sin(r) + Z3 * Math.cos(r); const d3 = 1 / (2.4 - Z4); return [x + w / 2 + X4 * d3 * sc, y + h / 2 + Y3 * d3 * sc]; });
    c.lineWidth = 2; c.beginPath(); for (let i = 0; i < 16; i++) for (let j = 0; j < 4; j++) { const k = i ^ (1 << j); if (k > i) { c.moveTo(P[i][0], P[i][1]); c.lineTo(P[k][0], P[k][1]); } } c.stroke();
  }
  c.restore();
}
function drawFace(m) {
  const c = m.ctx, byp = !!state.bypass[m.id], foc = state.focus === m.id;
  c.globalAlpha = 1; c.clearRect(0, 0, FW, FH);
  rr(c, 6, 6, FW - 12, FH - 12, 28); c.fillStyle = '#0C1220'; c.fill(); c.lineWidth = foc ? 6 : 3; c.strokeStyle = foc ? ACC : '#2A3550'; c.stroke();
  c.globalAlpha = byp ? .42 : 1; c.textBaseline = 'alphabetic';
  c.beginPath(); c.arc(44, 56, 10, 0, Math.PI * 2); c.fillStyle = byp ? '#3A4766' : '#9BE15D'; c.fill();
  c.fillStyle = '#E6ECF5'; c.font = '700 38px "Chakra Petch", sans-serif'; c.fillText(m.name, 66, 70);
  c.fillStyle = '#94A3BD'; c.font = '400 21px "IBM Plex Mono", monospace'; c.fillText(m.kind, 34, 112);
  drawScreen(m, c); m.params.forEach((p, i) => drawKnob(c, KX[i], KY, p));
  c.globalAlpha = 1; m.tex.needsUpdate = true;
}
function drawLabel(s) {
  const c = s.ctx, on = state.carrying === s.id;
  c.clearRect(0, 0, 256, 128); rr(c, 3, 3, 250, 122, 16); c.fillStyle = 'rgba(10,14,23,.88)'; c.fill(); c.strokeStyle = s.color; c.lineWidth = on ? 5 : 2; c.stroke();
  c.fillStyle = s.color; c.font = '700 26px "Chakra Petch", sans-serif'; c.fillText(s.name, 16, 34);
  c.fillStyle = '#94A3BD'; c.font = '400 16px "IBM Plex Mono", monospace'; c.fillText(on ? 'carrying · pick a knob' : s.sub, 16, 58);
  c.beginPath(); s.hist.forEach((v, i) => { const px = 16 + i * (224 / 119), py = 96 - v * 20; i ? c.lineTo(px, py) : c.moveTo(px, py); }); c.strokeStyle = s.color; c.lineWidth = 2.5; c.stroke();
  s.tex.needsUpdate = true;
}

// ------------------------------------------------------------------ camera and input
const cam = { theta: 0, phi: 1.12, r: 30, target: new THREE.Vector3(0, 3.6, 0) };
const camGoal = { r: 21, target: new THREE.Vector3(0, 3.6, 0) };
function placeCamera() { const t = cam.target; camera.position.set(t.x + cam.r * Math.sin(cam.phi) * Math.sin(cam.theta), t.y + cam.r * Math.cos(cam.phi), t.z + cam.r * Math.sin(cam.phi) * Math.cos(cam.theta)); camera.lookAt(t); }
function flyTo(m) { camGoal.target.set(m.group.position.x, 2.8, m.group.position.z); camGoal.r = 11; cam.theta = Math.atan2(m.group.position.x, 22) * .9; }
function overview() { camGoal.target.set(0, 3.6, 0); camGoal.r = 21; cam.theta = 0; cam.phi = 1.12; }
const ray = new THREE.Raycaster(), ndc = new THREE.Vector2();
function setNdc(e) { const r = canvas.getBoundingClientRect(); ndc.x = ((e.clientX - r.left) / r.width) * 2 - 1; ndc.y = -((e.clientY - r.top) / r.height) * 2 + 1; }
function pick(e) {
  setNdc(e); ray.setFromCamera(ndc, camera);
  const hit = ray.intersectObjects(pickables, false)[0]; if (!hit) return null;
  const u = hit.object.userData; if (u.type === 'src') return { type: 'src', src: u.src };
  const m = u.mod, px = hit.uv.x * FW, py = (1 - hit.uv.y) * FH; let knob = null;
  m.params.forEach((p, i) => { if (Math.hypot(px - KX[i], py - KY) < 100) knob = p; });
  return { type: 'face', mod: m, knob };
}
function setParam(p, v) { p.v = clamp(v, 0, 1); rig.cmd(`param_set ${p.mod.inst} ${p.sym} ${p.v.toFixed(4)}`); }
function connect(pid, sid) {
  const p = PARAMS[pid];
  if (sid) { state.conns[pid] = sid; rig.cmd(`mod_connect ${p.mod.inst} ${p.sym} ${sid} ${DEPTH}`); }
  else { delete state.conns[pid]; rig.cmd(`mod_disconnect ${p.mod.inst} ${p.sym}`); }
  syncCables();
}
let drag = null;
canvas.addEventListener('pointerdown', (e) => {
  canvas.setPointerCapture(e.pointerId); const h = pick(e);
  drag = { x: e.clientX, y: e.clientY, moved: false, hit: h, mode: 'orbit' };
  if (h && h.type === 'face' && h.knob && !state.carrying) { drag.mode = 'knob'; drag.start = h.knob.v; }
});
canvas.addEventListener('pointermove', (e) => {
  setNdc(e);
  if (!drag) { const h = pick(e); canvas.style.cursor = h ? 'pointer' : (state.carrying ? 'crosshair' : 'grab'); return; }
  const dy = e.clientY - drag.y; if (Math.hypot(e.clientX - drag.x, dy) > 5) drag.moved = true;
  if (drag.mode === 'knob') { if (drag.moved) { setParam(drag.hit.knob, drag.start - dy / 220); state.focus = drag.hit.mod.id; renderPanelSoon(); } }
  else if (drag.moved) { cam.theta -= e.movementX * .005; cam.phi = clamp(cam.phi - e.movementY * .004, .45, 1.45); canvas.style.cursor = 'grabbing'; }
});
canvas.addEventListener('pointerup', () => { if (drag && !drag.moved) click(drag.hit); drag = null; canvas.style.cursor = 'grab'; });
canvas.addEventListener('wheel', (e) => { e.preventDefault(); cam.r = camGoal.r = clamp(cam.r * (1 + e.deltaY * .001), 6, 34); }, { passive: false });
function click(h) {
  if (!h) { if (state.carrying) { state.carrying = null; update(); } return; }
  if (h.type === 'src') { state.carrying = state.carrying === h.src.id ? null : h.src.id; update(); return; }
  if (h.knob && state.carrying) { connect(h.knob.id, state.conns[h.knob.id] === state.carrying ? null : state.carrying); state.carrying = null; state.focus = h.mod.id; update(); return; }
  state.focus = h.mod.id; flyTo(h.mod); update();
}
window.addEventListener('keydown', (e) => {
  if (e.target.closest && e.target.closest('input,textarea,select')) return;
  if (e.key === 'Escape' && state.carrying) { state.carrying = null; update(); }
  const n = parseInt(e.key, 10); if (n >= 1 && n <= 5) { state.focus = MODS[n - 1].id; flyTo(MODS[n - 1]); update(); }
});

// ------------------------------------------------------------------ HUD
const panel = $('panel'), matrix = $('matrix'), hint = $('hint');
function renderPanel() {
  const m = MODS.find((x) => x.id === state.focus), byp = !!state.bypass[m.id];
  let h = `<div><div class="eyebrow">Module ${m.idx + 1}/5 · instance ${m.inst}</div><h2>${m.name}</h2><div class="kind">${m.kind}</div></div>
  <div class="row"><button class="btn ${byp ? 'byp' : 'on'}" data-act="bypass" aria-pressed="${byp}">${byp ? 'Bypassed' : 'Active'}</button></div>`;
  if (m.id === 'amp') h += `<p class="note">Built-in waveshaper amp (2x oversampled, anti-aliased). The NAM A2 engine replaces this slot next.</p>`;
  if (m.id === 'cab') h += `<p class="note">Load your own cab IR from Settings.</p>`;
  h += `<div class="params">`;
  m.params.forEach((p) => {
    const sid = state.conns[p.id], S = sid && SRCMAP[sid];
    h += `<div class="param"><div class="prow"><b><label for="r-${p.id}" style="color:inherit;font:inherit;display:inline">${p.label}</label></b><span class="num" data-disp="${p.id}"></span></div>
    <input type="range" id="r-${p.id}" data-pid="${p.id}" min="0" max="1" step="0.001" value="${p.v}">`;
    if (S) h += `<div class="mod"><span class="dot" style="background:${S.color}"></span><span class="grow">${S.name} <span class="muted mono">±${Math.round(DEPTH * 100)}%</span></span><button class="btn" data-act="unplug" data-pid="${p.id}">Unplug</button></div>`;
    else h += `<div class="mod"><label for="m-${p.id}" class="muted" style="flex-direction:row;align-items:center;gap:8px;font-size:13px">Patch <select id="m-${p.id}" data-patch="${p.id}" style="min-height:32px;width:auto"><option value="">—</option>${SRC.map((s) => `<option value="${s.id}">${s.name}</option>`).join('')}</select></label></div>`;
    h += `</div>`;
  });
  h += `</div><div class="hw"><span class="eyebrow">Text protocol</span><span class="mono" style="font-size:12px">param_set ${m.inst} ${m.params[0].sym} 0.5<br>bypass ${m.inst} 1</span></div>`;
  panel.innerHTML = h;
}
let panelPending = false;
function renderPanelSoon() { if (panelPending) return; panelPending = true; requestAnimationFrame(() => { panelPending = false; renderPanel(); }); }
function renderMatrix() {
  const cols = []; MODS.forEach((m) => m.params.forEach((p) => cols.push(p)));
  let h = `<div class="eyebrow" style="margin-bottom:12px">Source → target</div><div style="overflow-x:auto"><table><thead><tr><th></th>`;
  cols.forEach((p) => h += `<th scope="col"><small>${p.mod.name}</small>${p.label}</th>`);
  h += `</tr></thead><tbody>`;
  SRC.forEach((s) => {
    h += `<tr><th scope="row"><b style="color:${s.color}">${s.name}</b><small>${s.sub}</small></th>`;
    cols.forEach((p) => { const on = state.conns[p.id] === s.id;
      h += `<td><button class="cell" data-pid="${p.id}" data-sid="${s.id}" aria-pressed="${on}" aria-label="${s.name} to ${p.mod.name} ${p.label}${on ? ', patched' : ''}" style="${on ? `background:${s.color};border-color:${s.color};box-shadow:0 0 16px ${s.color}88` : ''}"></button></td>`; });
    h += `</tr>`;
  });
  h += `</tbody></table></div><p class="note" style="margin-top:14px">Each knob takes one source at ±${Math.round(DEPTH * 100)}%. Click a dot to patch or unpatch.</p>`;
  matrix.innerHTML = h;
}
function setHint() {
  if (state.storing) { hint.className = 'hint carry'; hint.textContent = 'Store mode: click a scene letter to save the current settings into it'; return; }
  if (state.carrying) { const s = SRCMAP[state.carrying]; hint.className = 'hint carry'; hint.innerHTML = `<span style="color:${s.color}">Carrying ${s.name}</span> · click a knob to patch it · Esc to drop`; return; }
  hint.className = 'hint'; hint.textContent = 'Drag to orbit · scroll to zoom · drag a knob to turn it · click an orb, then a knob, to patch · keys 1–5';
}
function update() { renderPanel(); if (state.view === 'matrix') renderMatrix(); setHint(); carryLine.visible = !!state.carrying; if (state.carrying) carryLine.material.color.set(SRCMAP[state.carrying].color); }
panel.addEventListener('click', (e) => {
  const b = e.target.closest('[data-act]'); if (!b) return; const m = MODS.find((x) => x.id === state.focus);
  if (b.dataset.act === 'bypass') { state.bypass[m.id] = !state.bypass[m.id]; rig.cmd(`bypass ${m.inst} ${state.bypass[m.id] ? 1 : 0}`); }
  if (b.dataset.act === 'unplug') connect(b.dataset.pid, null);
  update();
});
panel.addEventListener('input', (e) => { const pid = e.target.dataset && e.target.dataset.pid; if (pid) setParam(PARAMS[pid], Number(e.target.value)); });
panel.addEventListener('change', (e) => { const pid = e.target.dataset && e.target.dataset.patch; if (pid && e.target.value) { connect(pid, e.target.value); update(); } });
matrix.addEventListener('click', (e) => { const b = e.target.closest('.cell'); if (!b) return; const { pid, sid } = b.dataset; connect(pid, state.conns[pid] === sid ? null : sid); update(); });
function setView(v) { state.view = v; state.carrying = null; $('vStage').setAttribute('aria-pressed', v === 'stage'); $('vMatrix').setAttribute('aria-pressed', v === 'matrix'); matrix.hidden = v !== 'matrix'; update(); }
$('vStage').onclick = () => setView('stage'); $('vMatrix').onclick = () => setView('matrix'); $('overview').onclick = overview;
$('settingsBtn').onclick = () => { const d = $('drawer'); d.hidden = !d.hidden; $('settingsBtn').setAttribute('aria-expanded', String(!d.hidden)); };

// bottom bar: scenes, tap, master, controller
function snapshot() { return { params: Object.fromEntries(Object.values(PARAMS).map((p) => [p.id, p.v])), bypass: { ...state.bypass }, conns: { ...state.conns }, bpm: state.bpm }; }
function applySnapshot(s) {
  Object.entries(s.params || {}).forEach(([id, v]) => { if (PARAMS[id]) PARAMS[id].v = v; });
  state.bypass = { ...(s.bypass || {}) }; state.conns = { ...(s.conns || {}) };
  if (s.bpm) { state.bpm = s.bpm; $('bpm').textContent = Math.round(s.bpm); }
  if (s.master !== undefined) { state.master = s.master; $('master').value = s.master; $('masterVal').textContent = s.master + ' dB'; }
  sendAll(); syncCables(); update();
}
document.querySelectorAll('.scene').forEach((b) => b.onclick = () => {
  const id = b.dataset.scene;
  if (state.storing) { state.scenes[id] = snapshot(); state.storing = false; $('storeBtn').setAttribute('aria-pressed', 'false'); b.classList.add('stored'); logLine(`scene ${id} stored`); }
  else if (state.scenes[id]) applySnapshot(state.scenes[id]);
  else logLine(`scene ${id} is empty: press Store, then ${id}`);
  state.scene = state.scenes[id] ? id : state.scene;
  document.querySelectorAll('.scene').forEach((x) => { x.setAttribute('aria-pressed', String(x.dataset.scene === state.scene)); x.classList.toggle('storing', state.storing); });
  setHint();
});
$('storeBtn').onclick = () => { state.storing = !state.storing; $('storeBtn').setAttribute('aria-pressed', String(state.storing)); document.querySelectorAll('.scene').forEach((x) => x.classList.toggle('storing', state.storing)); setHint(); };
let taps = [];
$('tap').onclick = () => { const n = performance.now(); taps = taps.filter((x) => n - x < 3000); taps.push(n);
  if (taps.length > 1) { const d = (taps[taps.length - 1] - taps[0]) / (taps.length - 1); state.bpm = clamp(Math.round(60000 / d), 30, 300); $('bpm').textContent = state.bpm; rig.cmd(`set_bpm ${state.bpm}`); } };
$('master').oninput = (e) => { state.master = Number(e.target.value); $('masterVal').textContent = state.master + ' dB'; rig.cmd(`master ${state.master}`); };
let lastCtrlSent = 0;
function setCtrl(v, fromPad) {
  state.ctrl = v; $('ctrlVal').textContent = v.toFixed(2); if (fromPad) $('ctrlSlider').value = v;
  const now = performance.now(); if (now - lastCtrlSent > 25) { rig.cmd(`ext ctrl ${v.toFixed(3)}`); lastCtrlSent = now; }
}
$('ctrlSlider').oninput = (e) => setCtrl(Number(e.target.value), false);

// settings drawer
$('srcSel').onchange = async (e) => {
  try {
    if (e.target.value === 'file') { $('diFile').click(); return; }
    await setSource(e.target.value, e.target.value === 'device' ? $('inSel').value : undefined);
  } catch (err) { logLine('source error: ' + err.message, true); }
};
$('diFile').onchange = async (e) => { const f = e.target.files[0]; if (!f) return;
  try { const buf = await rig.ctx.decodeAudioData(await f.arrayBuffer()); await setSource(buf); $('srcSel').value = 'file'; logLine('DI file: ' + f.name); }
  catch (err) { logLine('could not decode ' + f.name + ': ' + err.message, true); } };
$('inSel').onchange = async (e) => { try { await setSource('device', e.target.value); } catch (err) { logLine('input error: ' + err.message, true); } };
$('chSel').onchange = (e) => rig.cmd(`input_channel ${e.target.value}`);
$('outSel').onchange = async (e) => { try { await rig.ctx.setSinkId(e.target.value); logLine('output: ' + e.target.selectedOptions[0].textContent); } catch (err) { logLine('output error: ' + err.message, true); } };
$('irLoad').onclick = () => $('irFile').click();
$('irFile').onchange = async (e) => { const f = e.target.files[0]; if (!f || !rig.ctx) return;
  try {
    const buf = await rig.ctx.decodeAudioData(await f.arrayBuffer());
    const data = buf.getChannelData(0).slice(0, 2048);
    rig.node.port.postMessage({ type: 'ir', data }, [data.buffer]);
    irResp = []; const sr = buf.sampleRate, d = buf.getChannelData(0), n = Math.min(d.length, 2048); let ref = 0;
    for (let i = 0; i <= 63; i++) { const hz = 40 * Math.pow(400, i / 63); let re = 0, im = 0; for (let k = 0; k < n; k++) { const ph = -2 * Math.PI * hz * k / sr; re += d[k] * Math.cos(ph); im += d[k] * Math.sin(ph); } irResp.push(10 * Math.log10(re * re + im * im + 1e-12)); }
    ref = Math.max(...irResp); irResp = irResp.map((v) => v - ref);
    $('irName').textContent = `${f.name} · ${Math.min(buf.length, 2048)} taps${buf.length > 2048 ? ' (first 2048 used)' : ''}`;
    MODS[2].kind = 'IR · ' + f.name.slice(0, 18);
  } catch (err) { logLine('could not load IR: ' + err.message, true); } };
$('irReset').onclick = () => { if (!rig.node) return; rig.node.port.postMessage({ type: 'ir_default' }); irResp = null; $('irName').textContent = 'Built-in synthetic cab (21 ms)'; MODS[2].kind = 'built-in cab'; };
$('heartBtn').onclick = async () => {
  if (!navigator.bluetooth) { $('heartNote').textContent = 'This browser has no Web Bluetooth. Use Chrome or Edge on Windows, Mac or Android.'; return; }
  try {
    const dev = await navigator.bluetooth.requestDevice({ filters: [{ services: ['heart_rate'] }] });
    const ch = await (await (await dev.gatt.connect()).getPrimaryService('heart_rate')).getCharacteristic('heart_rate_measurement');
    await ch.startNotifications();
    ch.addEventListener('characteristicvaluechanged', (ev) => { const v = ev.target.value, bpm = (v.getUint8(0) & 1) ? v.getUint16(1, true) : v.getUint8(1); rig.cmd(`ext heart ${bpm}`); $('heartNote').textContent = `Heart source: ${dev.name || 'strap'} · ${bpm} bpm`; SRCMAP.heart.sub = `${bpm} bpm · live`; });
    $('heartNote').textContent = `Connected to ${dev.name || 'strap'}; waiting for the first beat…`;
  } catch (err) { $('heartNote').textContent = /permissions policy/i.test(err.message) ? 'Bluetooth is blocked where this page is open. Open the file directly in Chrome.' : 'Strap not connected: ' + err.message; }
};
// presets (this browser only; Export/Import for files)
const PKEY = 'rig01.presets';
function readPresets() { try { return JSON.parse(localStorage.getItem(PKEY) || '{}'); } catch (e) { return {}; } }
function writePresets(p) { try { localStorage.setItem(PKEY, JSON.stringify(p)); } catch (e) { logLine('presets could not be saved in this browser', true); } }
function refreshPresetList() { const p = readPresets(), sel = $('presetSel'); sel.innerHTML = ''; Object.keys(p).forEach((k) => { const o = document.createElement('option'); o.value = k; o.textContent = k; sel.appendChild(o); }); }
$('presetSave').onclick = () => { const name = $('presetInput').value.trim() || 'Untitled'; const p = readPresets(); p[name] = { ...snapshot(), master: state.master, scenes: state.scenes }; writePresets(p); refreshPresetList(); $('presetSel').value = name; $('presetName').textContent = name; logLine('preset saved: ' + name); };
$('presetLoad').onclick = () => { const name = $('presetSel').value, p = readPresets()[name]; if (!p) return; state.scenes = p.scenes || {}; document.querySelectorAll('.scene').forEach((x) => x.classList.toggle('stored', !!state.scenes[x.dataset.scene])); applySnapshot(p); $('presetName').textContent = name; $('presetInput').value = name; logLine('preset loaded: ' + name); };
$('presetExport').onclick = () => { const name = $('presetInput').value.trim() || 'preset'; const blob = new Blob([JSON.stringify({ name, ...snapshot(), master: state.master, scenes: state.scenes }, null, 2)], { type: 'application/json' }); const a = document.createElement('a'); a.href = URL.createObjectURL(blob); a.download = name.replace(/[^\w-]+/g, '_') + '.rig.json'; a.click(); };
$('presetImport').onclick = () => $('presetFile').click();
$('presetFile').onchange = async (e) => { const f = e.target.files[0]; if (!f) return; try { const p = JSON.parse(await f.text()); const all = readPresets(); all[p.name || f.name] = p; writePresets(all); refreshPresetList(); logLine('imported ' + (p.name || f.name)); } catch (err) { logLine('not a preset file: ' + err.message, true); } };
$('cmdSend').onclick = () => { const v = $('cmdIn').value.trim(); if (v) { rig.cmd(v); $('cmdIn').value = ''; } };
$('cmdIn').onkeydown = (e) => { if (e.key === 'Enter') $('cmdSend').click(); };

// start overlay
$('startBtn').onclick = async () => {
  const src = document.querySelector('input[name=src]:checked').value;
  $('startBtn').disabled = true; $('startBtn').textContent = 'Starting…'; $('startErr').hidden = true;
  try {
    await startAudio(src);
    $('startOverlay').hidden = true;
  } catch (err) {
    $('startBtn').disabled = false; $('startBtn').textContent = 'Start audio';
    $('startErr').hidden = false;
    const blocked = err.name === 'NotAllowedError' || /permissions policy|not allowed/i.test(err.message);
    $('startErr').textContent = blocked
      ? 'Microphone access is blocked here. If you are viewing this inside an app preview, download rig-test.html and open it in Chrome. Otherwise allow the microphone in the address bar, or choose the built-in test riff.'
      : 'Audio could not start: ' + err.message;
    if (rig.ctx) { try { rig.ctx.close(); } catch (e) {} rig.ctx = null; rig.node = null; }
  }
};

// ------------------------------------------------------------------ resize and loop
function resize() {
  const w = innerWidth, h = innerHeight; renderer.setSize(w, h, false); camera.aspect = w / h; camera.updateProjectionMatrix();
  document.documentElement.style.setProperty('--topH', $('top').offsetHeight + 'px');
  document.documentElement.style.setProperty('--botH', $('bottom').offsetHeight + 'px');
}
addEventListener('resize', resize);
function meterBar(el, pk) { const db = 20 * Math.log10(pk + 1e-9), f = clamp((db + 60) / 60, 0, 1); el.style.width = (f * 100) + '%'; el.className = db > -1 ? 'clip' : db > -9 ? 'hot' : ''; }
const clock = new THREE.Clock(); let frame = 0, lastMeterSeq = null; const tmp = new THREE.Vector3();
// Gamepads (and the mic, Bluetooth, output choice) can be blocked by the page's host, e.g. an
// in-app file preview. Each optional feature fails softly instead of stopping the render loop.
let padBlocked = false;
function readPad() {
  if (padBlocked || !navigator.getGamepads) return null;
  try { const pads = navigator.getGamepads(); return pads ? Array.from(pads).find((p) => p) || null : null; }
  catch (e) { padBlocked = true; $('padNote').textContent = 'Gamepads are blocked where this page is open. Open the file directly in Chrome to use one; the CONTROLLER slider still works.'; logLine('gamepad blocked: ' + e.message, true); return null; }
}
function loop() {
  requestAnimationFrame(loop);              // schedule first, so one bad frame can never freeze the stage
  const dt = Math.min(clock.getDelta(), .05); T += dt; frame++;
  cam.r += (camGoal.r - cam.r) * .06; cam.target.lerp(camGoal.target, .06); placeCamera();
  // gamepad -> controller source (left stick up = +1)
  const pad = readPad();
  if (pad) { const v = -(pad.axes[1] || 0); if (Math.abs(v - state.ctrl) > 0.01) setCtrl(clamp(Math.abs(v) < 0.06 ? 0 : v, -1, 1), true); if (frame % 60 === 0) $('padNote').textContent = 'Gamepad: ' + pad.id.slice(0, 48) + ' · left stick → CONTROLLER'; }
  if (meters !== lastMeterSeq) { lastMeterSeq = meters; SRC.forEach((s, i) => { s.hist.push(srcVal(i)); if (s.hist.length > 120) s.hist.shift(); }); }
  const level = meters ? clamp((20 * Math.log10(meters.outPk + 1e-9) + 50) / 50, 0, 1) : 0;
  flowMat.opacity = .25 + .75 * level; flowMat.size = .14 + .2 * level;
  MODS.forEach((m, i) => { m.group.position.y = 2.8 + (RM ? 0 : .09 * Math.sin(T * .9 + i * 1.3)); const foc = state.focus === m.id; m.edges.material.color.set(foc ? ACC : '#2A3550'); m.glow.material.opacity = foc ? .22 : .07; });
  SRC.forEach((s, i) => { const v = srcVal(i), k = 1 + .22 * v; s.orb.scale.setScalar(k); s.halo.scale.setScalar(state.carrying === s.id ? 1.5 + .2 * Math.sin(T * 8) : k); if (!RM) { s.ring.rotation.x = T * .7; s.ring.rotation.y = T * .5; } });
  if (frame % 2 === 0) { MODS.forEach(drawFace); SRC.forEach(drawLabel); }
  for (let i = 0; i < NP; i++) { const u = ((i / NP) + T * (RM ? .01 : .05 + .1 * level)) % 1; flowCurve.getPointAt(u, tmp); flowPos[i * 3] = tmp.x; flowPos[i * 3 + 1] = tmp.y + .06; flowPos[i * 3 + 2] = tmp.z; }
  flowGeo.attributes.position.needsUpdate = true;
  cables.forEach((c) => {
    const curve = bez(c.s.orb.position.clone(), knobWorld(PARAMS[c.pid])), arr = c.line.geometry.attributes.position.array;
    for (let i = 0; i < CN; i++) { curve.getPoint(i / (CN - 1), tmp); arr[i * 3] = tmp.x; arr[i * 3 + 1] = tmp.y; arr[i * 3 + 2] = tmp.z; }
    c.line.geometry.attributes.position.needsUpdate = true;
    c.ph += dt * (RM ? .05 : .2 + .5 * Math.abs(srcVal(c.s.idx)));
    c.pulses.forEach((p, k) => curve.getPoint((c.ph + k / 3) % 1, p.position));
  });
  if (state.carrying) {
    ray.setFromCamera(ndc, camera); const n = new THREE.Vector3(); camera.getWorldDirection(n);
    ray.ray.intersectPlane(new THREE.Plane().setFromNormalAndCoplanarPoint(n.negate(), cam.target), pointerWorld);
    const curve = bez(SRCMAP[state.carrying].orb.position.clone(), pointerWorld.clone()), arr = carryGeo.attributes.position.array;
    for (let i = 0; i < CN; i++) { curve.getPoint(i / (CN - 1), tmp); arr[i * 3] = tmp.x; arr[i * 3 + 1] = tmp.y; arr[i * 3 + 2] = tmp.z; }
    carryGeo.attributes.position.needsUpdate = true; carryLine.computeLineDistances();
  }
  if (frame % 3 === 0) {
    document.querySelectorAll('[data-disp]').forEach((el) => { const p = PARAMS[el.dataset.disp]; el.textContent = p.fmt(effOf(p)); });
    if (meters) { meterBar($('mIn'), meters.inPk); meterBar($('mOut'), meters.outPk); $('dspLoad').textContent = (meters.load * 100).toFixed(1) + '%'; }
  }
  renderer.render(scene, camera);
}
if (window.top !== window.self) {
  const p = document.createElement('p'); p.className = 'warn';
  p.textContent = 'This looks like an in-app preview, which blocks the microphone and gamepads. For real use, download rig-test.html and open it in Chrome. The built-in test riff works here.';
  document.querySelector('#startOverlay .card').insertBefore(p, document.getElementById('startErr'));
}
syncCables(); resize(); overview(); update(); refreshPresetList();
$('master').value = state.master;
canvas.style.cursor = 'grab';
(document.fonts && document.fonts.ready ? document.fonts.ready : Promise.resolve()).then(() => { drawPill(inLbl); drawPill(outLbl); });
drawPill(inLbl); drawPill(outLbl);
requestAnimationFrame(loop);
})();

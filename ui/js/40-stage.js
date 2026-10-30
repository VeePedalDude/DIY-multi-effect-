// 40-stage.js - three.js stage: module faces on an arc, source orbs above, patch cables, camera, picking.
// Layout adapts to however many modules, sources and knobs the engine describes.
const canvas = $('stage');
const renderer = new THREE.WebGLRenderer({ canvas, antialias: true, alpha: true });
renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 1.5));
renderer.setClearColor(0x000000, 0);
const scene = new THREE.Scene();
scene.fog = new THREE.FogExp2(SKIN.stage.fog, SKIN.stage.fogDensity);
// The skin's background image is the page background behind the transparent 3D canvas (57-skins.js),
// so it always fills the window at its own proportions.
const camera = new THREE.PerspectiveCamera(48, 1, 0.1, 300);
function rr(c, x, y, w, h, r) { c.beginPath(); c.moveTo(x + r, y); c.arcTo(x + w, y, x + w, y + h, r); c.arcTo(x + w, y + h, x, y + h, r); c.arcTo(x, y + h, x, y, r); c.arcTo(x, y, x + w, y, r); c.closePath(); }
function mkTex(cv) { const t = new THREE.CanvasTexture(cv); t.minFilter = THREE.LinearFilter; t.generateMipmaps = false; return t; }

// Face geometry, in canvas pixels: the card art's own size (518 x 754). One knob row per two params;
// cards with more than one row grow by stretching a plain strip of the side rails (45-faces.js drawCard).
const FW = 518, CARD_H = 754, PW = 3.4, ROW_H = 150, KNOB_Y0 = 492;
const faceH = (m) => CARD_H + ROW_H * Math.max(0, Math.ceil(m.params.length / 2) - 1);
function knobXY(p) {
  const n = p.mod.params.length, row = Math.floor(p.pi / 2), inRow = Math.min(2, n - row * 2);
  return [inRow === 2 ? (p.pi % 2 ? 372 : 146) : FW / 2, KNOB_Y0 + row * ROW_H];
}
const SPACING = 4;
const pickables = [];
let flowCurve, flowTube, flowGeo, flowMat, flowPos, inLbl, outLbl, carryLine, carryGeo;
const NP = 70, CN = 48;
let cables = [];
const pointerWorld = new THREE.Vector3(0, 4, 0);

// Soft rounded-rectangle glow (white; tinted by the material colour), shared by every card.
let glowTexCache = null;
function glowTex() {
  if (glowTexCache) return glowTexCache;
  const cv = document.createElement('canvas'); cv.width = 256; cv.height = 256; const c = cv.getContext('2d');
  c.shadowColor = '#fff'; c.shadowBlur = 34; c.fillStyle = '#fff';
  for (let k = 0; k < 3; k++) { rr(c, 40, 40, 176, 176, 26); c.fill(); }
  c.globalCompositeOperation = 'destination-out'; c.shadowBlur = 18; rr(c, 52, 52, 152, 152, 18); c.fill();
  return (glowTexCache = mkTex(cv));
}
function pill(txt) { const cv = document.createElement('canvas'); cv.width = 128; cv.height = 64; const s = new THREE.Sprite(new THREE.SpriteMaterial({ map: mkTex(cv), transparent: true })); s.scale.set(1.4, .7, 1); return { s, cv, txt }; }
function drawPill(l) { const c = l.cv.getContext('2d'); c.clearRect(0, 0, 128, 64); rr(c, 4, 4, 120, 56, 28); c.fillStyle = SKIN.stage.pillBg; c.fill(); c.strokeStyle = SKIN.stage.pillLine; c.lineWidth = 3; c.stroke(); c.fillStyle = SKIN.stage.pillText; c.font = `600 26px ${SKIN.ui.fontMono}`; c.textAlign = 'center'; c.textBaseline = 'middle'; c.fillText(l.txt, 64, 33); l.s.material.map.needsUpdate = true; }

function buildStage() {
  flowPos = new Float32Array(NP * 3); flowGeo = new THREE.BufferGeometry(); flowGeo.setAttribute('position', new THREE.BufferAttribute(flowPos, 3));
  flowMat = new THREE.PointsMaterial({ color: SKIN.stage.flow, size: .22, transparent: true, opacity: .95 });
  const flowDots = new THREE.Points(flowGeo, flowMat); flowDots.frustumCulled = false; scene.add(flowDots);
  inLbl = pill('IN'); outLbl = pill('OUT'); scene.add(inLbl.s, outLbl.s);
  carryGeo = new THREE.BufferGeometry(); carryGeo.setAttribute('position', new THREE.BufferAttribute(new Float32Array(CN * 3), 3));
  carryLine = new THREE.Line(carryGeo, new THREE.LineDashedMaterial({ color: SKIN.stage.carry, dashSize: .25, gapSize: .18 })); carryLine.frustumCulled = false; carryLine.visible = false; scene.add(carryLine);
  buildModules();
  CHAIN_HOOKS.push(() => { buildModules(); buildSources(); update(); });
  buildSources();
  RACK_HOOKS.push(() => { buildSources(); update(); });
}
// The cards: one per chain instance, on an arc in signal order, and a "+" card at the end to add one.
// Rebuilt whenever the chain changes. The arc flattens as the chain grows, so long chains stay in view.
let modObjs = [], addCard = null;
function cardSpot(i, n) {
  const half = (n - 1) / 2, k = i - half, curve = 0.65 * Math.min(1, 4 / Math.max(1, half * half));
  return new THREE.Vector3(k * SPACING, 0, 0.9 - curve * k * k);
}
function disposeObj(o) {
  scene.remove(o);
  o.traverse((x) => { if (x.geometry) x.geometry.dispose(); if (x.material) { if (x.material.map && x.material.map !== glowTexCache) x.material.map.dispose(); x.material.dispose(); } });
  const k = pickables.indexOf(o); if (k >= 0) pickables.splice(k, 1);
  o.traverse((x) => { const q = pickables.indexOf(x); if (q >= 0) pickables.splice(q, 1); });
}
function buildModules() {
  modObjs.forEach(disposeObj); modObjs = []; addCard = null;
  if (flowTube) { scene.remove(flowTube); flowTube.geometry.dispose(); flowTube.material.dispose(); flowTube = null; }
  const n = MODS.length + (chainFull() ? 0 : 1);
  MODS.forEach((m, i) => {
    const pos = cardSpot(i, n);
    const FH = faceH(m), PH = PW * FH / FW, tileGlow = styleOf(m).glow;
    const g = new THREE.Group(); g.position.set(pos.x, 0.8 + PH / 2, pos.z); g.lookAt(0, g.position.y, 22);
    // the card's thickness (inset so its rounded corners stay clear) and a glow behind it when focused
    const slab = new THREE.Mesh(new THREE.BoxGeometry(PW * .96, PH * .97, .12), new THREE.MeshBasicMaterial({ color: SKIN.stage.slab })); slab.position.z = .02; g.add(slab);
    const halo = new THREE.Mesh(new THREE.PlaneGeometry(PW * 1.3, PH * 1.2), new THREE.MeshBasicMaterial({ map: glowTex(), color: tileGlow, transparent: true, opacity: 0, depthWrite: false, blending: THREE.AdditiveBlending }));
    halo.position.z = -.06; g.add(halo);
    const cv = document.createElement('canvas'); cv.width = FW; cv.height = FH; const tex = mkTex(cv);
    const face = new THREE.Mesh(new THREE.PlaneGeometry(PW, PH), new THREE.MeshBasicMaterial({ map: tex, transparent: true })); face.position.z = .1; g.add(face);
    const glow = new THREE.Mesh(new THREE.RingGeometry(1.2, 2.2, 48), new THREE.MeshBasicMaterial({ color: tileGlow, transparent: true, opacity: .08, side: THREE.DoubleSide, depthWrite: false }));
    glow.rotation.x = -Math.PI / 2; glow.position.set(pos.x, .02, pos.z);
    scene.add(glow, g); modObjs.push(glow, g);
    Object.assign(m, { group: g, baseY: g.position.y, face, slab, halo, glow, ctx: cv.getContext('2d'), tex, FH, PH });
    face.userData = { type: 'face', mod: m }; pickables.push(face);
    drawFace(m);
  });
  if (!chainFull()) {                                   // the "+" card: a plain dashed outline, half height
    const pos = cardSpot(MODS.length, n), PH = PW * 0.62, cv = document.createElement('canvas'); cv.width = FW; cv.height = Math.round(FW * 0.62);
    const g = new THREE.Group(); g.position.set(pos.x, 0.8 + PH / 2, pos.z); g.lookAt(0, g.position.y, 22);
    const face = new THREE.Mesh(new THREE.PlaneGeometry(PW, PH), new THREE.MeshBasicMaterial({ map: mkTex(cv), transparent: true })); g.add(face);
    scene.add(g); modObjs.push(g);
    addCard = { group: g, face, cv }; face.userData = { type: 'fxadd' }; pickables.push(face);
    drawAddCard(true);
  }
  const span = Math.max(0, (n - 1) / 2) * SPACING, endX = span + 4.5;
  const pts = MODS.map((m) => new THREE.Vector3(m.group.position.x, .55, m.group.position.z + .6));
  flowCurve = new THREE.CatmullRomCurve3([new THREE.Vector3(-endX, 1.0, -3.2), ...(pts.length ? pts : [new THREE.Vector3(0, .55, 1.5)]), new THREE.Vector3(endX, 1.0, -3.2)]);
  flowTube = new THREE.Mesh(new THREE.TubeGeometry(flowCurve, 160, .05, 8, false), new THREE.MeshBasicMaterial({ color: SKIN.stage.flow, transparent: true, opacity: .35 })); scene.add(flowTube);
  inLbl.s.position.set(-endX, 1.9, -3.2); outLbl.s.position.set(endX, 1.9, -3.2);
  syncCables();
}
function drawAddCard(force) {
  if (!addCard) return;
  const open = state.focus === 'addfx', key = `${open}|${MODS.length}|${SKIN.name}`;
  if (!force && addCard.key === key) return;
  addCard.key = key;
  const c = addCard.cv.getContext('2d'), W = addCard.cv.width, H = addCard.cv.height;
  c.clearRect(0, 0, W, H); rr(c, 10, 10, W - 20, H - 20, 30); c.fillStyle = SKIN.labels.bg; c.fill();
  c.setLineDash([18, 12]); c.strokeStyle = SKIN.labels.text; c.lineWidth = open ? 8 : 4; c.stroke(); c.setLineDash([]);
  c.fillStyle = SKIN.labels.text; c.textAlign = 'center'; c.font = `700 120px ${SKIN.ui.fontDisplay}`; c.fillText('+', W / 2, H / 2 + 20);
  c.font = `700 34px ${SKIN.ui.fontDisplay}`; c.fillText('ADD EFFECT', W / 2, H / 2 + 80);
  c.font = `400 24px ${SKIN.ui.fontMono}`; c.fillText(`${MODS.length} of ${DESC.maxModules}`, W / 2, H / 2 + 116);
  addCard.face.material.map.needsUpdate = true;
}
// The source row: one orb, ring, glow and name label per rack instance, and a "+" orb to add one.
// Rebuilt whenever the rack changes. Up to 8 in a row, about as wide as the cards; more go on a
// second row behind and above (the camera re-frames to show it).
let srcObjs = [], addOrb = null, srcRowCount = 1;
const srcRows = (total) => (total > 8 ? 2 : 1);
function srcSpacing(cnt) { return Math.min(SPACING, Math.max(16, MODS.length * SPACING + 2) / Math.max(1, cnt - 1)); }
function srcSpot(i, total) {
  const rows = srcRows(total), per = Math.ceil(total / rows), row = Math.floor(i / per), col = i % per, cnt = Math.min(per, total - row * per);
  const sp = srcSpacing(cnt);
  return new THREE.Vector3((col - (cnt - 1) / 2) * sp, 7.6 + row * 3.4, -4.2 - row * 2.6);
}
const srcLabelW = (total) => Math.min(3.2, srcSpacing(Math.ceil(total / srcRows(total))) * 0.94);
function buildSources() {
  srcObjs.forEach((o) => {
    scene.remove(o); if (o.geometry) o.geometry.dispose();
    if (o.material) { if (o.material.map) o.material.map.dispose(); o.material.dispose(); }
    const k = pickables.indexOf(o); if (k >= 0) pickables.splice(k, 1);
  });
  srcObjs = []; addOrb = null;
  const total = SRC.length + (rackFull() ? 0 : 1), lw = srcLabelW(total);
  SRC.forEach((s, i) => {
    const pos = srcSpot(i, total), col = new THREE.Color(s.color);
    const orb = new THREE.Mesh(new THREE.IcosahedronGeometry(.42, 1), new THREE.MeshBasicMaterial({ color: col })); orb.position.copy(pos);
    const ring = new THREE.Mesh(new THREE.TorusGeometry(.75, .03, 8, 48), new THREE.MeshBasicMaterial({ color: col, transparent: true, opacity: .7 })); ring.position.copy(pos);
    const halo = new THREE.Mesh(new THREE.SphereGeometry(.9, 16, 12), new THREE.MeshBasicMaterial({ color: col, transparent: true, opacity: .1, depthWrite: false })); halo.position.copy(pos);
    const cv = document.createElement('canvas'); cv.width = 256; cv.height = 128; const tex = mkTex(cv);
    const lbl = new THREE.Sprite(new THREE.SpriteMaterial({ map: tex, transparent: true })); lbl.scale.set(lw, lw / 2, 1); lbl.position.set(pos.x, pos.y + 1.7, pos.z);
    scene.add(orb, ring, halo, lbl); srcObjs.push(orb, ring, halo, lbl);
    Object.assign(s, { orb, ring, halo, lbl, ctx: cv.getContext('2d'), tex });
    orb.userData = halo.userData = { type: 'src', src: s }; lbl.userData = { type: 'srclabel', src: s }; pickables.push(orb, halo, lbl);
    drawLabel(s);
  });
  if (!rackFull()) {
    const pos = srcSpot(SRC.length, total), cv = document.createElement('canvas'); cv.width = 256; cv.height = 128;
    const orb = new THREE.Mesh(new THREE.IcosahedronGeometry(.36, 0), new THREE.MeshBasicMaterial({ color: SKIN.labels.text, wireframe: true, transparent: true, opacity: .6 })); orb.position.copy(pos);
    const lbl = new THREE.Sprite(new THREE.SpriteMaterial({ map: mkTex(cv), transparent: true })); lbl.scale.set(lw, lw / 2, 1); lbl.position.set(pos.x, pos.y + 1.7, pos.z);
    scene.add(orb, lbl); srcObjs.push(orb, lbl);
    addOrb = { orb, lbl, cv }; orb.userData = lbl.userData = { type: 'srcadd' }; pickables.push(orb, lbl);
    drawAddLabel();
  }
  syncCables();
  const rows = srcRows(total); if (rows !== srcRowCount) { srcRowCount = rows; overview(); }
}
function drawAddLabel(force) {
  if (!addOrb) return;
  const open = state.focus === 'add', key = `${open}|${SRC.length}|${SKIN.name}`;
  if (!force && addOrb.key === key) return;                // only when what it says changes
  addOrb.key = key;
  const c = addOrb.cv.getContext('2d');
  c.clearRect(0, 0, 256, 128); rr(c, 3, 3, 250, 122, 16); c.fillStyle = SKIN.labels.bg; c.fill(); c.setLineDash([8, 6]); c.strokeStyle = SKIN.labels.text; c.lineWidth = open ? 4 : 2; c.stroke(); c.setLineDash([]);
  c.fillStyle = SKIN.labels.text; c.font = `700 26px ${SKIN.ui.fontDisplay}`; c.fillText('+ ADD', 16, 34);
  c.font = `400 16px ${SKIN.ui.fontMono}`; c.fillText(`a source · ${SRC.length}/${DESC.maxSources}`, 16, 58);
  addOrb.lbl.material.map.needsUpdate = true;
}
function knobWorld(p) { const m = p.mod, [kx, ky] = knobXY(p); return m.face.localToWorld(new THREE.Vector3((kx / FW - .5) * PW, (.5 - ky / m.FH) * m.PH, .12)); }
function bez(a, b) { return new THREE.CubicBezierCurve3(a, a.clone().add(new THREE.Vector3(0, -3.5, 1.5)), b.clone().add(new THREE.Vector3(0, 3, 2.5)), b); }
// Where a cable ends: a module's knob, or just under the orb of the source whose knob it moves
// (one point per knob, spread across the orb).
function targetWorld(pid) {
  const k = knobOf(pid); if (k.mod) return knobWorld(k);
  const o = k.src.orb.position, n = k.src.params.length;
  return new THREE.Vector3(o.x + (k.pi - (n - 1) / 2) * .32, o.y - .62, o.z + .25);
}
// A cable's path: down to a module knob, or arching over to another source (or looping back to its own).
function cableCurve(s, pid) {
  const a = s.orb.position.clone(), b = targetWorld(pid);
  if (PARAMS[pid]) return bez(a, b);
  const self = SPARAMS[pid].src === s, side = self ? 1.6 : 0;
  return new THREE.CubicBezierCurve3(a, a.clone().add(new THREE.Vector3(-side, 2.6, 1.2)), b.clone().add(new THREE.Vector3(side, -2.4, 1.4)), b);
}
function syncCables() {
  cables.forEach((c) => { scene.remove(c.line); c.line.geometry.dispose(); c.line.material.dispose(); c.pulses.forEach((p) => { scene.remove(p); p.geometry.dispose(); p.material.dispose(); }); });
  cables = Object.keys(state.conns).filter((pid) => knobOf(pid) && SRCMAP[state.conns[pid]] && SRCMAP[state.conns[pid]].orb).map((pid) => {
    const s = SRCMAP[state.conns[pid]], col = new THREE.Color(s.color);
    const geo = new THREE.BufferGeometry(); geo.setAttribute('position', new THREE.BufferAttribute(new Float32Array(CN * 3), 3));
    const line = new THREE.Line(geo, new THREE.LineBasicMaterial({ color: col, transparent: true, opacity: .85 })); line.frustumCulled = false; scene.add(line);
    const pulses = [0, 1, 2].map(() => { const p = new THREE.Mesh(new THREE.SphereGeometry(.1, 10, 8), new THREE.MeshBasicMaterial({ color: col })); scene.add(p); return p; });
    return { pid, s, line, pulses, ph: 0 };
  });
}

// ------------------------------------------------------------------ camera and input
const cam = { theta: 0, phi: 1.12, r: 30, target: new THREE.Vector3(0, 3.6, 0) };
const camGoal = { r: 21, target: new THREE.Vector3(0, 3.6, 0) };
const overviewR = () => Math.max(21, (MODS.length + (chainFull() ? 0 : 1)) * 3.5) + (srcRowCount - 1) * 6;
function placeCamera() { const t = cam.target; camera.position.set(t.x + cam.r * Math.sin(cam.phi) * Math.sin(cam.theta), t.y + cam.r * Math.cos(cam.phi), t.z + cam.r * Math.sin(cam.phi) * Math.cos(cam.theta)); camera.lookAt(t); }
function flyTo(m) { camGoal.target.set(m.group.position.x, m.baseY, m.group.position.z); camGoal.r = 11 + 2 * (m.PH - 4); cam.theta = Math.atan2(m.group.position.x, 22) * .9; }
function overview() { camGoal.target.set(0, 3.6 + (srcRowCount - 1) * 2.4, 0); camGoal.r = overviewR(); cam.theta = 0; cam.phi = 1.12; }
const ray = new THREE.Raycaster(), ndc = new THREE.Vector2();
function setNdc(e) { const r = canvas.getBoundingClientRect(); ndc.x = ((e.clientX - r.left) / r.width) * 2 - 1; ndc.y = -((e.clientY - r.top) / r.height) * 2 + 1; }
function pick(e) {
  setNdc(e); ray.setFromCamera(ndc, camera);
  const hit = ray.intersectObjects(pickables, false)[0]; if (!hit) return null;
  const u = hit.object.userData; if (u.type === 'src' || u.type === 'srclabel') return { type: u.type, src: u.src };
  if (u.type === 'srcadd') return { type: 'srcadd' };
  if (u.type === 'fxadd') return { type: 'fxadd' };
  const m = u.mod, px = hit.uv.x * FW, py = (1 - hit.uv.y) * m.FH; let knob = null;
  m.params.forEach((p) => { const [kx, ky] = knobXY(p); if (Math.hypot(px - kx, py - ky) < 84) knob = p; });
  return { type: 'face', mod: m, knob };
}
let drag = null;
function wireStageInput() {
  canvas.addEventListener('pointerdown', (e) => {
    canvas.setPointerCapture(e.pointerId); const h = pick(e);
    drag = { x: e.clientX, y: e.clientY, moved: false, hit: h, mode: 'orbit' };
    if (h && h.type === 'face' && h.knob && !state.carrying) { drag.mode = 'knob'; drag.start = h.knob.v; }
  });
  canvas.addEventListener('pointermove', (e) => {
    setNdc(e);
    if (!drag) { const h = pick(e); canvas.style.cursor = h ? 'pointer' : (state.carrying ? 'crosshair' : 'grab'); return; }
    const dy = e.clientY - drag.y; if (Math.hypot(e.clientX - drag.x, dy) > 5) drag.moved = true;
    if (drag.mode === 'knob') { if (drag.moved) { setParam(drag.hit.knob, drag.hit.knob.snap(clamp(drag.start - dy / 220, 0, 1))); state.focus = drag.hit.mod.id; renderPanelSoon(); } }
    else if (drag.moved) { cam.theta -= e.movementX * .005; cam.phi = clamp(cam.phi - e.movementY * .004, .45, 1.45); canvas.style.cursor = 'grabbing'; }
  });
  canvas.addEventListener('pointerup', () => { if (drag && !drag.moved) stageClick(drag.hit); drag = null; canvas.style.cursor = 'grab'; });
  canvas.addEventListener('wheel', (e) => { e.preventDefault(); cam.r = camGoal.r = clamp(cam.r * (1 + e.deltaY * .001), 6, overviewR() * 1.6); }, { passive: false });
  window.addEventListener('keydown', (e) => {
    if (e.target.closest && e.target.closest('input,textarea,select')) return;
    if (e.key === 'Escape' && state.carrying) { state.carrying = null; update(); }
    const n = parseInt(e.key, 10); if (n >= 1 && n <= Math.min(9, MODS.length)) { state.focus = MODS[n - 1].id; flyTo(MODS[n - 1]); update(); }
  });
}
function stageClick(h) {
  if (!h) { if (state.carrying) { state.carrying = null; update(); } return; }
  if (h.type === 'srcadd') { state.carrying = null; state.focus = 'add'; update(); return; }
  if (h.type === 'fxadd') { state.carrying = null; state.focus = 'addfx'; update(); return; }
  // carrying a cable to another source: open it, and pick which of its knobs in the panel
  if ((h.type === 'src' || h.type === 'srclabel') && state.carrying && state.carrying !== h.src.id) { state.focus = 'src:' + h.src.id; update(); return; }
  // an orb opens the source's panel and picks up a cable; its name label just opens the panel
  if (h.type === 'src') { state.carrying = state.carrying === h.src.id ? null : h.src.id; state.focus = 'src:' + h.src.id; update(); return; }
  if (h.type === 'srclabel') { state.focus = 'src:' + h.src.id; update(); return; }
  if (h.knob && state.carrying) { connect(h.knob.id, state.conns[h.knob.id] === state.carrying ? null : state.carrying); state.carrying = null; state.focus = h.mod.id; update(); return; }
  state.focus = h.mod.id; flyTo(h.mod); update();
}
// Test helpers: where a module's face, or a source's orb or name label, is on screen (client px).
rig.camSnap = () => { cam.r = camGoal.r; cam.target.copy(camGoal.target); placeCamera(); camera.updateMatrixWorld(); };   // tests: skip the camera's easing
rig.fxAddScreen = () => { if (!addCard) return null; const v = addCard.group.position.clone().project(camera), r = canvas.getBoundingClientRect(); return { x: r.left + (v.x + 1) / 2 * r.width, y: r.top + (1 - v.y) / 2 * r.height }; };
rig.cables = () => cables.map((c) => `${c.s.id}>${c.pid}`);
rig.addScreen = () => { if (!addOrb) return null; const v = addOrb.lbl.position.clone().project(camera), r = canvas.getBoundingClientRect(); return { x: r.left + (v.x + 1) / 2 * r.width, y: r.top + (1 - v.y) / 2 * r.height }; };
rig.srcScreen = (id, part = 'lbl') => { const s = SRCMAP[id]; if (!s) return null; const v = s[part].position.clone().project(camera), r = canvas.getBoundingClientRect(); return { x: r.left + (v.x + 1) / 2 * r.width, y: r.top + (1 - v.y) / 2 * r.height }; };
rig.modScreen = (id) => { const m = MODS.find((x) => x.id === id); if (!m) return null; const v = m.group.position.clone().project(camera), r = canvas.getBoundingClientRect(); return { x: r.left + (v.x + 1) / 2 * r.width, y: r.top + (1 - v.y) / 2 * r.height }; };

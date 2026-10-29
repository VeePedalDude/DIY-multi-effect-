// 90-main.js - boot sequence and the render loop.
function resize() {
  const w = innerWidth, h = innerHeight; renderer.setSize(w, h, false); camera.aspect = w / h; camera.updateProjectionMatrix();
  document.documentElement.style.setProperty('--topH', $('top').offsetHeight + 'px');
  document.documentElement.style.setProperty('--botH', $('bottom').offsetHeight + 'px');
}
function meterBar(el, pk) { const db = 20 * Math.log10(pk + 1e-9), f = clamp((db + 60) / 60, 0, 1); el.style.width = (f * 100) + '%'; el.className = db > -1 ? 'clip' : db > -9 ? 'hot' : ''; }
const clock = new THREE.Clock(); let frame = 0, lastMeterSeq = null; const tmpV = new THREE.Vector3();
function fillCurve(curve, attr) { const arr = attr.array; for (let i = 0; i < CN; i++) { curve.getPoint(i / (CN - 1), tmpV); arr[i * 3] = tmpV.x; arr[i * 3 + 1] = tmpV.y; arr[i * 3 + 2] = tmpV.z; } attr.needsUpdate = true; }
function loop() {
  requestAnimationFrame(loop);              // schedule first, so one bad frame can never freeze the stage
  const dt = Math.min(clock.getDelta(), .05); T += dt; frame++;
  cam.r += (camGoal.r - cam.r) * .06; cam.target.lerp(camGoal.target, .06); placeCamera();
  pollPad(frame);
  if (meters !== lastMeterSeq) { lastMeterSeq = meters; SRC.forEach((s) => { s.hist.push(srcVal(s.idx)); if (s.hist.length > 120) s.hist.shift(); }); }
  const level = meters ? clamp((20 * Math.log10(meters.outPk + 1e-9) + 50) / 50, 0, 1) : 0;
  flowMat.opacity = .25 + .75 * level; flowMat.size = .14 + .2 * level;
  MODS.forEach((m, i) => { m.group.position.y = m.baseY + (RM ? 0 : .09 * Math.sin(T * .9 + i * 1.3)); const foc = state.focus === m.id; m.edges.material.color.set(foc ? ACC : '#2A3550'); m.glow.material.opacity = foc ? .22 : .07; });
  SRC.forEach((s) => { const v = srcVal(s.idx), k = 1 + .22 * v; s.orb.scale.setScalar(k); s.halo.scale.setScalar(state.carrying === s.id ? 1.5 + .2 * Math.sin(T * 8) : k); if (!RM) { s.ring.rotation.x = T * .7; s.ring.rotation.y = T * .5; } });
  if (frame % 2 === 0) { MODS.forEach(drawFace); SRC.forEach(drawLabel); }
  for (let i = 0; i < NP; i++) { const u = ((i / NP) + T * (RM ? .01 : .05 + .1 * level)) % 1; flowCurve.getPointAt(u, tmpV); flowPos[i * 3] = tmpV.x; flowPos[i * 3 + 1] = tmpV.y + .06; flowPos[i * 3 + 2] = tmpV.z; }
  flowGeo.attributes.position.needsUpdate = true;
  cables.forEach((c) => {
    const curve = bez(c.s.orb.position.clone(), knobWorld(PARAMS[c.pid]));
    fillCurve(curve, c.line.geometry.attributes.position);
    c.ph += dt * (RM ? .05 : .2 + .5 * Math.abs(srcVal(c.s.idx)));
    c.pulses.forEach((p, k) => curve.getPoint((c.ph + k / 3) % 1, p.position));
  });
  if (state.carrying) {
    ray.setFromCamera(ndc, camera); const n = new THREE.Vector3(); camera.getWorldDirection(n);
    ray.ray.intersectPlane(new THREE.Plane().setFromNormalAndCoplanarPoint(n.negate(), cam.target), pointerWorld);
    fillCurve(bez(SRCMAP[state.carrying].orb.position.clone(), pointerWorld.clone()), carryGeo.attributes.position); carryLine.computeLineDistances();
  }
  if (frame % 3 === 0) {
    document.querySelectorAll('[data-disp]').forEach((el) => { const p = PARAMS[el.dataset.disp]; if (p) el.textContent = p.fmt(effOf(p)); });
    if (meters) { meterBar($('mIn'), meters.inPk); meterBar($('mOut'), meters.outPk); $('dspLoad').textContent = (meters.load * 100).toFixed(1) + '%'; }
  }
  renderer.render(scene, camera);
}
function wireStart() {
  $('startBtn').onclick = async () => {
    const src = document.querySelector('input[name=src]:checked').value;
    $('startBtn').disabled = true; $('startBtn').textContent = 'Starting…'; $('startErr').hidden = true;
    try { await startAudio(src); $('startOverlay').hidden = true; }
    catch (err) {
      $('startBtn').disabled = false; $('startBtn').textContent = 'Start audio'; $('startErr').hidden = false;
      const blocked = err.name === 'NotAllowedError' || /permissions policy|not allowed/i.test(err.message);
      $('startErr').textContent = blocked
        ? 'Microphone access is blocked here. If you are viewing this inside an app preview, download rig-test.html and open it in Chrome. Otherwise allow the microphone in the address bar, or choose the built-in test riff.'
        : 'Audio could not start: ' + err.message;
      if (rig.ctx) { try { rig.ctx.close(); } catch (e) {} rig.ctx = null; rig.node = null; }
    }
  };
  if (window.top !== window.self) {
    const p = document.createElement('p'); p.className = 'warn';
    p.textContent = 'This looks like an in-app preview, which blocks the microphone and gamepads. For real use, download rig-test.html and open it in Chrome. The built-in test riff works here.';
    document.querySelector('#startOverlay .card').insertBefore(p, $('startErr'));
  }
}
(async function boot() {
  try { await loadModel(); }
  catch (err) { $('startErr').hidden = false; $('startErr').textContent = 'Engine could not load: ' + err.message; $('startBtn').disabled = true; return; }
  buildStage(); wireStageInput(); wireHud(); wireControllers(); wirePresets(); wireStart();
  addEventListener('resize', resize); resize(); overview(); update();
  $('master').value = state.master; canvas.style.cursor = 'grab';
  drawPill(inLbl); drawPill(outLbl);
  if (document.fonts && document.fonts.ready) document.fonts.ready.then(() => { drawPill(inLbl); drawPill(outLbl); });
  rig.ready = true;
  requestAnimationFrame(loop);
})();

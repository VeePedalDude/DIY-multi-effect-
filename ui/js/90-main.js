// 90-main.js - boot sequence and the render loop.
function resize() {
  const w = innerWidth, h = innerHeight; renderer.setSize(w, h, false); camera.aspect = w / h;
  // with the side panel open, centre the stage in the space to its left (the panel hides below 900 px)
  const pw = !panelHidden && w > 900 ? $('panel').offsetWidth + 16 : 0;
  if (pw) camera.setViewOffset(w, h, pw / 2, 0, w, h); else camera.clearViewOffset();
  camera.updateProjectionMatrix();
  document.documentElement.style.setProperty('--topH', $('top').offsetHeight + 'px');
  document.documentElement.style.setProperty('--botH', $('bottom').offsetHeight + 'px');
}
// The skin's background image is a flat picture behind the 3D canvas. To sit in the scene it shifts
// a little against the cards as the camera orbits, tilts and zooms (SKIN.stage.parallax; off for
// reduced motion). It's drawn a little larger than the window so its edges never show.
let lastBackdrop = '';
function moveBackdrop() {
  const k = RM ? 0 : SKIN.stage.parallax, el = $('backdrop');
  const x = clamp(-cam.theta * 9 * k, -4.5 * k, 4.5 * k), y = clamp((cam.phi - 1.12) * 10 * k, -3.5 * k, 3.5 * k);
  const z = 1 + 0.1 * k + clamp((overviewR() / cam.r - 1) * 0.04 * k, -0.02, 0.06 * k);
  const t = k ? `translate(${x.toFixed(2)}%, ${y.toFixed(2)}%) scale(${z.toFixed(4)})` : '';
  if (t !== lastBackdrop) { el.style.transform = t; lastBackdrop = t; }
}
function meterBar(el, pk) { const db = 20 * Math.log10(pk + 1e-9), f = clamp((db + 60) / 60, 0, 1); el.style.width = (f * 100) + '%'; el.className = db > -1 ? 'clip' : db > -9 ? 'hot' : ''; }
const clock = new THREE.Clock(); let frame = 0; const tmpV = new THREE.Vector3();
function fillCurve(curve, attr) { const arr = attr.array; for (let i = 0; i < CN; i++) { curve.getPoint(i / (CN - 1), tmpV); arr[i * 3] = tmpV.x; arr[i * 3 + 1] = tmpV.y; arr[i * 3 + 2] = tmpV.z; } attr.needsUpdate = true; }
function loop() {
  requestAnimationFrame(loop);              // schedule first, so one bad frame can never freeze the stage
  const dt = Math.min(clock.getDelta(), .05); T += dt; frame++;
  const vizOn = state.view === 'viz';         // the Visualizer replaces the 3D stage: none of it is drawn meanwhile
  if (!vizOn) {
    cam.r += (camGoal.r - cam.r) * .06; cam.target.lerp(camGoal.target, .06); placeCamera();
    const level = meters ? clamp((20 * Math.log10(meters.outPk + 1e-9) + 50) / 50, 0, 1) : 0;
    flowMat.opacity = .25 + .75 * level; flowMat.size = .14 + .2 * level;
    MODS.forEach((m, i) => { m.group.position.y = m.baseY + (RM ? 0 : .09 * Math.sin(T * .9 + i * 1.3)); const foc = state.focus === m.id; m.halo.material.opacity = foc ? .32 + (RM ? 0 : .06 * Math.sin(T * 2.4)) : 0; m.glow.material.opacity = foc ? .24 : .07; });
    SRC.forEach((s) => { if (!s.orb) return; const v = srcVal(s), k = 1 + .22 * v; s.orb.scale.setScalar(k); s.halo.scale.setScalar(state.carrying === s.id ? 1.5 + .2 * Math.sin(T * 8) : k); if (!RM) { s.ring.rotation.x = T * .7; s.ring.rotation.y = T * .5; } });
    moveBackdrop();
    if (frame % 2 === 0) { MODS.forEach(drawFace); SRC.forEach((s) => s.ctx && drawLabel(s)); drawAddLabel(); drawAddCard(); }
    for (let i = 0; i < NP; i++) { const u = ((i / NP) + T * (RM ? .01 : .05 + .1 * level)) % 1; flowCurve.getPointAt(u, tmpV); flowPos[i * 3] = tmpV.x; flowPos[i * 3 + 1] = tmpV.y + .06; flowPos[i * 3 + 2] = tmpV.z; }
    flowGeo.attributes.position.needsUpdate = true;
    cables.forEach((c) => {
      const curve = cableCurve(c.s, c.pid);
      fillCurve(curve, c.line.geometry.attributes.position);
      c.ph += dt * (RM ? .05 : .2 + .5 * Math.abs(srcVal(c.s)));
      c.pulses.forEach((p, k) => curve.getPoint((c.ph + k / 3) % 1, p.position));
    });
    if (state.carrying) {
      ray.setFromCamera(ndc, camera); const n = new THREE.Vector3(); camera.getWorldDirection(n);
      ray.ray.intersectPlane(new THREE.Plane().setFromNormalAndCoplanarPoint(n.negate(), cam.target), pointerWorld);
      fillCurve(bez(SRCMAP[state.carrying].orb.position.clone(), pointerWorld.clone()), carryGeo.attributes.position); carryLine.computeLineDistances();
    }
  }
  if (frame % 2 === 0) drawSourceScope();       // the side panel's live trace and view of the source being edited
  drawSourceView(dt);
  if (frame % 3 === 0) {
    document.querySelectorAll('[data-disp]').forEach((el) => { const p = PARAMS[el.dataset.disp]; if (p) el.textContent = p.fmt(effOf(p)); });
    refreshSourceReadouts();
    if (meters) { meterBar($('mIn'), meters.inPk); meterBar($('mOut'), meters.outPk); $('dspLoad').textContent = (meters.load * 100).toFixed(1) + '%'; }
  }
  if (vizOn) drawViz(); else renderer.render(scene, camera);
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
  buildStage(); wireStageInput(); wireHud(); wireSourcePanel(); wirePicker(); wireAssetUi(); wireLibrary(); wireSkins(); wireTuner(); wireViz(); wireControllers(); wirePresets(); wireStart();
  const libraryReady = initLibrary().catch((e) => logLine('library: ' + e.message, true));
  restoreSkin(libraryReady).catch((e) => logLine('skin: ' + e.message, true));
  addEventListener('resize', resize); resize(); overview(); update();
  $('master').value = state.master; canvas.style.cursor = 'grab';
  drawPill(inLbl); drawPill(outLbl);
  if (document.fonts && document.fonts.ready) document.fonts.ready.then(() => { drawPill(inLbl); drawPill(outLbl); });
  rig.ready = true;
  requestAnimationFrame(loop);
})();

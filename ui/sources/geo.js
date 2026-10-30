// ui/sources/geo.js - GEO PULSE's 3D view, in its side panel.
//  - The shape as a luminous wireframe on its own tilted plane (VIEW TILT), with a faint surface;
//    edges curve along geodesics on the HYPERBOLIC shape, inside the Poincare boundary.
//  - The travelling point, with a fading trail (TRAIL); a vertex lights up and sends out a ring when
//    it fires; switched-off vertices are dim.
//  - Drag to turn it; click a vertex to edit its row (ON, PROB, VEL, ACCENT, GATE).
// Everything musical comes from the engine: the shape from rig_view() on the page's own copy of the
// engine (the same C++ the audio runs), the point from the "step" meter and the flashes from the
// "fired" meter. The frame clock here only fades a flash or a trail after the engine says it happened.
SRCVIEW.geo = (() => {
  const W = 560, H = 400, MAXTRAIL = 90;
  let R = null;                                           // the renderer and scene, made once, moved into each new panel
  const inst = {};                                        // per source id: what the view remembers
  const per = (s) => inst[s.id] || (inst[s.id] = { lastCount: null, sel: -1, yaw: 0.5, pitch: 0, flash: new Float32Array(32), pulses: [], pending: [], step: null, trail: [], key: '' });
  const pulseLog = []; rig.geoPulses = pulseLog;          // for tests: every flash, with the engine count that caused it

  function ensure() {
    if (R) return R;
    const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true });
    renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2)); renderer.setSize(W, H, false);
    renderer.domElement.className = 'geoview'; renderer.domElement.setAttribute('aria-label', 'The shape, the travelling point and the vertices that fire'); renderer.domElement.setAttribute('role', 'img');
    const scene = new THREE.Scene(), camera = new THREE.PerspectiveCamera(38, W / H, 0.1, 60); camera.position.set(0, 0, 5.2);
    const spin = new THREE.Group(), tilt = new THREE.Group(); spin.add(tilt); scene.add(spin);
    R = { renderer, scene, camera, spin, tilt, shape: null, verts: [], ind: null, trail: null, rings: [], ray: new THREE.Raycaster(), drag: null, built: null };
    const c = renderer.domElement;
    c.addEventListener('pointerdown', (e) => { R.drag = { x: e.clientX, y: e.clientY, moved: false }; c.setPointerCapture(e.pointerId); });
    c.addEventListener('pointermove', (e) => {
      const d = R.drag, s = srcFocus(); if (!d || !s) return;
      if (Math.hypot(e.clientX - d.x, e.clientY - d.y) > 4) d.moved = true;
      if (d.moved) { const p = per(s); p.yaw += e.movementX * 0.01; p.pitch = clamp(p.pitch + e.movementY * 0.01, -1.4, 1.4); }
    });
    c.addEventListener('pointerup', (e) => {
      const d = R.drag, s = srcFocus(); R.drag = null; if (!d || d.moved || !s) return;
      const r = c.getBoundingClientRect(), ndc = new THREE.Vector2(((e.clientX - r.left) / r.width) * 2 - 1, -((e.clientY - r.top) / r.height) * 2 + 1);
      R.ray.setFromCamera(ndc, R.camera); const hit = R.ray.intersectObjects(R.verts, false)[0];
      per(s).sel = hit ? hit.object.userData.v : -1; renderEditor(s);
    });
    return R;
  }
  // Mobius addition and scaling in the Poincare ball: the geodesic from a to b at fraction t of its length.
  const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
  function madd(u, v) { const uv = dot(u, v), uu = dot(u, u), vv = dot(v, v), d = 1 + 2 * uv + uu * vv, a = 1 + 2 * uv + vv, b = 1 - uu; return [0, 1, 2].map((i) => (a * u[i] + b * v[i]) / d); }
  function mscale(t, v) { const n = Math.sqrt(dot(v, v)); if (n < 1e-12) return [0, 0, 0]; const k = Math.tanh(t * Math.atanh(Math.min(n, 1 - 1e-9))) / n; return v.map((x) => x * k); }
  const geodesic = (a, b, t) => madd(a, mscale(t, madd(a.map((x) => -x), b)));

  // The shape for these knobs (after cross-modulation) and settings, from the engine's own code.
  function shapeOf(s) {
    const X = VIEWX, buf = new Float32Array(X.memory.buffer, X.rig_view_buf(), 64 + 512);
    s.params.forEach((q, i) => { buf[i] = srcEff(q); }); s.settings.forEach((q, i) => { buf[DESC.maxpar + i] = q.v; });
    const n = X.rig_view(TYPES[s.type].index), o = new Float32Array(X.memory.buffer, X.rig_view_buf() + 64 * 4, n).slice();
    const nv = o[0], hyper = o[1] > 0.5, pts = []; for (let i = 0; i < nv; i++) pts.push([o[2 + 3 * i], o[3 + 3 * i], o[4 + 3 * i]]);
    const at = 2 + 3 * nv, ne = o[at], edges = []; for (let e = 0; e < ne; e++) edges.push([o[at + 1 + 2 * e], o[at + 2 + 2 * e]]);
    return { pts, hyper, edges };
  }
  const settingReal = (s, sym, d) => { const q = s.settings.find((x) => x.sym === sym); return q ? q.real(q.v) : d; };
  function build(s, g) {
    const { tilt } = R; if (R.shape) { tilt.remove(R.shape); R.shape.traverse((o) => { if (o.geometry) o.geometry.dispose(); if (o.material) o.material.dispose(); }); }
    const shape = new THREE.Group(), col = new THREE.Color(s.color);
    const far = Math.max(1e-6, ...g.pts.map((p) => Math.hypot(p[0], p[1], p[2]))), k = g.hyper ? 1.5 : 1.5 / far;   // the ball's rim, or the farthest vertex, at 1.5
    const P = (p) => new THREE.Vector3(p[0] * k, p[1] * k, p[2] * k);
    const seg = [];
    g.edges.forEach(([a, b]) => {
      const A = g.pts[a], B = g.pts[b];
      if (!g.hyper) { seg.push(P(A), P(B)); return; }
      let prev = P(A); for (let i = 1; i <= 24; i++) { const q = P(geodesic(A, B, i / 24)); seg.push(prev, q); prev = q; }
    });
    const lines = new THREE.LineSegments(new THREE.BufferGeometry().setFromPoints(seg), new THREE.LineBasicMaterial({ color: col, transparent: true, opacity: 0.9, blending: THREE.AdditiveBlending, depthWrite: false }));
    shape.add(lines);
    if (g.hyper) {                                          // the boundary of the Poincare ball (seen as a disk)
      const ring = []; for (let i = 0; i <= 96; i++) ring.push(new THREE.Vector3(1.5 * Math.cos(i / 96 * Math.PI * 2), 1.5 * Math.sin(i / 96 * Math.PI * 2), 0));
      shape.add(new THREE.Line(new THREE.BufferGeometry().setFromPoints(ring), new THREE.LineBasicMaterial({ color: new THREE.Color(SKIN.screen.grid), transparent: true, opacity: 0.9 })));
      shape.add(new THREE.Mesh(new THREE.CircleGeometry(1.5, 96), new THREE.MeshBasicMaterial({ color: col, transparent: true, opacity: 0.05, side: THREE.DoubleSide, depthWrite: false })));
    } else if (g.pts.every((p) => Math.abs(p[2]) < 1e-6) && g.pts.length >= 3) {   // a flat shape: a faint surface inside it
      const outline = new THREE.Shape(g.pts.map((p) => new THREE.Vector2(p[0] * k, p[1] * k)));
      shape.add(new THREE.Mesh(new THREE.ShapeGeometry(outline), new THREE.MeshBasicMaterial({ color: col, transparent: true, opacity: 0.07, side: THREE.DoubleSide, depthWrite: false })));
    }
    R.verts = g.pts.map((p, v) => { const m = new THREE.Mesh(new THREE.SphereGeometry(0.07, 14, 10), new THREE.MeshBasicMaterial({ color: col.clone() })); m.position.copy(P(p)); m.userData = { v }; shape.add(m); return m; });
    R.ind = new THREE.Mesh(new THREE.SphereGeometry(0.09, 16, 12), new THREE.MeshBasicMaterial({ color: new THREE.Color(SKIN.screen.bright) })); shape.add(R.ind);
    const tg = new THREE.BufferGeometry(); tg.setAttribute('position', new THREE.Float32BufferAttribute(new Float32Array(MAXTRAIL * 3), 3)); tg.setAttribute('color', new THREE.Float32BufferAttribute(new Float32Array(MAXTRAIL * 3), 3));
    R.trail = new THREE.Line(tg, new THREE.LineBasicMaterial({ vertexColors: true, transparent: true, blending: THREE.AdditiveBlending, depthWrite: false })); shape.add(R.trail);
    R.sel = new THREE.Mesh(new THREE.RingGeometry(0.12, 0.15, 32), new THREE.MeshBasicMaterial({ color: new THREE.Color(SKIN.screen.bright), side: THREE.DoubleSide, transparent: true })); R.sel.visible = false; shape.add(R.sel);
    R.shape = shape; R.g = g; R.k = k; R.P = P; tilt.add(shape);
    R.scene.fog = new THREE.Fog(new THREE.Color(SKIN.screen.glass), 4.2, 8.5);
  }
  // Where the travelling point is: from vertex `a` toward `b`, `f` of the way (along the geodesic when hyperbolic).
  function pointAt(a, b, f) { const g = R.g; if (!g.pts[a] || !g.pts[b]) return null; return R.P(g.hyper ? geodesic(g.pts[a], g.pts[b], f) : g.pts[a].map((x, i) => x + (g.pts[b][i] - x) * f)); }

  function renderEditor(s) {
    const el = document.getElementById('geoVedit'); if (!el || !s.table) return;
    const p = per(s), v = p.sel, n = R && R.g ? R.g.pts.length : 0;
    if (v < 0 || v >= n) { el.innerHTML = `<p class="note">Click a vertex to edit it: on or off, its chance, velocity, accent and gate.</p>`; return; }
    const row = s.table.v[v], col = (sym) => s.table.cols.find((c) => c.sym === sym);
    const tog = (sym) => { const c = col(sym), on = row[c.j] >= 0.5; return `<button class="btn mini ${on ? 'on' : ''}" data-vtog="${v}:${c.j}" aria-pressed="${on}">${c.label === 'ON' ? (on ? 'ON' : 'OFF') : `${c.label} ${on ? 'ON' : 'OFF'}`}</button>`; };
    const sl = (sym) => { const c = col(sym); return `<label class="depth" for="vc-${v}-${c.j}"><span>${c.label}</span><input type="range" id="vc-${v}-${c.j}" data-vcell="${v}:${c.j}" min="0" max="1" step="0.01" value="${row[c.j]}" style="accent-color:${s.color}"><span class="mono" data-vdisp="${v}:${c.j}">${c.fmt(row[c.j])}</span></label>`; };
    el.innerHTML = `<b style="letter-spacing:.08em">VERTEX ${v + 1} of ${n}</b><div class="row">${tog('on')}${tog('accent')}<button class="btn mini" data-vclose="1">Done</button></div>${sl('prob')}${sl('vel')}${sl('gate')}`;
  }
  function wireEditor(s) {
    const el = document.getElementById('geoVedit'); if (!el) return;
    el.addEventListener('input', (e) => { const d = e.target.dataset; if (!d.vcell) return; const [r, j] = d.vcell.split(':').map(Number); setSrcCell(s, r, j, Number(e.target.value));
      const sp = el.querySelector(`[data-vdisp="${d.vcell}"]`); if (sp) sp.textContent = s.table.cols[j].fmt(s.table.v[r][j]); });
    el.addEventListener('click', (e) => { const b = e.target.closest('[data-vtog],[data-vclose]'); if (!b) return;
      if (b.dataset.vclose) per(s).sel = -1; else { const [r, j] = b.dataset.vtog.split(':').map(Number); setSrcCell(s, r, j, s.table.v[r][j] >= 0.5 ? 0 : 1); }
      renderEditor(s); });
  }

  return {
    html: () => `<div class="srcview"><div id="geoCanvasSlot"></div><div id="geoVedit" class="vedit"></div></div>`,
    mount(s) {
      ensure(); const slot = document.getElementById('geoCanvasSlot'); if (!slot) return;
      slot.appendChild(R.renderer.domElement); renderEditor(s); wireEditor(s);
    },
    frame(s) {
      if (!R || !R.renderer.domElement.isConnected || !VIEWX) return;
      const p = per(s), tnow = performance.now();             // flashes and rings fade on the wall clock (slow frames don't stretch them)
      // the shape: rebuilt only when what it depends on changes (knobs after cross-modulation, settings)
      const key = s.id + '|' + s.color + '|' + s.params.map((q) => srcEff(q).toFixed(3)).join(',') + '|' + s.settings.map((q) => q.v.toFixed(4)).join(',');
      if (key !== p.key || !R.g || R.builtFor !== s.id) {
        const g = shapeOf(s);
        if (!R.g || p.key.split('|').slice(0, 2).join() !== key.split('|').slice(0, 2).join() || JSON.stringify(g) !== JSON.stringify(R.g) || R.builtFor !== s.id) { build(s, g); p.trail = []; }
        p.key = key; R.builtFor = s.id;
        if (p.sel >= g.pts.length) { p.sel = -1; renderEditor(s); }
      }
      const off = new THREE.Color(SKIN.screen.off), on = new THREE.Color(s.color), white = new THREE.Color(SKIN.screen.bright);
      // flashes the engine reported since the last frame (meters(), below), and where the point is now
      p.pending.splice(0).forEach((v) => {
        if (v >= R.verts.length) return;
        p.flash[v] = 1; const ring = new THREE.Mesh(new THREE.RingGeometry(0.1, 0.13, 40), new THREE.MeshBasicMaterial({ color: on.clone(), transparent: true, opacity: 1, side: THREE.DoubleSide, blending: THREE.AdditiveBlending, depthWrite: false }));
        ring.position.copy(R.verts[v].position); ring.userData.born = performance.now(); R.shape.add(ring); p.pulses.push(ring);
        while (p.pulses.length > 12) { const old = p.pulses.shift(); R.shape.remove(old); old.geometry.dispose(); old.material.dispose(); }
      });
      if (p.step) { const pt = pointAt(p.step[0], p.step[1], p.step[2]); if (pt) { R.ind.position.copy(pt); if (!p.trail.length || !p.trail[0].equals(pt)) p.trail.unshift(pt.clone()); } }
      // trail: the last positions, fading out (TRAIL sets how many)
      const keep = Math.max(2, Math.round(MAXTRAIL * settingReal(s, 'trail', 50) / 100)); if (p.trail.length > keep) p.trail.length = keep;
      const pos = R.trail.geometry.attributes.position, colr = R.trail.geometry.attributes.color;
      for (let i = 0; i < MAXTRAIL; i++) { const q = p.trail[Math.min(i, p.trail.length - 1)] || R.ind.position, fade = i < p.trail.length ? 1 - i / p.trail.length : 0;
        pos.setXYZ(i, q.x, q.y, q.z); colr.setXYZ(i, on.r * fade, on.g * fade, on.b * fade); }
      pos.needsUpdate = colr.needsUpdate = true; R.trail.geometry.setDrawRange(0, Math.max(2, p.trail.length));
      // vertices: dim when switched off, brighter and bigger when they fire
      const decay = Math.exp(-(tnow - (p.lastFrame || tnow)) / 220); p.lastFrame = tnow;
      R.verts.forEach((m, v) => { const enabled = !s.table || s.table.v[v][0] >= 0.5; p.flash[v] *= decay;
        m.material.color.copy(enabled ? on : off).lerp(white, p.flash[v] * 0.8); m.scale.setScalar(1 + 1.4 * p.flash[v]); });
      p.pulses = p.pulses.filter((r) => { const u = (tnow - r.userData.born) / 450; if (u >= 1) { R.shape.remove(r); r.geometry.dispose(); r.material.dispose(); return false; }
        r.scale.setScalar(1 + 2.2 * u); r.material.opacity = (1 - u) * (1 - u); r.quaternion.copy(R.camera.quaternion); return true; });
      if (p.sel >= 0 && R.verts[p.sel]) { R.sel.visible = true; R.sel.position.copy(R.verts[p.sel].position); R.sel.quaternion.copy(R.camera.quaternion); } else R.sel.visible = false;
      R.tilt.rotation.x = -settingReal(s, 'tilt', 35) * Math.PI / 180;    // its orbital plane
      R.spin.rotation.y = p.yaw; R.spin.rotation.x = p.pitch;
      R.renderer.setClearColor(new THREE.Color(SKIN.screen.glass), 1);
      R.renderer.render(R.scene, R.camera);
    },
    // Every meter packet from the engine (about 31 a second, for every instance, open or not): the "step"
    // meter is from vertex + 32 x to vertex + 1024 x how far along (0..1023); "fired" is 32 x events fired
    // + the last vertex that fired. A flash is queued only when the engine's count goes up.
    meters(s) {
      const p = per(s), step = srcMeter(s, 'step', null), fired = srcMeter(s, 'fired', null);
      if (step === null || fired === null) return;
      p.step = [step % 32, Math.floor(step / 32) % 32, Math.floor(step / 1024) / 1024];
      const count = Math.floor(fired / 32), v = fired % 32;
      if (p.lastCount === null) { p.lastCount = count; return; }
      if (count === p.lastCount) return;
      const delta = (count - p.lastCount + 524288) % 524288; p.lastCount = count;
      if (state.focus === 'src:' + s.id) p.pending.push(v);
      pulseLog.push({ id: s.id, count, vertex: v, delta, t: performance.now() }); if (pulseLog.length > 2000) pulseLog.shift();
    },
    // test helpers: where a vertex is on screen (client px), and the view's state
    vertexScreen(i) { if (!R || !R.verts[i]) return null; const v = R.verts[i].getWorldPosition(new THREE.Vector3()).project(R.camera), r = R.renderer.domElement.getBoundingClientRect();
      return { x: r.left + (v.x + 1) / 2 * r.width, y: r.top + (1 - v.y) / 2 * r.height }; },
    info: (id) => { const p = inst[id]; return p && R ? { sel: p.sel, verts: R.verts.length, hyper: !!(R.g && R.g.hyper), ind: R.ind ? R.ind.position.toArray() : null, trail: p.trail.length } : null; },
  };
})();
rig.geoView = SRCVIEW.geo;

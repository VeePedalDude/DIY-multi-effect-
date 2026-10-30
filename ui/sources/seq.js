// ui/sources/seq.js - GATE SEQ's panel: the squares and the step lanes.
//  - Squares, 8 to a row (two beats of 1/16): click one to switch it on or off, or drag across several to paint. The
//    playing square is outlined, a square flashes when it fires; in EUCLID, squares FILL leaves out are
//    dimmed. Keyboard: Tab to a square, Enter or Space.
//  - Lanes under them, lined up with the squares: draw each step's VEL, PROB, RATCHET (1-4), GATE or
//    VALUE by dragging.
//  - Tools: CLEAR, ALL, INVERT, RANDOM (about half on), and rotate the whole pattern a step left or right.
// The playhead and flashes come from the engine's "step" and "fired" meters; which squares EUCLID plays
// comes from the engine's own code (rig_view).
SRCVIEW.seq = (() => {
  const LANES = ['vel', 'prob', 'ratchet', 'gate', 'value'], PER = 8, ROWH = 110;   // squares per row; lane row height (canvas px)
  const inst = {};
  const per = (s) => inst[s.id] || (inst[s.id] = { lane: 'vel', lastCount: null, cur: -1, flash: {}, mask: null, key: '', paint: null, drawing: false });
  const flashLog = []; rig.seqFlashes = flashLog;            // for tests: every flash, with the engine count behind it
  const setting = (s, sym) => { const q = s.settings.find((x) => x.sym === sym); return q ? q.real(q.v) : 0; };
  const stepsOf = (s) => Math.round(setting(s, 'steps'));
  const col = (s, sym) => s.table.cols.findIndex((c) => c.sym === sym);

  // which squares can play (EUCLID leaves some out), from the engine's own code
  function maskOf(s) {
    const X = VIEWX, buf = new Float32Array(X.memory.buffer, X.rig_view_buf(), 64 + 512);
    s.params.forEach((q, i) => { buf[i] = srcEff(q); }); s.settings.forEach((q, i) => { buf[DESC.maxpar + i] = q.v; });
    const n = X.rig_view(TYPES[s.type].index), o = new Float32Array(X.memory.buffer, X.rig_view_buf() + 64 * 4, n);
    return Array.from(o.slice(1, 1 + o[0]), (x) => x > 0.5);
  }
  function squaresHtml(s) {
    const n = stepsOf(s), on = col(s, 'on'), vel = col(s, 'vel');
    let h = '';
    for (let i = 0; i < n; i++) {
      const lit = s.table.v[i][on] >= 0.5, v = s.table.v[i][vel];
      h += `<button class="sq${lit ? ' on' : ''}${i % 4 === 0 ? ' beat' : ''}" data-sq="${i}" aria-pressed="${lit}" aria-label="Step ${i + 1}${lit ? ', on' : ', off'}"
        style="${lit ? `background:${s.color};opacity:${(0.45 + 0.55 * v).toFixed(2)}` : ''}"></button>`;
    }
    return h;
  }
  function refreshSquares(s) { const g = document.getElementById('seqGrid'); if (g) g.innerHTML = squaresHtml(s); per(s).cur = -1; }
  function setOn(s, i, lit) { const j = col(s, 'on'); if ((s.table.v[i][j] >= 0.5) !== lit) setSrcCell(s, i, j, lit ? 1 : 0); }

  // the lane under the squares: one column per step, rows of 8 like the squares
  function laneGeom(s) { const c = document.getElementById('seqLane'); if (!c) return null; const n = stepsOf(s), rows = Math.ceil(n / PER); return { c, n, rows, cw: c.width / PER, rh: c.height / rows }; }
  function drawLane(s) {
    const g = laneGeom(s); if (!g) return; const p = per(s), x = g.c.getContext('2d'), j = col(s, p.lane), cdesc = s.table.cols[j];
    x.clearRect(0, 0, g.c.width, g.c.height); x.fillStyle = SKIN.screen.glass; x.fillRect(0, 0, g.c.width, g.c.height);
    for (let i = 0; i < g.n; i++) {
      const cx = (i % PER) * g.cw, cy = Math.floor(i / PER) * g.rh, v = s.table.v[i][j], lit = s.table.v[i][col(s, 'on')] >= 0.5;
      x.fillStyle = i % 4 === 0 ? SKIN.screen.track : SKIN.screen.grid; x.fillRect(cx + 2, cy + 3, g.cw - 4, g.rh - 6);
      x.fillStyle = lit ? s.color : SKIN.screen.off;
      if (p.lane === 'value') { const mid = cy + g.rh / 2, hgt = (v - 0.5) * (g.rh - 8); x.fillRect(cx + 4, Math.min(mid, mid - hgt), g.cw - 8, Math.max(1, Math.abs(hgt))); x.fillStyle = SKIN.screen.dim; x.fillRect(cx + 2, mid, g.cw - 4, 1); }
      else if (p.lane === 'ratchet') { const r = Math.round(cdesc.real(v)), bh = (g.rh - 10) / 4; for (let k = 0; k < r; k++) x.fillRect(cx + 4, cy + g.rh - 5 - (k + 1) * bh + 1, g.cw - 8, bh - 2); }
      else { const hgt = v * (g.rh - 8); x.fillRect(cx + 4, cy + g.rh - 4 - hgt, g.cw - 8, hgt); }
    }
    const lbl = document.getElementById('seqLaneVal'); if (lbl && p.hover != null && p.hover < g.n) lbl.textContent = `step ${p.hover + 1}: ${cdesc.fmt(s.table.v[p.hover][j])}`;
  }
  function laneAt(s, e) {
    const g = laneGeom(s); if (!g) return null; const r = g.c.getBoundingClientRect(), px = (e.clientX - r.left) / r.width * g.c.width, py = (e.clientY - r.top) / r.height * g.c.height;
    const i = Math.floor(py / g.rh) * PER + Math.floor(px / g.cw); if (i < 0 || i >= g.n) return null;
    const cy = Math.floor(py / g.rh) * g.rh, u = clamp(1 - (py - cy - 4) / (g.rh - 8), 0, 1);
    return { i, u };
  }
  function paintLane(s, e) {
    const at = laneAt(s, e); if (!at) return; const p = per(s), j = col(s, p.lane), c = s.table.cols[j];
    let v = at.u; if (p.lane === 'ratchet') v = Math.round(v * 3) / 3; else v = Math.round(v * 100) / 100;
    if (Math.abs(c.real(v) - c.real(s.table.v[at.i][j])) > 1e-9) setSrcCell(s, at.i, j, v);
    p.hover = at.i; drawLane(s);
  }
  function tool(s, t) {
    const n = stepsOf(s), on = col(s, 'on');
    if (t === 'clear' || t === 'all') for (let i = 0; i < n; i++) setOn(s, i, t === 'all');
    if (t === 'invert') for (let i = 0; i < n; i++) setOn(s, i, s.table.v[i][on] < 0.5);
    if (t === 'random') for (let i = 0; i < n; i++) setOn(s, i, Math.random() < 0.5);
    if (t === 'left' || t === 'right') {                      // rotate every lane of the first STEPS rows
      const rows = s.table.v.slice(0, n).map((r) => r.slice()), d = t === 'left' ? 1 : n - 1;
      for (let i = 0; i < n; i++) rows[(i + d) % n].forEach((v, j) => { if (s.table.v[i][j] !== v) setSrcCell(s, i, j, v); });
    }
    refreshSquares(s); drawLane(s);
  }
  return {
    html: (s) => {
      const p = per(s);
      return `<div class="seqview"><div class="seqgrid" id="seqGrid" role="group" aria-label="Steps: click to switch on or off, drag to paint">${squaresHtml(s)}</div>
      <div class="row seqtools">${['clear', 'all', 'invert', 'random'].map((t) => `<button class="btn mini" data-seqtool="${t}">${t.toUpperCase()}</button>`).join('')}
        <button class="btn mini" data-seqtool="left" aria-label="Rotate the pattern one step left">◀</button><button class="btn mini" data-seqtool="right" aria-label="Rotate the pattern one step right">▶</button></div>
      <div class="row seqlanes" role="group" aria-label="Lane to draw">${LANES.map((l) => `<button class="btn mini${p.lane === l ? ' on' : ''}" data-seqlane="${l}" aria-pressed="${p.lane === l}">${s.table.cols[col(s, l)].label}</button>`).join('')}
        <span class="mono muted" id="seqLaneVal" style="margin-left:auto;font-size:12px"></span></div>
      <canvas id="seqLane" class="seqlane" width="560" height="${Math.ceil(stepsOf(s) / PER) * ROWH}" aria-label="Drag to draw each step's ${s.table.cols[col(s, p.lane)].label}" role="img"></canvas></div>`;
    },
    mount(s) {
      const grid = document.getElementById('seqGrid'), lane = document.getElementById('seqLane'), p = per(s); if (!grid) return;
      p.cur = -1; p.key = '';
      const sqOf = (e) => { const b = e.target.closest('[data-sq]'); return b ? Number(b.dataset.sq) : null; };
      grid.addEventListener('pointerdown', (e) => { const i = sqOf(e); if (i === null) return; e.preventDefault(); p.paint = s.table.v[i][col(s, 'on')] < 0.5; setOn(s, i, p.paint); refreshSquares(s); drawLane(s); });
      grid.addEventListener('pointerover', (e) => { if (p.paint === null || !(e.buttons & 1)) return; const i = sqOf(e); if (i === null) return; setOn(s, i, p.paint); refreshSquares(s); drawLane(s); });
      grid.addEventListener('click', (e) => { if (e.detail !== 0) return; const i = sqOf(e); if (i === null) return; setOn(s, i, s.table.v[i][col(s, 'on')] < 0.5); refreshSquares(s); drawLane(s);   // keyboard
        const b = document.querySelector(`#seqGrid [data-sq="${i}"]`); if (b) b.focus(); });
      const root = grid.parentElement;
      root.addEventListener('click', (e) => {
        const t = e.target.closest('[data-seqtool]'); if (t) { tool(s, t.dataset.seqtool); return; }
        const l = e.target.closest('[data-seqlane]'); if (l) { p.lane = l.dataset.seqlane; root.querySelectorAll('[data-seqlane]').forEach((b) => { const on = b.dataset.seqlane === p.lane; b.classList.toggle('on', on); b.setAttribute('aria-pressed', on); }); drawLane(s); }
      });
      lane.addEventListener('pointerdown', (e) => { p.drawing = true; lane.setPointerCapture(e.pointerId); paintLane(s, e); });
      lane.addEventListener('pointermove', (e) => { if (p.drawing) paintLane(s, e); else { const at = laneAt(s, e); p.hover = at ? at.i : null; drawLane(s); } });
      lane.addEventListener('pointerup', () => { p.drawing = false; });
      drawLane(s);
    },
    // every meter packet: where the playhead is, and flashes when the engine's count of hits goes up
    meters(s) {
      const p = per(s), step = srcMeter(s, 'step', null), fired = srcMeter(s, 'fired', null); if (step === null || fired === null) return;
      if (p.now !== step % 32) { p.now = step % 32;               // the playhead moves on the engine's packet, not on a frame
        const grid = state.focus === 'src:' + s.id && document.getElementById('seqGrid');
        if (grid) [...grid.children].forEach((b, i) => b.classList.toggle('now', i === p.now)); }
      const count = Math.floor(fired / 32), sq = fired % 32;
      if (p.lastCount === null) { p.lastCount = count; return; }
      if (count === p.lastCount) return;
      const delta = (count - p.lastCount + 524288) % 524288; p.lastCount = count; p.flash[sq] = performance.now();
      flashLog.push({ id: s.id, count, square: sq, delta, t: performance.now() }); if (flashLog.length > 2000) flashLog.shift();
    },
    frame(s) {
      const grid = document.getElementById('seqGrid'), p = per(s); if (!grid || !VIEWX) return;
      if (grid.children.length !== stepsOf(s)) { refreshSquares(s); const c = document.getElementById('seqLane'); if (c) { c.height = Math.ceil(stepsOf(s) / PER) * ROWH; drawLane(s); } }
      const key = s.params.map((q) => srcEff(q).toFixed(3)).join(',') + '|' + s.settings.map((q) => q.v.toFixed(4)).join(',');
      if (key !== p.key) { p.key = key; p.mask = maskOf(s); }
      const tnow = performance.now();
      [...grid.children].forEach((b, i) => {
        b.classList.toggle('mute', !!p.mask && p.mask[i] === false);
        b.classList.toggle('hit', tnow - (p.flash[i] || -1e9) < 140);
      });
    },
    state: (id) => { const p = inst[id]; return p ? { now: p.now, lane: p.lane, mask: p.mask } : null; },
  };
})();
rig.seqView = SRCVIEW.seq;

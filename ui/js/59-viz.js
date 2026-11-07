// 59-viz.js - the Visualizer view: the effect cards and source orbs give way to a circular, layered
// picture of every modulation source in the rack. One ring per source, innermost first in rack
// order, in the source's colour. Each ring traces its last ~8 s of output around the circle, newest
// value at 12 o'clock (the head, with a dot whose size follows the value) and fading as it ages
// round to the gap just right of the top, where the ring's name sits. Cross-modulation (a source
// moving another source's knob) shows as a dashed link from the modulator's head, through the middle,
// to the bottom of the ring it moves (one point per knob). The core in the middle swells with how busy the rack is. Click a ring to edit that source.
// Along the bottom, a keyboard (E2 to E6, the guitar's range) lights the notes being played, with the
// chord's name above it (14-chords.js). The engine finds them (engine/core/listen.h, the tracker Poly
// Voice uses) only while this view is up: `listen 1` / `listen 0`.
// The 3D stage isn't drawn while this is up. Colours: the skin's `viz` section and source colours.
const vizCanvas = $('viz'), vizCtx = vizCanvas.getContext('2d');
const VIZ_N = 256, VIZ_SPAN = 0.92;              // samples per ring (~8 s at ~31 meter packets/s); share of the circle they use
const viz = { rings: [], hover: null, cx: 0, cy: 0, R: 0, r0: 0, W: 0, H: 0, dpr: 1, label: 0, kb: null, chord: null, chordT: -9, listenT: -9 };
const KEY_LO = 40, KEY_HI = 88, isBlack = (n) => [1, 3, 6, 8, 10].includes(n % 12);

// Every meter packet: one more point for each source (20-bridge.js calls this).
function vizPush() { SRC.forEach((s) => { if (!s.vhist) s.vhist = []; s.vhist.push(srcVal(s)); if (s.vhist.length > VIZ_N) s.vhist.shift(); }); }

function vizLayout() {
  const W = innerWidth, H = innerHeight, dpr = Math.min(2, devicePixelRatio || 1);
  if (vizCanvas.width !== Math.round(W * dpr) || vizCanvas.height !== Math.round(H * dpr)) { vizCanvas.width = Math.round(W * dpr); vizCanvas.height = Math.round(H * dpr); }
  const right = !panelHidden && W > 900 ? W - $('panel').offsetWidth - 32 : W - 16, left = 16;
  // the keyboard: above the bottom bar and the pedalboard, as wide as the view; the chord's name above it
  const floor = Math.min(H - $('bottom').offsetHeight, $('board').getBoundingClientRect().top) - 12;
  const kh = clamp(H * 0.13, 70, 120), kw = Math.min(right - left, kh * 9.5);
  viz.kb = { x0: (left + right) / 2 - kw / 2, x1: (left + right) / 2 + kw / 2, yb: floor - kh, yf: floor - kh * 0.22, t: kh * 0.16 };
  const top = $('top').offsetHeight + 14, bottom = viz.kb.yb - 64;
  const R = Math.max(40, Math.min(right - left, bottom - top) / 2 - 24), r0 = R * 0.2;
  const live = SRC.filter((s) => s.slot !== null), bw = (R - r0) / Math.max(1, live.length);
  Object.assign(viz, { W, H, dpr, cx: (left + right) / 2, cy: (top + bottom) / 2, R, r0,
    rings: live.map((s, i) => ({ s, rb: r0 + bw * (i + 0.5), bw })) });
}
// Angle of history point j of n: newest at the top, older counter-clockwise.
const vizAngle = (j, n) => -Math.PI / 2 - 2 * Math.PI * VIZ_SPAN * (n - 1 - j) / (VIZ_N - 1);
const vizAt = (r, a) => [viz.cx + r * Math.cos(a), viz.cy + r * Math.sin(a)];

function drawVizRing(c, ring, V) {
  const s = ring.s, h = s.vhist || [], n = h.length, amp = ring.bw * 0.42, hov = viz.hover === s.id, v = srcVal(s);
  c.save(); c.strokeStyle = V.track; c.globalAlpha = hov ? 0.95 : 0.6; c.lineWidth = ring.bw * 0.86;
  c.beginPath(); c.arc(viz.cx, viz.cy, ring.rb, 0, Math.PI * 2); c.stroke(); c.restore();
  if (n > 1) {
    const pts = h.map((x, j) => vizAt(ring.rb + clamp(x, -1, 1) * amp, vizAngle(j, n)));
    c.save(); c.fillStyle = s.color; c.globalAlpha = hov ? 0.2 : 0.12; c.beginPath();       // the area between the ring's centre line and the trace
    pts.forEach((p, j) => (j ? c.lineTo(p[0], p[1]) : c.moveTo(p[0], p[1])));
    c.arc(viz.cx, viz.cy, ring.rb, vizAngle(n - 1, n), vizAngle(0, n), true); c.closePath(); c.fill(); c.restore();
    const K = 16, step = Math.ceil(n / K);                                                   // the trace, fading with age
    c.save(); c.strokeStyle = s.color; c.lineWidth = hov ? 3 : 2; c.lineJoin = 'round';
    for (let k = 0; k * step < n - 1; k++) {
      const a = k * step, b = Math.min(n - 1, a + step);
      c.globalAlpha = 0.12 + 0.88 * Math.pow(b / (n - 1), 1.6);
      c.beginPath(); c.moveTo(pts[a][0], pts[a][1]); for (let j = a + 1; j <= b; j++) c.lineTo(pts[j][0], pts[j][1]); c.stroke();
    }
    c.restore();
  }
  const [hx, hy] = vizAt(ring.rb + clamp(v, -1, 1) * amp, -Math.PI / 2);                    // the head: where it is now
  c.save(); c.fillStyle = s.color; if (!RM) { c.shadowColor = s.color; c.shadowBlur = 8 + 14 * Math.abs(v); }
  c.beginPath(); c.arc(hx, hy, 3 + 4 * Math.abs(v) + (hov ? 1.5 : 0), 0, Math.PI * 2); c.fill(); c.restore();
  if (ring.bw >= 11) {                                                                         // its name in the gap right of the top
    const moves = Object.entries(state.conns).filter(([pid, sid]) => sid === s.id && knobOf(pid)).length;
    const fs = clamp(ring.bw * 0.34, 10, 15), [lx, ly] = vizAt(ring.rb, -Math.PI / 2 + 0.035 + 6 / ring.rb);
    c.save(); c.font = `600 ${fs}px ${SKIN.ui.fontDisplay}`; c.textBaseline = 'middle'; c.fillStyle = s.color;
    c.translate(lx, ly); c.fillText(s.name, 0, 0);
    const w = c.measureText(s.name).width; c.font = `400 ${fs * 0.85}px ${SKIN.ui.fontMono}`; c.fillStyle = hov ? V.bright : V.text;
    c.fillText(` ${v >= 0 ? '+' : ''}${v.toFixed(2)}${moves ? ` → ${moves}` : ''}`, w, 0); c.restore();
  }
}
// Where a link to knob `pi` of a ring lands: near 6 o'clock, spread by knob.
const vizLinkAngle = (pi) => Math.PI / 2 + 0.22 * pi;
// Cross-modulation: a source patched to another source's knob.
function drawVizLinks(c) {
  const at = Object.fromEntries(viz.rings.map((r) => [r.s.id, r]));
  let n = 0;
  Object.entries(state.conns).forEach(([pid, sid]) => {
    const q = SPARAMS[pid], from = at[sid], to = q && at[q.src.id]; if (!from || !to) return;
    const side = n++ % 2 ? 1 : -1;                                                           // bow left, then right, past the core
    const v = srcVal(from.s), [x0, y0] = vizAt(from.rb + clamp(v, -1, 1) * from.bw * 0.42, -Math.PI / 2);
    const [x1, y1] = vizAt(to.rb, vizLinkAngle(q.pi)), mx = viz.cx + side * (viz.r0 * 1.4 + 0.35 * Math.max(from.rb, to.rb)), my = viz.cy;
    c.save(); c.strokeStyle = from.s.color; c.lineWidth = 2; c.globalAlpha = 0.85; c.setLineDash([7, 5]); c.lineDashOffset = RM ? 0 : -T * 30;
    c.beginPath(); c.moveTo(x0, y0); c.quadraticCurveTo(mx, my, x1, y1); c.stroke();
    c.setLineDash([]); c.globalAlpha = 1; c.fillStyle = from.s.color; if (!RM) { c.shadowColor = from.s.color; c.shadowBlur = 10; }
    c.beginPath(); c.arc(x1, y1, 5, 0, Math.PI * 2); c.fill(); c.restore();
  });
}
function drawViz() {
  vizLayout(); const c = vizCtx, V = SKIN.viz, { cx, cy, R, r0 } = viz;
  c.setTransform(viz.dpr, 0, 0, viz.dpr, 0, 0); c.clearRect(0, 0, viz.W, viz.H);
  c.fillStyle = V.bg; c.fillRect(0, 0, viz.W, viz.H);
  c.save(); c.strokeStyle = V.grid; c.lineWidth = 1; c.beginPath();                       // spokes and the outer edges
  for (let k = 0; k < 12; k++) { const a = k * Math.PI / 6, [x0, y0] = vizAt(r0, a), [x1, y1] = vizAt(R, a); c.moveTo(x0, y0); c.lineTo(x1, y1); }
  c.stroke(); c.beginPath(); c.arc(cx, cy, R, 0, Math.PI * 2); c.stroke(); c.restore();
  viz.rings.forEach((ring) => drawVizRing(c, ring, V));
  drawVizLinks(c);
  drawKeys(c, V);
  const busy = viz.rings.length ? viz.rings.reduce((a, r) => a + Math.abs(srcVal(r.s)), 0) / viz.rings.length : 0;   // the core
  c.save(); c.fillStyle = V.core; c.globalAlpha = 0.12 + 0.3 * busy; c.beginPath(); c.arc(cx, cy, r0 * (0.55 + 0.35 * busy), 0, Math.PI * 2); c.fill(); c.restore();
  c.save(); c.textAlign = 'center'; c.textBaseline = 'middle'; c.fillStyle = V.bright; c.font = `700 ${clamp(r0 * 0.3, 14, 26)}px ${SKIN.ui.fontDisplay}`;
  const n = viz.rings.length;
  if (n) { c.fillText(String(n), cx, cy - r0 * 0.12); c.fillStyle = V.text; c.font = `400 ${clamp(r0 * 0.13, 10, 13)}px ${SKIN.ui.fontMono}`; c.fillText(n === 1 ? 'source' : 'sources', cx, cy + r0 * 0.2); }
  else { c.font = `600 18px ${SKIN.ui.fontDisplay}`; c.fillText('No modulation sources running', cx, cy - 12); c.fillStyle = V.text; c.font = `400 13px ${SKIN.ui.fontMono}`; c.fillText('add one from the 3D Stage (+ orb)', cx, cy + 14); }
  c.restore();
  if (T - viz.label > 1) {                                                                     // what a screen reader hears, once a second
    viz.label = T;
    const ch = viz.chord ? ` Playing: ${viz.chord.name || viz.chord.interval || viz.chord.notes.join(' ')}.` : '';
    vizCanvas.setAttribute('aria-label', (n ? `Visualizer, ${n} modulation source${n > 1 ? 's' : ''}: ` + viz.rings.map((r) => `${r.s.name} ${srcVal(r.s).toFixed(2)}`).join(', ') : 'Visualizer: no modulation sources running') + '.' + ch);
  }
}
// The notes the engine hears now: [{ n: MIDI (with its fraction), amp }], loudest first.
function heardNotes() {
  if (!meters || !LAYOUT || LAYOUT.notes === undefined) return [];
  const m = meters.m, o = LAYOUT.notes, k = Math.round(m[o + 1]), out = [];
  for (let i = 0; i < Math.min(k, LAYOUT.notesMax); i++) out.push({ n: m[o + 2 + i], amp: m[o + 2 + LAYOUT.notesMax + i] });
  return out;
}
// Keep the engine listening while the view is up (a restarted engine starts with it off).
function vizListen() {
  if (!meters || !LAYOUT || LAYOUT.notes === undefined || T - viz.listenT < 1) return;
  if (meters.m[LAYOUT.notes] === 0) { viz.listenT = T; rig.cmd('listen 1'); }
}
// A point on the keyboard: u across (0..1), d from the back (0) to the front edge (1); it narrows away.
function kbAt(u, d) { const k = viz.kb, cx = (k.x0 + k.x1) / 2, s = 0.9 + 0.1 * d; return [cx + (u - 0.5) * (k.x1 - k.x0) * s, k.yb + (k.yf - k.yb) * d]; }
function quad(c, a, b, d, e, fill, edge) { c.beginPath(); c.moveTo(a[0], a[1]); c.lineTo(b[0], b[1]); c.lineTo(d[0], d[1]); c.lineTo(e[0], e[1]); c.closePath(); c.fillStyle = fill; c.fill(); if (edge) { c.strokeStyle = edge; c.lineWidth = 1; c.stroke(); } }
// Where key `n` is: its top face's corners (for drawing and the tests).
function keyShape(n) {
  const whites = []; for (let x = KEY_LO; x <= KEY_HI; x++) if (!isBlack(x)) whites.push(x);
  const nw = whites.length;
  if (!isBlack(n)) { const i = whites.indexOf(n); return { black: false, u0: i / nw + 0.003, u1: (i + 1) / nw - 0.003, d1: 1 }; }
  const i = whites.indexOf(n - 1) + 1, bw = 0.6 / nw;
  return { black: true, u0: i / nw - bw / 2, u1: i / nw + bw / 2, d1: 0.62 };
}
function drawKeys(c, V) {
  vizListen();
  const k = viz.kb, heard = heardNotes(), lit = new Map(heard.map((x) => [Math.round(x.n), x.amp]));
  const top = Math.max(0, ...heard.map((x) => x.amp)) || 1;
  // the chord's name; it holds through a gap of a moment so a strum doesn't blink
  const ch = heard.length ? chordHeard(heard.map((x) => x.n)) : null;
  if (ch) { viz.chord = ch; viz.chordT = T; } else if (T - viz.chordT > 0.35) viz.chord = null;
  const cx = (k.x0 + k.x1) / 2, ny = k.yb - k.t * 1.1 - 36;
  c.save(); c.textAlign = 'center'; c.textBaseline = 'alphabetic';
  if (viz.chord) {
    const C = viz.chord, big = C.name || C.interval || C.notes.join(' ');
    c.fillStyle = V.bright; c.font = `700 ${C.name ? 30 : 20}px ${SKIN.ui.fontDisplay}`; c.fillText(big, cx, ny);
    c.fillStyle = V.text; c.font = `400 12px ${SKIN.ui.fontMono}`; c.fillText(C.notes.join('  '), cx, ny + 20);
  } else { c.fillStyle = V.text; c.font = `400 14px ${SKIN.ui.fontMono}`; c.fillText('play a note or a chord', cx, ny + 10); }
  c.restore();
  const press = (n) => (lit.has(n) ? 1 : 0), glow = (n) => 0.45 + 0.55 * Math.min(1, (lit.get(n) || 0) / top);
  c.save(); c.lineJoin = 'round';
  for (let n = KEY_LO; n <= KEY_HI; n++) {                                     // white keys: top, then the front edge
    if (isBlack(n)) continue;
    const s = keyShape(n), dy = press(n) * k.t * 0.45;
    const a = kbAt(s.u0, 0), b = kbAt(s.u1, 0), f1 = kbAt(s.u1, 1), f0 = kbAt(s.u0, 1);
    const up = (p) => [p[0], p[1] + dy];
    quad(c, up(f0), up(f1), [f1[0], f1[1] + k.t], [f0[0], f0[1] + k.t], lit.has(n) ? V.keyLit : V.keySide, V.keyEdge);
    if (lit.has(n)) { c.save(); c.globalAlpha = glow(n); if (!RM) { c.shadowColor = V.keyLit; c.shadowBlur = 18; } quad(c, up(a), up(b), up(f1), up(f0), V.keyLit, V.keyEdge); c.restore(); }
    else quad(c, a, b, f1, f0, V.keyWhite, V.keyEdge);
  }
  for (let n = KEY_LO; n <= KEY_HI; n++) {                                     // black keys, raised above the white ones
    if (!isBlack(n)) continue;
    const s = keyShape(n), h = k.t * (1.1 - 0.5 * press(n));
    const a = kbAt(s.u0, 0), b = kbAt(s.u1, 0), f1 = kbAt(s.u1, s.d1), f0 = kbAt(s.u0, s.d1), lift = (p) => [p[0], p[1] - h];
    quad(c, lift(f0), lift(f1), f1, f0, V.keyEdge, V.keyEdge);
    if (lit.has(n)) { c.save(); c.globalAlpha = glow(n); if (!RM) { c.shadowColor = V.keyLit; c.shadowBlur = 18; } quad(c, lift(a), lift(b), lift(f1), lift(f0), V.keyLit, V.keyEdge); c.restore(); }
    else quad(c, lift(a), lift(b), lift(f1), lift(f0), V.keyBlack, V.keyEdge);
  }
  c.textAlign = 'center'; c.textBaseline = 'top';                              // names: the lit keys, and each C to find your way
  for (let n = KEY_LO; n <= KEY_HI; n++) {
    if (!lit.has(n) && n % 12 !== 0) continue;
    const s = keyShape(n), [x, y] = kbAt((s.u0 + s.u1) / 2, s.black ? 0.05 : 1);
    c.font = `${lit.has(n) ? 700 : 400} ${lit.has(n) ? 12 : 10}px ${SKIN.ui.fontMono}`;
    c.fillStyle = lit.has(n) ? V.bright : V.text;
    c.fillText(midiName(n), x, s.black ? y - k.t * 1.1 - 16 : y + k.t + 4);
  }
  c.restore();
}
// The ring under a point (page coordinates), or null.
function vizRingAt(x, y) {
  const d = Math.hypot(x - viz.cx, y - viz.cy);
  const r = viz.rings.find((g) => Math.abs(d - g.rb) <= g.bw / 2); return r ? r.s.id : null;
}
function vizShow(on) {
  rig.cmd(`listen ${on ? 1 : 0}`); viz.listenT = T; viz.chord = null;
  vizCanvas.hidden = !on; canvas.style.visibility = on ? 'hidden' : '';
  $('overview').disabled = on; document.body.classList.toggle('viz-on', on);
  if (on) drawViz();
}
function wireViz() {
  vizCanvas.addEventListener('pointermove', (e) => { const id = vizRingAt(e.clientX, e.clientY); viz.hover = id; vizCanvas.style.cursor = id ? 'pointer' : ''; });
  vizCanvas.addEventListener('pointerleave', () => { viz.hover = null; });
  vizCanvas.addEventListener('click', (e) => {
    const id = vizRingAt(e.clientX, e.clientY); if (!id) return;
    state.focus = 'src:' + id; if (panelHidden) setPanelHidden(false); update();
  });
  window.addEventListener('keydown', (e) => {
    if (e.target.closest && e.target.closest('input,textarea,select')) return;
    if ((e.key === 'v' || e.key === 'V') && !e.ctrlKey && !e.metaKey && !e.altKey) setView(state.view === 'viz' ? 'stage' : 'viz');
  });
}
// Test helpers: the rings as drawn (centre, radii, colour, value now) and the view's state.
rig.viz = {
  rings: () => viz.rings.map((r) => ({ id: r.s.id, rb: r.rb, bw: r.bw, color: r.s.color, value: srcVal(r.s), points: (r.s.vhist || []).length })),
  centre: () => ({ x: viz.cx, y: viz.cy, R: viz.R, r0: viz.r0 }),
  linkEnd: (id, pi) => { const r = viz.rings.find((g) => g.s.id === id); if (!r) return null; const [x, y] = vizAt(r.rb, vizLinkAngle(pi)); return { x, y }; },
  head: (id) => { const r = viz.rings.find((g) => g.s.id === id); if (!r) return null; const [x, y] = vizAt(r.rb + clamp(srcVal(r.s), -1, 1) * r.bw * 0.42, -Math.PI / 2); return { x, y }; },
  pixel: (x, y) => Array.from(vizCtx.getImageData(Math.round(x * viz.dpr), Math.round(y * viz.dpr), 1, 1).data),
  draw: () => drawViz(),
  stageFrames: () => renderer.info.render.frame,
  // the keyboard: what the engine hears, the chord's name, and where a key's top face is (its centre)
  colors: () => SKIN.viz,
  keys: () => ({ on: meters && LAYOUT.notes !== undefined ? meters.m[LAYOUT.notes] : null, heard: heardNotes(), chord: viz.chord, kb: viz.kb }),
  keyAt: (n) => { const s = keyShape(n), [x, y] = kbAt((s.u0 + s.u1) / 2, s.black ? 0.3 : 0.6); return { x, y: s.black ? y - viz.kb.t * 1.1 : y }; },        // 3D frames drawn so far (the Visualizer stops them)
};

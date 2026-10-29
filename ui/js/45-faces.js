// 45-faces.js - draws module faces (title, screen, knobs) and source labels onto canvas textures.
// The screen area is delegated to SCREENS[module id] (ui/screens/*.js); otherwise genericScreen.
function arcDeg(c, cx, cy, r, a0, a1, col, w) { c.beginPath(); c.arc(cx, cy, r, (a0 - 90) * Math.PI / 180, (Math.max(a1, a0 + .5) - 90) * Math.PI / 180); c.strokeStyle = col; c.lineWidth = w; c.lineCap = 'round'; c.stroke(); }
function drawKnob(c, cx, cy, p) {
  const R = 64, a = -135 + 270 * p.v, e = effOf(p), sid = state.conns[p.id], S = sid && SRCMAP[sid];
  if (state.carrying) { c.save(); c.setLineDash([10, 10]); c.beginPath(); c.arc(cx, cy, 100, 0, Math.PI * 2); c.strokeStyle = SRCMAP[state.carrying].color; c.lineWidth = 4; c.stroke(); c.restore(); }
  arcDeg(c, cx, cy, R, -135, 135, '#263149', 10);
  arcDeg(c, cx, cy, R, -135, a, ACC, 10);
  if (S) {
    const dp = depthOf(p.id), lo = clamp(p.v - dp, 0, 1), hi = clamp(p.v + dp, 0, 1);
    const ga = c.globalAlpha; c.globalAlpha = ga * .5; arcDeg(c, cx, cy, R + 19, -135 + 270 * lo, -135 + 270 * hi, S.color, 7); c.globalAlpha = ga;
    const ea = (-135 + 270 * e - 90) * Math.PI / 180; c.beginPath(); c.arc(cx + (R + 19) * Math.cos(ea), cy + (R + 19) * Math.sin(ea), 9, 0, Math.PI * 2); c.fillStyle = S.color; c.fill();
  }
  c.beginPath(); c.arc(cx, cy, 44, 0, Math.PI * 2); c.fillStyle = '#151C2E'; c.fill(); c.strokeStyle = '#2A3550'; c.lineWidth = 2; c.stroke();
  const pa = (-135 + 270 * e - 90) * Math.PI / 180;
  c.beginPath(); c.moveTo(cx + 14 * Math.cos(pa), cy + 14 * Math.sin(pa)); c.lineTo(cx + 38 * Math.cos(pa), cy + 38 * Math.sin(pa)); c.strokeStyle = '#E6ECF5'; c.lineWidth = 6; c.lineCap = 'round'; c.stroke();
  c.textAlign = 'center'; c.fillStyle = '#E6ECF5'; c.font = '700 25px "Chakra Petch", sans-serif'; c.fillText(p.label, cx, cy + 112);
  c.fillStyle = S ? S.color : '#94A3BD'; c.font = '500 21px "IBM Plex Mono", monospace'; c.fillText(p.fmt(e), cx, cy + 144); c.textAlign = 'left';
}
// Helper handed to every screen: live values for this module, by index or meter name.
function screenHelper(m) {
  return { eff: (i) => effOf(m.params[i]), param: (i) => m.params[i], meter: (name, fb) => modMeter(m, name, fb),
    inPk: meters ? meters.inPk : null, T, ACC, data: MODDATA[m.id] || (MODDATA[m.id] = {}) };
}
function genericScreen(c, b, m, h) {               // bars of each effective parameter value
  const n = m.params.length, bw = (b.w - 40) / n;
  m.params.forEach((p, i) => {
    const v = h.eff(i), bh = (b.h - 50) * v;
    c.globalAlpha = .9; c.fillRect(b.x + 20 + i * bw + 6, b.y + b.h - 14 - bh, bw - 12, bh);
  });
  c.globalAlpha = 1; c.fillStyle = '#94A3BD'; c.fillText('live parameters', b.x + 20, b.y + 26);
}
function drawScreen(m, c) {
  const b = { x: 34, y: 132, w: 444, h: 150 };
  c.save(); rr(c, b.x, b.y, b.w, b.h, 14); c.fillStyle = '#070B14'; c.fill(); c.strokeStyle = '#1F2A40'; c.lineWidth = 2; c.stroke(); c.clip();
  c.strokeStyle = ACC; c.fillStyle = ACC; c.lineWidth = 3; c.font = '500 18px "IBM Plex Mono", monospace';
  try { (SCREENS[m.id] || genericScreen)(c, b, m, screenHelper(m)); }
  catch (err) { c.fillStyle = '#FF5A6E'; c.fillText('screen error: ' + err.message.slice(0, 30), b.x + 20, b.y + 26); }
  c.restore();
}
// Subtitle under a module's name: the loaded file if there is one, otherwise the module's kind.
function moduleSubtitle(m) { const an = MODDATA[m.id] && MODDATA[m.id].assetName; return an ? `${m.asset.kind.toUpperCase()} · ${an}` : m.kind; }
function drawFace(m) {
  const c = m.ctx, byp = !!state.bypass[m.id], foc = state.focus === m.id;
  c.globalAlpha = 1; c.clearRect(0, 0, FW, m.FH);
  rr(c, 6, 6, FW - 12, m.FH - 12, 28); c.fillStyle = '#0C1220'; c.fill(); c.lineWidth = foc ? 6 : 3; c.strokeStyle = foc ? ACC : '#2A3550'; c.stroke();
  c.globalAlpha = byp ? .42 : 1; c.textBaseline = 'alphabetic';
  c.beginPath(); c.arc(44, 56, 10, 0, Math.PI * 2); c.fillStyle = byp ? '#3A4766' : '#9BE15D'; c.fill();
  c.fillStyle = '#E6ECF5'; c.font = '700 38px "Chakra Petch", sans-serif'; c.fillText(m.name, 66, 70);
  const sub = moduleSubtitle(m);
  c.fillStyle = '#94A3BD'; c.font = '400 21px "IBM Plex Mono", monospace'; c.fillText(sub.length > 34 ? sub.slice(0, 33) + '…' : sub, 34, 112);
  drawScreen(m, c); m.params.forEach((p) => { const [kx, ky] = knobXY(p); drawKnob(c, kx, ky, p); });
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

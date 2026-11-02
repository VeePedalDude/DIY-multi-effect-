// 45-faces.js - draws module faces (card, title, screen, knobs) and source labels onto canvas textures.
// Every colour and image comes from the active skin (SKIN, 04-skin-core.js): each module gets a tile
// (card frame + colours) from the skin's module map or its tile order. The screen area is delegated
// to SCREENS[module id] (ui/screens/*.js); otherwise genericScreen.
const SKIN_IMG = {};                      // decoded images of the active skin, by file name (57-skins.js fills it)
const tileOf = (m) => tileNameFor(SKIN, m.id, m.idx);
const styleOf = (m) => tileStyle(SKIN, tileOf(m));
// The card at FW x H: the tile's frame image, or a plain rounded card in the tile's colours.
// Taller faces (more than four knobs) stretch a plain strip of the side rails.
function drawCard(c, st, H) {
  const im = st.image && SKIN_IMG[st.image];
  if (!im) { rr(c, 6, 6, FW - 12, H - 12, 30); c.fillStyle = st.card.plain; c.fill(); c.strokeStyle = st.rim; c.lineWidth = 3; c.stroke(); return; }
  if (H <= CARD_H) { c.drawImage(im, 0, 0, FW, H); return; }
  const ih = im.naturalHeight || im.height, k = ih / CARD_H, a = 400, b = 470, extra = H - CARD_H;   // source rows scaled to the image
  c.drawImage(im, 0, 0, im.naturalWidth || im.width, a * k, 0, 0, FW, a);
  c.drawImage(im, 0, a * k, im.naturalWidth || im.width, (b - a) * k, 0, a, FW, b - a + extra);
  c.drawImage(im, 0, b * k, im.naturalWidth || im.width, (CARD_H - b) * k, 0, b + extra, FW, CARD_H - b);
}

function arcDeg(c, cx, cy, r, a0, a1, col, w) { c.beginPath(); c.arc(cx, cy, r, (a0 - 90) * Math.PI / 180, (Math.max(a1, a0 + .5) - 90) * Math.PI / 180); c.strokeStyle = col; c.lineWidth = w; c.lineCap = 'round'; c.stroke(); }
function drawKnob(c, cx, cy, p, st) {
  const K = st.knob, R = 56, a = -135 + 270 * p.v, e = effOf(p), sid = state.conns[p.id], S = sid && SRCMAP[sid];
  if (state.carrying) { c.save(); c.setLineDash([10, 10]); c.beginPath(); c.arc(cx, cy, 88, 0, Math.PI * 2); c.strokeStyle = SRCMAP[state.carrying].color; c.lineWidth = 4; c.stroke(); c.restore(); }
  arcDeg(c, cx, cy, R, -135, 135, K.track, 9);
  arcDeg(c, cx, cy, R, -135, a, st.accent, 9);
  if (S) {
    const dp = depthOf(p.id), lo = clamp(p.v - dp, 0, 1), hi = clamp(p.v + dp, 0, 1);
    const ga = c.globalAlpha; c.globalAlpha = ga * .7; arcDeg(c, cx, cy, R + 16, -135 + 270 * lo, -135 + 270 * hi, S.color, 6); c.globalAlpha = ga;
    const ea = (-135 + 270 * e - 90) * Math.PI / 180;
    c.beginPath(); c.arc(cx + (R + 16) * Math.cos(ea), cy + (R + 16) * Math.sin(ea), 8, 0, Math.PI * 2); c.fillStyle = S.color; c.fill(); c.strokeStyle = K.pointer; c.lineWidth = 2; c.stroke();
  }
  const g = c.createRadialGradient(cx - 12, cy - 14, 4, cx, cy, 40); g.addColorStop(0, K.bodyLight); g.addColorStop(.55, K.bodyMid); g.addColorStop(1, K.bodyDark);
  c.beginPath(); c.arc(cx, cy, 39, 0, Math.PI * 2); c.fillStyle = g; c.fill(); c.strokeStyle = K.rim; c.lineWidth = 2; c.stroke();
  const pa = (-135 + 270 * e - 90) * Math.PI / 180;
  c.beginPath(); c.moveTo(cx + 12 * Math.cos(pa), cy + 12 * Math.sin(pa)); c.lineTo(cx + 34 * Math.cos(pa), cy + 34 * Math.sin(pa)); c.strokeStyle = K.pointer; c.lineWidth = 6; c.lineCap = 'round'; c.stroke();
  c.textAlign = 'center'; c.fillStyle = K.label; c.font = `700 23px ${SKIN.ui.fontDisplay}`; c.fillText(p.label, cx, cy + 92);
  c.fillStyle = K.value; c.font = `500 19px ${SKIN.ui.fontMono}`; c.fillText(p.fmt(e), cx, cy + 120);
  if (S) { c.beginPath(); c.arc(cx - c.measureText(p.fmt(e)).width / 2 - 12, cy + 114, 5, 0, Math.PI * 2); c.fillStyle = S.color; c.fill(); }
  c.textAlign = 'left';
}
// Helper handed to every screen: live values for this module, by index or meter name.
function screenHelper(m) {
  return { eff: (i) => effOf(m.params[i]), param: (i) => m.params[i], meter: (name, fb) => modMeter(m, name, fb),
    inPk: meters ? meters.inPk : null, T, ACC, col: SKIN.screen, data: MODDATA[m.id] || (MODDATA[m.id] = {}) };
}
function genericScreen(c, b, m, h) {               // bars of each effective parameter value
  const n = m.params.length, bw = (b.w - 40) / n;
  m.params.forEach((p, i) => {
    const v = h.eff(i), bh = (b.h - 50) * v;
    c.globalAlpha = .9; c.fillRect(b.x + 20 + i * bw + 6, b.y + b.h - 14 - bh, bw - 12, bh);
  });
  c.globalAlpha = 1; c.fillStyle = h.col.text; c.fillText('live parameters', b.x + 20, b.y + 26);
}
// The face's dark glass display, set into the card.
function drawScreen(m, c, st) {
  const b = { x: 50, y: 206, w: 418, h: 168 };
  c.save(); rr(c, b.x - 4, b.y - 4, b.w + 8, b.h + 8, 16); c.fillStyle = st.rim; c.fill();
  rr(c, b.x, b.y, b.w, b.h, 13); c.fillStyle = SKIN.screen.glass; c.fill(); c.clip();
  const g = c.createLinearGradient(0, b.y, 0, b.y + b.h); g.addColorStop(0, 'rgba(255,255,255,.07)'); g.addColorStop(.4, 'rgba(255,255,255,0)'); c.fillStyle = g; c.fillRect(b.x, b.y, b.w, b.h);
  c.strokeStyle = ACC; c.fillStyle = ACC; c.lineWidth = 3; c.font = `500 18px ${SKIN.ui.fontMono}`;
  try { (SCREENS[m.id] || genericScreen)(c, b, m, screenHelper(m)); }
  catch (err) { c.fillStyle = SKIN.ui.bad; c.fillText('screen error: ' + err.message.slice(0, 30), b.x + 20, b.y + 26); }
  c.restore();
}
// Subtitle under a module's name: the loaded file if there is one, otherwise the module's kind.
function moduleSubtitle(m) { const an = MODDATA[m.id] && MODDATA[m.id].assetName; return an ? `${m.asset.kind.toUpperCase()} · ${an}` : m.kind; }
function drawFace(m) {
  const c = m.ctx, byp = !!state.bypass[m.id], st = styleOf(m), C = st.card;
  c.globalAlpha = 1; c.clearRect(0, 0, FW, m.FH);
  drawCard(c, st, m.FH);
  c.textBaseline = 'alphabetic';
  c.beginPath(); c.arc(64, 146, 9, 0, Math.PI * 2); c.fillStyle = byp ? C.powerOff : C.powerOn; c.fill(); c.strokeStyle = C.title; c.lineWidth = 2; c.stroke();
  c.fillStyle = C.title; c.font = `700 36px ${SKIN.ui.fontDisplay}`; c.fillText(m.name, 84, 158);
  const sub = byp ? 'bypassed' : moduleSubtitle(m);
  c.fillStyle = byp ? C.bypassed : C.text; c.font = `400 18px ${SKIN.ui.fontMono}`; c.fillText(sub.length > 36 ? sub.slice(0, 35) + '…' : sub, 58, 190);
  c.globalAlpha = byp ? .45 : 1;
  drawScreen(m, c, st); m.params.forEach((p) => { const [kx, ky] = knobXY(p); drawKnob(c, kx, ky, p, st); });
  c.globalAlpha = 1; m.tex.needsUpdate = true;
}
function drawLabel(s) {
  const c = s.ctx, on = state.carrying === s.id, open = state.focus === 'src:' + s.id;
  c.clearRect(0, 0, 256, 128); rr(c, 3, 3, 250, 122, 16); c.fillStyle = SKIN.labels.bg; c.fill(); c.strokeStyle = s.color; c.lineWidth = on || open ? 5 : 2; c.stroke();
  c.fillStyle = s.color; c.font = `700 26px ${SKIN.ui.fontDisplay}`; c.fillText(s.name, 16, 34);
  c.fillStyle = SKIN.labels.text; c.font = `400 16px ${SKIN.ui.fontMono}`; c.fillText(on ? 'carrying · pick a knob' : open ? 'editing · see panel' : s.sub, 16, 58);
  c.beginPath(); s.hist.forEach((v, i) => { const px = 16 + i * (224 / 119), py = 96 - v * 20; i ? c.lineTo(px, py) : c.moveTo(px, py); }); c.strokeStyle = s.color; c.lineWidth = 2.5; c.stroke();
  s.tex.needsUpdate = true;
}
// Test helper: one pixel of a module's face canvas, [r, g, b, a].
rig.facePixel = (id, x, y) => { const m = MODS.find((q) => q.id === id); return m ? Array.from(m.ctx.getImageData(x, y, 1, 1).data) : null; };

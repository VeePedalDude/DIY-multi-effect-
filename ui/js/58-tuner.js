// 58-tuner.js - the strobe tuner: a face-down mini card in the top bar that flips open into a card
// turned sideways. The engine does the measuring (engine/core/tuner.h); this draws it.
//  - Click the mini card or press T: the tuner opens (and the output is muted while it's open, if
//    Mute is on). T again or Collapse folds it back into the mini card, which keeps showing the note
//    while you play. Off stops it.
//  - The three strobe bands are the string compared with the target note at 1x, 2x and 4x: they
//    stand still when the string is in tune and drift right when it's sharp, left when flat, faster
//    the further off it is. Their motion comes from the engine's band phases (31 per second),
//    run forward between packets at the measured rate so it stays smooth at 60 fps.
// Colours, the card frame and the card back come from the skin (SKIN.tuner, docs/SKINS.md).
const TUN_NAMES = ['C', 'C♯', 'D', 'D♯', 'E', 'F', 'F♯', 'G', 'G♯', 'A', 'A♯', 'B'];
const TW = CARD_H, TH = FW;                        // the card turned sideways: 754 x 518
const tun = {
  open: false, on: false,
  mute: lsGet('rig01.tunerMute', '1') === '1',
  ref: clamp(Number(lsGet('rig01.tunerRef', '440')) || 440, 400, 480),
  disp: [0, 0, 0], packet: null, packetT: 0, raf: 0, said: '',
};
const IN_TUNE_CENTS = 1.5;

function tunReading() {
  if (!meters || !LAYOUT || LAYOUT.tun === undefined) return null;
  const m = meters.m, b = LAYOUT.tun, nb = LAYOUT.tunBands;
  return { on: m[b] > 0.5, level: m[b + 1], freq: m[b + 2], note: Math.round(m[b + 3]), cents: m[b + 4], target: m[b + 5],
    ph: Array.from({ length: nb }, (_, k) => m[b + 6 + k]), amp: Array.from({ length: nb }, (_, k) => m[b + 6 + nb + k]) };
}
const tunHarm = [1, 2, 4];
const noteName = (n) => n >= 0 ? TUN_NAMES[n % 12] : '–';
const noteOct = (n) => n >= 0 ? String(Math.floor(n / 12) - 1) : '';
function tunColour(r) {
  const T = SKIN.tuner;
  if (!r || r.note < 0) return T.text;
  return Math.abs(r.cents) <= IN_TUNE_CENTS ? T.inTune : r.cents < 0 ? T.flat : T.sharp;
}

// ------------------------------------------------------------------ engine
function tunSend() {
  rig.cmd(`tuner_ref ${tun.ref}`);
  rig.cmd(tun.on ? `tuner 1 ${tun.open && tun.mute ? 1 : 0}` : 'tuner 0');
}
rig.afterSend.push(() => { if (tun.on) tunSend(); });

// ------------------------------------------------------------------ open / collapse / off
function tunOpen() {
  tun.open = true; tun.on = true; tunSend();
  const el = $('tuner'); el.hidden = false;
  el.classList.remove('flip'); void el.offsetWidth; el.classList.add('flip');      // replay the flip
  $('tunerBtn').setAttribute('aria-expanded', 'true');
  tunControls(); drawTunerCard(tunReading()); tunLoop();
}
function tunCollapse() {
  tun.open = false; tunSend();
  $('tuner').hidden = true; $('tunerBtn').setAttribute('aria-expanded', 'false');
  drawTunerMini(tunReading()); tunLoop();
}
function tunOff() {
  tun.open = false; tun.on = false; tunSend();
  $('tuner').hidden = true; $('tunerBtn').setAttribute('aria-expanded', 'false');
  tun.disp = [0, 0, 0]; drawTunerMini();
}
function tunToggle() { if (tun.open) tunCollapse(); else tunOpen(); }
function tunControls() {
  $('tunerMute').setAttribute('aria-pressed', String(tun.mute));
  $('tunerMute').textContent = tun.mute ? 'Muted' : 'Mute';
  $('tunerRef').textContent = `A4 ${tun.ref}`;
}
function tunSetRef(hz) { tun.ref = clamp(Math.round(hz), 400, 480); lsSet('rig01.tunerRef', String(tun.ref)); tunControls(); tunSend(); }

// ------------------------------------------------------------------ drawing
function tunLoop() {
  if (tun.raf) return;
  const step = (now) => {
    tun.raf = 0;
    if (!tun.on) { drawTunerMini(); return; }
    const r = tunReading();
    if (r && r.note >= 0 && r.freq > 0 && r.target > 0) {
      // each band's phase now: the engine's last measurement, run forward at the measured rate for the
      // time since it arrived. disp keeps counting whole turns (it's unwrapped), so it never jumps.
      if (meters !== tun.packet) { tun.packet = meters; tun.packetT = now; }
      for (let k = 0; k < tun.disp.length; k++) {
        const want = r.ph[k] + 2 * Math.PI * tunHarm[k] * (r.freq - r.target) * (now - tun.packetT) / 1000;
        let e = want - tun.disp[k]; e -= 2 * Math.PI * Math.round(e / (2 * Math.PI)); tun.disp[k] += e;
      }
    }
    if (tun.open) drawTunerCard(r);
    drawTunerMini(r);
    tunSay(r);
    tun.raf = requestAnimationFrame(step);
  };
  tun.raf = requestAnimationFrame(step);
}
// for screen readers: the note, said when it or its tuning changes
function tunSay(r) {
  if (!tun.open) return;
  const s = !r || r.note < 0 ? 'No note' : `${noteName(r.note).replace('♯', ' sharp')}${noteOct(r.note)}, ` +
    (Math.abs(r.cents) <= IN_TUNE_CENTS ? 'in tune' : `${Math.round(Math.abs(r.cents))} cents ${r.cents < 0 ? 'flat' : 'sharp'}`);
  if (s !== tun.said) { tun.said = s; $('tunerRead').textContent = s; $('tunerCard').setAttribute('aria-label', 'Strobe tuner: ' + s); }
}

function drawTunerCard(r) {
  const cv = $('tunerCard'), c = cv.getContext('2d'), T = SKIN.tuner, st = tileStyle(SKIN, T.tile);
  c.setTransform(cv.width / TW, 0, 0, cv.height / TH, 0, 0);
  c.clearRect(0, 0, TW, TH);
  // the card frame, turned sideways
  c.save(); c.translate(TW, 0); c.rotate(Math.PI / 2); drawCard(c, st, CARD_H); c.restore();
  // title and reference on the frame
  c.textBaseline = 'alphabetic';
  c.textAlign = 'left'; c.fillStyle = st.card.title; c.font = `700 26px ${SKIN.ui.fontDisplay}`; c.fillText('STROBE TUNER', 128, 86);
  c.textAlign = 'right'; c.fillStyle = st.card.text; c.font = `500 19px ${SKIN.ui.fontMono}`; c.fillText(`A4 = ${tun.ref} Hz`, 626, 86);
  // the display
  const gx = 112, gy = 104, gw = 530, gh = 296;
  rr(c, gx, gy, gw, gh, 18); c.fillStyle = T.glass; c.fill(); c.strokeStyle = st.rim; c.lineWidth = 3; c.stroke();
  const has = r && r.note >= 0, col = tunColour(r);
  // note, cents, Hz
  c.textAlign = 'center'; c.fillStyle = has ? col : T.text;
  c.font = `700 92px ${SKIN.ui.fontDisplay}`; c.fillText(noteName(has ? r.note : -1), 192, 232);
  if (has) { c.font = `600 28px ${SKIN.ui.fontDisplay}`; c.textAlign = 'left'; c.fillText(noteOct(r.note), 192 + c.measureText(noteName(r.note)).width / 2 + 34, 232); c.textAlign = 'center'; }
  // flat / sharp arrows either side of the note
  const arrow = (x, dir, lit) => { c.beginPath(); c.moveTo(x, 186); c.lineTo(x + dir * -16, 196); c.lineTo(x, 206); c.closePath(); c.fillStyle = lit; c.fill(); };
  const inTune = has && Math.abs(r.cents) <= IN_TUNE_CENTS;
  arrow(138, -1, has && (r.cents < -IN_TUNE_CENTS || inTune) ? (inTune ? T.inTune : T.flat) : SKIN.screen.track);
  arrow(246, 1, has && (r.cents > IN_TUNE_CENTS || inTune) ? (inTune ? T.inTune : T.sharp) : SKIN.screen.track);
  c.font = `500 26px ${SKIN.ui.fontMono}`; c.fillStyle = has ? T.bright : T.text;
  c.fillText(has ? `${r.cents >= 0 ? '+' : '−'}${Math.abs(r.cents).toFixed(1)}¢` : 'play a', 192, 290);
  c.font = `400 18px ${SKIN.ui.fontMono}`; c.fillStyle = T.text;
  c.fillText(has ? `${r.freq.toFixed(2)} Hz` : 'string', 192, 322);
  c.fillText(tun.mute ? 'output muted' : 'output on', 192, 372);
  c.beginPath(); c.moveTo(282, gy + 18); c.lineTo(282, gy + gh - 18); c.strokeStyle = SKIN.screen.grid; c.lineWidth = 2; c.stroke();
  // the strobe bands
  const bx = 336, bw = 284, bh = 60, P = 34;
  for (let k = 0; k < 3; k++) {
    const by = 122 + k * 72;
    c.textAlign = 'right'; c.fillStyle = T.text; c.font = `500 17px ${SKIN.ui.fontMono}`; c.fillText(`×${tunHarm[k]}`, bx - 10, by + bh / 2 + 6);
    c.save(); rr(c, bx, by, bw, bh, 8); c.fillStyle = SKIN.screen.track; c.fill(); c.clip();
    const a = has ? clamp(r.amp[k] * 2.5, 0.12, 1) : 0.1;
    c.globalAlpha = a; c.fillStyle = inTune ? T.inTune : T.stripe;
    const off = ((tun.disp[k] / (2 * Math.PI)) % 1 + 1) % 1 * P;
    for (let x = bx - P + off; x < bx + bw; x += P) c.fillRect(x, by, P / 2, bh);
    c.restore();
  }
  // cents scale, -50 .. +50
  const sx = bx, sw = bw, sy = 362;
  c.strokeStyle = SKIN.screen.grid; c.lineWidth = 2;
  for (let v = -50; v <= 50; v += 10) { const x = sx + (v + 50) / 100 * sw, big = v === 0; c.beginPath(); c.moveTo(x, sy - (big ? 14 : 7)); c.lineTo(x, sy + (big ? 14 : 7)); c.strokeStyle = big ? T.inTune : SKIN.screen.grid; c.stroke(); }
  c.beginPath(); c.moveTo(sx, sy); c.lineTo(sx + sw, sy); c.strokeStyle = SKIN.screen.grid; c.stroke();
  if (has) {
    const x = sx + (clamp(r.cents, -50, 50) + 50) / 100 * sw;
    c.beginPath(); c.moveTo(x, sy - 4); c.lineTo(x - 9, sy - 20); c.lineTo(x + 9, sy - 20); c.closePath(); c.fillStyle = col; c.fill();
  }
  c.textAlign = 'center'; c.fillStyle = T.text; c.font = `400 14px ${SKIN.ui.fontMono}`;
  c.fillText('♭  −50', sx + 22, sy + 30); c.fillText('+50  ♯', sx + sw - 22, sy + 30); c.fillText('0', sx + sw / 2, sy + 30);
}

// The mini card: face down (the card back) when the tuner is off or open; face up with the note
// while it's collapsed and still listening.
function drawTunerMini(r) {
  const cv = $('tunerMini'); if (!cv) return;
  const c = cv.getContext('2d'), T = SKIN.tuner, W = 64, H = 44;
  c.setTransform(cv.width / W, 0, 0, cv.height / H, 0, 0); c.clearRect(0, 0, W, H);
  rr(c, 1, 1, W - 2, H - 2, 6); c.fillStyle = T.back; c.fill(); c.strokeStyle = T.backLine; c.lineWidth = 1.5; c.stroke();
  const live = tun.on && !tun.open;
  if (live) {
    const has = r && r.note >= 0, col = tunColour(r);
    rr(c, 4, 4, W - 8, H - 8, 4); c.fillStyle = T.glass; c.fill(); c.strokeStyle = has ? col : T.backLine; c.lineWidth = 1.5; c.stroke();
    c.textAlign = 'center'; c.textBaseline = 'middle'; c.fillStyle = has ? col : T.text; c.font = `700 17px ${SKIN.ui.fontDisplay}`;
    c.fillText(has ? noteName(r.note) : '–', W / 2, 19);
    if (has) { const x = W / 2 + clamp(r.cents, -50, 50) / 50 * 22; c.fillRect(W / 2 - 0.5, 31, 1, 6); c.beginPath(); c.arc(x, 34, 3, 0, Math.PI * 2); c.fill(); }
    return;
  }
  // the card back: a lattice inside a border, and a little strobe as its mark
  c.save(); rr(c, 4, 4, W - 8, H - 8, 4); c.clip();
  c.strokeStyle = T.backLine; c.globalAlpha = 0.45; c.lineWidth = 1;
  for (let i = -H; i < W + H; i += 7) { c.beginPath(); c.moveTo(i, 0); c.lineTo(i + H, H); c.moveTo(i + H, 0); c.lineTo(i, H); c.stroke(); }
  c.restore();
  rr(c, 4, 4, W - 8, H - 8, 4); c.strokeStyle = T.backLine; c.lineWidth = 1; c.stroke();
  rr(c, W / 2 - 13, H / 2 - 8, 26, 16, 3); c.fillStyle = T.back; c.fill(); c.strokeStyle = T.backMark; c.lineWidth = 1.2; c.stroke();
  c.fillStyle = T.backMark; for (let k = 0; k < 4; k++) c.fillRect(W / 2 - 10 + k * 6, H / 2 - 5, 3, 10);
}

function wireTuner() {
  $('tunerBtn').onclick = tunToggle;
  $('tunerHide').onclick = tunCollapse;
  $('tunerOff').onclick = tunOff;
  $('tunerMute').onclick = () => { tun.mute = !tun.mute; lsSet('rig01.tunerMute', tun.mute ? '1' : '0'); tunControls(); tunSend(); };
  $('tunerRefDn').onclick = () => tunSetRef(tun.ref - 1);
  $('tunerRefUp').onclick = () => tunSetRef(tun.ref + 1);
  window.addEventListener('keydown', (e) => {
    if (e.target.closest && e.target.closest('input,textarea,select')) return;
    if ((e.key === 't' || e.key === 'T') && !e.ctrlKey && !e.metaKey && !e.altKey) tunToggle();
  });
  rig.skinHooks.push(() => { drawTunerMini(tunReading()); if (tun.open) drawTunerCard(tunReading()); });
  tunControls(); drawTunerMini();
}
rig.tuner = { open: tunOpen, collapse: tunCollapse, off: tunOff, setRef: tunSetRef, state: tun, reading: tunReading, skin: () => SKIN.tuner, layout: () => LAYOUT.tun };

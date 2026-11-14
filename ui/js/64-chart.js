// 64-chart.js - the Chord Chart view (C): a chord dictionary on one card turned sideways, like the tuner's,
// and stretched to the shape of the space it has (cardFit, shared with views, 66-views.js). The left of the
// card picks the chord: key, formula, extension, additions, alterations, bass note. The right names it,
// spells it (degrees = notes, optional ones in brackets) and shows its shapes on the fretboard for a
// tuning and a fret position, best first, four rows of as many as the width holds (six or more) to a page.
// The music is 15-chordbook.js. Like the Visualizer, the 3D stage isn't drawn while this is up.
// Picks are remembered in this browser. Colours: the skin's `chart` section and its tile (docs/SKINS.md).
const CH_H = FW, CH_RX = 662, CH_LMIN = 1300, CH_LMAX = 1700;   // the card's height; where its right part starts; its length, least and most
const chart = { k: 1, dpr: 1, L: CH_LMIN, page: 0, shapes: [], counts: [], chord: null };
const chRW = (L) => L - CH_RX - 112;                                   // the right part's width
const chGlass = () => [CH_RX, 100, chRW(chart.L), 300];               // the shapes' display: x, y, w, h
const chCols = () => Math.max(6, Math.floor(chRW(chart.L) / 88)), chPer = () => chCols() * 4;
const CH_ROWS = [['key', 'KEY', 104], ['formula', 'FORMULA', 142], ['ext', 'EXTENSION', 250], ['adds', 'ADD', 286], ['alts', 'ALTER', 322], ['bass', 'BASS', 410]];
const chPick = (() => {
  const d = { key: { letter: 0, acc: 0 }, formula: 'maj', ext: 0, adds: [], alts: [], bass: null, tuning: 'std', pos: 0 };
  try { const o = JSON.parse(lsGet('rig01.chart', '{}')); return { ...d, ...o }; } catch (e) { return d; }
})();
const chSave = () => lsSet('rig01.chart', JSON.stringify(chPick));
const chTuning = () => (CB_TUNINGS.find((t) => t[0] === chPick.tuning) || CB_TUNINGS[0]);

// ------------------------------------------------------------------ the chord and its shapes
function chCompute() {
  chart.chord = chordSpell({ root: chPick.key, formula: chPick.formula, ext: chPick.ext, adds: chPick.adds, alts: chPick.alts, bass: chPick.bass });
  // every position's count, for the position menu: a rich chord may have shapes at only a few
  const all = Array.from({ length: CB_POSITIONS + 1 }, (_, p) => chordVoicings(chart.chord, chTuning()[2], p));
  chart.counts = all.map((l) => l.length); chart.shapes = all[chPick.pos] || [];
  chart.page = clamp(chart.page, 0, Math.max(0, Math.ceil(chart.shapes.length / chPer()) - 1));
}
function chChanged(keepPage) { if (!keepPage) chart.page = 0; chSave(); chCompute(); chControls(); chDraw(); }

// ------------------------------------------------------------------ the buttons on the left of the card
function chChip(label, pressed, act, val, disabled) {
  return `<button class="cchip" aria-pressed="${pressed}" data-ch="${act}" data-v="${val}"${disabled ? ' disabled' : ''}>${label}</button>`;
}
function chNoteRow(act, cur) {
  return CB_LETTERS.map((l, i) => chChip(l, cur.letter === i, act, i)).join('') + chChip('♭', cur.acc === -1, act + 'Acc', -1) + chChip('♯', cur.acc === 1, act + 'Acc', 1);
}
function chControls() {
  const av = chart.chord.avail, bass = chPick.bass || chPick.key;
  const rows = {
    key: chNoteRow('key', chPick.key),
    formula: CB_FORMULAS.map(([id, suf]) => chChip(id === 'maj' ? 'maj' : suf, chPick.formula === id, 'formula', id)).join(''),
    ext: CB_EXT.map((e) => chChip(String(e), chPick.ext === e && av.ext[e], 'ext', e, !av.ext[e])).join(''),
    adds: CB_ADDS.map(([a]) => chChip(a, chPick.adds.includes(a) && av.adds[a], 'adds', a, !av.adds[a] && !chPick.adds.includes(a))).join(''),
    alts: CB_ALTS.map(([a, label]) => chChip(label, chPick.alts.includes(a) && av.alts[a], 'alts', a, !av.alts[a])).join(''),
    bass: chNoteRow('bass', bass),
  };
  $('chartPick').innerHTML = CH_ROWS.map(([id, label, y]) => `<div class="crow${id === 'formula' ? ' formulas' : ''}" style="top:${y}px" role="group" aria-label="${label.toLowerCase()}">${rows[id]}</div>`).join('');
  $('chartTuning').value = chTuning()[0];
  $('chartPos').innerHTML = chart.counts.map((n, p) => `<option value="${p}">${p ? `Fret ${p}` : 'Open'} · ${n}</option>`).join('');
  $('chartPos').value = String(chPick.pos);
  const pages = Math.max(1, Math.ceil(chart.shapes.length / chPer()));
  $('chartPage').textContent = `${chart.shapes.length} shape${chart.shapes.length === 1 ? '' : 's'} · ${chart.page + 1}/${pages}`;
  $('chartPrev').disabled = chart.page === 0; $('chartNext').disabled = chart.page >= pages - 1;
}
function chClick(e) {
  const b = e.target.closest('[data-ch]'); if (!b || b.disabled) return;
  const act = b.dataset.ch, v = b.dataset.v, flip = (list, x) => list.includes(x) ? list.filter((y) => y !== x) : [...list, x];
  if (act === 'key') chPick.key = { letter: Number(v), acc: 0 };
  if (act === 'keyAcc') chPick.key = { ...chPick.key, acc: chPick.key.acc === Number(v) ? 0 : Number(v) };
  if (act === 'bass') chPick.bass = chPick.bass && chPick.bass.letter === Number(v) ? null : { letter: Number(v), acc: 0 };
  if (act === 'bassAcc') { const b0 = chPick.bass || chPick.key; chPick.bass = { ...b0, acc: b0.acc === Number(v) ? 0 : Number(v) }; }
  if (act === 'formula') chPick.formula = v;
  if (act === 'ext') chPick.ext = chPick.ext === Number(v) ? 0 : Number(v);
  if (act === 'adds') chPick.adds = flip(chPick.adds, v);
  if (act === 'alts') chPick.alts = flip(chPick.alts, v);
  // what the new formula can't take is dropped, so it doesn't come back unseen later
  const av = chordOptions({ root: chPick.key, formula: chPick.formula, ext: chPick.ext, adds: [], alts: chPick.alts });
  if (!av.ext[chPick.ext]) chPick.ext = 0;
  chPick.alts = chPick.alts.filter((a) => av.alts[a]);
  const av2 = chordOptions({ root: chPick.key, formula: chPick.formula, ext: chPick.ext, adds: [], alts: chPick.alts });
  chPick.adds = chPick.adds.filter((a) => av2.adds[a]);
  chChanged();
}

// ------------------------------------------------------------------ drawing
function chCardFrame(c, st, L) { c.save(); c.translate(L, 0); c.rotate(Math.PI / 2); drawCard(c, st, L); c.restore(); }
// a card's canvas at its length and scale (fit: { k, dpr, L }); returns its context in card units
function chCanvas(cv, fit) {
  const w = Math.round(fit.L * fit.k * fit.dpr), h = Math.round(CH_H * fit.k * fit.dpr);
  if (cv.width !== w || cv.height !== h) { cv.width = w; cv.height = h; }
  const c = cv.getContext('2d'); c.setTransform(w / fit.L, 0, 0, h / CH_H, 0, 0); c.clearRect(0, 0, fit.L, CH_H); return c;
}
function chDraw() {
  if ($('chart').hidden || !chart.chord) return;
  const P = SKIN.chart, st = tileStyle(SKIN, P.tile);
  $('chart').style.background = P.bg;
  // the left of the card: its title and the row labels (the buttons are HTML over it)
  const c = chCanvas($('chartCv'), chart), RX = CH_RX, RE = CH_RX + chRW(chart.L);
  chCardFrame(c, st, chart.L);
  c.textBaseline = 'alphabetic'; c.textAlign = 'left'; c.fillStyle = st.card.title; c.font = `700 26px ${SKIN.ui.fontDisplay}`; c.fillText('CHORD', 128, 86);
  c.textAlign = 'right'; c.fillStyle = st.card.text; c.font = `500 17px ${SKIN.ui.fontMono}`; c.fillText(chTuning()[1].split(' (')[0], 626, 86);
  c.font = `600 13px ${SKIN.ui.fontDisplay}`; c.textAlign = 'left';
  CH_ROWS.forEach(([, label, y]) => c.fillText(label, 128, y + 19));
  // the right: the name and spelling, then the shapes on the display
  const ch = chart.chord;
  c.fillStyle = st.card.title; c.font = `700 26px ${SKIN.ui.fontDisplay}`; c.fillText(ch.name, RX + 16, 86);
  const nw = c.measureText(ch.name).width, spell = `${ch.degrees}  =  ${ch.spelled}`, room = RE - 16 - RX - 16 - nw - 24;
  let fs = 16; c.font = `500 ${fs}px ${SKIN.ui.fontMono}`;
  while (fs > 10 && c.measureText(spell).width > room) { fs--; c.font = `500 ${fs}px ${SKIN.ui.fontMono}`; }
  c.textAlign = 'right'; c.fillStyle = st.card.text; c.fillText(spell, RE - 16, 86); c.textAlign = 'left';
  const [gx, gy, gw, gh] = chGlass();
  rr(c, gx, gy, gw, gh, 18); c.fillStyle = P.glass; c.fill(); c.strokeStyle = st.rim; c.lineWidth = 3; c.stroke();
  const page = chart.shapes.slice(chart.page * chPer(), chart.page * chPer() + chPer());
  if (!page.length) {
    c.textAlign = 'center'; c.fillStyle = P.text; c.font = `500 17px ${SKIN.ui.fontMono}`;
    c.fillText(chPick.pos ? `no shapes at fret ${chPick.pos}` : 'no shapes in open position', gx + gw / 2, gy + gh / 2 - 6);
    c.font = `400 14px ${SKIN.ui.fontMono}`; const at = chart.counts.map((n, p) => n ? (p ? String(p) : 'open') : '').filter(Boolean);
    c.fillText(at.length ? `shapes at: ${at.join(', ')}` : 'none at any position in this tuning', gx + gw / 2, gy + gh / 2 + 18); c.textAlign = 'left';
  }
  page.forEach((v, i) => chDiagram(c, v, i, P, ch.rootPc));
  $('chartCv').setAttribute('aria-label', `${ch.name}: ${ch.spelled}. ` + (page.length ? `Shapes, low string first: ${page.map((v) => fretsText(v.frets)).join(', ')}.` : 'No shapes at this fret position.'));
}
// One shape in a cell: x / o over the nut, the strings and four frets, a dot per fretted note (the root's
// notes in the root colour), and the starting fret beside it above fret 1.
// chGrid(i): where shape i of the page has its top-left string and fret line, its string and fret spacing.
function chGrid(i, S) {
  const [gx, gy, gw, gh] = chGlass(), n = chCols(), cw = gw / n, sp = 12;
  return { x0: gx + (i % n) * cw + (cw - sp * (S - 1)) / 2 + 4, y0: gy + Math.floor(i / n) * gh / 4 + 18, sp, fh: 13 };
}
function chDiagram(c, v, i, P, rootPc) {
  const S = v.frets.length, { x0, y0, sp, fh } = chGrid(i, S), gw = sp * (S - 1), tun = chTuning()[2];
  const base = chPick.pos || 1;
  c.strokeStyle = P.grid; c.lineWidth = 1;
  for (let s = 0; s < S; s++) { c.beginPath(); c.moveTo(x0 + s * sp, y0); c.lineTo(x0 + s * sp, y0 + 4 * fh); c.stroke(); }
  for (let k = 0; k <= 4; k++) { c.beginPath(); c.moveTo(x0, y0 + k * fh); c.lineTo(x0 + gw, y0 + k * fh); c.lineWidth = k === 0 && base === 1 ? 3.5 : 1; c.stroke(); }
  if (base > 1) { c.textAlign = 'right'; c.fillStyle = P.text; c.font = `500 11px ${SKIN.ui.fontMono}`; c.fillText(String(base), x0 - 4, y0 + fh * 0.5 + 4); }
  c.textAlign = 'center'; c.font = `600 12px ${SKIN.ui.fontMono}`;
  v.frets.forEach((f, s) => {
    const sx = x0 + s * sp, isRoot = f >= 0 && (tun[s] + f) % 12 === rootPc;
    if (f < 0) { c.fillStyle = P.mute; c.fillText('×', sx, y0 - 5); return; }
    if (f === 0) { c.beginPath(); c.arc(sx, y0 - 8, 3, 0, Math.PI * 2); c.strokeStyle = isRoot ? P.root : P.open; c.lineWidth = 1.4; c.stroke(); return; }
    c.beginPath(); c.arc(sx, y0 + (f - base + 0.5) * fh, 4.5, 0, Math.PI * 2); c.fillStyle = isRoot ? P.root : P.dot; c.fill();
  });
  c.textAlign = 'left';
}

// ------------------------------------------------------------------ layout, show / hide
// Lays out a one-card view (the Chord Chart's, a view's) between the bars and the panel: the card
// is stretched to the shape of that space (between CH_LMIN and CH_LMAX long) and scaled to fill it.
// Returns { k, dpr, L }; the right part of the card is chRW(L) wide.
function cardFit(board, card) {
  const W = innerWidth, H = innerHeight;
  const right = !panelHidden && W > 900 ? W - $('panel').offsetWidth - 32 : W - 16, left = 16;
  const top = $('top').offsetHeight + 14, bottom = Math.min(H - $('bottom').offsetHeight, $('board').getBoundingClientRect().top) - 12;
  const aw = Math.max(100, right - left), ah = Math.max(100, bottom - top);
  const L = Math.round(clamp(aw / ah * CH_H, CH_LMIN, CH_LMAX)), k = Math.min(1.6, aw / L, ah / CH_H);
  board.style.width = card.style.width = L + 'px'; board.style.height = CH_H + 'px';
  board.style.transform = `translate(${Math.round(left + (aw - L * k) / 2)}px, ${Math.round(top + (ah - CH_H * k) / 2)}px) scale(${k})`;
  return { k, dpr: Math.min(2, devicePixelRatio || 1), L };
}
// the footer under the right part: tuning and the rest, as wide as that part
function chFoot(el, L) { el.style.left = CH_RX + 'px'; el.style.width = chRW(L) + 'px'; }
function chLayout() { Object.assign(chart, cardFit($('chartBoard'), $('chartCard'))); chFoot($('chartFoot'), chart.L); chart.page = 0; chCompute(); chControls(); chDraw(); }
function chartShow(on) {
  $('chart').hidden = !on;
  if (!on) return;
  canvas.style.visibility = 'hidden'; $('overview').disabled = true;
  chLayout();
}
function wireChart() {
  $('chartTuning').innerHTML = CB_TUNINGS.map(([id, label]) => `<option value="${id}">${label}</option>`).join('');
  $('chartPick').addEventListener('click', chClick);
  $('chartTuning').onchange = (e) => { chPick.tuning = e.target.value; chChanged(); };
  $('chartPos').onchange = (e) => { chPick.pos = Number(e.target.value); chChanged(); };
  $('chartPrev').onclick = () => { chart.page--; chChanged(true); };
  $('chartNext').onclick = () => { chart.page++; chChanged(true); };
  $('vChart').onclick = () => setView('chart');
  window.addEventListener('keydown', (e) => {
    if (e.target.closest && e.target.closest('input,textarea,select')) return;
    if ((e.key === 'c' || e.key === 'C') && !e.ctrlKey && !e.metaKey && !e.altKey) setView(state.view === 'chart' ? 'stage' : 'chart');
  });
  rig.skinHooks.push(chDraw);
}
// Test helpers: the chord on show, its shapes on this page, the picks, and the board's scale
rig.chart = {
  chord: () => chart.chord && { name: chart.chord.name, degrees: chart.chord.degrees, spelled: chart.chord.spelled },
  shapes: () => chart.shapes.slice(chart.page * chPer(), chart.page * chPer() + chPer()).map((v) => fretsText(v.frets)),
  count: () => chart.shapes.length, counts: () => chart.counts.slice(), page: () => chart.page, pick: () => JSON.parse(JSON.stringify(chPick)),
  layout: () => ({ k: chart.k, L: chart.L, cols: chCols(), per: chPer() }), colors: () => SKIN.chart, glass: () => chGlass(),
  // a pixel of the card, in its own coordinates (L x 518); where shape i's note on string s is drawn
  pixel: (x, y) => { const cv = $('chartCv'); return Array.from(cv.getContext('2d').getImageData(Math.round(x * cv.width / chart.L), Math.round(y * cv.height / CH_H), 1, 1).data); },
  noteAt: (i, s) => { const v = chart.shapes[chart.page * chPer() + i], g = chGrid(i, v.frets.length), f = v.frets[s];
    return { x: g.x0 + s * g.sp, y: f > 0 ? g.y0 + (f - (chPick.pos || 1) + 0.5) * g.fh : g.y0 - 8, fret: f }; },
};

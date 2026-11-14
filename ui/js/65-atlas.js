// 65-atlas.js - the Scale Atlas view (S): a scale on the fretboard, beside the Chord Chart and laid out the
// same way (one sideways card, cardFit and chCanvas from 64-chart.js). The left of the card picks the key and
// the scale and highlights degrees; the right shows every note of the scale on frets 0 to as many as fit (high
// string at the top, as you look down at the neck), labelled by degree or note, and under it the chords
// on each degree that use only the scale's notes. Click a chord to highlight its notes on the neck.
// The music is 16-scales.js. Picks are remembered in this browser; the highlight isn't.
// Colours: the skin's `chart` section, shared with the Chord Chart (docs/SKINS.md).
const AT_ROWS = [['key', 'KEY', 104], ['scale', 'SCALE', 142], ['hl', 'HIGHLIGHT', 352]];   // the Exotic menu sits at 316 (#atlasExotic)
// the neck in card units: open notes, the nut, the last fret, the top string, string spacing; 12-22 frets, ~30 units each
const atNeck = () => { const RW = chRW(atlas.L); return { open: CH_RX + 24, x0: CH_RX + 42, x1: CH_RX + RW - 12, y0: 120, sp: 24, frets: clamp(Math.floor((RW - 54) / 30), 12, 22) }; };
const atlas = { k: 1, dpr: 1, L: CH_LMIN, notes: [], chords: [], hl: [], chord: null };
const atPick = (() => {
  const d = { key: { letter: 5, acc: 0 }, scale: 'minpent', tuning: 'std', labels: 'deg' };
  try { return { ...d, ...JSON.parse(lsGet('rig01.atlas', '{}')) }; } catch (e) { return d; }
})();
const atTuning = () => (CB_TUNINGS.find((t) => t[0] === atPick.tuning) || CB_TUNINGS[0]);
const atFretX = (f) => { const N = atNeck(); return f === 0 ? N.open : N.x0 + (f - 0.5) * (N.x1 - N.x0) / N.frets; };

function atChanged(keepHl) {
  lsSet('rig01.atlas', JSON.stringify(atPick));
  atlas.notes = scaleSpell(atPick.key, atPick.scale); atlas.chords = scaleChords(atlas.notes);
  if (!keepHl) { atlas.hl = []; atlas.chord = null; }
  atControls(); atDraw();
}

// ------------------------------------------------------------------ buttons
function atControls() {
  const rows = {
    key: chNoteRow('key', atPick.key),
    scale: SC_SCALES.map(([id, name, , short]) => chChip(short, atPick.scale === id, 'scale', id).replace('<button', `<button title="${name}"`)).join(''),
    hl: atlas.notes.map((x, i) => chChip(x.deg, atlas.hl.includes(i), 'hl', i)).join('') + chChip('clear', false, 'clear', 0, !atlas.hl.length),
  };
  $('atlasPick').innerHTML = AT_ROWS.map(([id, label, y]) => `<div class="crow${id === 'scale' ? ' scales' : id === 'hl' ? ' hl' : ''}" style="top:${y}px" role="group" aria-label="${label.toLowerCase()}">${rows[id]}</div>`).join('');
  // the chords: a column per degree under the neck, a button per chord
  const n = atlas.chords.length, cw = chRW(atlas.L) / n;
  $('atlasChords').innerHTML = atlas.chords.map((d, i) => d.chords.map((c, j) =>
    `<button class="cchip" style="left:${Math.round(CH_RX + i * cw + 3)}px;top:${304 + j * 25}px;width:${Math.round(cw - 6)}px" aria-pressed="${atlas.chord === c.name}" data-at="chord" data-v="${i}.${j}" title="${c.name}: ${c.idx.map((k) => atlas.notes[k].name).join(' ')}">${c.name}</button>`).join('')).join('');
  $('atlasChords').className = 'achords';
  $('atlasTuning').value = atTuning()[0]; $('atlasLabels').value = atPick.labels;
  const ex = SC_EXOTIC.some((x) => x[0] === atPick.scale);
  $('atlasExotic').value = ex ? atPick.scale : ''; $('atlasExotic').classList.toggle('on', ex);
  const ch = atlas.chord && atlas.chords.flatMap((d) => d.chords).find((c) => c.name === atlas.chord);
  $('atlasInfo').textContent = ch ? `${ch.name}: ${ch.idx.map((k) => atlas.notes[k].name).join(' ')}` : atlas.hl.length ? `${atlas.hl.length} of ${atlas.notes.length} highlighted` : `${atlas.notes.length} notes`;
}
function atClick(e) {
  const b = e.target.closest('[data-ch],[data-at]'); if (!b || b.disabled) return;
  const act = b.dataset.ch || b.dataset.at, v = b.dataset.v;
  if (act === 'key') atPick.key = { letter: Number(v), acc: 0 };
  if (act === 'keyAcc') atPick.key = { ...atPick.key, acc: atPick.key.acc === Number(v) ? 0 : Number(v) };
  if (act === 'scale') atPick.scale = v;
  if (act === 'key' || act === 'keyAcc' || act === 'scale') { atChanged(); return; }
  if (act === 'hl') { const i = Number(v); atlas.hl = atlas.hl.includes(i) ? atlas.hl.filter((x) => x !== i) : [...atlas.hl, i].sort((a, b) => a - b); atlas.chord = null; }
  if (act === 'clear') { atlas.hl = []; atlas.chord = null; }
  if (act === 'chord') {
    const [i, j] = v.split('.').map(Number), c = atlas.chords[i].chords[j];
    if (atlas.chord === c.name) { atlas.chord = null; atlas.hl = []; } else { atlas.chord = c.name; atlas.hl = [...new Set(c.idx)].sort((a, b) => a - b); }
  }
  atChanged(true);
}

// ------------------------------------------------------------------ drawing
function atDraw() {
  if ($('atlas').hidden || !atlas.notes.length) return;
  const P = SKIN.chart, st = tileStyle(SKIN, P.tile), N = atNeck(), RW = chRW(atlas.L);
  $('atlas').style.background = P.bg;
  const c = chCanvas($('atlasCv'), atlas);
  chCardFrame(c, st, atlas.L);
  c.textBaseline = 'alphabetic'; c.textAlign = 'left'; c.fillStyle = st.card.title; c.font = `700 26px ${SKIN.ui.fontDisplay}`; c.fillText('SCALE', 128, 86);
  c.textAlign = 'right'; c.fillStyle = st.card.text; c.font = `500 17px ${SKIN.ui.fontMono}`; c.fillText(atTuning()[1].split(' (')[0], 626, 86);
  c.font = `600 13px ${SKIN.ui.fontDisplay}`; c.textAlign = 'left';
  AT_ROWS.forEach(([, label, y]) => c.fillText(label, 128, y + 19));
  c.fillText('EXOTIC', 128, 316 + 19);
  c.font = `500 14px ${SKIN.ui.fontMono}`;
  c.fillText(atlas.notes.map((x) => x.name).join('  '), 214, 412); c.fillText(atlas.notes.map((x) => x.deg).join('  '), 214, 434);
  // the right: the scale's name, the neck, and the degree over each column of chords
  const name = `${cbNote(atPick.key)} ${scScale(atPick.scale)[1]}`;
  c.fillStyle = st.card.title; c.font = `700 24px ${SKIN.ui.fontDisplay}`; c.fillText(name, CH_RX + 16, 86);
  rr(c, CH_RX, 100, RW, 172, 18); c.fillStyle = P.glass; c.fill(); c.strokeStyle = st.rim; c.lineWidth = 3; c.stroke();
  const tun = atTuning()[2], S = tun.length, yOf = (s) => N.y0 + (S - 1 - s) * N.sp, fw = (N.x1 - N.x0) / N.frets;
  c.strokeStyle = P.grid; c.lineWidth = 1;
  for (let f = 0; f <= N.frets; f++) { c.beginPath(); c.moveTo(N.x0 + f * fw, yOf(S - 1)); c.lineTo(N.x0 + f * fw, yOf(0)); c.lineWidth = f === 0 ? 4 : 1; c.stroke(); }
  c.lineWidth = 1;
  for (let s = 0; s < S; s++) { c.beginPath(); c.moveTo(N.x0, yOf(s)); c.lineTo(N.x1, yOf(s)); c.stroke(); }
  c.textAlign = 'center'; c.fillStyle = P.text; c.font = `500 11px ${SKIN.ui.fontMono}`;
  [3, 5, 7, 9, 12, 15, 17, 19, 21].filter((f) => f <= N.frets).forEach((f) => c.fillText(String(f), atFretX(f), yOf(0) + 24));
  const pcs = atlas.notes.map((x) => x.pc), lit = atlas.hl.length > 0;
  c.textBaseline = 'middle';
  for (let s = 0; s < S; s++) for (let f = 0; f <= N.frets; f++) {
    const i = pcs.indexOf((tun[s] + f) % 12); if (i < 0) continue;
    const on = atlas.hl.includes(i), x = atFretX(f), y = yOf(s);
    c.globalAlpha = lit && !on ? 0.3 : 1;
    c.beginPath(); c.arc(x, y, 10, 0, Math.PI * 2); c.fillStyle = i === 0 ? P.root : on ? P.hi : P.dot; c.fill();
    c.fillStyle = P.glass; c.font = `600 ${atPick.labels === 'deg' ? 10 : 11}px ${SKIN.ui.fontMono}`;
    c.fillText(atPick.labels === 'deg' ? atlas.notes[i].deg : atlas.notes[i].name, x, y + 0.5);
  }
  c.globalAlpha = 1; c.textBaseline = 'alphabetic';
  const n = atlas.chords.length, cw = RW / n;
  c.fillStyle = st.card.text; c.font = `600 12px ${SKIN.ui.fontMono}`;
  atlas.chords.forEach((d, i) => c.fillText(`${d.deg} · ${d.root}`, CH_RX + (i + 0.5) * cw, 296));
  c.textAlign = 'left';
  $('atlasCv').setAttribute('aria-label', `${name}: ${atlas.notes.map((x) => `${x.name} (${x.deg})`).join(', ')}, frets 0 to ${N.frets}` +
    (lit ? `; highlighted: ${atlas.hl.map((i) => atlas.notes[i].name).join(' ')}` : '') + '.');
}

// ------------------------------------------------------------------ layout, show / hide
function atLayout() { Object.assign(atlas, cardFit($('atlasBoard'), $('atlasCard'))); chFoot($('atlasFoot'), atlas.L); atControls(); atDraw(); }
function atlasShow(on) {
  $('atlas').hidden = !on;
  if (!on) return;
  canvas.style.visibility = 'hidden'; $('overview').disabled = true;
  atChanged(true); atLayout();
}
function wireAtlas() {
  $('atlasTuning').innerHTML = CB_TUNINGS.map(([id, label]) => `<option value="${id}">${label}</option>`).join('');
  $('atlasPick').addEventListener('click', atClick); $('atlasChords').addEventListener('click', atClick);
  $('atlasTuning').onchange = (e) => { atPick.tuning = e.target.value; atChanged(true); };
  $('atlasLabels').onchange = (e) => { atPick.labels = e.target.value; atChanged(true); };
  $('atlasExotic').innerHTML = `<option value="">${SC_EXOTIC.length} exotic scales…</option>` + SC_EXOTIC.map(([id, name, ds]) => `<option value="${id}">${name}  (${ds.map(([n, a]) => cbAcc(a) + n).join(' ')})</option>`).join('');
  $('atlasExotic').onchange = (e) => { if (e.target.value) { atPick.scale = e.target.value; atChanged(); } };
  $('vAtlas').onclick = () => setView('atlas');
  window.addEventListener('keydown', (e) => {
    if (e.target.closest && e.target.closest('input,textarea,select')) return;
    if ((e.key === 's' || e.key === 'S') && !e.ctrlKey && !e.metaKey && !e.altKey) setView(state.view === 'atlas' ? 'stage' : 'atlas');
  });
  rig.skinHooks.push(atDraw);
}
// Test helpers: the scale, its chords, the highlight, and where a fret's note is drawn on the card
rig.atlas = {
  notes: () => atlas.notes.map((x) => `${x.name}:${x.deg}`), chords: () => atlas.chords.map((d) => d.chords.map((c) => c.name)),
  hl: () => atlas.hl.slice(), chord: () => atlas.chord, pick: () => JSON.parse(JSON.stringify(atPick)), layout: () => ({ k: atlas.k, L: atlas.L }),
  at: (s, f) => ({ x: atFretX(f), y: atNeck().y0 + (atTuning()[2].length - 1 - s) * atNeck().sp }), frets: () => atNeck().frets, glass: () => [CH_RX, 100, chRW(atlas.L), 172],
  pixel: (x, y) => { const cv = $('atlasCv'); return Array.from(cv.getContext('2d').getImageData(Math.round(x * cv.width / atlas.L), Math.round(y * cv.height / CH_H), 1, 1).data); },
};

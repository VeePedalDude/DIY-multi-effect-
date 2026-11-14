// views/scale-atlas/view.js - the Scale Atlas, as a view (docs/VIEWS.md): a scale on the fretboard, degrees
// to highlight, and the chords on each degree that use only the scale's notes. The left of the card picks
// the key and the scale (buttons, or the Exotic menu) and highlights degrees; the right shows every note of
// the scale on frets 0 to as many as fit (high string at the top), labelled by degree or note, and the
// chords under it: click one to light its notes. The music is the theory kit (scaleSpell, scaleChords,
// SC_SCALES, SC_EXOTIC, CB_TUNINGS...). Picks are kept with rig.save; the highlight isn't.
'use strict';
const ROWS = [['key', 'KEY', 104], ['scale', 'SCALE', 142], ['hl', 'HIGHLIGHT', 352]];   // the Exotic menu sits at 316
const NOTE_LETTERS = ['C', 'D', 'E', 'F', 'G', 'A', 'B'];
const st = { notes: [], chords: [], hl: [], chord: null, pick: null };
const $ = (id) => document.getElementById(id);
const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
const esc = (t) => String(t).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/"/g, '&quot;');
document.body.innerHTML = `<canvas id="cv" role="img" aria-label="Fretboard"></canvas><div id="pick"></div>
  <select id="exotic" class="cexotic" aria-label="Exotic scales"></select><div id="chords" class="achords"></div>
  <div class="cfoot" id="foot"><select id="tuning" aria-label="Tuning"></select>
  <select id="labels" aria-label="Labels on the notes"><option value="deg">Degrees</option><option value="note">Notes</option></select>
  <span class="mono cpage" id="info"></span></div>`;

const tuning = () => CB_TUNINGS.find((t) => t[0] === st.pick.tuning) || CB_TUNINGS[0];
const RW = () => rig.card.RW, RX = () => rig.card.RX;
// the neck in card units: open notes, the nut, the last fret, the top string, string spacing; 12-22 frets, ~30 units each
const neck = () => ({ open: RX() + 24, x0: RX() + 42, x1: RX() + RW() - 12, y0: 120, sp: 24, frets: clamp(Math.floor((RW() - 54) / 30), 12, 22) });
const fretX = (f) => { const N = neck(); return f === 0 ? N.open : N.x0 + (f - 0.5) * (N.x1 - N.x0) / N.frets; };
const chip = (label, pressed, act, val, disabled) => `<button class="cchip" aria-pressed="${pressed}" data-ch="${act}" data-v="${val}"${disabled ? ' disabled' : ''}>${label}</button>`;
const noteRow = (cur) => NOTE_LETTERS.map((l, i) => chip(l, cur.letter === i, 'key', i)).join('') + chip('♭', cur.acc === -1, 'keyAcc', -1) + chip('♯', cur.acc === 1, 'keyAcc', 1);

function changed(keepHl) {
  rig.save(st.pick);
  st.notes = scaleSpell(st.pick.key, st.pick.scale); st.chords = scaleChords(st.notes);
  if (!keepHl) { st.hl = []; st.chord = null; }
  controls(); draw();
}

// ------------------------------------------------------------------ buttons
function controls() {
  const rows = {
    key: noteRow(st.pick.key),
    scale: SC_SCALES.map(([id, name, , short]) => chip(short, st.pick.scale === id, 'scale', id).replace('<button', `<button title="${esc(name)}"`)).join(''),
    hl: st.notes.map((x, i) => chip(x.deg, st.hl.includes(i), 'hl', i)).join('') + chip('clear', false, 'clear', 0, !st.hl.length),
  };
  $('pick').innerHTML = ROWS.map(([id, label, y]) => `<div class="crow${id === 'scale' ? ' scales' : id === 'hl' ? ' hl' : ''}" style="top:${y}px" role="group" aria-label="${label.toLowerCase()}">${rows[id]}</div>`).join('');
  const n = st.chords.length, cw = RW() / n;                         // the chords: a column per degree under the neck
  $('chords').innerHTML = st.chords.map((d, i) => d.chords.map((c, j) =>
    `<button class="cchip" style="left:${Math.round(RX() + i * cw + 3)}px;top:${304 + j * 25}px;width:${Math.round(cw - 6)}px" aria-pressed="${st.chord === c.name}" data-ch="chord" data-v="${i}.${j}" title="${esc(c.name)}: ${c.idx.map((k) => st.notes[k].name).join(' ')}">${c.name}</button>`).join('')).join('');
  $('tuning').value = tuning()[0]; $('labels').value = st.pick.labels;
  const ex = SC_EXOTIC.some((x) => x[0] === st.pick.scale);
  $('exotic').value = ex ? st.pick.scale : ''; $('exotic').classList.toggle('on', ex);
  const ch = st.chord && st.chords.flatMap((d) => d.chords).find((c) => c.name === st.chord);
  $('info').textContent = ch ? `${ch.name}: ${ch.idx.map((k) => st.notes[k].name).join(' ')}` : st.hl.length ? `${st.hl.length} of ${st.notes.length} highlighted` : `${st.notes.length} notes`;
  $('foot').style.width = RW() + 'px';
}
document.addEventListener('click', (e) => {
  const b = e.target.closest('[data-ch]'); if (!b || b.disabled) return;
  const act = b.dataset.ch, v = b.dataset.v;
  if (act === 'key') st.pick.key = { letter: Number(v), acc: 0 };
  if (act === 'keyAcc') st.pick.key = { ...st.pick.key, acc: st.pick.key.acc === Number(v) ? 0 : Number(v) };
  if (act === 'scale') st.pick.scale = v;
  if (act === 'key' || act === 'keyAcc' || act === 'scale') { changed(); return; }
  if (act === 'hl') { const i = Number(v); st.hl = st.hl.includes(i) ? st.hl.filter((x) => x !== i) : [...st.hl, i].sort((a, b) => a - b); st.chord = null; }
  if (act === 'clear') { st.hl = []; st.chord = null; }
  if (act === 'chord') {
    const [i, j] = v.split('.').map(Number), c = st.chords[i].chords[j];
    if (st.chord === c.name) { st.chord = null; st.hl = []; } else { st.chord = c.name; st.hl = [...new Set(c.idx)].sort((a, b) => a - b); }
  }
  changed(true);
});

// ------------------------------------------------------------------ drawing (the page draws the card's frame underneath)
function rr(c, x, y, w, h, r) { c.beginPath(); c.moveTo(x + r, y); c.arcTo(x + w, y, x + w, y + h, r); c.arcTo(x + w, y + h, x, y + h, r); c.arcTo(x, y + h, x, y, r); c.arcTo(x, y, x + w, y, r); c.closePath(); }
function draw() {
  if (!st.notes.length) return;
  const S = rig.skin, P = S.chart, card = S.card, N = neck(), L = rig.card.L, H = rig.card.H, cv = $('cv');
  const w = Math.round(L * rig.card.k * rig.card.dpr), h = Math.round(H * rig.card.k * rig.card.dpr);
  if (cv.width !== w || cv.height !== h) { cv.width = w; cv.height = h; }
  cv.style.width = L + 'px'; cv.style.height = H + 'px';
  const c = cv.getContext('2d'); c.setTransform(w / L, 0, 0, h / H, 0, 0); c.clearRect(0, 0, L, H);
  c.textBaseline = 'alphabetic'; c.textAlign = 'left'; c.fillStyle = card.title; c.font = `700 26px ${S.ui.fontDisplay}`; c.fillText('SCALE', 128, 86);
  c.textAlign = 'right'; c.fillStyle = card.text; c.font = `500 17px ${S.ui.fontMono}`; c.fillText(tuning()[1].split(' (')[0], 626, 86);
  c.font = `600 13px ${S.ui.fontDisplay}`; c.textAlign = 'left';
  ROWS.forEach(([, label, y]) => c.fillText(label, 128, y + 19));
  c.fillText('EXOTIC', 128, 316 + 19);
  c.font = `500 14px ${S.ui.fontMono}`;
  c.fillText(st.notes.map((x) => x.name).join('  '), 214, 412); c.fillText(st.notes.map((x) => x.deg).join('  '), 214, 434);
  // the right: the scale's name, the neck, and the degree over each column of chords
  const name = `${cbNote(st.pick.key)} ${scScale(st.pick.scale)[1]}`;
  c.fillStyle = card.title; c.font = `700 24px ${S.ui.fontDisplay}`; c.fillText(name, RX() + 16, 86);
  rr(c, RX(), 100, RW(), 172, 18); c.fillStyle = P.glass; c.fill(); c.strokeStyle = S.tile.rim; c.lineWidth = 3; c.stroke();
  const tun = tuning()[2], n = tun.length, yOf = (s) => N.y0 + (n - 1 - s) * N.sp, fw = (N.x1 - N.x0) / N.frets;
  c.strokeStyle = P.grid; c.lineWidth = 1;
  for (let f = 0; f <= N.frets; f++) { c.beginPath(); c.moveTo(N.x0 + f * fw, yOf(n - 1)); c.lineTo(N.x0 + f * fw, yOf(0)); c.lineWidth = f === 0 ? 4 : 1; c.stroke(); }
  c.lineWidth = 1;
  for (let s = 0; s < n; s++) { c.beginPath(); c.moveTo(N.x0, yOf(s)); c.lineTo(N.x1, yOf(s)); c.stroke(); }
  c.textAlign = 'center'; c.fillStyle = P.text; c.font = `500 11px ${S.ui.fontMono}`;
  [3, 5, 7, 9, 12, 15, 17, 19, 21].filter((f) => f <= N.frets).forEach((f) => c.fillText(String(f), fretX(f), yOf(0) + 24));
  const pcs = st.notes.map((x) => x.pc), lit = st.hl.length > 0;
  c.textBaseline = 'middle';
  for (let s = 0; s < n; s++) for (let f = 0; f <= N.frets; f++) {
    const i = pcs.indexOf((tun[s] + f) % 12); if (i < 0) continue;
    const on = st.hl.includes(i), x = fretX(f), y = yOf(s);
    c.globalAlpha = lit && !on ? 0.3 : 1;
    c.beginPath(); c.arc(x, y, 10, 0, Math.PI * 2); c.fillStyle = i === 0 ? P.root : on ? P.hi : P.dot; c.fill();
    c.fillStyle = P.glass; c.font = `600 ${st.pick.labels === 'deg' ? 10 : 11}px ${S.ui.fontMono}`;
    c.fillText(st.pick.labels === 'deg' ? st.notes[i].deg : st.notes[i].name, x, y + 0.5);
  }
  c.globalAlpha = 1; c.textBaseline = 'alphabetic';
  const cw = RW() / st.chords.length;
  c.fillStyle = card.text; c.font = `600 12px ${S.ui.fontMono}`;
  st.chords.forEach((d, i) => c.fillText(`${d.deg} · ${d.root}`, RX() + (i + 0.5) * cw, 296));
  c.textAlign = 'left';
  cv.setAttribute('aria-label', `${name}: ${st.notes.map((x) => `${x.name} (${x.deg})`).join(', ')}, frets 0 to ${N.frets}` +
    (lit ? `; highlighted: ${st.hl.map((i) => st.notes[i].name).join(' ')}` : '') + '.');
}

// ------------------------------------------------------------------ the rig
rig.on('init', () => {
  const d = { key: { letter: 5, acc: 0 }, scale: 'minpent', tuning: 'std', labels: 'deg' };
  st.pick = { ...d, ...rig.settings };
  $('tuning').innerHTML = CB_TUNINGS.map(([id, label]) => `<option value="${id}">${esc(label)}</option>`).join('');
  $('exotic').innerHTML = `<option value="">${SC_EXOTIC.length} exotic scales…</option>` + SC_EXOTIC.map(([id, name, ds]) => `<option value="${id}">${esc(name)}  (${ds.map(([n, a]) => cbAcc(a) + n).join(' ')})</option>`).join('');
  $('tuning').onchange = (e) => { st.pick.tuning = e.target.value; changed(true); };
  $('labels').onchange = (e) => { st.pick.labels = e.target.value; changed(true); };
  $('exotic').onchange = (e) => { if (e.target.value) { st.pick.scale = e.target.value; changed(); } };
  changed(true); rig.ready();
});
rig.on('layout', () => { controls(); draw(); });
rig.on('skin', draw);
// for the tests (tests/browser/atlas.cjs reads these inside the frame)
window.atlasTest = {
  notes: () => st.notes.map((x) => `${x.name}:${x.deg}`), chords: () => st.chords.map((d) => d.chords.map((c) => c.name)),
  hl: () => st.hl.slice(), chord: () => st.chord, pick: () => JSON.parse(JSON.stringify(st.pick)), frets: () => neck().frets,
  at: (s, f) => ({ x: fretX(f), y: neck().y0 + (tuning()[2].length - 1 - s) * neck().sp }), glass: () => [RX(), 100, RW(), 172],
  pixel: (x, y) => { const cv = $('cv'); return Array.from(cv.getContext('2d').getImageData(Math.round(x * cv.width / rig.card.L), Math.round(y * cv.height / rig.card.H), 1, 1).data); },
  colors: () => rig.skin.chart,
};

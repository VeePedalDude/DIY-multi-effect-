// 15-chordbook.js - the Chord Chart's music (its view is 64-chart.js). chordSpell() builds a chord from a
// key, a formula and the options picked (extension, additions, alterations, bass note) and spells it:
// its name, its degrees (1-3-(5)-♭7) and its notes (C-E-(G)-B♭), optional ones in brackets.
// chordVoicings() finds every playable shape of it on a tuning at one fret position, best first.
//  - A note is a letter (0..6 = C..B) plus an accidental (-1 flat, 0, +1 sharp). Degrees are spelled from
//    the root's letter, so C♯'s third is E♯ and B♭7's seventh is A♭.
//  - Options a formula can't take are refused, not ignored silently: chordOptions() says which are open.
//  - Fret positions: 0 is Open (at least one open string, fretted notes within frets 1-4); p >= 1 is a
//    four-fret window starting at fret p, no open strings, with a note on fret p.
const CB_LETTERS = ['C', 'D', 'E', 'F', 'G', 'A', 'B'], CB_LETTER_PC = [0, 2, 4, 5, 7, 9, 11];
const CB_SEMI = { 1: 0, 2: 2, 3: 4, 4: 5, 5: 7, 6: 9, 7: 11, 9: 14, 11: 17, 13: 21 };
// [id, suffix, degrees]: a degree is [number, alteration, optional]
const CB_FORMULAS = [
  ['maj', '', [[1, 0], [3, 0], [5, 0, 1]]],
  ['m', 'm', [[1, 0], [3, -1], [5, 0, 1]]],
  ['aug', 'aug', [[1, 0], [3, 0], [5, 1]]],
  ['dim', 'dim', [[1, 0], [3, -1], [5, -1]]],
  ['5', '5', [[1, 0], [5, 0]]],
  ['6', '6', [[1, 0], [3, 0], [5, 0, 1], [6, 0]]],
  ['m6', 'm6', [[1, 0], [3, -1], [5, 0, 1], [6, 0]]],
  ['7', '7', [[1, 0], [3, 0], [5, 0, 1], [7, -1]]],
  ['maj7', 'maj7', [[1, 0], [3, 0], [5, 0, 1], [7, 0]]],
  ['m7', 'm7', [[1, 0], [3, -1], [5, 0, 1], [7, -1]]],
  ['m7b5', 'm7♭5', [[1, 0], [3, -1], [5, -1], [7, -1]]],
  ['dim7', 'dim7', [[1, 0], [3, -1], [5, -1], [7, -2]]],
  ['mMaj7', 'mMaj7', [[1, 0], [3, -1], [5, 0, 1], [7, 0]]],
];
const CB_EXT = [9, 11, 13];
const CB_ADDS = [['add2', 2], ['add4', 4], ['add6', 6], ['add9', 9], ['add11', 11]];
// [id, label, degree, alteration]: sus2/sus4 take the third's place, the rest replace their degree or add it
const CB_ALTS = [['b5', '♭5', 5, -1], ['s5', '♯5', 5, 1], ['b9', '♭9', 9, -1], ['s9', '♯9', 9, 1],
  ['s11', '♯11', 11, 1], ['b13', '♭13', 13, -1], ['sus2', 'sus2', 2, 0], ['sus4', 'sus4', 4, 0]];
const CB_TUNINGS = [
  ['std', 'Standard (E A D G B E)', [40, 45, 50, 55, 59, 64]],
  ['dropd', 'Drop D (D A D G B E)', [38, 45, 50, 55, 59, 64]],
  ['eb', 'Half step down (E♭ A♭ D♭ G♭ B♭ E♭)', [39, 44, 49, 54, 58, 63]],
  ['dstd', 'D standard (D G C F A D)', [38, 43, 48, 53, 57, 62]],
  ['dropc', 'Drop C (C G C F A D)', [36, 43, 48, 53, 57, 62]],
  ['dadgad', 'DADGAD', [38, 45, 50, 55, 57, 62]],
  ['openg', 'Open G (D G D G B D)', [38, 43, 50, 55, 59, 62]],
  ['opend', 'Open D (D A D F♯ A D)', [38, 45, 50, 54, 57, 62]],
  ['opene', 'Open E (E B E G♯ B E)', [40, 47, 52, 56, 59, 64]],
  ['openc', 'Open C (C G C G C E)', [36, 43, 48, 55, 60, 64]],
];
const CB_POSITIONS = 12;                          // Open, then frets 1..12

const cbPc = (n) => (CB_LETTER_PC[n.letter] + n.acc + 12) % 12;
const cbAcc = (a) => a === 0 ? '' : a > 0 ? '♯'.repeat(a) : '♭'.repeat(-a);
const cbNote = (n) => CB_LETTERS[n.letter] + cbAcc(n.acc);
const cbFormula = (id) => CB_FORMULAS.find((f) => f[0] === id) || CB_FORMULAS[0];
const cbHas7 = (id) => ['7', 'maj7', 'm7', 'm7b5', 'mMaj7'].includes(id);

// Which options this formula can take, given what else is picked: { ext: {9:true..}, adds: {...}, alts: {...} }
function chordOptions(o) {
  const id = cbFormula(o.formula)[0], seven = cbHas7(id), triadish = !['aug', 'dim', 'm7b5', 'dim7', '5'].includes(id);
  const ext = Object.fromEntries(CB_EXT.map((e) => [e, seven]));
  const alts = { b5: triadish, s5: triadish, b9: seven, s9: seven, s11: seven, b13: seven, sus2: triadish, sus4: triadish };
  // an addition is open unless the chord already has that note (6 on a 6 chord, 9 under a 9th or 13th ...)
  const core = chordDegrees({ ...o, adds: [] }), pcs = new Set(core.map((d) => (CB_SEMI[d[0]] + d[1]) % 12));
  const adds = Object.fromEntries(CB_ADDS.map(([a, d]) => [a, !pcs.has(CB_SEMI[d] % 12)]));
  return { ext, adds, alts };
}
// The degrees of the chord picked, low to high: [number, alteration, optional]. Options it can't take are skipped.
function chordDegrees(o) {
  const [id] = cbFormula(o.formula), seven = cbHas7(id), triadish = !['aug', 'dim', 'm7b5', 'dim7', '5'].includes(id);
  let ds = cbFormula(id)[2].map((d) => [d[0], d[1], !!d[2]]);
  const put = (n, a, opt) => { ds = ds.filter((d) => d[0] !== n); ds.push([n, a, !!opt]); };
  const ext = seven && CB_EXT.includes(o.ext) ? o.ext : 0, minor = ds.some((d) => d[0] === 3 && d[1] === -1);
  if (ext === 9) put(9, 0);
  if (ext === 11) { put(9, 0, 1); put(11, 0); if (!minor) put(3, 0, 1); }   // the third is usually left out under an 11
  if (ext === 13) { put(9, 0, 1); if (minor) put(11, 0, 1); put(13, 0); }
  const alt = new Set(o.alts || []), on = (a) => alt.has(a) && (['b9', 's9', 's11', 'b13'].includes(a) ? seven : triadish);
  if (on('b5') || on('s5')) { ds = ds.filter((d) => d[0] !== 5); if (on('b5')) ds.push([5, -1, false]); if (on('s5')) ds.push([5, 1, false]); }
  if (on('b9') || on('s9')) { ds = ds.filter((d) => d[0] !== 9); if (on('b9')) ds.push([9, -1, false]); if (on('s9')) ds.push([9, 1, false]); }
  if (on('s11')) put(11, 1);
  if (on('b13')) put(13, -1);
  if (on('sus2') || on('sus4')) { ds = ds.filter((d) => d[0] !== 3); if (on('sus2')) ds.push([2, 0, false]); if (on('sus4')) ds.push([4, 0, false]); }
  const seen = new Set(ds.map((d) => (CB_SEMI[d[0]] + d[1]) % 12));
  for (const a of o.adds || []) {
    const e = CB_ADDS.find((x) => x[0] === a); if (!e) continue;
    const pc = CB_SEMI[e[1]] % 12; if (seen.has(pc)) continue;
    seen.add(pc); ds.push([e[1], 0, false]);
  }
  return ds.sort((a, b) => CB_SEMI[a[0]] + a[1] - CB_SEMI[b[0]] - b[1] || a[0] - b[0]);
}
// The chord's name, degrees and notes. o: { root: {letter, acc}, formula, ext, adds: [], alts: [], bass: {letter, acc} | null }
function chordSpell(o) {
  const [id, suf0] = cbFormula(o.formula), ds = chordDegrees(o), avail = chordOptions(o);
  const root = o.root, rpc = cbPc(root);
  const notes = ds.map(([n, a, opt]) => {
    const letter = (root.letter + n - 1) % 7, pc = (rpc + CB_SEMI[n] + a) % 12;
    let acc = (pc - CB_LETTER_PC[letter] + 12) % 12; if (acc > 6) acc -= 12;
    return { deg: cbAcc(a) + n, note: { letter, acc }, name: CB_LETTERS[letter] + cbAcc(acc), pc, opt };
  });
  // the name: formula, its seventh swapped for the extension, sus in place of the third, alterations, additions
  let suf = suf0;
  const ext = avail.ext[o.ext] ? o.ext : 0;
  if (ext) suf = suf.replace('7', String(ext));
  const alts = (o.alts || []).filter((a) => avail.alts[a]), adds = (o.adds || []).filter((a) => avail.adds[a]);
  const sus = ['sus2', 'sus4'].filter((a) => alts.includes(a));
  if (sus.length) suf = suf.replace(/^m(?!aj)/, '').replace(/^Maj/, 'maj') + 'sus' + sus.map((s) => s[3]).join('');
  const marks = CB_ALTS.filter((a) => !a[0].startsWith('sus') && alts.includes(a[0])).map((a) => a[1]).join('');
  if (marks) suf += /\d/.test(suf) ? marks : `(${marks})`;
  if (id === '6' && !sus.length && !marks && adds.length === 1 && adds[0] === 'add9') suf = '6/9';
  else if (adds.length) suf += suf || marks ? `(${adds.join(',')})` : adds.join('');
  const bass = o.bass && cbPc(o.bass) !== rpc ? o.bass : null;
  const name = cbNote(root) + suf + (bass ? '/' + cbNote(bass) : '');
  const br = (s, opt) => opt ? `(${s})` : s;
  return { name, notes, bassPc: bass ? cbPc(bass) : rpc, bassName: bass ? cbNote(bass) : null, rootPc: rpc,
    degrees: notes.map((x) => br(x.deg, x.opt)).join('-'), spelled: notes.map((x) => br(x.name, x.opt)).join('-') + (bass ? ' / ' + cbNote(bass) : ''),
    avail, ext, adds, alts };
}

// Every playable voicing of chord c (from chordSpell) on tuning (MIDI notes, low string first) at fret
// position pos, best first. A voicing is { frets: [-1 muted | 0 open | fret, ...], notes: [MIDI], score }.
// Playable: the bass note lowest; every required note there, nothing outside the chord (a slash bass only
// at the bottom); at most one muted string between sounding ones; at most four fingers, a barre on the
// lowest fret counting as one when nothing under it is open or muted.
function chordVoicings(c, tuning, pos) {
  const req = new Set(c.notes.filter((x) => !x.opt).map((x) => x.pc)), opt = new Set(c.notes.filter((x) => x.opt).map((x) => x.pc));
  const all = new Set([...req, ...opt]), S = tuning.length;
  const choices = pos === 0 ? [-1, 0, 1, 2, 3, 4] : [-1, pos, pos + 1, pos + 2, pos + 3];
  const minStr = all.size <= 2 ? 2 : Math.min(Math.max(3, req.size), S);
  const out = [], f = new Array(S), ok = new Set([...all, c.bassPc]);       // ok: notes a string may sound
  const visit = (s) => {
    if (s < S) { for (const x of choices) if (x < 0 || ok.has((tuning[s] + x) % 12)) { f[s] = x; visit(s + 1); } return; }
    const played = []; for (let i = 0; i < S; i++) if (f[i] >= 0) played.push(i);
    if (played.length < minStr) return;
    const fretted = played.filter((i) => f[i] > 0), lo = fretted.length ? Math.min(...fretted.map((i) => f[i])) : 0, hi = fretted.length ? Math.max(...fretted.map((i) => f[i])) : 0;
    if (pos === 0 ? (!played.some((i) => f[i] === 0) || hi - lo > 3) : lo !== pos) return;
    let inner = 0; for (let i = played[0]; i <= played[played.length - 1]; i++) if (f[i] < 0) inner++;
    if (inner > 1) return;
    const midi = played.map((i) => tuning[i] + f[i]), pcs = midi.map((m) => m % 12);
    let low = 0; for (let k = 1; k < midi.length; k++) if (midi[k] < midi[low]) low = k;
    if (pcs[low] !== c.bassPc) return;
    for (let k = 0; k < pcs.length; k++) if (!all.has(pcs[k]) && !(k === low && pcs[k] === c.bassPc)) return;
    for (const p of req) if (!pcs.includes(p)) return;
    // fingers: one per fretted note, or a barre across the lowest fret plus one per note above it
    let fingers = fretted.length;
    if (fingers > 4) {
      const at = fretted.filter((i) => f[i] === lo), a = at[0], b = at[at.length - 1];
      for (let i = a; i <= b; i++) if (f[i] < lo) return;          // an open or muted string under the barre
      fingers = 1 + fretted.filter((i) => f[i] > lo).length;
      if (fingers > 4) return;
    }
    const missing = [...opt].filter((p) => !pcs.includes(p)).length, opens = played.filter((i) => f[i] === 0).length;
    const score = 10 * inner + 2 * (S - played.length) + 4 * missing + played[0] + 0.5 * fingers + (hi - lo) - (pos === 0 ? 0.5 * opens : 0) + (midi[0] === midi[low] ? 0 : 3);
    out.push({ frets: f.slice(), notes: midi, score });
  };
  visit(0);
  return out.sort((a, b) => a.score - b.score || a.frets.join().localeCompare(b.frets.join()));
}
// "x32010": the frets low string first; frets above 9 in brackets
const fretsText = (frets) => frets.map((x) => x < 0 ? 'x' : x > 9 ? `(${x})` : String(x)).join('');

// tests/ui/chordbook.test.mjs - the Chord Chart's music (15-chordbook.js): spelling, names, which options a
// formula takes, and the shapes found on the fretboard.
import { loadUi, check, done } from './load.mjs';
const u = await loadUi();
const K = (n) => ({ letter: 'CDEFGAB'.indexOf(n[0]), acc: n.includes('b') ? -1 : n.includes('#') ? 1 : 0 });
const sp = (root, o = {}) => u.chordSpell({ root: K(root), formula: 'maj', ext: 0, adds: [], alts: [], bass: null, ...o, bass: o.bass ? K(o.bass) : null });
const std = u.CB_TUNINGS[0][2], dropd = u.CB_TUNINGS.find((t) => t[0] === 'dropd')[2];
const shapes = (c, pos, tun = std) => u.chordVoicings(c, tun, pos).map((v) => u.fretsText(v.frets));

console.log('spelling');
const spells = [[sp('C'), 'C', '1-3-(5)', 'C-E-(G)'], [sp('C#', { formula: '7' }), 'C♯7', '1-3-(5)-♭7', 'C♯-E♯-(G♯)-B'],
  [sp('Bb', { formula: 'dim7' }), 'B♭dim7', '1-♭3-♭5-♭♭7', 'B♭-D♭-F♭-A♭♭'], [sp('F#', { formula: 'm7b5' }), 'F♯m7♭5', '1-♭3-♭5-♭7', 'F♯-A-C-E'],
  [sp('Eb', { formula: 'maj7' }), 'E♭maj7', '1-3-(5)-7', 'E♭-G-(B♭)-D'], [sp('C', { formula: '7', ext: 13, alts: ['b9'] }), 'C13♭9', '1-3-(5)-♭7-♭9-13', 'C-E-(G)-B♭-D♭-A'],
  [sp('A', { formula: 'm7', ext: 11 }), 'Am11', '1-♭3-(5)-♭7-(9)-11', 'A-C-(E)-G-(B)-D']];
const bad = spells.filter(([c, n, d, s]) => c.name !== n || c.degrees !== d || c.spelled !== s).map(([c, n]) => `${n}: got ${c.name} ${c.degrees} = ${c.spelled}`);
check(!bad.length, `${spells.length - bad.length} of ${spells.length} chords spelled from the root's letter (C♯7 has E♯, B♭dim7 has A♭♭)${bad.length ? ': ' + bad.join('; ') : ''}`);

console.log('names');
const names = [[sp('C', { formula: '7', alts: ['s9'] }), 'C7♯9'], [sp('C', { formula: '6', adds: ['add9'] }), 'C6/9'], [sp('C', { formula: 'm7', alts: ['sus4'] }), 'C7sus4'],
  [sp('C', { formula: 'm', alts: ['s5'] }), 'Cm(♯5)'], [sp('C', { adds: ['add9'] }), 'Cadd9'], [sp('C', { formula: 'm', adds: ['add9'] }), 'Cm(add9)'],
  [sp('C', { bass: 'E' }), 'C/E'], [sp('C', { bass: 'C' }), 'C'], [sp('C', { formula: 'maj7', ext: 9 }), 'Cmaj9'], [sp('G', { formula: '7', alts: ['b5', 'b9'] }), 'G7♭5♭9']];
const badN = names.filter(([c, n]) => c.name !== n).map(([c, n]) => `${n}: got ${c.name}`);
check(!badN.length, `${names.length - badN.length} of ${names.length} named right${badN.length ? ': ' + badN.join('; ') : ''}`);

console.log('options');
const o = (f, x = {}) => u.chordOptions({ root: K('C'), formula: f, ext: 0, adds: [], alts: [], ...x });
check(!o('maj').ext[9] && !o('maj').alts.b9 && o('maj').alts.sus4 && o('7').ext[13] && o('7').alts.s11, 'a triad takes no extension or ♭9/♯9/♯11/♭13; a seventh chord does');
check(!o('dim').alts.b5 && !o('5').alts.sus2 && !o('6').adds.add6 && !o('7', { ext: 9 }).adds.add9 && o('7').adds.add9, 'nothing the chord already has: no ♭5 on dim, no sus on a power chord, no add6 on a 6, no add9 under a 9th');
check(sp('C', { ext: 9, alts: ['b9'] }).name === 'C', 'options a formula can’t take are left out of the chord');

console.log('shapes');
const has = (list, s) => list.includes(s);
const open = [['C', {}, 'x32010'], ['G', {}, '320003'], ['A', {}, 'x02220'], ['E', {}, '022100'], ['D', {}, 'xx0232'], ['A', { formula: 'm' }, 'x02210'], ['C', { bass: 'E' }, '032010']];
const badS = open.filter(([r, x, s]) => !has(shapes(sp(r, x), 0), s)).map(([r, , s]) => `${r} ${s}`);
check(!badS.length, `the open chords are found (C x32010, G 320003, A, E, D, Am, C/E 032010)${badS.length ? ': missing ' + badS.join(', ') : ''}`);
check(shapes(sp('C'), 0)[0] === 'x32010' && shapes(sp('G'), 0)[0] === '320003', `best first: C ${shapes(sp('C'), 0)[0]}, G ${shapes(sp('G'), 0)[0]}`);
check(has(shapes(sp('F'), 1), '133211') && has(shapes(sp('B', { formula: 'm' }), 2), 'x24432') && has(shapes(sp('D'), 0, dropd), '000232'), 'barres (F 133211 at fret 1, Bm x24432 at fret 2) and another tuning (D in Drop D 000232)');
// every shape, every key and formula, every position: only the chord's notes, the bass lowest, every required note, playable
let n = 0, wrong = [];
const t0 = Date.now();
for (const root of ['C', 'Db', 'D', 'Eb', 'E', 'F', 'F#', 'G', 'Ab', 'A', 'Bb', 'B']) for (const [f] of u.CB_FORMULAS) for (let pos = 0; pos <= 12; pos++) {
  const c = sp(root, { formula: f, bass: f === 'maj' ? 'E' : null }), pcs = new Set(c.notes.map((x) => x.pc)), req = c.notes.filter((x) => !x.opt).map((x) => x.pc);
  for (const v of u.chordVoicings(c, std, pos)) {
    n++;
    const ns = v.frets.map((x, s) => x < 0 ? null : std[s] + x).filter((x) => x !== null), low = Math.min(...ns), fr = v.frets.filter((x) => x > 0);
    const ok = low % 12 === c.bassPc && ns.every((m) => pcs.has(m % 12) || m === low) && req.every((p) => ns.some((m) => m % 12 === p))
      && (pos === 0 ? v.frets.includes(0) && Math.max(0, ...fr) <= 4 : !v.frets.includes(0) && Math.min(...fr) === pos && Math.max(...fr) <= pos + 3);
    if (!ok && wrong.length < 3) wrong.push(`${c.name} ${u.fretsText(v.frets)}`);
  }
}
check(!wrong.length && n > 5000, `${n} shapes over 12 keys x ${u.CB_FORMULAS.length} formulas x 13 positions: only chord notes, bass lowest, every required note, inside the position (${Date.now() - t0} ms)${wrong.length ? ': ' + wrong.join(', ') : ''}`);
done('chordbook');

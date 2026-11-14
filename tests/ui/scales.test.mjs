// tests/ui/scales.test.mjs - the Scale Atlas's music (16-scales.js): spelling and the chords that fit.
import { loadUi, check, done } from './load.mjs';
const u = await loadUi();
const K = (n) => ({ letter: 'CDEFGAB'.indexOf(n[0]), acc: n.includes('b') ? -1 : n.includes('#') ? 1 : 0 });
const names = (root, id) => u.scaleSpell(K(root), id).map((x) => x.name).join(' ');
const chords = (root, id) => u.scaleChords(u.scaleSpell(K(root), id)).map((d) => d.chords.map((c) => c.name));

console.log('spelling');
const cases = [['C', 'major', 'C D E F G A B'], ['F#', 'major', 'F♯ G♯ A♯ B C♯ D♯ E♯'], ['A', 'minpent', 'A C D E G'], ['E', 'blues', 'E G A B♭ B D'],
  ['A', 'harmonic', 'A B C D E F G♯'], ['D', 'dorian', 'D E F G A B C'], ['Eb', 'lydian', 'E♭ F G A B♭ C D'], ['C', 'whole', 'C D E F♯ G♯ B♭'], ['E', 'phrygdom', 'E F G♯ A B C D']];
const bad = cases.filter(([r, id, want]) => names(r, id) !== want).map(([r, id, want]) => `${r} ${id}: got ${names(r, id)}, want ${want}`);
check(!bad.length, `${cases.length - bad.length} of ${cases.length} scales spelled from the root's letter (F♯ major has E♯)${bad.length ? ': ' + bad.join('; ') : ''}`);
check(u.SC_SCALES.every(([, , ds]) => new Set(ds.map(([n, a]) => ({ 1: 0, 2: 2, 3: 4, 4: 5, 5: 7, 6: 9, 7: 11 })[n] + a)).size === ds.length), `${u.SC_SCALES.length} scales, no note twice`);

const semis = ([n, a]) => ({ 1: 0, 2: 2, 3: 4, 4: 5, 5: 7, 6: 9, 7: 11 })[n] + a;
check(u.SC_EXOTIC.length >= 30 && u.SC_EXOTIC.every(([, , ds]) => new Set(ds.map((d) => (semis(d) + 12) % 12)).size === ds.length && semis(ds[0]) === 0),
  `${u.SC_EXOTIC.length} exotic scales, each from its root with no note twice`);
const ids = [...u.SC_SCALES, ...u.SC_EXOTIC].map((x) => x[0]), sets = [...u.SC_SCALES, ...u.SC_EXOTIC].map(([, , ds]) => ds.map((d) => (semis(d) + 12) % 12).sort((a, b) => a - b).join());
check(new Set(ids).size === ids.length && new Set(sets).size === sets.length, 'no id or set of notes listed twice across the buttons and the Exotic menu');
const ex = [['C', 'dblharm', 'C D♭ E F G A♭ B'], ['C', 'altered', 'C D♭ E♭ F♭ G♭ A♭ B♭'], ['A', 'enigmatic', 'A B♭ C♯ D♯ E♯ F♯♯ G♯'], ['E', 'insen', 'E F A B D'], ['C', 'messiaen3', 'C D E♭ E F♯ G A♭ B♭ B']];
const badE = ex.filter(([r, id, want]) => names(r, id) !== want).map(([r, id, want]) => `${r} ${id}: got ${names(r, id)}, want ${want}`);
check(!badE.length, `exotic spellings: C double harmonic, C altered (F♭), A enigmatic (F𝄪 as F♯♯), E insen, Messiaen 3 (9 notes)${badE.length ? ': ' + badE.join('; ') : ''}`);
console.log('chords that fit');
const cm = chords('C', 'major');
check(cm.map((x) => x[0]).join() === 'C,Dm,Em,F,G,Am,Bdim' && cm.map((x) => x.find((n) => /7/.test(n))).join() === 'Cmaj7,Dm7,Em7,Fmaj7,G7,Am7,Bm7♭5',
  `C major: the diatonic triads and sevenths (${cm.map((x) => x.slice(0, 2).join('/')).join(', ')})`);
const ah = chords('A', 'harmonic');
check(ah[0].includes('Am') && ah[0].includes('AmMaj7') && ah[2].includes('Caug') && ah[4].includes('E7') && ah[6].includes('G♯dim7'), `A harmonic minor: Am, AmMaj7, Caug, E7, G♯dim7 (${ah.map((x) => x[0]).join(', ')})`);
const ap = u.scaleChords(u.scaleSpell(K('A'), 'minpent'));
const pcsOf = new Set(u.scaleSpell(K('A'), 'minpent').map((x) => x.pc));
check(ap.every((d) => d.chords.length <= 4 && d.chords.every((c) => c.pcs.every((p) => pcsOf.has(p)))) && ap[0].chords.some((c) => c.name === 'Am7'),
  `A minor pentatonic: only chords from its five notes, at most 4 a degree (${ap.map((d) => d.chords.map((c) => c.name).join('/')).join(', ')})`);
let all = 0, wrong = 0;
for (const r of ['C', 'Db', 'D', 'Eb', 'E', 'F', 'F#', 'G', 'Ab', 'A', 'Bb', 'B']) for (const [id] of [...u.SC_SCALES, ...u.SC_EXOTIC]) {
  const ns = u.scaleSpell(K(r), id), ps = new Set(ns.map((x) => x.pc));
  for (const d of u.scaleChords(ns)) for (const c of d.chords) { all++; if (!c.pcs.every((p) => ps.has(p)) || c.idx.some((i) => ns[i].pc !== c.pcs[c.idx.indexOf(i)])) wrong++; }
}
check(!wrong && all > 1000, `${all} chords over 12 keys x ${u.SC_SCALES.length + u.SC_EXOTIC.length} scales, every note in its scale (${wrong} wrong)`);
done('scales');

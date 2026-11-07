// tests/ui/chords.test.mjs - naming chords for the Visualizer's keyboard (14-chords.js).
import { loadUi, check, done } from './load.mjs';
const u = await loadUi();
const n = (notes) => { const r = u.chordName(notes); return r ? r.name || (r.interval || '') : null; };
const cases = [
  [[48, 52, 55], 'C'], [[45, 52, 57, 60, 64], 'Am'], [[40, 47, 52, 55, 59, 64], 'Em'], [[43, 47, 50, 53], 'G7'],
  [[50, 57], 'D5'], [[40, 47, 52], 'E5'], [[48, 52, 55, 59], 'Cmaj7'], [[45, 52, 55, 60], 'Am7'], [[48, 52, 55, 57], 'C6'],
  [[52, 55, 60], 'C/E'], [[43, 50, 55, 60], 'Gsus4'], [[50, 57, 62, 64], 'Dsus2'], [[47, 50, 53], 'Bdim'], [[48, 52, 56], 'Caug'],
  [[47, 50, 53, 57], 'Bm7b5'], [[48, 52, 58], 'C7'], [[48, 50, 52, 55], 'Cadd9'], [[48, 52, 55, 58, 62], 'C9'], [[45, 48, 52, 55, 59], 'Am9'],
  [[57], 'A'], [[64, 76], 'E'],
];
let bad = [];
for (const [notes, want] of cases) { const got = n(notes); if (got !== want) bad.push(`${notes.map(u.midiName).join(' ')} -> ${got} (want ${want})`); }
check(!bad.length, `${cases.length - bad.length} of ${cases.length} chords named right${bad.length ? ': ' + bad.join('; ') : ''}`);
check(n([48, 52]) === 'C–E: major 3rd' && n([48, 49, 50]) === '' && u.chordName([]) === null, 'two notes that aren’t a fifth: the interval; notes that fit no shape: no name; no notes: null');
const r = u.chordName([52.3, 55.1, 58.8, 64]);
check(r.name === 'Em' && r.notes.join(' ') === 'E3 G3 B3 E4', `fractional note numbers (a few cents off) are rounded first: ${r.notes.join(' ')} -> ${r.name}`);
const h1 = u.chordHeard([40, 47, 52, 77]), h2 = u.chordHeard([48, 52, 55, 61]), h3 = u.chordHeard([45, 52, 57, 60, 64]);
check(h1.name === 'E5' && h2.name === 'C' && h2.left.join() === 'C#4' && h3.name === 'Am' && !h3.left,
  `heard (loudest first): a stray quiet note is left out (E2 B2 E3 + F5 -> ${h1.name}; C3 E3 G3 + C#4 -> ${h2.name}, leaving ${h2.left}); a chord that fits is named whole (${h3.name})`);
done('chords');

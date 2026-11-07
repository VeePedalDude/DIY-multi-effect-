// 14-chords.js - naming what's played, for the Visualizer's keyboard (59-viz.js). Takes MIDI note numbers
// (the engine's listener, engine/core/listen.h) and names the chord from its pitch classes: the shapes in
// CHORD_SHAPES, tried with each note as the root. A chord whose root is the lowest note wins; otherwise
// the name gets the bass after a slash (C/E). Shapes are tried in order, so the plainer reading of the
// same notes comes first (Am7 before C6 when A is the bass, C6 when C is). One note is its own name; two
// notes are a power chord (a fifth) or named as an interval; notes that fit no shape get no name.
// chordHeard() names what the engine hears: when all of it fits no shape, the quietest notes are left out
// one at a time (a note still ringing from the last chord, an overtone).
const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
const midiName = (midi) => NOTE_NAMES[((midi % 12) + 12) % 12] + (Math.floor(midi / 12) - 1);
// [suffix, intervals above the root]; a shape without its fifth is listed after it
const CHORD_SHAPES = [
  ['', [0, 4, 7]], ['m', [0, 3, 7]], ['5', [0, 7]],
  ['7', [0, 4, 7, 10]], ['maj7', [0, 4, 7, 11]], ['m7', [0, 3, 7, 10]], ['6', [0, 4, 7, 9]], ['m6', [0, 3, 7, 9]],
  ['sus4', [0, 5, 7]], ['sus2', [0, 2, 7]], ['dim', [0, 3, 6]], ['aug', [0, 4, 8]],
  ['m7b5', [0, 3, 6, 10]], ['dim7', [0, 3, 6, 9]], ['mMaj7', [0, 3, 7, 11]], ['7sus4', [0, 5, 7, 10]],
  ['add9', [0, 2, 4, 7]], ['madd9', [0, 2, 3, 7]], ['9', [0, 2, 4, 7, 10]], ['maj9', [0, 2, 4, 7, 11]], ['m9', [0, 2, 3, 7, 10]], ['6/9', [0, 2, 4, 7, 9]],
  ['7', [0, 4, 10]], ['maj7', [0, 4, 11]], ['m7', [0, 3, 10]],
];
const INTERVALS = ['unison', 'minor 2nd', 'major 2nd', 'minor 3rd', 'major 3rd', '4th', 'tritone', '5th', 'minor 6th', 'major 6th', 'minor 7th', 'major 7th'];
// notes: MIDI numbers. Returns { name, root, bass, notes } (name '' when nothing fits) or null for none.
function chordName(notes) {
  const ns = [...new Set(notes.map((n) => Math.round(n)))].sort((a, b) => a - b);
  if (!ns.length) return null;
  const pcs = [...new Set(ns.map((n) => ((n % 12) + 12) % 12))].sort((a, b) => a - b), bass = ((ns[0] % 12) + 12) % 12;
  const out = (name, root) => ({ name, root, bass, notes: ns.map(midiName) });
  if (pcs.length === 1) return out(NOTE_NAMES[pcs[0]], pcs[0]);
  let best = null;
  for (let s = 0; s < CHORD_SHAPES.length; s++) {
    const [suf, iv] = CHORD_SHAPES[s];
    if (iv.length !== pcs.length) continue;
    for (const root of pcs) {
      const set = iv.map((i) => (root + i) % 12).sort((a, b) => a - b);
      if (set.some((x, k) => x !== pcs[k])) continue;
      const score = (root === bass ? 0 : 1000) + s;             // the bass as root first, then the shape's order
      if (!best || score < best.score) best = { score, root, suf };
    }
  }
  if (best) return out(NOTE_NAMES[best.root] + best.suf + (best.root === bass ? '' : '/' + NOTE_NAMES[bass]), best.root);
  if (pcs.length === 2) { const i = (pcs[1] - pcs[0] + 12) % 12, lo = bass, up = pcs[0] === bass ? pcs[1] : pcs[0];
    return { ...out('', lo), interval: `${NOTE_NAMES[lo]}–${NOTE_NAMES[up]}: ${INTERVALS[(up - lo + 12) % 12]}`, i }; }
  return out('', bass);
}
// notes: MIDI numbers, loudest first (as the engine sends them).
function chordHeard(notes) {
  const all = chordName(notes);
  for (let k = notes.length - 1; all && !all.name && k >= 2; k--) {
    const r = chordName(notes.slice(0, k));
    if (r && r.name) return { ...r, left: notes.slice(k).map((n) => midiName(Math.round(n))) };
  }
  return all;
}

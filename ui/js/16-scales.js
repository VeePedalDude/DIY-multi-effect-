// 16-scales.js - the Scale Atlas's music (its view is 65-atlas.js). scaleSpell() spells a scale from a key
// (degrees from the root's letter, as 15-chordbook.js spells chords); scaleChords() lists, for each note
// of the scale as a root, the chords whose notes all lie in the scale: triads first, then sevenths, then
// sus, sixths and power chords, at most SC_PER_DEG each.
// [id, name, degrees, short name for its button]: a degree is [number 1..7, alteration]
const SC_SCALES = [
  ['major', 'Major (Ionian)', [[1, 0], [2, 0], [3, 0], [4, 0], [5, 0], [6, 0], [7, 0]], 'Major'],
  ['dorian', 'Dorian', [[1, 0], [2, 0], [3, -1], [4, 0], [5, 0], [6, 0], [7, -1]], 'Dorian'],
  ['phrygian', 'Phrygian', [[1, 0], [2, -1], [3, -1], [4, 0], [5, 0], [6, -1], [7, -1]], 'Phrygian'],
  ['lydian', 'Lydian', [[1, 0], [2, 0], [3, 0], [4, 1], [5, 0], [6, 0], [7, 0]], 'Lydian'],
  ['mixolydian', 'Mixolydian', [[1, 0], [2, 0], [3, 0], [4, 0], [5, 0], [6, 0], [7, -1]], 'Mixolydian'],
  ['minor', 'Natural minor (Aeolian)', [[1, 0], [2, 0], [3, -1], [4, 0], [5, 0], [6, -1], [7, -1]], 'Minor'],
  ['locrian', 'Locrian', [[1, 0], [2, -1], [3, -1], [4, 0], [5, -1], [6, -1], [7, -1]], 'Locrian'],
  ['harmonic', 'Harmonic minor', [[1, 0], [2, 0], [3, -1], [4, 0], [5, 0], [6, -1], [7, 0]], 'Harm. minor'],
  ['melodic', 'Melodic minor', [[1, 0], [2, 0], [3, -1], [4, 0], [5, 0], [6, 0], [7, 0]], 'Mel. minor'],
  ['phrygdom', 'Phrygian dominant', [[1, 0], [2, -1], [3, 0], [4, 0], [5, 0], [6, -1], [7, -1]], 'Phryg. dom.'],
  ['hungarian', 'Hungarian minor', [[1, 0], [2, 0], [3, -1], [4, 1], [5, 0], [6, -1], [7, 0]], 'Hungarian'],
  ['majpent', 'Major pentatonic', [[1, 0], [2, 0], [3, 0], [5, 0], [6, 0]], 'Maj. pent.'],
  ['minpent', 'Minor pentatonic', [[1, 0], [3, -1], [4, 0], [5, 0], [7, -1]], 'Min. pent.'],
  ['blues', 'Blues', [[1, 0], [3, -1], [4, 0], [5, -1], [5, 0], [7, -1]], 'Blues'],
  ['hirajoshi', 'Hirajōshi', [[1, 0], [2, 0], [3, -1], [5, 0], [6, -1]], 'Hirajōshi'],
  ['whole', 'Whole tone', [[1, 0], [2, 0], [3, 0], [4, 1], [5, 1], [7, -1]], 'Whole tone'],
  ['dimwh', 'Diminished (whole-half)', [[1, 0], [2, 0], [3, -1], [4, 0], [5, -1], [6, -1], [6, 0], [7, 0]], 'Dim. W-H'],
  ['dimhw', 'Diminished (half-whole)', [[1, 0], [2, -1], [3, -1], [3, 0], [4, 1], [5, 0], [6, 0], [7, -1]], 'Dim. H-W'],
];
// [suffix, intervals above the root], in the order they're listed under a root
const SC_CHORDS = [['', [0, 4, 7]], ['m', [0, 3, 7]], ['dim', [0, 3, 6]], ['aug', [0, 4, 8]],
  ['maj7', [0, 4, 7, 11]], ['7', [0, 4, 7, 10]], ['m7', [0, 3, 7, 10]], ['m7♭5', [0, 3, 6, 10]], ['dim7', [0, 3, 6, 9]], ['mMaj7', [0, 3, 7, 11]],
  ['sus2', [0, 2, 7]], ['sus4', [0, 5, 7]], ['6', [0, 4, 7, 9]], ['m6', [0, 3, 7, 9]], ['5', [0, 7]]];
const SC_PER_DEG = 4;
const scScale = (id) => SC_SCALES.find((s) => s[0] === id) || SC_SCALES[0];

// The scale's notes: [{ deg: '♭3', name: 'E♭', pc }], low to high from the root.
function scaleSpell(root, id) {
  const rpc = cbPc(root);
  return scScale(id)[2].map(([n, a]) => {
    const letter = (root.letter + n - 1) % 7, pc = (rpc + CB_SEMI[n] + a) % 12;
    let acc = (pc - CB_LETTER_PC[letter] + 12) % 12; if (acc > 6) acc -= 12;
    return { deg: cbAcc(a) + n, name: CB_LETTERS[letter] + cbAcc(acc), pc };
  });
}
// For each note of the scale: the chords on it that use only the scale's notes.
// [{ deg, root, chords: [{ name, pcs, idx (the scale notes it uses) }] }]
function scaleChords(notes) {
  const pcs = notes.map((x) => x.pc);
  return notes.map((r) => {
    const chords = [];
    for (const [suf, iv] of SC_CHORDS) {
      if (chords.length >= SC_PER_DEG) break;
      const cp = iv.map((i) => (r.pc + i) % 12);
      if (cp.every((p) => pcs.includes(p))) chords.push({ name: r.name + suf, pcs: cp, idx: cp.map((p) => pcs.indexOf(p)) });
    }
    return { deg: r.deg, root: r.name, chords };
  });
}

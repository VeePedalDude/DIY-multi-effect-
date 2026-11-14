// 16-scales.js - scales for views (part of the theory kit every view's frame gets, docs/VIEWS.md; the Scale
// Atlas view, views/scale-atlas/, puts SC_SCALES on its buttons and SC_EXOTIC in its Exotic menu). scaleSpell() spells a scale from a key
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
// The Exotic menu: rarer and non-Western scales, by name (formulas as in Wikipedia's List of musical scales
// and modes and common guitar references; a scale with two names of the same notes is listed once)
const SC_EXOTIC = [
  ['altered', 'Altered (Super Locrian)', [[1, 0], [2, -1], [3, -1], [4, -1], [5, -1], [6, -1], [7, -1]]],
  ['augmented', 'Augmented', [[1, 0], [3, -1], [3, 0], [5, 0], [5, 1], [7, 0]]],
  ['balinese', 'Balinese (Pelog)', [[1, 0], [2, -1], [3, -1], [5, 0], [6, -1]]],
  ['bebopdom', 'Bebop dominant', [[1, 0], [2, 0], [3, 0], [4, 0], [5, 0], [6, 0], [7, -1], [7, 0]]],
  ['bebopmaj', 'Bebop major', [[1, 0], [2, 0], [3, 0], [4, 0], [5, 0], [5, 1], [6, 0], [7, 0]]],
  ['bebopmin', 'Bebop minor', [[1, 0], [2, 0], [3, -1], [3, 0], [4, 0], [5, 0], [6, 0], [7, -1]]],
  ['chinese', 'Chinese', [[1, 0], [3, 0], [4, 1], [5, 0], [7, 0]]],
  ['dblharm', 'Double harmonic (Byzantine)', [[1, 0], [2, -1], [3, 0], [4, 0], [5, 0], [6, -1], [7, 0]]],
  ['egyptian', 'Egyptian', [[1, 0], [2, 0], [4, 0], [5, 0], [7, -1]]],
  ['enigmatic', 'Enigmatic', [[1, 0], [2, -1], [3, 0], [4, 1], [5, 1], [6, 1], [7, 0]]],
  ['harmmaj', 'Harmonic major', [[1, 0], [2, 0], [3, 0], [4, 0], [5, 0], [6, -1], [7, 0]]],
  ['hungmaj', 'Hungarian major', [[1, 0], [2, 1], [3, 0], [4, 1], [5, 0], [6, 0], [7, -1]]],
  ['in', 'In', [[1, 0], [2, -1], [4, 0], [5, 0], [6, -1]]],
  ['insen', 'Insen', [[1, 0], [2, -1], [4, 0], [5, 0], [7, -1]]],
  ['istrian', 'Istrian', [[1, 0], [2, -1], [3, -1], [4, -1], [5, -1], [5, 0]]],
  ['iwato', 'Iwato', [[1, 0], [2, -1], [4, 0], [5, -1], [7, -1]]],
  ['kumoi', 'Kumoi', [[1, 0], [2, 0], [3, -1], [5, 0], [6, 0]]],
  ['leadwhole', 'Leading whole tone', [[1, 0], [2, 0], [3, 0], [4, 1], [5, 1], [6, 1], [7, 0]]],
  ['locnat2', 'Locrian ♮2 (half diminished)', [[1, 0], [2, 0], [3, -1], [4, 0], [5, -1], [6, -1], [7, -1]]],
  ['locnat6', 'Locrian ♮6', [[1, 0], [2, -1], [3, -1], [4, 0], [5, -1], [6, 0], [7, -1]]],
  ['lydaug', 'Lydian augmented', [[1, 0], [2, 0], [3, 0], [4, 1], [5, 1], [6, 0], [7, 0]]],
  ['lyddim', 'Lydian diminished', [[1, 0], [2, 0], [3, -1], [4, 1], [5, 0], [6, 0], [7, 0]]],
  ['lyddom', 'Lydian dominant', [[1, 0], [2, 0], [3, 0], [4, 1], [5, 0], [6, 0], [7, -1]]],
  ['majloc', 'Major Locrian (Arabian)', [[1, 0], [2, 0], [3, 0], [4, 0], [5, -1], [6, -1], [7, -1]]],
  ['messiaen3', 'Messiaen mode 3', [[1, 0], [2, 0], [3, -1], [3, 0], [4, 1], [5, 0], [6, -1], [7, -1], [7, 0]]],
  ['neapmaj', 'Neapolitan major', [[1, 0], [2, -1], [3, -1], [4, 0], [5, 0], [6, 0], [7, 0]]],
  ['neapmin', 'Neapolitan minor', [[1, 0], [2, -1], [3, -1], [4, 0], [5, 0], [6, -1], [7, 0]]],
  ['oriental', 'Oriental', [[1, 0], [2, -1], [3, 0], [4, 0], [5, -1], [6, 0], [7, -1]]],
  ['persian', 'Persian', [[1, 0], [2, -1], [3, 0], [4, 0], [5, -1], [6, -1], [7, 0]]],
  ['prometheus', 'Prometheus', [[1, 0], [2, 0], [3, 0], [4, 1], [6, 0], [7, -1]]],
  ['romani', 'Romani minor', [[1, 0], [2, 0], [3, -1], [4, 1], [5, 0], [6, -1], [7, -1]]],
  ['harmonics', 'Scale of harmonics', [[1, 0], [3, -1], [3, 0], [4, 0], [5, 0], [6, 0]]],
  ['spanish8', 'Spanish 8-tone', [[1, 0], [2, -1], [3, -1], [3, 0], [4, 0], [5, -1], [6, -1], [7, -1]]],
  ['tritone', 'Tritone', [[1, 0], [2, -1], [3, 0], [5, -1], [5, 0], [7, -1]]],
  ['tritone2', 'Two-semitone tritone', [[1, 0], [2, -1], [2, 0], [4, 1], [5, 0], [6, -1]]],
  ['ukrdorian', 'Ukrainian Dorian (Romanian minor)', [[1, 0], [2, 0], [3, -1], [4, 1], [5, 0], [6, 0], [7, -1]]],
  ['yo', 'Yo', [[1, 0], [2, 0], [4, 0], [5, 0], [6, 0]]],
];
// [suffix, intervals above the root], in the order they're listed under a root
const SC_CHORDS = [['', [0, 4, 7]], ['m', [0, 3, 7]], ['dim', [0, 3, 6]], ['aug', [0, 4, 8]],
  ['maj7', [0, 4, 7, 11]], ['7', [0, 4, 7, 10]], ['m7', [0, 3, 7, 10]], ['m7♭5', [0, 3, 6, 10]], ['dim7', [0, 3, 6, 9]], ['mMaj7', [0, 3, 7, 11]],
  ['sus2', [0, 2, 7]], ['sus4', [0, 5, 7]], ['6', [0, 4, 7, 9]], ['m6', [0, 3, 7, 9]], ['5', [0, 7]]];
const SC_PER_DEG = 4;
const scScale = (id) => SC_SCALES.find((s) => s[0] === id) || SC_EXOTIC.find((s) => s[0] === id) || SC_SCALES[0];

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

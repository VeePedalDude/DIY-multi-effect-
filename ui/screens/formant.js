// screens/formant.js - the three formants of the vowel it's on (the same table as engine/modules/synth/formant.h).
const VOWELS = [
  { f: [600, 1040, 2250], bw: [60, 70, 110], db: [0, -7, -9] }, { f: [400, 1620, 2400], bw: [40, 80, 100], db: [0, -12, -9] },
  { f: [250, 1750, 2600], bw: [60, 90, 100], db: [0, -15, -20] }, { f: [400, 750, 2400], bw: [40, 80, 100], db: [0, -11, -21] },
  { f: [350, 600, 2400], bw: [40, 80, 100], db: [0, -20, -32] },
];
screen('formant', (c, b, m, h) => {
  const env = h.meter('env', 0), sh = Math.pow(2, h.param(1).real(h.eff(1)) / 12), qs = 0.35 * Math.pow(8, h.eff(2)), mix = h.eff(4);
  const pos = clamp(h.param(0).real(h.eff(0)) + 4 * h.param(3).real(h.eff(3)) / 100 * env, 0, 4), a = Math.min(3, Math.floor(pos)), t = pos - a;
  const x = VOWELS[a], y = VOWELS[a + 1];
  const bands = [0, 1, 2].map((i) => { const hz = x.f[i] * Math.pow(y.f[i] / x.f[i], t) * sh, bw = (x.bw[i] + t * (y.bw[i] - x.bw[i])) * sh;
    return { hz, q: hz / bw * qs, g: Math.pow(10, 0.5 * (x.db[i] + t * (y.db[i] - x.db[i])) / 20) }; });
  const norm = 5 / bands.reduce((s, q) => s + q.g, 0);
  EQ.plot(c, b, h, (f) => {
    let sum = [0, 0];
    for (const bd of bands) { const s = CX.jw(f, bd.hz), k = 1 / bd.q; sum = CX.add(sum, CX.scale(CX.div(CX.scale(s, k), CX.add(CX.add(CX.mul(s, s), CX.scale(s, k)), [1, 0])), bd.g)); }
    return CX.db(CX.add([1 - mix, 0], CX.scale(sum, mix * norm)));
  }, 30);
  VA.header(c, b, h, `“${'AEIOU'[Math.round(pos)]}” · ${VA.hz(bands[0].hz)}`, { env });
});
NOTES.formant = 'Three resonances where a voice puts them: VOWEL sweeps A-E-I-O-U, SHIFT makes the mouth smaller or bigger, and ENV moves through the vowels as you pick (a talk-box wah).';

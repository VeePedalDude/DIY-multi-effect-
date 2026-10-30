// screens/comb.js - the comb's teeth: 1 / (1 - FEEDBACK x damping x delay), at the live FREQ.
screen('comb', (c, b, m, h) => {
  const L = VA.live(h, 0), fb = h.param(1).real(h.eff(1)) / 100, da = 0.9 * h.eff(2), mix = h.eff(4), sr = 48000, P = sr / L.hz;
  const { fx } = EQ.plot(c, b, h, (f) => {
    const w = 2 * Math.PI * f / sr, z1 = [Math.cos(w), -Math.sin(w)], zP = [Math.cos(w * P), -Math.sin(w * P)];
    const damp = CX.div([1 - da, 0], CX.add([1, 0], CX.scale(z1, -da)));
    const wet = CX.scale(CX.div([1, 0], CX.add([1, 0], CX.scale(CX.mul(damp, zP), -fb))), 1 - Math.abs(fb));
    return CX.db(CX.add([1 - mix, 0], CX.scale(wet, mix)));
  }, 24);
  VA.marker(c, b, h, fx, L.hz);
  VA.header(c, b, h, `${VA.hz(L.hz)}${fb < 0 ? ' · odd harmonics' : ''}`, L);
});
NOTES.comb = 'A short delay fed back, tuned to a pitch: it rings at FREQ and its harmonics. Negative FEEDBACK rings an octave lower with only the odd ones (hollow).';

// screens/diode.js - the diode ladder's response (s^4 + 7s^3 + 15s^2 + 10s + 1, plus feedback), at the live cutoff.
screen('diode', (c, b, m, h) => {
  const L = VA.live(h, 0), k = 1.08 * 18.3877551 * h.eff(1), lv = h.param(4).real(h.eff(4)), W = 1.19522861;
  const { fx } = EQ.plot(c, b, h, (f) => {
    const s = CX.scale(CX.jw(f, L.hz), W), s2 = CX.mul(s, s), s3 = CX.mul(s2, s), s4 = CX.mul(s3, s);
    const den = CX.add(CX.add(CX.add(s4, CX.scale(s3, 7)), CX.add(CX.scale(s2, 15), CX.scale(s, 10))), [1 + k, 0]);
    return CX.db(CX.div([Math.sqrt(1 + k), 0], den)) + lv;
  }, 30);
  VA.marker(c, b, h, fx, L.hz);
  VA.header(c, b, h, `diode · ${VA.hz(L.hz)}${h.eff(1) > 0.93 ? ' · singing' : ''}`, L);
});
NOTES.diode = 'The diode ladder of the TB-303 and EMS synths: the stages load each other, so it squelches. Add ENV and a little DRIVE for acid; from about 93% RESO it sings on its own.';

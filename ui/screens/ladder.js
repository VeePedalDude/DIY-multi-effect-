// screens/ladder.js - the Moog ladder's response, with its bass loss and COMP, at the live cutoff.
screen('ladder', (c, b, m, h) => {
  const L = VA.live(h, 0), k = 4.4 * h.eff(1), comp = 1 + k * h.param(4).real(h.eff(4)) / 100, lv = h.param(5).real(h.eff(5));
  const { fx } = EQ.plot(c, b, h, (f) => { const s = CX.jw(f, L.hz); return CX.db(CX.div([comp, 0], CX.add(CX.pow(CX.add(s, [1, 0]), 4), [k, 0]))) + lv; }, 30);
  VA.marker(c, b, h, fx, L.hz);
  VA.header(c, b, h, `24 dB/oct · ${VA.hz(L.hz)}${h.eff(1) > 0.91 ? ' · singing' : ''}`, L);
});
NOTES.ladder = 'The Moog transistor ladder: 24 dB/octave, a growling resonance that self-oscillates from about 91%. The bass thins as RESO rises, like the original; COMP puts it back.';

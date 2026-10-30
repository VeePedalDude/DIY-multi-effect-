// screens/ota.js - the OTA cascade at its POLES tap, with resonance and level make-up, at the live cutoff.
screen('ota', (c, b, m, h) => {
  const L = VA.live(h, 0), k = 4.4 * h.eff(1), poles = h.param(2).real(h.eff(2)), lv = h.param(5).real(h.eff(5));
  const tap = (s, n) => CX.div([1 + k, 0], CX.mul(CX.pow(CX.add(s, [1, 0]), n), CX.add([1, 0], CX.div([k, 0], CX.pow(CX.add(s, [1, 0]), 4)))));
  const a = Math.min(3, Math.floor(poles)), t = poles - a;
  const { fx } = EQ.plot(c, b, h, (f) => { const s = CX.jw(f, L.hz); return CX.db(CX.add(CX.scale(tap(s, a), 1 - t), CX.scale(tap(s, Math.min(4, a + 1)), t))) + lv; }, 30);
  VA.marker(c, b, h, fx, L.hz);
  VA.header(c, b, h, `${(poles * 6).toFixed(0)} dB/oct · ${VA.hz(L.hz)}${h.eff(1) > 0.91 ? ' · singing' : ''}`, L);
});
NOTES.ota = 'Four OTA stages with resonance around them all (Juno, Jupiter, Prophet-5). POLES picks 6 to 24 dB/octave; the level holds as RESO rises, and from about 91% it sings on its own.';

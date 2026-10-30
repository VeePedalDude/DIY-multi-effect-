// screens/sallen.js - the high-pass and the low-pass in series (small-signal), the low-pass at its live cutoff.
screen('sallen', (c, b, m, h) => {
  const L = VA.live(h, 2), hpHz = h.param(0).real(h.eff(0)), kh = 2.3 * h.eff(1), kl = 2.3 * h.eff(3), lv = h.param(6).real(h.eff(6));
  const sec = (s, k) => CX.add(CX.add(CX.mul(s, s), CX.scale(s, Math.max(2 - k, 0.02))), [1, 0]);
  const { fx } = EQ.plot(c, b, h, (f) => {
    const sh = CX.jw(f, hpHz), sl = CX.jw(f, L.hz);
    return CX.db(CX.mul(CX.div(CX.mul(sh, sh), sec(sh, kh)), CX.div([1, 0], sec(sl, kl)))) + lv;
  }, 30);
  VA.marker(c, b, h, fx, L.hz);
  const scream = kl >= 2 || kh >= 2;
  VA.header(c, b, h, `HP ${VA.hz(hpHz)} · LP ${VA.hz(L.hz)}${scream ? ' · screaming' : ''}`, L);
});
NOTES.sallen = 'High-pass into low-pass, each a Sallen-Key with its peak fed back through clipping diodes (Korg MS-20). Past about 87% a PEAK screams; the clipping keeps it in hand.';

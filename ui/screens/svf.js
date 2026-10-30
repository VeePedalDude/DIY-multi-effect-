// screens/svf.js - the state variable filter's response for its MODE blend, at the live cutoff.
screen('svf', (c, b, m, h) => {
  const L = VA.live(h, 0), q = 0.5 * Math.pow(50, h.eff(1)), k = 1 / q, mode = h.param(2).real(h.eff(2));
  const w = [0, 1, 2, 3].map((i) => Math.max(0, 1 - Math.abs(mode - i))), lv = h.param(5).real(h.eff(5));
  const { fx } = EQ.plot(c, b, h, (f) => {
    const s = CX.jw(f, L.hz), s2 = CX.mul(s, s), den = CX.add(CX.add(s2, CX.scale(s, k)), [1, 0]);
    const num = CX.add(CX.add(CX.scale([1, 0], w[0]), CX.scale(CX.add(s2, [1, 0]), w[1])), CX.add(CX.scale(s2, w[2]), CX.scale(s, k * w[3])));
    return CX.db(CX.div(num, den)) + lv;
  }, 24);
  VA.marker(c, b, h, fx, L.hz);
  VA.header(c, b, h, `${['LP', 'NOTCH', 'HP', 'BP'][Math.round(clamp(mode, 0, 3))]} · ${VA.hz(L.hz)}`, L);
});
NOTES.svf = 'The two-pole multimode of the Oberheim SEM: MODE sweeps low-pass, notch, high-pass, band-pass. ENV moves the cutoff with your picking (the bar shows the level it reads).';

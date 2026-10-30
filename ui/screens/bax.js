// screens/bax.js - the two shelves.
screen('bax', (c, b, m, h) => {
  const v = [0, 1, 2, 3, 4].map((i) => h.param(i).real(h.eff(i)));
  EQ.plot(c, b, h, (f) => EQ.lshelf1(f, v[2], v[0]) + EQ.hshelf1(f, v[3], v[1]) + v[4], 18);
  c.fillStyle = h.col.text; c.fillText(`bass ${v[0] >= 0 ? '+' : ''}${v[0].toFixed(1)}  treble ${v[1] >= 0 ? '+' : ''}${v[1].toFixed(1)} dB`, b.x + 20, b.y + 26);
});
NOTES.bax = 'The hi-fi bass and treble pair. B FREQ and T FREQ are where each shelf has done half its work.';

// screens/peq.js - shelves and the two sweepable mids, with a dot on each mid.
screen('peq', (c, b, m, h) => {
  const v = [0, 1, 2, 3, 4, 5, 6, 7].map((i) => h.param(i).real(h.eff(i)));
  const { fx, dy } = EQ.plot(c, b, h, (f) => EQ.lshelf(f, 100, 0.7071, v[0]) + EQ.bell(f, v[1], v[3], v[2]) + EQ.bell(f, v[4], v[6], v[5]) + EQ.hshelf(f, 5000, 0.7071, v[7]), 18);
  c.save(); c.fillStyle = h.col.alt;
  for (const [hz, g] of [[v[1], v[2]], [v[4], v[5]]]) { c.beginPath(); c.arc(fx(hz), dy(g), 6, 0, Math.PI * 2); c.fill(); }
  c.restore(); c.fillStyle = h.col.text;
  const k = (hz) => hz >= 1000 ? (hz / 1000).toFixed(1) + 'k' : hz.toFixed(0);
  c.fillText(`mids ${k(v[1])} · ${k(v[4])} Hz`, b.x + 20, b.y + 26);
});
NOTES.peq = 'Studio four-band: fixed shelves at 100 Hz and 5 kHz, and two mids you can put anywhere, as wide or narrow as you like.';

// screens/prog.js - the program EQ's curve (the same shapes as engine/modules/filter/prog.h) and the drive.
screen('prog', (c, b, m, h) => {
  const v = [0, 1, 2, 3, 4, 5, 6, 7].map((i) => h.param(i).real(h.eff(i))), q = 2.5 * Math.pow(0.2, v[5] / 10);
  EQ.plot(c, b, h, (f) => EQ.lshelf(f, v[0], 0.9, 1.5 * v[1]) + EQ.lshelf1(f, v[0] * 3, -1.0 * v[2]) + EQ.bell(f, v[3], q, 1.6 * v[4]) + EQ.hshelf(f, 10000, 0.6, -1.6 * v[6]), 18);
  c.fillStyle = h.col.text; c.fillText(v[1] > 0.5 && v[2] > 0.5 ? 'boost + cut: tight low end' : 'passive program eq', b.x + 20, b.y + 26);
  EQ.note(c, b, h, `tube ${v[7].toFixed(0)}%`);
});
NOTES.prog = 'In the style of a passive tube program EQ. Turn LOW BOOST and LOW ATTEN up together for a big low end with a dip just above it.';

// screens/geq.js - the seven sliders, with the curve they make.
const GEQ_HZ = [100, 200, 400, 800, 1600, 3200, 6400];
screen('geq', (c, b, m, h) => {
  const db = GEQ_HZ.map((_, i) => h.param(i).real(h.eff(i))), lv = h.param(7).real(h.eff(7));
  const { fx, dy } = EQ.plot(c, b, h, (f) => db.reduce((s, d, i) => s + EQ.bell(f, GEQ_HZ[i], 0.9 + 1.5 * Math.abs(d) / 15, d), lv), 18);
  c.save(); c.fillStyle = h.col.alt;
  GEQ_HZ.forEach((hz, i) => { c.beginPath(); c.arc(fx(hz), dy(db[i] + lv), 5, 0, Math.PI * 2); c.fill(); });
  c.restore(); c.fillStyle = h.col.text; c.fillText('7 bands', b.x + 20, b.y + 26);
  if (Math.abs(lv) > 0.05) EQ.note(c, b, h, `level ${lv > 0 ? '+' : ''}${lv.toFixed(1)} dB`);
});
NOTES.geq = 'The seven-band guitar graphic: 100 Hz to 6.4 kHz, an octave apart. Small moves blend; big ones stay on their band.';

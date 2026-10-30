// screens/dyneq.js - the band as it is right now (it moves as you play), and its level against the threshold.
screen('dyneq', (c, b, m, h) => {
  const f0 = h.param(0).real(h.eff(0)), q = h.param(1).real(h.eff(1)), th = h.param(2).real(h.eff(2)), range = h.param(3).real(h.eff(3));
  const g = h.meter('gain', 0), lvl = h.meter('level', -120);
  c.save(); c.globalAlpha = .35; c.strokeStyle = h.col.dim; EQ.plot(c, b, h, (f) => EQ.bell(f, f0, q, range), 18); c.restore();
  EQ.plot(c, b, h, (f) => EQ.bell(f, f0, q, g), 18);
  const x0 = b.x + 20, w = 120, y = b.y + 18, at = (db) => x0 + w * clamp((db + 60) / 60, 0, 1);
  c.fillStyle = h.col.track; c.fillRect(x0, y, w, 10);
  c.fillStyle = lvl > th ? h.col.warn : h.col.ok; c.fillRect(x0, y, at(lvl) - x0, 10);
  c.fillStyle = h.col.bright; c.fillRect(at(th) - 1.5, y - 4, 3, 18);
  EQ.note(c, b, h, `${g >= 0 ? '+' : ''}${g.toFixed(1)} dB`);
});
NOTES.dyneq = 'A bell that moves only when its band gets loud: the faint curve is the most it can do (RANGE), the bright one what it is doing now.';

// screens/tilt.js - the tilt about its pivot.
screen('tilt', (c, b, m, h) => {
  const t = h.param(0).real(h.eff(0)), p = h.param(1).real(h.eff(1)), lv = h.param(2).real(h.eff(2)), G = Math.pow(10, t / 20);
  const { fx } = EQ.plot(c, b, h, (f) => EQ.split1(f, p * G, 1 / G, G) + lv, 12);
  c.fillStyle = h.col.warn; c.fillRect(fx(p) - 1.5, b.y + 38, 3, b.h - 50);
  c.fillStyle = h.col.text; c.fillText(t > 0.05 ? 'brighter' : t < -0.05 ? 'darker' : 'flat', b.x + 20, b.y + 26);
  EQ.note(c, b, h, `pivot ${p.toFixed(0)} Hz`);
});
NOTES.tilt = 'One knob for the whole tone: the top end goes up as the bottom goes down (or the reverse), turning about PIVOT.';

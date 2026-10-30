// 47-va-plot.js - the synth filters' face screens (ui/screens/svf.js, comb.js, formant.js, ota.js,
// ladder.js, diode.js, sallen.js): complex arithmetic for their analog responses (the same models as
// engine/modules/synth/), the live cutoff and the level ENV reads. Plots go through EQ.plot (46-eq-plot.js).
const CX = {
  c: (re, im = 0) => [re, im],
  add: (a, b) => [a[0] + b[0], a[1] + b[1]],
  mul: (a, b) => [a[0] * b[0] - a[1] * b[1], a[0] * b[1] + a[1] * b[0]],
  div: (a, b) => { const d = b[0] * b[0] + b[1] * b[1] || 1e-30; return [(a[0] * b[0] + a[1] * b[1]) / d, (a[1] * b[0] - a[0] * b[1]) / d]; },
  scale: (a, k) => [a[0] * k, a[1] * k],
  pow: (a, n) => { let r = [1, 0]; for (let i = 0; i < n; i++) r = CX.mul(r, a); return r; },
  db: (a) => 20 * Math.log10(Math.hypot(a[0], a[1]) + 1e-12),
  jw: (hz, fc) => [0, hz / fc],
};
const VA = {
  // Cutoff and ENV level from the engine's meters, and a one-line read-out under the curve.
  live: (h, knob) => ({ hz: h.meter('cutoff', h.param(knob).real(h.eff(knob))), env: h.meter('env', 0) }),
  hz: (hz) => hz >= 1000 ? (hz / 1000).toFixed(hz >= 10000 ? 0 : 1) + ' kHz' : hz.toFixed(0) + ' Hz',
  header(c, b, h, left, live) {
    c.fillStyle = h.col.text; c.fillText(left, b.x + 20, b.y + 26);
    const x = b.x + b.w - 20 - 90, y = b.y + 16;
    c.fillStyle = h.col.track; c.fillRect(x, y, 90, 8); c.fillStyle = h.col.alt; c.fillRect(x, y, 90 * clamp(live.env, 0, 1), 8);
  },
  marker(c, b, h, fx, hz) { c.fillStyle = h.col.warn; c.fillRect(fx(clamp(hz, 20, 20000)) - 1.5, b.y + 38, 3, b.h - 50); },
};

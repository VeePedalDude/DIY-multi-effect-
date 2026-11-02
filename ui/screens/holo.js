// screens/holo.js - decaying repeats shimmered by HALO, plus the live delay time.
screen('holo', (c, b, m, h) => {
  const n = 12, fb = h.param(0).real(h.eff(0)) / 100, halo = h.eff(1);
  for (let k = 0; k < n; k++) {
    const amp = Math.pow(fb * .95 + .02, k), sh = halo * .25 * Math.sin(h.T * 4 + k);
    const bx = b.x + 18 + k * ((b.w - 36) / n), bh = (b.h - 44) * amp * (1 + sh);
    c.globalAlpha = .35 + .65 * amp; c.fillRect(bx, b.y + b.h - 12 - bh, 16, bh);
  }
  c.globalAlpha = 1; c.fillStyle = h.col.text; c.fillText(h.meter('delay_ms', 375).toFixed(0) + ' ms', b.x + 20, b.y + 26);
});

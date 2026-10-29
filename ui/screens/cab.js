// screens/cab.js - cabinet response: the loaded IR's measured curve, or a sketch of the built-in one.
// h.data.irResp (64 log-spaced dB values, 40 Hz..16 kHz) is set by the IR loader in 50-hud.js.
screen('cab', (c, b, m, h) => {
  const lc = h.param(0).real(h.eff(0)), resp = h.data.irResp;
  c.beginPath();
  for (let i = 0; i <= 63; i++) {
    const f = i / 63, hz = 40 * Math.pow(400, f);
    const g = resp ? clamp((resp[i] + 30) / 36, 0, 1)
      : clamp((10 * Math.log10(1 / (1 + Math.pow(hz / 5500, 4))) + 4 * Math.exp(-Math.pow(Math.log(hz / 115), 2) * 4) + 4 * Math.exp(-Math.pow(Math.log(hz / 2600), 2) * 5) + 30) / 36, 0, 1);
    const hp = hz < lc ? Math.pow(hz / lc, 2) : 1;
    const px = b.x + 16 + (b.w - 32) * f, py = b.y + b.h - 14 - g * hp * (b.h - 40);
    i ? c.lineTo(px, py) : c.moveTo(px, py);
  }
  c.stroke(); c.fillStyle = '#94A3BD'; c.fillText(resp ? 'loaded IR' : 'built-in cab', b.x + 20, b.y + 26);
});
NOTES.cab = 'Load your own cab IR from Settings.';

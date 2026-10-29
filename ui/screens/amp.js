// screens/amp.js - the shaper's transfer curve at the current drive.
screen('amp', (c, b, m, h) => {
  const k = Math.pow(10, (18 + h.param(0).real(h.eff(0))) / 20) * 0.08;
  c.beginPath();
  for (let i = 0; i <= 100; i++) {
    const u = i / 50 - 1, out = Math.tanh(k * u + .15) - Math.tanh(.15);
    const px = b.x + 20 + (b.w - 40) * i / 100, py = b.y + b.h / 2 - out * (b.h / 2 - 16) * .9;
    i ? c.lineTo(px, py) : c.moveTo(px, py);
  }
  c.stroke(); c.fillStyle = '#94A3BD'; c.fillText('NAM A2 drops in here', b.x + 20, b.y + 26);
});
NOTES.amp = 'Built-in waveshaper amp (2x oversampled, anti-aliased) until the NAM A2 engine is compiled in.';

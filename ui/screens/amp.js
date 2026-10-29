// screens/amp.js - a loaded NAM capture (what it is, how it's running), or the built-in shaper's curve.
// h.data.namMeta comes from the .nam file (53-assets.js parseNamMeta); h.meter('rate') is the
// capture's sample rate as the engine reports it (0 = built-in amp playing).
screen('amp', (c, b, m, h) => {
  const meta = h.data.namMeta, rate = h.meter('rate', 0);
  if (meta && rate > 0) {
    // a little layered "network": columns of nodes whose brightness follows the input level
    const lvl = clamp((h.inPk || 0) * 3, 0.15, 1), cols = 6, rows = 5, x0 = b.x + b.w - 190, y0 = b.y + 40;
    c.save(); c.globalAlpha = 0.35; c.lineWidth = 1.5;
    for (let i = 0; i < cols - 1; i++) for (let r = 0; r < rows; r++) for (let s = 0; s < rows; s += 2) {
      c.beginPath(); c.moveTo(x0 + i * 34, y0 + r * 20); c.lineTo(x0 + (i + 1) * 34, y0 + s * 20 + 10 * ((i + r) % 2)); c.stroke();
    }
    c.globalAlpha = 1;
    for (let i = 0; i < cols; i++) for (let r = 0; r < rows; r++) {
      const on = 0.5 + 0.5 * Math.sin(T * 3 + i * 1.3 + r * 0.7);
      c.globalAlpha = 0.25 + 0.75 * on * lvl; c.beginPath(); c.arc(x0 + i * 34, y0 + r * 20 + (i % 2 ? 10 : 0), 4.5, 0, Math.PI * 2); c.fill();
    }
    c.restore();
    const ctxRate = rig.ctx ? rig.ctx.sampleRate : rate;
    const lines = [meta.gear || meta.name || 'NAM capture', [meta.architecture, meta.toneType].filter(Boolean).join(' · '),
      `${(rate / 1000).toFixed(1)} kHz${Math.abs(rate - ctxRate) > 1 ? ` capture at ${(ctxRate / 1000).toFixed(1)} kHz` : ''}`];
    c.fillStyle = '#E6ECF5'; c.fillText(lines[0].length > 22 ? lines[0].slice(0, 21) + '…' : lines[0], b.x + 20, b.y + 34);
    c.fillStyle = '#94A3BD'; c.fillText(lines[1].slice(0, 24), b.x + 20, b.y + 62);
    c.fillStyle = Math.abs(rate - ctxRate) > 1 ? '#FFB547' : '#94A3BD'; c.fillText(lines[2], b.x + 20, b.y + 90);
    if (meta.modeledBy) { c.fillStyle = '#5E6B85'; c.fillText(('by ' + meta.modeledBy).slice(0, 24), b.x + 20, b.y + 118); }
    return;
  }
  const k = Math.pow(10, (18 + h.param(0).real(h.eff(0))) / 20) * 0.08;
  c.beginPath();
  for (let i = 0; i <= 100; i++) {
    const u = i / 50 - 1, out = Math.tanh(k * u + .15) - Math.tanh(.15);
    const px = b.x + 20 + (b.w - 40) * i / 100, py = b.y + b.h / 2 - out * (b.h / 2 - 16) * .9;
    i ? c.lineTo(px, py) : c.moveTo(px, py);
  }
  c.stroke(); c.fillStyle = '#94A3BD'; c.fillText('built-in amp · drop a .nam here', b.x + 20, b.y + 26);
});
NOTES.amp = 'Plays any .nam capture: WaveNet (A1, A2, slimmable), LSTM, ConvNet, Linear, Sequential. INPUT is the gain into the capture (0 dB = as captured); captures with loudness data are levelled like the NAM plugin. Without a capture, a built-in waveshaper plays (it adds its own +18 dB).';

// screens/gate.js - input level against the threshold, gate state.
screen('gate', (c, b, m, h) => {
  const inDb = h.inPk != null ? 20 * Math.log10(h.inPk + 1e-9) : -100;
  const lvl = clamp((inDb + 80) / 80, 0, 1), thDb = h.param(0).real(h.eff(0)), th = clamp((thDb + 80) / 80, 0, 1);
  const open = h.meter('open', 0) > .5;
  c.fillStyle = '#1C2538'; c.fillRect(b.x + 20, b.y + 54, b.w - 40, 30);
  c.fillStyle = open ? h.ACC : '#3A4766'; c.fillRect(b.x + 20, b.y + 54, (b.w - 40) * lvl, 30);
  c.fillStyle = '#FFB547'; c.fillRect(b.x + 20 + (b.w - 40) * th - 2, b.y + 36, 4, 66);
  c.fillStyle = open ? '#9BE15D' : '#94A3BD'; c.fillText(open ? 'OPEN' : 'CLOSED', b.x + 20, b.y + 128);
  c.fillStyle = '#94A3BD'; c.textAlign = 'right'; c.fillText(h.inPk != null ? inDb.toFixed(0) + ' dBFS in' : 'no signal', b.x + b.w - 20, b.y + 128); c.textAlign = 'left';
});
TILE.gate = 'teal';   // card colour (ui/assets/tile-teal.webp)

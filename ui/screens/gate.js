// screens/gate.js - input level against the threshold, gate state.
screen('gate', (c, b, m, h) => {
  const inDb = h.inPk != null ? 20 * Math.log10(h.inPk + 1e-9) : -100;
  const lvl = clamp((inDb + 80) / 80, 0, 1), thDb = h.param(0).real(h.eff(0)), th = clamp((thDb + 80) / 80, 0, 1);
  const open = h.meter('open', 0) > .5;
  c.fillStyle = h.col.track; c.fillRect(b.x + 20, b.y + 54, b.w - 40, 30);
  c.fillStyle = open ? h.ACC : h.col.off; c.fillRect(b.x + 20, b.y + 54, (b.w - 40) * lvl, 30);
  c.fillStyle = h.col.warn; c.fillRect(b.x + 20 + (b.w - 40) * th - 2, b.y + 36, 4, 66);
  c.fillStyle = open ? h.col.ok : h.col.text; c.fillText(open ? 'OPEN' : 'CLOSED', b.x + 20, b.y + 128);
  c.fillStyle = h.col.text; c.textAlign = 'right'; c.fillText(h.inPk != null ? inDb.toFixed(0) + ' dBFS in' : 'no signal', b.x + b.w - 20, b.y + 128); c.textAlign = 'left';
});

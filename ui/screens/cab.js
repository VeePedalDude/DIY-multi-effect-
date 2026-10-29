// screens/cab.js - the loaded IR's frequency response, its energy decay for long IRs (reverbs), or
// a sketch of the built-in cab. h.data.irResp / irDecay / irSecs / irCh are set by the IR loader
// (56-asset-ui.js); h.meter('taps'/'chans') is what the engine is running.
screen('cab', (c, b, m, h) => {
  const lc = h.param(0).real(h.eff(0)), resp = h.data.irResp, decay = h.data.irDecay, secs = h.data.irSecs || 0;
  const long = resp && decay && secs > 0.25;
  c.beginPath();
  for (let i = 0; i <= 63; i++) {
    const f = i / 63, hz = 40 * Math.pow(400, f);
    const g = resp ? clamp((resp[i] + 30) / 36, 0, 1)
      : clamp((10 * Math.log10(1 / (1 + Math.pow(hz / 5500, 4))) + 4 * Math.exp(-Math.pow(Math.log(hz / 115), 2) * 4) + 4 * Math.exp(-Math.pow(Math.log(hz / 2600), 2) * 5) + 30) / 36, 0, 1);
    const hp = hz < lc ? Math.pow(hz / lc, 2) : 1;
    const px = b.x + 16 + (b.w - 32) * f, py = b.y + b.h - 14 - g * hp * (b.h - 40) * (long ? 0.5 : 1);
    i ? c.lineTo(px, py) : c.moveTo(px, py);
  }
  c.stroke();
  if (long) {                                    // decay over the IR's length, 0 to -60 dB
    c.save(); c.strokeStyle = '#7C9CFF'; c.lineWidth = 2.5; c.beginPath();
    decay.forEach((v, i) => { const px = b.x + 16 + (b.w - 32) * i / (decay.length - 1), py = b.y + 40 + clamp(-v / 60, 0, 1) * (b.h / 2 - 34); i ? c.lineTo(px, py) : c.moveTo(px, py); });
    c.stroke(); c.restore();
  }
  c.fillStyle = '#94A3BD';
  const taps = h.meter('taps', 0), ch = h.meter('chans', 0) || h.data.irCh || 1;
  c.fillText(resp ? `${secs >= 1 ? secs.toFixed(2) + ' s' : (secs * 1000).toFixed(0) + ' ms'} · ${ch > 1 ? 'stereo' : 'mono'}` : 'built-in cab', b.x + 20, b.y + 26);
  if (resp && taps) { c.textAlign = 'right'; c.fillText(`${taps} taps`, b.x + b.w - 20, b.y + 26); c.textAlign = 'left'; }
});
NOTES.cab = 'Any IR length, mono or stereo: a speaker cab, a room, or a convolution reverb (up to 20 s). Drop a .wav on this module, or pick one from the library below. Long IRs work best with MIX below 100%.';
TILE.cab = 'gold';   // card colour (ui/assets/tile-gold.webp)

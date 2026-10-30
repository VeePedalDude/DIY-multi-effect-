// screens/polyv.js - a keyboard from E2 to E6 with the notes POLY VOICE is playing lit (the engine sends the
// three loudest as one packed number: (n1 + 1) + 129 (n2 + 1) + 129^2 (n3 + 1), 0 for none), its model and
// how many voices are sounding.
screen('polyv', (c, b, m, h) => {
  const packed = Math.round(h.meter('notes', 0)), lit = new Set();
  for (let k = 0, p = packed; k < 3; k++, p = Math.floor(p / 129)) { const n = (p % 129) - 1; if (n >= 0) lit.add(n); }
  const lo = 40, hi = 88, black = (n) => [1, 3, 6, 8, 10].includes(n % 12);
  const whites = []; for (let n = lo; n <= hi; n++) if (!black(n)) whites.push(n);
  const x0 = b.x + 16, w = (b.w - 32) / whites.length, y0 = b.y + 52, kh = 92, at = {};
  whites.forEach((n, i) => { at[n] = x0 + i * w; });
  for (const n of whites) {                           // white keys, then the black ones over them
    c.fillStyle = lit.has(n) ? h.ACC : h.col.track; c.fillRect(at[n] + 1, y0, w - 2, kh);
  }
  for (let n = lo; n <= hi; n++) if (black(n)) {
    const x = at[n - 1] + w * 0.65; c.fillStyle = lit.has(n) ? h.ACC : h.col.off; c.fillRect(x, y0, w * 0.7, kh * 0.6);
  }
  const names = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
  const playing = [...lit].sort((a, z) => a - z).map((n) => names[n % 12] + (Math.floor(n / 12) - 1)).join(' ');
  c.fillStyle = h.col.bright; c.fillText(h.param(0).fmt(h.eff(0)), b.x + 20, b.y + 32);
  c.fillStyle = h.col.text; c.textAlign = 'right';
  const v = Math.round(h.meter('voices', 0));
  c.fillText(v ? `${v} voice${v === 1 ? '' : 's'} · ${playing}` : 'play a chord', b.x + b.w - 20, b.y + 32); c.textAlign = 'left';
});
NOTES.polyv = 'Finds up to six notes in what you play (a spectrum every 5 ms) and plays them on a tape keyboard or a polysynth. Octaves of a note you play merge into it. The voices are original: no samples.';

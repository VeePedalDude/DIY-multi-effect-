// 46-eq-plot.js - frequency-response plots for the equaliser face screens (ui/screens/tilt.js, bax.js,
// geq.js, peq.js, stack.js, prog.js, dyneq.js). EQ.* gives the analog responses the engine's filters
// are built from (engine/core/eq.h), in dB; EQ.plot draws one on a log frequency axis.
const EQ = {
  mag: (re, im) => Math.hypot(re, im),
  // (n0 + n1 s + n2 s^2) / (d0 + d1 s + d2 s^2) at s = j w, in dB
  quad(w, n0, n1, n2, d0, d1, d2) { return 20 * Math.log10(EQ.mag(n0 - n2 * w * w, n1 * w) / EQ.mag(d0 - d2 * w * w, d1 * w)); },
  bell(f, f0, q, db) { const A = Math.pow(10, db / 40), w = f / f0; return EQ.quad(w, 1, A / q, 1, 1, 1 / (A * q), 1); },
  lshelf(f, f0, q, db) { const A = Math.pow(10, db / 40), w = f / f0, r = Math.sqrt(A) / q; return 20 * Math.log10(A) + EQ.quad(w, A, r, 1, 1, r, A); },
  hshelf(f, f0, q, db) { const A = Math.pow(10, db / 40), w = f / f0, r = Math.sqrt(A) / q; return 20 * Math.log10(A) + EQ.quad(w, 1, r, A, A, r, 1); },
  // first-order split at fc: lo * lowpass + hi * highpass (Shelf1)
  split1(f, fc, lo, hi) { const w = f / fc; return 20 * Math.log10(EQ.mag(lo, hi * w) / EQ.mag(1, w)); },
  lshelf1(f, f0, db) { const A = Math.pow(10, db / 20); return EQ.split1(f, f0 / Math.sqrt(A), A, 1); },
  hshelf1(f, f0, db) { const A = Math.pow(10, db / 20); return EQ.split1(f, f0 * Math.sqrt(A), 1, A); },
  // The curve dbAt(hz) from 20 Hz to 20 kHz, +-range dB, with a grid at 100 Hz, 1 kHz, 10 kHz and 0 dB.
  plot(c, b, h, dbAt, range = 18) {
    const x0 = b.x + 16, x1 = b.x + b.w - 16, y0 = b.y + 38, y1 = b.y + b.h - 12, ym = (y0 + y1) / 2;
    const fx = (hz) => x0 + (x1 - x0) * Math.log(hz / 20) / Math.log(1000), dy = (db) => ym - clamp(db / range, -1, 1) * (y1 - y0) / 2;
    c.save(); c.strokeStyle = h.col.grid; c.lineWidth = 1.5; c.beginPath();
    for (const hz of [100, 1000, 10000]) { c.moveTo(fx(hz), y0); c.lineTo(fx(hz), y1); }
    for (const db of [-12, 12]) { c.moveTo(x0, dy(db)); c.lineTo(x1, dy(db)); }
    c.stroke(); c.strokeStyle = h.col.dim; c.beginPath(); c.moveTo(x0, ym); c.lineTo(x1, ym); c.stroke(); c.restore();
    c.beginPath();
    for (let i = 0; i <= 96; i++) { const hz = 20 * Math.pow(1000, i / 96), px = x0 + (x1 - x0) * i / 96, py = dy(dbAt(hz)); i ? c.lineTo(px, py) : c.moveTo(px, py); }
    c.stroke();
    return { fx, dy };
  },
  // A right-aligned note in the screen's top line.
  note(c, b, h, text) { c.fillStyle = h.col.text; c.textAlign = 'right'; c.fillText(text, b.x + b.w - 20, b.y + 26); c.textAlign = 'left'; },
};

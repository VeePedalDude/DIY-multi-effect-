// screens/tess.js - a rotating tesseract; SIZE scales it, 4D FOLD speeds the 4-D rotation.
screen('tess', (c, b, m, h) => {
  const e0 = h.eff(0), e1 = h.eff(1), T = h.T;
  const V = []; for (let i = 0; i < 16; i++) V.push([(i & 1) ? 1 : -1, (i & 2) ? 1 : -1, (i & 4) ? 1 : -1, (i & 8) ? 1 : -1]);
  const a = T * (.3 + e1 * .6), bb = T * .4 + e1 * Math.PI, sc = 110 + 50 * e0;
  const P = V.map(([X, Y, Z, W]) => {
    const x1 = X * Math.cos(a) - W * Math.sin(a), w1 = X * Math.sin(a) + W * Math.cos(a);
    const y1 = Y * Math.cos(bb) - Z * Math.sin(bb), z1 = Y * Math.sin(bb) + Z * Math.cos(bb);
    const d4 = 1 / (2.6 - w1), X3 = x1 * d4, Y3 = y1 * d4, Z3 = z1 * d4, r = T * .5;
    const X4 = X3 * Math.cos(r) - Z3 * Math.sin(r), Z4 = X3 * Math.sin(r) + Z3 * Math.cos(r), d3 = 1 / (2.4 - Z4);
    return [b.x + b.w / 2 + X4 * d3 * sc, b.y + b.h / 2 + Y3 * d3 * sc];
  });
  c.lineWidth = 2; c.beginPath();
  for (let i = 0; i < 16; i++) for (let j = 0; j < 4; j++) { const k = i ^ (1 << j); if (k > i) { c.moveTo(P[i][0], P[i][1]); c.lineTo(P[k][0], P[k][1]); } }
  c.stroke();
});

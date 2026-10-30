// screens/stack.js - the amp tone stack's response (the same circuit formula as engine/modules/filter/stack.h).
const STACK_AMPS = [
  { name: 'BASSMAN', p: [250e3, 1e6, 25e3, 56e3, 250e-12, 20e-9, 20e-9] },
  { name: 'JCM800', p: [220e3, 1e6, 22e3, 33e3, 470e-12, 22e-9, 22e-9] },
  { name: 'TWIN', p: [250e3, 250e3, 10e3, 100e3, 250e-12, 100e-9, 47e-9] },
];
const stackTaper = (k) => (Math.pow(10, 2 * k) - 1) / 99;
function stackParts(pos) {
  pos = clamp(pos, 0, 2); const i = pos >= 2 ? 1 : Math.floor(pos), f = pos - i;
  return STACK_AMPS[i].p.map((x, j) => x * Math.pow(STACK_AMPS[i + 1].p[j] / x, f));
}
function stackDb([R1, R2, R3, R4, C1, C2, C3], l, m, t, hz) {
  const C = C1 * C2 * C3, w = 2 * Math.PI * hz;
  const b1 = t * C1 * R1 + m * C3 * R3 + l * (C1 * R2 + C2 * R2) + (C1 * R3 + C2 * R3);
  const b2 = t * (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4) - m * m * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3) + m * (C1 * C3 * R1 * R3 + C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
    + l * (C1 * C2 * R1 * R2 + C1 * C2 * R2 * R4 + C1 * C3 * R2 * R4) + l * m * (C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3) + (C1 * C2 * R1 * R3 + C1 * C2 * R3 * R4 + C1 * C3 * R3 * R4);
  const b3 = l * m * C * (R1 * R2 * R3 + R2 * R3 * R4) - m * m * C * (R1 * R3 * R3 + R3 * R3 * R4) + m * C * (R1 * R3 * R3 + R3 * R3 * R4) + t * C * R1 * R3 * R4 - t * m * C * R1 * R3 * R4 + t * l * C * R1 * R2 * R4;
  const a1 = (C1 * R1 + C1 * R3 + C2 * R3 + C2 * R4 + C3 * R4) + m * C3 * R3 + l * (C1 * R2 + C2 * R2);
  const a2 = m * (C1 * C3 * R1 * R3 - C2 * C3 * R3 * R4 + C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3) + l * m * (C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3) - m * m * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
    + l * (C1 * C2 * R2 * R4 + C1 * C2 * R1 * R2 + C1 * C3 * R2 * R4 + C2 * C3 * R2 * R4) + (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4 + C1 * C2 * R3 * R4 + C1 * C2 * R1 * R3 + C1 * C3 * R3 * R4 + C2 * C3 * R3 * R4);
  const a3 = l * m * C * (R1 * R2 * R3 + R2 * R3 * R4) - m * m * C * (R1 * R3 * R3 + R3 * R3 * R4) + m * C * (R3 * R3 * R4 + R1 * R3 * R3 - R1 * R3 * R4) + l * C * R1 * R2 * R4 + C * R1 * R3 * R4;
  return 20 * Math.log10(EQ.mag(-b2 * w * w, b1 * w - b3 * w * w * w) / EQ.mag(1 - a2 * w * w, a1 * w - a3 * w * w * w));
}
const STACK_MAKEUP = STACK_AMPS.map((a) => { let s = 0; for (let i = 0; i < 32; i++) s += stackDb(a.p, stackTaper(0.5), 0.5, 0.5, 80 * Math.pow(5000 / 80, i / 31)); return -s / 32; });
screen('stack', (c, b, m, h) => {
  const pos = h.param(3).real(h.eff(3)), lv = h.param(4).real(h.eff(4)), parts = stackParts(pos);
  const i = pos >= 2 ? 1 : Math.floor(pos), fr = pos - i, mk = STACK_MAKEUP[i] + fr * (STACK_MAKEUP[i + 1] - STACK_MAKEUP[i]);
  EQ.plot(c, b, h, (f) => stackDb(parts, stackTaper(h.eff(0)), h.eff(1), h.eff(2), f) + mk + lv, 24);
  const near = Math.round(pos), blend = Math.abs(pos - near) > 0.05;
  c.fillStyle = h.col.text; c.fillText(blend ? `${STACK_AMPS[i].name} → ${STACK_AMPS[i + 1].name} ${(fr * 100).toFixed(0)}%` : STACK_AMPS[near].name, b.x + 20, b.y + 26);
});
NOTES.stack = "The passive tone stack from inside the amp, from the circuit: the knobs interact as they do on the amp. MODEL 0 = '59 Bassman, 1 = JCM800, 2 = Twin Reverb; in between blends their parts.";

// screens/hyped.js - HYPED FUZZ: the circuit's three stages with the path MODE takes lit, the output
// level (meter 'drive'), and which solver ENGINE runs. The stages are the model's own partition:
// front (input buffer + boost preamp), fuzz (FULL gain stage, SCOOP gain stage, octave diode), output (EQ).
screen('hyped', (c, b, m, h) => {
  const mode = Math.round(clamp(h.param(4).real(h.eff(4)), 0, 2)), eng = Math.round(clamp(h.param(7).real(h.eff(7)), 0, 2));
  const drive = clamp(h.meter('drive', 0) * 2.5, 0, 1);
  const names = ['FULL', 'SCOOP', 'BOOST'], engines = ['2x REF', '2x FAST', '1x FAST'];
  const top = b.y + 62, bot = b.y + b.h - 26, y = (top + bot) / 2, x0 = b.x + 22, x1 = b.x + b.w - 22, w = (x1 - x0) / 4;   // below the two text lines, above the meter
  const box = (x, yy, label, on) => {
    c.strokeStyle = on ? h.col.bright : h.col.dim; c.lineWidth = on ? 2 : 1;
    c.strokeRect(x - w * 0.36, yy - 13, w * 0.72, 26);
    c.fillStyle = on ? h.col.bright : h.col.dim; c.fillText(label, x - w * 0.3, yy + 5);
  };
  const line = (xa, ya, xb, yb, on) => { c.strokeStyle = on ? h.col.warn : h.col.dim; c.lineWidth = on ? 1 + 3 * drive : 1; c.beginPath(); c.moveTo(xa, ya); c.lineTo(xb, yb); c.stroke(); };
  const xs = [x0 + w * 0.5, x0 + w * 1.5, x0 + w * 2.5, x0 + w * 3.5], dy = Math.max(14, Math.min(34, (bot - top) / 2 - 13));
  // boost -> (full | scoop | straight through for BOOST) -> EQ
  line(xs[0] + w * 0.36, y, xs[1] - w * 0.36, y - dy, mode === 0);
  line(xs[0] + w * 0.36, y, xs[1] - w * 0.36, y + dy, mode === 1);
  line(xs[0] + w * 0.36, y, xs[3] - w * 0.36, y, mode === 2);
  line(xs[1] + w * 0.36, y - dy, xs[3] - w * 0.36, y, mode === 0);
  line(xs[1] + w * 0.36, y + dy, xs[3] - w * 0.36, y, mode === 1);
  box(xs[0], y, 'BOOST', true); box(xs[1], y - dy, 'FULL', mode === 0); box(xs[1], y + dy, 'SCOOP', mode === 1); box(xs[3], y, 'EQ', true);
  c.fillStyle = h.col.bright; c.fillText(`HYPED FUZZ · ${names[mode]}`, b.x + 20, b.y + 26);
  c.fillStyle = eng === 2 ? h.col.warn : h.col.text; c.fillText(`${engines[eng]} · circuit model`, b.x + 20, b.y + 48);
  c.fillStyle = h.col.dim; c.fillRect(b.x + 20, b.y + b.h - 18, b.w - 40, 5);
  c.fillStyle = h.col.warn; c.fillRect(b.x + 20, b.y + b.h - 18, (b.w - 40) * drive, 5);
});
NOTES.hyped = 'A circuit model of PedalPCB\'s Hyped Fuzz (Boss Hyper Fuzz FZ-2): every part of the schematic solved sample by sample. Estimated transistor, diode and op-amp values, not measured from a pedal. MODE is the three-way switch; OCTAVE moves R14 (the octave-up character), BITE shrinks C11 (FULL\'s low end). ENGINE trades accuracy for CPU: 2x REF is the reference solver, 2x FAST its quicker solver (within 0.0003), 1x FAST skips oversampling (half the CPU, some aliasing). It is the heaviest effect in the rig: watch the CPU readout.';

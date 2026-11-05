# Chorus Lab judge (locked)

v1.1: the gate now covers the seven v1.1 controls (extremes, modulation, buffer size with every stage on, state v2 with v1 loading), checks each feature with a measurement, and checks that switching each one on and off doesn't click. `ref/` is unchanged. With the v1.1 controls off, the output must still null against the v1.0 renders.

`./eval.sh` builds the effect and prints `CHECK` lines, `FIDELITY`/`REPORT` lines, then `GATE=PASS|FAIL` and `OBJECTIVE=<n>`.

- OBJECTIVE: mean over the 8 voicings of the median (11 runs × 20 s of audio) CPU % of one core, 48 kHz, 64-frame blocks, feedback 30 %, other params default. Lower is better. Pinned to core 1 when available.
- Noise, measured at lock on Intel Xeon @ 2.80 GHz (2-core cloud VM), 6 runs of the baseline: 0.8288–0.8485 (2.4 % spread), median 0.838. A change counts as an improvement only if it beats the current best by more than 3 %.
- Fidelity gate: every render must null against the locked baseline renders in `ref/` to −60 dB residual or better.
- `ref/` was rendered once from the baseline source (sha256 in `ref/manifest.json`). Never regenerate it during the loop.

This folder is read-only after the baseline commit. If the judge looks wrong, stop and report it; don't edit it to make a change pass.

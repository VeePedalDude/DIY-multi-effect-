# Chorus Lab v1.0.0 — test report

Machine: cloud Linux VM, Intel Xeon @ 2.80 GHz (2 cores), gcc 13.3, 48 kHz unless noted. Not measured on the iMacs.

## Verified (measured)

| Check | Result |
|---|---|
| clap-validator (official, built from source), Linux build | 33 passed, 0 failed, 0 warnings, 11 skipped (note ports, 64-bit audio, preset factory: not applicable), 8 runs |
| ASan + UBSan (plugin + host): full signal suite, state test, threads test | 0 errors, 0 leaks |
| TSan: main thread hammering get_value / value_to_text / state save+load / tail while audio processes with events | 0 races, 3 runs |
| Build warnings, `-std=c11 -Wall -Wextra -Wpedantic` | gcc Linux 0, clang 0, mingw Windows 0 |
| Buffer-size invariance | Bit-identical over 11 schedules (1, 32, 33, 64, 127, 128, 256, 509, 512, random 1–512 ×2) with 332 value/mod events |
| Sample-accurate events | Value and mod events at sample 1000 take effect at sample 1000 |
| Modulation | 109,720 PARAM_MOD events (every 7 samples, ±200 offsets) leave every knob value unchanged |
| Smoothing | Level step reaches 63 % in 4.98 ms |
| State | 88 bytes, deterministic; loads at 1–8 bytes per read; rescan(VALUES) called; 20,798 garbage blobs (truncations, every bit flip, random data, NaN/inf/out-of-range/fractional enum with valid checksum, wrong version/count) all rejected, values untouched |
| Silence | Exact zeros out, every voicing, feedback 90 % |
| Subnormal input | No subnormal output samples |
| Level change on guitar phrase (defaults) | −0.44 to +0.39 dB, Random +1.45 dB |
| Vibrato depth (2 Hz, 1 kHz sine) | ±32.6 cents at 50 % (theory ±32.3), ±65.3 at 100 % (theory ±64.1), same at 44.1 kHz |
| Detune pitch (depth 50 %) | +15.01 / −15.00 cents (target ±15), 44.1 and 48 kHz |
| Detune splice dips (wet only, 20 ms RMS) | 1 kHz sine −0.0 dB, 220 Hz sine −0.2 dB, 4-note chord −2.1 dB worst (first version: −41 dB on 1 kHz sine) |
| Wet −3 dB point, Tone 0/70/100 % | Clean voicings 1.5 k / 8.5 k / 18.0 kHz; Classic 0.72 k / 4.1 k / 9.1 kHz; Lo-Fi 1.1 k / 4.3 k / 4.3 kHz (clock-limited) |
| THD+N, 1 kHz −6 dBFS, wet only, depth 0 | Clean voicings < 0.0001 %; Classic 0.99 % (was 3.6 %, drive reduced); Lo-Fi 3.0 % (intended) |
| Latency | Dry path 0 samples; no latency extension needed |
| Tail | Feedback 90 %, Time 200 % (Tri-Chorus): measured −60 dB at 0.21 s, reported 1.92 s (safe upper bound) |
| Voicing switches | 12 switches mid-chord: worst +2.5 dB above surrounding audio in the >6 kHz band (Lo-Fi aliasing fading in). No clicks |
| Hard cases | Full-scale square and +0.5 DC with extreme params: always finite; DC does not build up in the feedback loop |
| CPU, one core, 128-frame blocks | Lo-Fi 0.52 %, Vibrato 0.60 %, Dimension 0.64 %, Classic 0.69 %, Detune 0.80 %, Tri 1.01 %, Random 1.21 %, Ensemble 1.39 % |

## Inferred (not measured)

- Windows build: compiles clean, exports `clap_entry`, depends only on KERNEL32 and msvcrt. Not run (no Windows host or wine here).
- During a 30 ms voicing crossfade both engines run, so CPU roughly doubles for that moment (~2.8 % worst case on this machine).
- On the 2015 iMac (1.6 GHz dual-core i5), expect roughly 2–3× these CPU figures: about 3–4 % of a core worst case.
- A detune splice adds a bounded correlation search (≤ 300 candidates × 96 samples, ~57k operations) inside one sample, about every 1.4–3 s per voice. Estimated at tens of µs, well inside a 128-frame block. Not timed per block.

## Known limitations

- Peak output can exceed 0 dBFS with full-scale input, high feedback and +12 dB Level (11.4 = +21 dBFS worst case). Output is float; the host or next stage must handle it.
- Random voicing runs about 1.5 dB louder than the others at default settings.
- Detune on dense chords still dips up to ~2 dB during a splice (correlation can't align every partial at once).
- Rate does nothing in Detune.
- Built and tested against the v0.3 spec rules recovered from the Nov 4 session, not the spec file or prototype test host, which weren't available here. Reconcile against `docs/effects-plugin-spec.md` and `prototype/` when merging.

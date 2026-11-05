# Chorus Lab — test report

## v1.1.0 (character stages)

Same machine (Intel Xeon @ 2.80 GHz, 2-core cloud VM). Full judge: `eval/eval.sh` → GATE=PASS, 23 of 23 checks.

| Check | Result |
|---|---|
| Builds | gcc and mingw, `-Wall -Wextra -Wpedantic`: 0 warnings |
| clap-validator | 33 passed, 0 failed, 0 warnings (the known `process-audio-denormals` timing flake still appears on some runs) |
| ASan+UBSan, TSan | no errors, leaks or races |
| Buffer-size invariance | bit-identical at 32, 37, 64, 128, 256, 333, 512 frames, with the v1.1 stages off and with all of them on, 532 events |
| State v2 | 144 bytes; 17-byte reads; 21,305 garbage blobs rejected; Chorus Lab 1.0 (v1) presets load with the new controls off |
| v1.0 compatibility | new controls off: output bit-identical to all 18 v1.0 reference renders |
| Drive | guitar level +2.4 dB at 50 %, +0.6 dB at 100 %; THD+N (1 kHz, −12 dBFS) 0 % → 36 %; 2nd harmonic −26.6 dB |
| Compander | steady tones at −10/−30/−50 dBFS: wet level change 0.00 dB (complementary); guitar envelope deviation p5/p95 −2.2/−0.1 dB (breathing) |
| Emphasis | −50 dBFS response vs off at 200 Hz–10 kHz: 0.00 dB; −20 dBFS THD+N 3 kHz 8.7 % vs 200 Hz 0.41 % |
| Low Cut 200 Hz | wet −3 dB at 200 Hz; 100 Hz −12.3 dB; 50 Hz −24.1 dB |
| Mono Bass 150 Hz | side (L−R) at 60 Hz −32.1 dB, at 1 kHz 0.00 dB; mono input passes bit-identical |
| Lush | wet impulse 90 % energy spread 0 → 66.5 ms; L/R correlation +1.00 → +0.04 |
| Shift | feedback 70 %, 220 Hz in: +12 st partial −6 dB, −12 st −3 dB, +7 st −3 dB (re fundamental; absent when off) |
| Switching controls on/off mid-chord | worst >6 kHz excess +3.8 dB (Emphasis adding its own grit); no clicks |
| CPU, new stages off | 0.584 % vs v1.0 0.579 % (4 interleaved rounds); inside the noise |
| CPU, all new stages on | 1.31 % mean, 1.50 % worst (Tri-Chorus) |

Fixed during v1.1 testing:
- Shift: the first shifter cancelled itself (sidebands at 420/460 Hz instead of 440 Hz). It now uses correlation-matched splices, like Detune.
- Mono Bass: the first version only cut side lows by 1.6 dB. It now high-passes the side (LR4): −32 dB at 60 Hz.
- Drive and Emphasis switched on with a jump (a full-strength clip at the first notch of the knob). Both now fade in from zero.
- Lush made an onset burst when switched on (+10 dB above 6 kHz); its input now fades in over 5 ms.
- With the new stages off, CPU was 9 % above v1.0; a fast path brought it back level.

Known limitations (v1.1):
- Shift on dense chords still has some splice texture, inherent to a time-domain shifter.
- The compander has no noise source, so it can't pump a noise floor the way a real BBD does. Silence-in must give silence-out.
- Windows binary compiles clean but hasn't been run in a host.

## v1.0.0

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

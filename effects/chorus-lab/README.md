# Chorus Lab

Stereo chorus for RIG/01 with eight selectable voicings and a set of analog-style character stages, built as a standalone CLAP plugin (`com.veepedaldude.rig01.chorus-lab`).

It isn't part of the RIG/01 core. Drop the `chorus-lab` folder into the app's `effects/` folder or the user effects folder and the host picks it up at startup. The `.clap` files also work without the sidecar, in any CLAP host.

## Signal path

```
in ─ Drive ─┬──────────────────────────────────────────────── dry ─┐
            └─ Pre-emphasis ─ Compressor ─ BBD clip ─► delay line   ├─ Mix ─ Level ─ Mono Bass ─ out
                     ▲                                    │ voicing │
                     │                                    ▼         │
              Feedback (+ Shift)   (voicing + Tone) ─ Expander ─ De-emphasis ─ Low Cut ─ Lush ─ wet ─┘
```

Every v1.1 stage is fully bypassed at its default. Presets from Chorus Lab 1.0 load unchanged and sound identical (the judge's null test against the 1.0 renders is bit-identical).

## Controls

Hosts list the controls in this order, grouped by panel. Ids are fixed forever, so the numbers don't follow the display order.

**Main**

| Id | Control | Range | Default | What it does |
|---|---|---|---|---|
| 0 | Voicing | 8 voicings | Classic | Picks the chorus engine (below). Changes crossfade over 30 ms. |
| 1 | Rate | 0.05–10 Hz | 0.8 Hz | LFO speed |
| 2 | Depth | 0–100 % | 50 % | Sweep depth (Detune: 0–30 cents) |
| 3 | Mix | 0–100 % | 50 % | Equal-power dry/wet |
| 4 | Tone | 0–100 % | 70 % | Wet low-pass, 1.5–18 kHz |
| 8 | Level | −24 to +12 dB | 0 dB | Output level |

**Character**

| Id | Control | Range | Default | What it does |
|---|---|---|---|---|
| 9 | Drive | 0–100 % | 0 (off) | Asymmetric preamp on the input (dry and wet); adds even and odd harmonics, level roughly held |
| 10 | Compander | 0–100 % | 0 (off) | NE570-style 2:1 compression into the delay, 1:2 expansion out. Complementary on steady notes; breathes on attacks and decays |
| 11 | Emphasis | 0–100 % | 0 (off) | Boosts highs into a BBD-style clip, then cuts them back. Flat for quiet playing; grit on the highs as you dig in |

**Space**

| Id | Control | Range | Default | What it does |
|---|---|---|---|---|
| 14 | Lush | 0–100 % | 0 (off) | Allpass diffusion on the wet; smears voices into a dense, wide ensemble |
| 6 | Width | 0–100 % | 100 % | Stereo spread of the wet voices |
| 7 | Time | 50–200 % | 100 % | Scales each voicing's delay and sweep |

**Feedback**

| Id | Control | Range | Default | What it does |
|---|---|---|---|---|
| 5 | Feedback | −90 to +90 % | 0 % | Regeneration through the voices |
| 15 | Shift | −12 to +12 st | 0 (off) | Pitch shift inside the feedback loop: octaves or fifths cascade upward or downward (shimmer). Needs Feedback above 0. |

**Low End**

| Id | Control | Range | Default | What it does |
|---|---|---|---|---|
| 12 | Low Cut | Off, 20–800 Hz | Off | High-pass on the wet only, so the chorus doesn't muddy the bass |
| 13 | Mono Bass | Off, 20–400 Hz | Off | Stereo content below the crossover is summed to mono (mid untouched) |

## Voicings

| # | Voicing | Character |
|---|---|---|
| 0 | Classic | Single BBD-style voice, triangle LFO, mild saturation, darker filter; Boss-style inverted-wet stereo |
| 1 | Dimension | Two voices on one inverted sine LFO, short and shallow; wide with little wobble |
| 2 | Ensemble | Three voices 120° apart plus a 6.3 Hz shimmer, string-machine style |
| 3 | Tri-Chorus | Three triangle voices panned left, centre and right |
| 4 | Vibrato | Single voice; use Mix 100 % for pure vibrato |
| 5 | Detune | One voice up and one down by 0–30 cents, no sweep; Rate is unused |
| 6 | Lo-Fi | Clocked sample-and-hold with 8-bit quantising; the aliasing is intended |
| 7 | Random | Three voices on smoothed random LFOs |

## Starting points

These are settings to try, not measured sound claims.

- **Vintage BBD**: Classic, Compander 60 %, Emphasis 50 %, Drive 20 %, Tone 60 %.
- **Wide ensemble**: Ensemble, Lush 60 %, Low Cut 120 Hz, Mono Bass 150 Hz.
- **Shimmer**: Dimension, Feedback 60 %, Shift +12, Lush 70 %, Mix 40 %.
- **Tight rhythm chorus**: Tri-Chorus, Depth 35 %, Low Cut 200 Hz, Mono Bass 120 Hz.

## CPU

Measured on an Intel Xeon @ 2.80 GHz cloud VM, 48 kHz, 64-frame blocks:

- With the v1.1 stages off: 0.58 % of one core (mean of the eight voicings), the same as v1.0. A dedicated fast path skips the new stages entirely when they're off and settled.
- With every v1.1 stage on: 1.31 % mean, 1.50 % worst (Tri-Chorus).

Expect roughly 2–3× these figures on the 2015 iMac (not measured).

## Building

```
./build.sh          # Linux .clap, plus the Windows .clap if x86_64-w64-mingw32-gcc is installed
./build.sh test     # also runs eval/eval.sh: every gate check, the feature checks, the null test and CPU
```

The script fetches the CLAP SDK headers into `./clap` if they're missing. Don't commit that folder.

## Credits and licence

- Chorus Lab © 2026 KC, GPL-3.0-or-later.
- CLAP SDK headers (free-audio/clap, https://github.com/free-audio/clap): MIT licence, used at build time, not included.
- No code is adapted from earlier Daisy Seed pedals. The Lo-Fi voicing shares the idea of a clocked, artifact-heavy chorus with the LOFI-Style Chorus pedal, but was written from scratch.
- No third-party DSP code. The Hermite interpolation, TPT state-variable and one-pole filters, Lambert tanh approximant, Schroeder allpasses and WSOLA-style splice search are all written from their standard published forms.

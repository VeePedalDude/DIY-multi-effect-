# Chorus Lab

Stereo chorus for RIG/01 with eight selectable voicings, built as a CLAP plugin (`com.veepedaldude.rig01.chorus-lab`).

All voicings share one stereo delay line. Switching voicing crossfades the old and new engine over 30 ms (equal power, smoothstep-shaped), so you can change it while playing. Every other parameter is smoothed over 5 ms and can be modulated per sample from the RIG/01 matrix.

## Voicings

| # | Voicing | What it is |
|---|---|---|
| 0 | Classic | Single BBD-style voice, triangle LFO, mild saturation (~1 % THD at −6 dBFS) and a darker 4-pole wet filter. At full Width the right channel gets the wet signal inverted, like Boss stereo choruses. |
| 1 | Dimension | Two voices on one sine LFO, inverted against each other, short and shallow sweep. Wide and subtle, with almost no audible wobble. |
| 2 | Ensemble | Three voices 120° apart on a slow sine, plus a fixed 6.3 Hz shimmer, string-machine style. |
| 3 | Tri-Chorus | Three triangle-LFO voices 120° apart, panned left/centre/right. |
| 4 | Vibrato | Single sine-LFO voice. Set Mix to 100 % for pure vibrato. Width offsets the right channel's LFO by up to 90°. |
| 5 | Detune | Static micro-pitch: one voice up, one voice down by 0–30 cents (Depth), no LFO sweep. Splices are correlation-matched so they don't cancel. Rate is not used. |
| 6 | Lo-Fi | Classic voice run through a clocked sample-and-hold (256-stage "BBD" clock that tracks the delay time) and 8-bit quantising. The aliasing is intended. |
| 7 | Random | Three voices driven by smoothed random LFOs (new target each Rate cycle). |

## Parameters

| Id | Name | Range | Default | Notes |
|---|---|---|---|---|
| 0 | Voicing | 0–7 (enum) | 0 Classic | Automatable, not modulatable (discrete; switches are crossfaded) |
| 1 | Rate | 0.05–10 Hz | 0.8 Hz | LFO rate; segment rate in Random |
| 2 | Depth | 0–100 % | 50 % | Sweep depth; detune amount in Detune (0–30 cents) |
| 3 | Mix | 0–100 % | 50 % | Equal-power dry/wet |
| 4 | Tone | 0–100 % | 70 % | Wet low-pass, 1.5–18 kHz (Classic ×0.6; Lo-Fi also capped by its clock) |
| 5 | Feedback | −90 to +90 % | 0 % | DC-blocked, soft-limited |
| 6 | Width | 0–100 % | 100 % | Stereo spread of the wet voices |
| 7 | Time | 50–200 % | 100 % | Scales each voicing's base delay and sweep |
| 8 | Level | −24 to +12 dB | 0 dB | Output level |

Param ids are fixed. Voicing base delays at Time 100 %: Classic 7 ms, Dimension 6 ms, Ensemble 8 ms, Tri 10 ms, Vibrato 5 ms, Detune 4 ms + 25 ms window, Lo-Fi 9 ms, Random 9 ms.

## CPU

Mean 0.59 % of one core across the eight voicings (worst: Ensemble, 0.76 %), measured at 48 kHz with 64-frame blocks and feedback at 30 %, on an Intel Xeon @ 2.80 GHz cloud VM. That's down from 0.84 % mean (1.36 % worst) before the optimisation loop on `loop/chorus-lab`, with output bit-identical to the baseline on every judge render. Expect roughly 2–3× these figures on the 2015 iMac (not measured).

## Building

```
./build.sh          # Linux .clap, plus the Windows .clap if x86_64-w64-mingw32-gcc is installed
./build.sh test     # also runs eval/eval.sh: every gate check, the fidelity null test and the CPU objective
```

`eval/` is the locked judge: test signals, judge host, gate checks, and reference renders of the baseline (`eval/ref/`) used for a −60 dB null test so CPU work can't quietly change the sound.

The script fetches the CLAP SDK headers into `./clap` if they're missing. Don't commit that folder.

## Credits and licence

- Chorus Lab: © 2026 KC, GPL-3.0-or-later.
- CLAP SDK headers (free-audio/clap, https://github.com/free-audio/clap): MIT licence, used unmodified at build time and not included in this folder.
- No code is adapted from earlier Daisy Seed pedals. The Lo-Fi voicing shares the idea of a clocked, artifact-heavy chorus with the LOFI-Style Chorus pedal, but was written from scratch.
- No third-party DSP code. Hermite interpolation, the Cytomic-style TPT state-variable filter and the WSOLA-style splice search are written from the standard published forms.

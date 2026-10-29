# RIG/01 Test Bench

A real-time test rig for the iMac multi-effects project that runs in Chrome on any PC.
Your guitar goes through the Scarlett 2i2 into a C++ DSP engine compiled to WebAssembly,
and the 3D stage shows what the engine is actually doing.

## Run it on your PC

1. Plug the guitar into **Input 1** of the 2i2, press **INST**, and turn **Direct Monitor off**.
2. Download `dist/rig-test.html` (open it on GitHub and click **Download raw file**), then open it
   in **Chrome or Edge**. Not in an app preview, which blocks the microphone and gamepads.
   The first load needs internet for three.js and the fonts.
3. Choose **Guitar through my audio interface**, press **Start audio**, and allow the microphone.
4. Open the menu (top left) and set **Input device** to the 2i2 (on Windows it usually reads
   *Analogue 1 + 2 (Focusrite USB Audio)*) and **Output device** to the 2i2 as well.
5. Raise **MASTER** slowly from -12 dB, wearing headphones or using the 2i2's outputs.

No guitar handy? Pick **Built-in test riff**, or load a DI recording from the menu.

If Chrome won't ask for microphone access on a file opened from disk, serve the folder
instead: `python -m http.server` inside `dist/`, then open `http://localhost:8000/rig-test.html`.

### What to expect for latency

The top bar shows the round trip the browser reports. Chrome on Windows uses shared-mode WASAPI,
so expect roughly 20–40 ms: fine for developing sounds, UI and modulation, but it will feel
behind your picking. The JACK rig on the iMac is where the 5–10 ms target lives. The DSP
code is the same in both places.

## What's in it

| Module | Params (0..1 in, real units out) | Notes |
|---|---|---|
| 0 Noise gate | thresh −80..−20 dB, release 20..500 ms | 6 dB hysteresis, 10 ms hold |
| 1 Amp | input −12..+24 dB, output −18..+6 dB | **Stand-in for the NAM A2 slot.** Tanh stage, 2x oversampled with ADAA |
| 2 Cab IR | lo cut 20..400 Hz, mix | Up to 2048 taps (≈43 ms at 48 kHz). Load your own `.wav` |
| 3 Hologram | feedback 0..95 %, halo | Dotted 8th from tap tempo, diffused shimmer taps, stereo |
| 4 Tesseract | size → RT60 0.5..7.5 s, 4D fold | 8-line FDN; fold rotates the two 4-D halves of the matrix |

Modulation sources: LFO (one cycle per 8 beats), envelope follower, pick-triggered ADSR,
three-body orbit, heart rate (simulated 72 bpm, or a Bluetooth chest strap via **Connect heart
strap**), and controller (gamepad left stick or the CONTROLLER slider). Patch by clicking an orb
and then a knob, from the Mod Matrix tab, or from the focus panel's *Patch* menu. Each patch
swings its knob ±22 %.

Scenes A–D: press **Store**, then a letter, to save the current settings into it; press the letter to recall.
Presets save in this browser and export/import as `.rig.json`.

## The engine protocol

The UI never touches the DSP directly. It sends text lines, modeled on mod-host's commands, so the
same UI can later drive the real rig over a WebSocket without changes:

```
param_set <inst> <symbol> <0..1>        bypass <inst> <0|1>
mod_connect <inst> <symbol> <source> <depth>
mod_disconnect <inst> <symbol>          set_bpm <bpm>
master <dB>                             ext <source> <value>
input_channel <0|1>
```

Instances and symbols: `0 gate thresh|release`, `1 amp input|output`, `2 cab locut|mix`,
`3 holo feedback|halo`, `4 tess size|fold`. Sources: `lfo env adsr orbit heart ctrl`.
Replies are `resp <status>`: 0 is OK, and negative numbers follow mod-host's error codes
(−1 bad instance, −103 bad symbol, −207 out of range, −902 unknown command). You can type commands
into **Engine log** in the menu.

## Files

```
dsp/rig.h            all DSP: modules, modulation sources, engine (header only)
dsp/rig_api.cpp      flat C API exported to WebAssembly
web/engine-worklet.js AudioWorklet: runs rig.wasm, parses the protocol, streams meters
web/app.js           3D stage, HUD, audio setup, devices, presets
web/index.html       page template
web/build_web.py     inlines worklet + wasm + app into dist/rig-test.html
test/harness.cpp     native DSP tests (stability, RT60, aliasing, gate, clicks, onsets, CPU)
test/dump.cpp, test/parity.mjs   native vs WebAssembly comparison
test/browser.cjs     headless Chromium test with a fake microphone
test/blocked.cjs     same page when the host blocks gamepads (e.g. an in-app preview)
build.sh             builds everything and runs the native and parity tests
CLAUDE.md            working rules for Claude sessions on this repo
```

Rebuilding needs `g++`, Python 3 with `pip install ziglang` (used as the WebAssembly compiler),
and Node. Run `./build.sh` on Linux, macOS or WSL; it stops on the first failing test.
The browser tests also need `playwright-core` and a local three.js r128:
`PLAYWRIGHT_CORE=... THREE_JS=... node test/browser.cjs`.

## For AI assistants working on this repo

- **Architecture.** DSP lives only in `dsp/rig.h`. Each effect is a `Module` with
  `process(inL, inR, outL, outR, n, eff)`, where `eff` holds this block's normalized params
  (0..1) after modulation. Blocks are at most 128 samples. Allocating memory in `process` is forbidden.
- **Adding an effect.** Write a `Module`, add it to `Engine::mods`, bump `NMOD`, add its
  defaults to `Engine::base`, add an entry to `INSTANCES` in `engine-worklet.js`, and add an entry to
  `MODS` in `app.js` whose `fmt` functions mirror the C++ mapping exactly. Then add a harness test.
- **Adding a modulation source.** Extend `Sources::update` (output −1..1 per block), bump
  `NSRC`, and add it to `SOURCES` (worklet) and `SRC` (app.js). The meter layout in
  `Engine::process` places source values at `meters[1..NSRC]` and must stay in sync with the UI.
- **Rules.** Keep params normalized at the protocol boundary. Smooth every parameter inside
  modules (one-pole, about 20–30 ms) so block-rate modulation never clicks. Flush denormals in
  feedback paths with `undenorm`. Keep total loop gain below 1 in any feedback structure.
- **Before claiming a change works,** run `./build.sh` (the harness must stay at zero failures)
  and `node test/parity.mjs`. Report measured numbers, not impressions.
- **Path to the real rig.** Each `Module` maps to one DPF/LV2 plugin. The protocol maps to
  mod-host commands in the control hub; `mod_connect` becomes `cv_map` with
  min = base − depth and max = base + depth.

## Test results (this build)

Native, 48 kHz:
- Full chain with 4 patches: about 2 % of one core (49x real time), output peak −6.6 dBFS, no NaN.
- Gate: a −75 dBFS noise floor is fully muted; guitar passes at −0.04 dB.
- Amp aliasing, worst case (2.5 kHz sine, maximum drive): 55 dB below the harmonics.
  Measured alternatives: 2x alone 30 dB, 4x alone 46 dB, 4x with ADAA 60 dB.
- Delay at 95 % feedback with full halo stays bounded and decays. First repeat lands at exactly 375 ms at 120 bpm.
- Reverb RT60: 0.53 / 3.29 / 6.20 s for size 0 / 0.5 / 1 (labels say 0.5 / 4.0 / 7.5; see below).
- Bypass toggles and a tempo jump add no clicks (largest sample step 0.030 vs 0.029 steady).
- Pick detector: 24 of 24 picks found in the test riff, no false triggers.
- WebAssembly vs native: difference energy 86 dB below the signal (math-library rounding); about 2.9 % of one core in Node.

Headless Chromium with a fake mic at 44.1 kHz: 16/16 checks pass, plus 4/4 in `test/blocked.cjs`. These cover audio in and out,
protocol errors, matrix patching, the controller slider sweeping cab mix to exactly 0.68 and 1.00,
bypass from the panel, live LFO motion, onsets, the riff source, and no console errors.

## Known gaps

- **Not tested on real hardware.** That includes your 2i2, real latency on Windows, and the
  Bluetooth heart strap and gamepad paths. Those need your PC.
- The amp is a stand-in. Compiling NAM A2 (NeuralAudio) into the WebAssembly engine is the next step.
- Reverb decay runs about 18 % shorter than its label at large sizes because of in-loop damping.
  It is stable; the scale just needs calibrating.
- In Chrome the gamepad gives stick axes, not the DualShock gyro.
- Browser DSP load is estimated from a 1 ms clock averaged over about 2 s. Use it for trends, not exact numbers.

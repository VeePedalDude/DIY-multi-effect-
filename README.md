# RIG/01: DIY multi-effect

A guitar multi-effects system for old computers: a C++ effects engine, a 3D interface, and
modulation from anything that produces a number (LFOs, your picking, physics, a heart strap,
a gamepad). Today it runs as a test bench in Chrome; next it boots straight into effects on a
Linux iMac.

## Play it now

1. Guitar into **Input 1** of the Scarlett 2i2, **INST** on, **Direct Monitor** off.
2. Download [`dist/rig-test.html`](dist/rig-test.html) (**Download raw file**) and open it in
   **Chrome or Edge**, not in an app preview, which blocks the microphone.
3. **Start audio**, allow the microphone, then in the menu (top left) pick the Focusrite for
   input and output. Raise **MASTER** slowly from −12 dB.

No guitar? Choose **Built-in test riff**. If Chrome won't ask for the microphone on a file
opened from disk, run `python -m http.server` in `dist/` and open `http://localhost:8000/rig-test.html`.

Browser audio on Windows adds roughly 20–40 ms. Fine for building sounds; the Linux host
(`hosts/linux/`) is where the 5–10 ms target lives.

## Layout

```
engine/
  core/        dsp.h (filters, delays, smoothing) · module.h · source.h   ← the contracts
  modules/     gate.h amp.h cab.h holo.h tess.h + registry.h (chain order)
  sources/     lfo.h env.h adsr.h orbit.h heart.h ctrl.h + registry.h
  engine.h     the chain: routing, modulation, bypass, master, meters, self-description
  api/         rig_api.cpp: C API for WebAssembly and native hosts
ui/
  index.html   page shell
  js/          00-core 10-model 20-bridge 30-audio 40-stage 45-faces 50-hud 60-controllers 70-presets 90-main
  screens/     one face display per module: gate.js amp.js cab.js holo.js tess.js
  worklet/     engine-worklet.js: runs rig.wasm, speaks the text protocol
tests/
  native/      framework.h + one test file per area (chain, gate, amp, cab, holo, tess, sources)
  parity/      native vs WebAssembly comparison
  browser/     headless Chromium tests of the built page
hosts/linux/   the iMac's native JACK host (next)
tools/         build_web.py: assembles dist/rig-test.html
docs/          ARCHITECTURE · PROTOCOL · ADDING_A_MODULE · ADDING_A_SOURCE
dist/          rig-test.html (built, committed)
```

**Working on one piece touches one place:**

| To... | Edit |
|---|---|
| add an effect | new `engine/modules/<id>.h` + one line in its `registry.h` ([guide](docs/ADDING_A_MODULE.md)) |
| add a modulation source | new `engine/sources/<id>.h` + one line in its `registry.h` ([guide](docs/ADDING_A_SOURCE.md)) |
| give an effect a custom face display | new `ui/screens/<id>.js` |
| change the stage, panel, matrix | the matching `ui/js/` file |
| test an effect | new `tests/native/test_<id>.cpp` |

The engine describes itself (every parameter's range, unit, curve and default), and the
protocol and UI are built from that description. A new effect therefore shows up with knobs,
a panel, matrix columns and protocol commands without touching the UI. This was verified by
adding a three-knob tremolo with only its own file and one registry line.

## Build and test

Needs `g++`, Python 3 with `pip install ziglang` (the WebAssembly compiler), and Node.
Run on Linux, macOS or WSL:

```
./build.sh
```

It stops at the first failure. Steps: native tests (`-Wall -Wextra -Werror`) → `build/rig.wasm` →
native vs WebAssembly parity → `dist/rig-test.html` → syntax checks.

Browser tests (need `playwright-core` and three.js r128 locally):
`PLAYWRIGHT_CORE=... THREE_JS=... node tests/browser/browser.cjs` and `tests/browser/blocked.cjs`.

## Current effects and sources

| # | Module | Knobs | Notes |
|---|---|---|---|
| 0 | Noise gate | thresh −80..−20 dB, release 20..500 ms | 6 dB hysteresis, 10 ms hold |
| 1 | Amp | input −12..+24 dB, output −18..+6 dB | stand-in for the NAM A2 slot; 2x oversampled with ADAA |
| 2 | Cab IR | lo cut 20..399 Hz, mix | up to 2048 taps; load your own `.wav` |
| 3 | Hologram | feedback 0..95 %, halo | dotted 8th from tap tempo, stereo shimmer |
| 4 | Tesseract | size → RT60 0.5..7.5 s, 4D fold | 8-line FDN, rotating 4-D matrix |

Sources: LFO, envelope follower, pick-triggered ADSR, three-body orbit, heart rate (simulated,
or a Bluetooth strap), controller (gamepad stick or slider).

## Test results (this build)

- **Native:** 15 test groups pass. The full chain uses ~4% of one core on this build machine
  (the old engine measured the same here, back to back). Amp aliasing is 55 dB below the
  harmonics at the worst case; 24/24 picks detected; delay and reverb stay stable at maximum
  settings; no clicks on bypass or tempo jumps.
- **Restructure check:** the split engine reproduces the previous engine bit for bit with fixed
  knob settings. While knobs are modulated it differs by −81 to −92 dB, which is
  floating-point rounding from converting through real units.
- **WebAssembly vs native:** difference energy 86 dB below the signal; 57 KB; no imports.
- **Headless Chromium (fake mic, 44.1 kHz):** 17/17 checks, plus 4/4 with gamepads blocked.

## Known gaps

- Not yet tested on your 2i2, real Windows latency, a real gamepad, or a real heart strap.
- The amp is a stand-in until NAM A2 is compiled in.
- Reverb decay runs about 18% shorter than its label at large sizes.
- The chain order is fixed by the registry; reordering it from the UI comes later.
- Presets saved with the earlier build used different parameter names and won't load.

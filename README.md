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

4. Optional but recommended: menu → **Library → Choose folder…** and pick or create a folder such as
   `Documents/RIG Library`. Your IRs, NAM captures and presets live there as normal files
   ([how the library works](docs/LIBRARY.md)).

Drop a `.nam` capture onto the amp or a `.wav` IR onto the cab to import and play it. Any NAM
architecture plays (A1, A2, slimmable, LSTM, ConvNet...), and IRs can be up to 20 s, mono or stereo:
a cab, a room or a convolution reverb. Free captures and IRs: [TONE3000](https://www.tone3000.com).

No guitar? Choose **Built-in test riff**. If Chrome won't ask for the microphone on a file
opened from disk, run `python -m http.server` in `dist/` and open `http://localhost:8000/rig-test.html`.

Browser audio on Windows adds roughly 20–40 ms. Fine for building sounds; the Linux host
(`hosts/linux/`) is where the 5–10 ms target lives.

## Tuner

The small face-down card in the top bar is a strobe tuner: click it or press **T**. It flips open
into a card turned sideways with the note, cents and three strobe bands that stand still when the
string is in tune and drift right when sharp, left when flat. The output is muted while it's open
(**Mute** turns that off). **Collapse** (or **T** again) folds it back into the mini card, which
keeps showing the note while you play; **Off** stops it. The **A4** reference goes from 400 to
480 Hz. It reads 8-string F♯1 to C7 to ±0.01 cent, using [Q](https://github.com/cycfi/q)'s
pitch detector to find the note and the strobe itself for the fine reading.

## Skins

The look is swappable, Winamp style: **Settings → Skins**. Two are built in (Astral Hall and
Classic). Drop a `.rigskin` file on the stage to add one, ★ your favourites, and press **K** to flip
through them. **Export as template** writes out the skin in use with every setting listed, ready to
edit into your own. A skin changes colours, images, fonts and the style of every menu, panel and
table; the layout never changes. Reference: [docs/SKINS.md](docs/SKINS.md).

## Layout

```
engine/
  core/        dsp.h (filters, delays, smoothing) · module.h · source.h   ← the contracts
               nam.h (captures + IRs via NAM core) · model_slot.h (click-free swaps) · tuner.h (strobe tuner)
  modules/     gate.h amp.h cab.h holo.h tess.h + registry.h (chain order)
  sources/     lfo.h env.h adsr.h orbit.h heart.h ctrl.h + registry.h
  engine.h     the chain: routing, modulation, bypass, master, meters, self-description
  api/         rig_api.cpp: C API for WebAssembly and native hosts
ui/
  index.html   page shell
  js/          00-core 04-skin-core 10-model 20-bridge 30-audio 40-stage 45-faces 50-hud 51-source-panel
               52-vfs 53-assets 54-library-core 55-library 56-asset-ui   (file library) 57-skins 58-tuner
               60-controllers 70-presets 72-preset-format 90-main
  screens/     one face display (and card colour) per module: gate.js amp.js cab.js holo.js tess.js
  assets/      images of the built-in skins: hall background, card frames (embedded into the page)
  worklet/     engine-worklet.js: runs rig.wasm, speaks the text protocol
tests/
  native/      framework.h + one test file per area (chain, gate, amp, cab, holo, tess, sources, tuner)
  parity/      native vs WebAssembly comparison
  ui/          Node unit tests: library and preset format
  fixtures/    golden preset file (must keep loading forever)
  browser/     headless Chromium tests of the built page
  models/      .nam captures of every architecture, for the tests
hosts/linux/   the iMac's native JACK host (next)
third_party/   NeuralAmpModelerCore, Eigen, nlohmann/json (vendored, unchanged)
tools/         build_web.py: assembles dist/rig-test.html · wasm/: shims for the WebAssembly build
docs/          ARCHITECTURE · PROTOCOL · LIBRARY · ADDING_A_MODULE · ADDING_A_SOURCE
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
| let an effect take files (IRs, captures) | declare a file slot in its description ([guide](docs/ADDING_A_MODULE.md#optional-a-file-slot-irs-captures-samples)) |

The engine describes itself (every parameter's range, unit, curve and default), and the
protocol and UI are built from that description. A new effect therefore shows up with knobs,
a panel, matrix columns and protocol commands without touching the UI. This was verified by
adding a three-knob tremolo with only its own file and one registry line.

## Build and test

Needs `g++` (C++20), Python 3 with `pip install ziglang` (the WebAssembly compiler), and Node.
Run on Linux, macOS or WSL. The first run compiles NAM core for both targets (a few minutes);
after that it takes seconds.

```
./build.sh
```

It stops at the first failure. Steps: third-party objects (cached) → native tests (`-Wall -Wextra -Werror`) →
`build/rig.wasm` → library and preset-format unit tests → native vs WebAssembly parity (and what
each kind of file costs in WebAssembly) → `dist/rig-test.html` → syntax checks.

Browser tests (need `playwright-core` and three.js r128 locally):
`PLAYWRIGHT_CORE=... THREE_JS=... node tests/browser/browser.cjs`, `blocked.cjs`, `library.cjs`, `skins.cjs` and `tuner.cjs`.

## Current effects and sources

| # | Module | Knobs | Notes |
|---|---|---|---|
| 0 | Noise gate | thresh −80..−20 dB, release 20..500 ms | 6 dB hysteresis, 10 ms hold |
| 1 | Amp | input −12..+24 dB, output −18..+6 dB | plays any `.nam` capture through NeuralAmpModelerCore; built-in waveshaper when none is loaded |
| 2 | Cab IR | lo cut 20..399 Hz, mix | IRs up to 20 s, mono or stereo (cab, room, reverb); zero-latency partitioned FFT convolution |
| 3 | Hologram | feedback 0..95 %, halo | dotted 8th from tap tempo, stereo shimmer |
| 4 | Tesseract | size → RT60 0.5..7.5 s, 4D fold | 8-line FDN, rotating 4-D matrix |

Sources: LFO, envelope follower, pick-triggered ADSR, three-body orbit, heart rate (simulated,
or a Bluetooth strap), controller (gamepad stick or slider). Each has its own knobs (LFO rate and
shape, envelope attack/release/floor, ADSR times, orbit speed, heart rest rate, controller
smoothing and dead zone): click a source's orb or name to open them, along with every patch it
feeds and its depth. **P** (or the tab beside it) hides the side panel.

## Test results (this build)

- **Native:** 32 test groups pass. The full chain uses ~4% of one core on this build machine.
  - **NAM:** all 11 test captures play (A1 standard, A2 slimmable, A2 features, slimmable WaveNet and
    container, small WaveNet, condition-DSP WaveNet, LSTM, ConvNet, Linear, Sequential), sample-identical
    to NAM core run directly.
  - **Bad files:** damaged, unknown or too-wide files are refused with a reason while the old capture
    keeps playing.
  - **Swaps:** capture swaps are no steppier than steady playing.
  - **Real-time safety:** zero heap calls inside `process()` over 14 s with 5 loads and swaps and the tuner switching on and off.
  - **Tuner:** every note from B1 to C7, each 3–45 cents off, reads the right note and cents to within 0.004 cent; plucked strings read the model's own pitch to 0.2 cent; low E gives a reading after 56 ms; a held note bent 30 cents follows it; the strobe bands turn at exactly harmonic × the tuning error; mute silences the output, and off is bit-identical to no tuner. It costs 0.1–1% of one core.
  - **IRs** from 21 ms to 20 s stereo match the exact convolution to −134 dB with no latency.
  - **Other checks:** amp aliasing is 55 dB below the harmonics at the worst case; 24/24 picks are
    detected; delay and reverb stay stable at maximum settings; no clicks on bypass or tempo jumps.
- **Library and preset format (Node):** WAV and NAM parsing, search, duplicate and name handling,
  finding renamed files. The hand-written version-1 preset loads with no notes. Also tested: a full
  save and load round trip, renamed knobs, unknown modules, out-of-range values and newer files.
- **WebAssembly vs native:** difference 83 dB below the signal, with and without a capture and a 3 s
  stereo IR loaded. The engine has no imports.
- **Source knobs:** each knob measurably does its job (LFO rate and shape, envelope release, ADSR
  attack, orbit speed, heart rest rate, controller dead zone). At their defaults the sources'
  output is bit-identical to the fixed sources they replaced.
- **Skins (Node):** both built-in skins validate. Merging, per-module tiles, and every kind of
  mistake (bad colour, missing image, unknown setting) are reported by name while the good parts
  are kept. `.rigskin` zips are read stored or deflated, or with `skin.json` inside a folder.
- **Headless Chromium:** 25/25 main checks (including opening and editing sources, depth, patching
  from a source and hiding the panel), 4/4 with gamepads blocked, and 22/22 library checks.
  The 19/19 tuner checks play a steady A2 6 cents sharp into the fake microphone: the mini card opens
  the sideways card, the note and cents read right, the output mutes, the bands turn at the right rate,
  the A4 reference, collapsing with T, Off, the Classic skin and a reload.
  The 11/11 skin checks cover the built-ins, favourites and K, exporting a template, editing it into
  a new skin and dropping it on the stage, a broken skin, and the choice surviving a reload. The
  library checks cover dropping files on a panel and on the stage, search, saving a preset file,
  reloading the page and loading it back, and removing a file the preset uses. They also play four
  real captures (A2, LSTM, A1, ConvNet), refuse a fake one with its reason, and run a 3 s stereo IR.

What files cost (this build machine, one core; it's a shared cloud VM, so runs vary by a few points):

| Loaded | Native (Linux host) | Browser (WebAssembly) |
|---|---|---|
| built-in amp + cab, whole chain | 4% | 5% |
| A1 standard WaveNet | 13–21% | 35% |
| A2 (slimmable, full size) | 14–17% | 35% |
| LSTM / ConvNet | 1–3% | 5% |
| IR 2 s mono / 5 s stereo / 20 s stereo | 5% / 11% / 22% | 9% (1 s) / 19% / 33% |

The iMac's 1.6 GHz i5 is roughly half this speed per core, so A1/A2 captures belong on the native
Linux host there. In the browser on a recent PC they run comfortably.

## Known gaps

- Not yet tested on your 2i2, real Windows latency, a real gamepad, or a real heart strap.
- The **Choose folder** flow (Chrome's folder picker and its permission prompt) can't be automated
  here. The same library code is tested through browser storage, which uses the same file-handle
  interface, but the folder picker itself needs a try on your PC.
- In the browser, loading a capture pauses audio for about 0.1 s (a 20 s stereo IR, about 0.5 s),
  because loading runs on the audio thread there. The swap itself crossfades cleanly.
- Captures run at the page's rate. The page asks for 48 kHz (what captures are made at), and a capture
  at another rate plays with a slightly shifted tone and says so. NAM's plugin resamples; this doesn't yet.
- Slimmable (A2) captures play at full size; a size control comes later.
- NAM core trusts a capture file's weight count (see `third_party/README.md`); LSTM counts are checked.
- Reverb decay runs about 18% shorter than its label at large sizes.
- The chain order is fixed by the registry; reordering it from the UI comes later.
- Amp INPUT now defaults to 0 dB (unity into a capture). A fresh start of the built-in amp is cleaner and
  about 6 dB quieter than before; saved presets sound the same.

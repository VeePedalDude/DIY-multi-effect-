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
  core/        dsp.h (filters, delays, smoothing) · eq.h (EQ filters) · vaf.h (synth filters) · module.h · source.h · tap.h (per-patch phase and DIV)
               nam.h (captures + IRs via NAM core) · model_slot.h (click-free swaps) · tuner.h (strobe tuner)
               fft.h (a small FFT) · polytrack.h (finds the notes of a chord, and voices for them)
  modules/     <category>/<id>.h: dynamics/gate.h drive/amp.h cab/cab.h delay/holo.h reverb/tess.h
               filter/tilt.h bax.h geq.h peq.h stack.h prog.h dyneq.h (+ order.txt: their order in the tab)
               synth/svf.h comb.h formant.h ota.h ladder.h diode.h sallen.h (+ order.txt)
               voice/polyv.h (POLY VOICE: your chords on tape keys or a polysynth)
               categories.txt (the tabs) · default_chain.txt (a fresh rig) · registry.h (generated)
  sources/     time/lfo.h playing/env.h playing/adsr.h chaos/orbit.h outside/heart.h outside/ctrl.h
               categories.txt · default_rack.txt · registry.h (generated)
  fx_chain.h   the effect chain: module types table, the order as one atomic word
  mod_rack.h   the modulation rack: instances added/removed live, cross-modulation
  engine.h     the chain: routing, modulation, bypass, master, meters, self-description
  api/         rig_api.cpp: C API for WebAssembly and native hosts
ui/
  index.html   page shell
  sources/     a source type's own picture in its panel (geo.js: GEO PULSE's 3D view; seq.js: GATE SEQ's squares and lanes; ctrl.js: CONTROLLER's input and LEARN)
  js/          00-core 04-skin-core 10-model 20-bridge 22-rack 24-chain 30-audio 40-stage 45-faces 46-eq-plot 47-va-plot 49-picker 50-hud 51-source-panel
               52-vfs 53-assets 54-library-core 55-library 56-asset-ui   (file library) 57-skins 58-tuner
               60-controllers (USB gamepads, joysticks and MIDI for CONTROLLER sources; 12-inputs-core reads them) 70-presets 72-preset-format 90-main
  screens/     one face display per module type: gate.js amp.js cab.js holo.js tess.js, and the EQs' curves
               tilt.js bax.js geq.js peq.js stack.js prog.js dyneq.js, and the synth filters'
               svf.js comb.js formant.js ota.js ladder.js diode.js sallen.js, and polyv.js (a keyboard of the notes it plays)
  assets/      images of the built-in skins: hall background, card frames (embedded into the page)
  worklet/     engine-worklet.js: runs rig.wasm, speaks the text protocol
tests/
  native/      framework.h + one test file per area (chain, fxchain, gate, amp, cab, holo, tess, eq, synth, polyv, sources, lfo, orbit, geo, seq, tuner, rack)
               + test_conformance.cpp: the format and safety of every module and source type
  parity/      native vs WebAssembly comparison
  ui/          Node unit tests: library and preset format
  fixtures/    golden preset file (must keep loading forever)
  browser/     headless Chromium tests of the built page
  models/      .nam captures of every architecture, for the tests
hosts/linux/   the iMac's native JACK host (next)
third_party/   NeuralAmpModelerCore, Eigen, nlohmann/json (vendored, unchanged)
tools/         gen_registry.py: makes the registries from the folders · build_web.py: assembles dist/rig-test.html · wasm/: shims for the WebAssembly build · orbits/refine_orbits.cpp: re-derives THREE-BODY's orbit table
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
`PLAYWRIGHT_CORE=... THREE_JS=... node tests/browser/browser.cjs`, `blocked.cjs`, `library.cjs`, `skins.cjs`, `tuner.cjs`, `rack.cjs`, `chain.cjs`, `geo.cjs`, `seq.cjs` and `controllers.cjs`.

## Current effects and sources

| # | Module | Knobs | Notes |
|---|---|---|---|
| 0 | Noise gate | thresh −80..−20 dB, release 20..500 ms | 6 dB hysteresis, 10 ms hold |
| 1 | Amp | input −12..+24 dB, output −18..+6 dB | plays any `.nam` capture through NeuralAmpModelerCore; built-in waveshaper when none is loaded |
| 2 | Cab IR | lo cut 20..399 Hz, mix | IRs up to 20 s, mono or stereo (cab, room, reverb); zero-latency partitioned FFT convolution |
| 3 | Hologram | feedback 0..95 %, halo | dotted 8th from tap tempo, stereo shimmer |
| 4 | Tesseract | size → RT60 0.5..7.5 s, 4D fold | 8-line FDN, rotating 4-D matrix |

In the add panel, not in a fresh rig. Filter & EQ, simple to sophisticated:

| Module | Knobs | Notes |
|---|---|---|
| Tilt EQ | tilt ±6 dB, pivot 200 Hz..3 kHz, level | one knob, dark to bright about the pivot |
| Baxandall | bass ±15 dB, treble ±15 dB, their frequencies, level | first-order shelves, half the gain at each frequency |
| Graphic EQ | 7 bands at 100 Hz..6.4 kHz ±15 dB, level | the seven-band guitar graphic; bands narrow as you push them |
| Parametric | low and high shelf ±15 dB, two mids: frequency, gain ±15 dB, Q 0.3..10 | sweep a mid while you play: no zipper |
| Tone stack | bass, mid, treble 0..10, model, level | the amp's passive stack from the circuit (Yeh & Smith); '59 Bassman, JCM800, Twin Reverb and blends between |
| Program EQ | low freq, boost, atten; high freq, boost, bandwidth; high atten; drive | in the style of a passive tube program EQ: boost and cut together for a tight low end |
| Dynamic EQ | freq, Q, threshold, range ±18 dB, ratio, attack, release | a bell that only moves when its band gets loud (tames a harsh pick attack) |

Synth filters, simplest model first. Every one but the comb and formant has CUTOFF, RESO, ENV (the
cutoff follows your picking, up to 4 octaves either way), DRIVE and LEVEL; the saturating ones run
2x oversampled:

| Module | Its own knobs | Notes |
|---|---|---|
| State variable | MODE: low-pass → notch → high-pass → band-pass | the Oberheim SEM's 12 dB multimode; sings but never self-oscillates |
| Comb | FREQ, FEEDBACK ±95 %, DAMP, MIX | rings at a pitch and its harmonics; negative feedback: an octave down, odd harmonics |
| Formant | VOWEL A-E-I-O-U, SHIFT ±12 st, RESO, MIX | three resonances where a voice puts them; ENV talks through the vowels |
| OTA filter | POLES 1-4 (6-24 dB/oct) | Juno / Jupiter / Prophet-5 style; the level holds as RESO rises |
| Ladder | COMP (puts back the bass the resonance takes) | Moog transistor ladder, 24 dB/oct, self-oscillates on pitch |
| Diode ladder | | TB-303 / EMS: coupled stages, squelchy; acid with ENV |
| Sallen-Key | HP CUT, HP PEAK, LP CUT, LP PEAK | Korg MS-20 style: high-pass into low-pass, peaks screaming through clipping diodes |

Modulation sources: LFO, envelope follower, pick-triggered ADSR, three-body orbit, heart rate
(simulated, or a Bluetooth strap), CONTROLLER (any control on a USB gamepad, joystick, flight stick or MIDI
controller: open it, press LEARN and move the control; INVERT, 0…+1 or −1…+1, momentary or toggle buttons;
add more CONTROLLERs for more controls; bindings stay with presets and this browser), GATE SEQ (Time & tempo): a step sequencer, up to 32 squares you click on and off (or drag to paint),
each step with its own velocity, chance, ratchet (1–4 hits), gate length and value, drawn in lanes under
the squares; step length as a note value, swing, a Euclidean fill, shift, directions, resync every few
bars, and gate / trigger / envelope / velocity / stepped-value outputs. And GEO PULSE (also in
Time & tempo): a rhythm from a shape. A point travels round a polygon, star, warped or hyperbolic
polygon or Platonic solid in time with the tempo; each vertex can fire a gate, a trigger or an envelope,
with its own chance, velocity, accent and gate length. Step lengths can follow the geometry (the angle
turned, or the distance travelled, measured hyperbolically on the hyperbolic shape) while the cycle keeps
its length; 7 steps against a 16-step pattern stay in time with each other. Its panel shows the shape in
3D: drag to turn it, click a vertex to edit it. Each has its own knobs (LFO
rate as note values when synced or Hz when free, nine waves, depth, steps for a staircase, and
bipolar or unipolar output; envelope attack/release/floor, ADSR times; the three-body orbit: eleven known periodic orbits or the old chaotic one, what to read from it, its speed or a tempo-synced period, and how far each cycle strays; heart rest rate, controller
smoothing and dead zone): click a source's orb or name to open them, along with everything it moves
and the depths. Each patch from an LFO also has its own **Phase** and **Div** (¼× … 8× the LFO's
cycle), so one LFO can move several knobs at different offsets and speeds, locked together. **P** (or the tab beside it) hides the side panel.

### The effect chain

- **Add, remove and reorder effects while it plays.** The **+ ADD EFFECT** card at the end of the
  row (or **+ Add effect** in the Mod Matrix) opens a tab per kind of effect: Dynamics, Amps & drive,
  Cabs & IRs, Filter & EQ, Synth filters, Modulation, Pitch, Delay, Reverb, Utility, Experimental. A search box
  looks across all of them, and the panel remembers the last tab. Sources get the same: Time &
  tempo, Your playing, Physics & chaos, Outside world. Up to 12 in all, in any
  order, several of one kind if you like (two delays: HOLOGRAM and HOLOGRAM 2).
- **In each effect's panel:**
  - **◀ ▶** moves it earlier or later in the chain.
  - **Duplicate** puts a copy right after it, with the same knobs, bypass and file.
  - **Remove** asks twice; patches to it go with it.
- **No clicks:**
  - A new effect fades in from dry over 10 ms, and a removed one fades out the same way.
  - A move dips the sound for 10 ms either side of the swap, inside the chain too, so a delay
    doesn't record the jump and play it back later.
- Presets save the chain, in order. Older presets load as the five original modules.
- Every instance is a patch target for every source, has its own card, file slot and matrix columns,
  and is addressed by its id in the protocol (`param_set holo2 feedback 0.8`, `fx_move holo2 0`).

### The modulation rack

- **Add and remove sources while it plays.** The **+ ADD** orb at the end of the source row (or
  **+ Add source** in the Mod Matrix) lists every kind of source. Add as many as you like, up to 16
  in all; several of one kind is fine (LFO, LFO 2, LFO 3). A source's panel has **Duplicate** (same
  settings) and **Remove** (asks twice; its patches go with it).
- **Cross-modulation.** A source's knobs can be moved by another source, or by itself:
  - **Moved by** on any of its knobs, in its panel.
  - Carry a cable from one orb and click another orb, then pick the knob.
  - The matrix's **Source knobs** columns.

  An envelope can speed up an LFO while you dig in; an orbit can wander an LFO's shape; two LFOs can
  chase each other. The knob shows its setting and where modulation has it right now (`8.0 beats → 3.2 beats`).
- Presets save the rack and every patch; scenes switch knobs and patches within it.
- **Adding a new effect or source is one file.** The folders are the registry: put
  `engine/modules/<category>/<id>.h` (or `engine/sources/<category>/<id>.h`) in its category's
  folder and run `./build.sh`; it shows up in that tab. A new tab is one line in `categories.txt`.
  New ones don't join a fresh rig unless you list them in `default_chain.txt` / `default_rack.txt`.
  The build stops with what to fix if a file is in the wrong place or misnamed.
- New kinds of source follow one format ([ADDING_A_SOURCE.md](docs/ADDING_A_SOURCE.md), with a
  working template), and growing an existing one keeps old presets sounding the same
  ([EXPANDING.md](docs/EXPANDING.md)). Every registered type is checked automatically.

## Test results (this build)

- **Native:** 50 test groups pass. The full chain uses ~4% of one core on this build machine.
  - **NAM:** all 11 test captures play (A1 standard, A2 slimmable, A2 features, slimmable WaveNet and
    container, small WaveNet, condition-DSP WaveNet, LSTM, ConvNet, Linear, Sequential), sample-identical
    to NAM core run directly.
  - **Bad files:** damaged, unknown or too-wide files are refused with a reason while the old capture
    keeps playing.
  - **Swaps:** capture swaps are no steppier than steady playing.
  - **Real-time safety:** zero heap calls inside `process()` over 14 s with 5 loads and swaps and the tuner switching on and off.
  - **Effect chain:**
    - **Default chain:** output byte-identical to the fixed chain it replaces (42 s rendered both
      ways). CPU median 4.24% vs 4.29% of one core over five interleaved runs.
    - **Adding a module, bypassed:** output stays bit-identical.
    - **Ids, positions and limits:** checked, up to 12 modules.
    - **Removing:** fades out, leaves the audio order, is freed.
    - **No clicks:** add, remove and two moves on a held 220 Hz tone. The largest step is 1.5× the
      steady tone's. The fix for moves recording into a delay cut it from 5.2× to 1.5×.
    - **Two HOLOGRAMs** keep separate state.
    - **Real-time safety:** zero heap calls in `process()` through 94 adds, 88 removes and 210 moves.
    - **Cost:** a full chain of 12 runs at about 9% of one core.
  - **Modulation rack:** the default rack's output is byte-identical to the fixed sources it replaced
    (42 s rendered both ways), at the same CPU (4.1–4.5% vs 4.1–4.7%). Adding sources nobody listens
    to changes nothing. Ids, limits (16) and clean-up on remove are checked. Two LFOs at the same
    settings match exactly, and at 1 vs 2 beats run at double the rate. An envelope patched to an LFO's
    rate takes it from 3.75 Hz in silence to 0.03 Hz while a note plays. Loops and self-patches stay
    bounded for 10 minutes. There are zero heap calls in `process()` through 103 adds, 93 removes and 353
    cross patches. A full rack of 16, all cross-patched, adds 0.35% of one core.
  - **Conformance (every type, automatically):** each source's description, output in −1..1 and
    finite for 6 knob settings × 5 inputs (silence to full-scale noise) × 3 block sizes × 2 rates,
    no shared state between instances, no allocation; each module's format, finite output and no
    allocation.
  - **Tuner:** every note from B1 to C7, each 3–45 cents off, reads the right note and cents to within 0.004 cent; plucked strings read the model's own pitch to 0.2 cent; low E gives a reading after 56 ms; a held note bent 30 cents follows it; the strobe bands turn at exactly harmonic × the tuning error; mute silences the output, and off is bit-identical to no tuner. Re-picking a held note every 0.7 s moves the reading at most 0.35 cent (it was up to 5 cents before the hold fix). It costs 0.1–1% of one core.
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
  The 20/20 chain checks also cover the add panel's tabs (a tab per category, the first non-empty
  one open, empty ones saying which folder to use, Filter & EQ and Synth filters in their order),
  search across tabs, arrow keys and the remembered tab, and add each EQ and synth filter live to
  check its face draws. They add a second HOLOGRAM from the “+” card, set its knob, move it to the
  front with ◀, patch the LFO to it, duplicate the cab, remove the amp, check the matrix, reload a
  preset's chain in order, fill the chain to 12 and check Overview fits it.
  The 14/14 rack checks add an LFO from the “+” orb, set its knob, patch the envelope to its rate
  from the panel and an orbit to its shape by carrying a cable orb to orb, patch from the matrix's
  source-knob columns, duplicate and remove, fill the rack to 16, and reload a preset's rack.
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

- Not yet tested on your 2i2, real Windows latency, a real gamepad, joystick or MIDI controller (tested with simulated ones), or a real heart strap.
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
- Amp INPUT now defaults to 0 dB (unity into a capture). A fresh start of the built-in amp is cleaner and
  about 6 dB quieter than before; saved presets sound the same.

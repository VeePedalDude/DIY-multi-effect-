You're working on RIG/01, my DIY guitar multi-effects rig: a C++ effects engine (also compiled to
WebAssembly) with a three.js browser test bench. It's going to boot straight into effects on an old
Linux iMac. The repo is github.com/VeePedalDude/DIY-multi-effect- (clone it; work from the newest
branch, or main if the modulation-rack PR has been merged).

**The job: expand the six modulation sources we already have, so each one is deeper and more
musical.** The sources are the LFO, ENV FOLLOW, PICK ADSR, THREE-BODY, HEART RATE and CONTROLLER,
in `engine/sources/`. This is not about adding new source types.

**Read first, and follow them:**
- `CLAUDE.md`: the project rules.
- `docs/ARCHITECTURE.md`
- `docs/ADDING_A_SOURCE.md`: the source format. It's checked by `tests/native/test_conformance.cpp`.
- `docs/EXPANDING.md`: how to grow a source without breaking presets.
- `docs/PROTOCOL.md`: "The modulation rack".

Sources live in a modulation rack. There can be several instances of one type, added and removed
live, and any source can move any other source's knobs (cross-modulation). So every knob you add
becomes a cross-mod target automatically, and may change every block (2.7 ms).

**How to work:**
1. Read each source's code and run `./build.sh` to see where things stand. Then give me a short plan
   per source: the new knobs or modes (symbol, range, unit, default), what each does musically, how
   the default keeps today's sound, and how you'll test it. Wait for my go-ahead.
2. Implement one source at a time, building and testing after each. Start from these ideas and
   improve on them; tell me if one is a bad fit:
   - **LFO:**
     - phase offset (so two LFOs can run in quadrature)
     - free-running Hz as well as tempo sync
     - more shapes (saw up/down, stepped random, smooth random)
     - retrigger on pick
     - unipolar or bipolar
     - fade-in after a pick
   - **ENV FOLLOW:**
     - sensitivity / gain
     - a band to listen to (full, lows, highs)
     - invert
     - a gate mode with a threshold
   - **PICK ADSR:**
     - velocity (how hard you pick scales the peak)
     - legato / retrigger mode
     - a hold stage
     - curve shape (linear to exponential)
     - trigger sensitivity
   - **THREE-BODY:**
     - which quantity it outputs (a distance, a body's x, its speed)
     - mass ratio / chaos amount
     - damping
   - **HEART RATE:**
     - bpm smoothing
     - output shape (beat pulse, or a slow breathing wave at about a quarter of the heart rate)
     - simulated heart-rate variability
   - **CONTROLLER:**
     - response curve (linear / exponential)
     - output range
     - invert
3. Each source gets up to 8 knobs (`MAXPAR`) and 2 meters. If one truly needs more, ask me before
   changing a limit.

**Rules that matter here:**
- **Append knobs; never reorder or rename them.** If a rename is unavoidable, add it to
  `PRESET_RENAMES.sourceKnobs`.
- **Every new knob's default reproduces today's output bit for bit.** Prove it: render the riff
  before and after and compare with `==`, as EXPANDING.md describes. Report CPU before and after.
- **Knobs can jump every block** (cross-modulation), so never make a jump in the output from a knob
  change alone. Phase-accumulate rates, smooth what would click, and work out coefficients per
  block, cheaply.
- **Real-time safety:** nothing in `update()` allocates or locks, and all state lives in members.
  Output stays −1..1 and finite for any knob setting or input; the conformance tests enforce this.
- **A test with measured numbers for every new knob** (Hz, ms, counts), in
  `tests/native/test_sources.cpp` or a new `test_<source>.cpp`.
- `third_party/` is never edited.
- No literal colours, fonts or images in UI code: use the skin.
- Keep `./build.sh` at zero failures (`-Wall -Wextra -Werror`, WebAssembly build, native/WASM
  parity). Run the browser suites:
  - `browser.cjs`, `blocked.cjs`, `library.cjs`, `skins.cjs`, `tuner.cjs`, `rack.cjs`
  - each needs `PLAYWRIGHT_CORE=<playwright-core path>` and `THREE_JS=<three.min.js r128>`.
- Update the source tables in `docs/PROTOCOL.md` and the README. Rebuild `dist/rig-test.html` and
  commit it with every change.

**When you're done, give me:**
- one PR (a new branch off the one you started from)
- a table per source: new knobs, defaults, what you measured, before/after CPU
- anything you tried and dropped, and why
- the rebuilt `dist/rig-test.html`, so I can play it.

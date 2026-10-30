# Adding an effect module

One new file in the right folder, and a test. There is no list to edit: the folders are the registry.

## Where effects live

```
engine/modules/
  categories.txt        the categories, in tab order: folder | tab label | what goes here
  default_chain.txt     what a fresh rig starts with, in order (module ids)
  dynamics/gate.h       one effect per file, in its category's folder
  drive/amp.h
  cab/cab.h
  delay/holo.h
  reverb/tess.h
  filter/tilt.h …       seven equalisers; filter/order.txt lists them simple to sophisticated
  synth/ladder.h …      seven synth filters, simplest model first (synth/order.txt)
  voice/polyv.h         POLY VOICE: the notes of a chord, played by tape keyboards or a polysynth
  registry.h            GENERATED from the folders by tools/gen_registry.py (./build.sh runs it)
```

Each folder is a tab in the **Add an effect** panel: Dynamics, Amps & drive, Cabs & IRs,
Filter & EQ, Synth filters, Synth voices, Modulation, Pitch, Delay, Reverb, Utility, Experimental. A tab with no effects yet
still shows, and says which folder new ones go in. A new category is one line in `categories.txt`
plus its folder. Inside a tab, effects are listed by id, unless the folder has an `order.txt`: the
ids it names come first, in that order, then any others.

## 1. Write `engine/modules/<category>/<id>.h`

Building an equaliser or a filter? `engine/core/eq.h` has state-variable bells, shelves and passes
that stay clean while their knobs move, first-order shelves, and a third-order filter from s-domain
coefficients; the seven in `filter/` are worked examples, from one knob (`tilt.h`) to a dynamic EQ.
For a resonant filter that saturates, `engine/core/vaf.h` has a zero-delay-feedback loop for any small
analog system (`VaLoop`: give it A, b, c), 2x oversampling, an ENV amount and the loop noise that lets
a resonance start by itself; `synth/ladder.h` is the shortest example.
Turning what's played into notes? `engine/core/polytrack.h` finds up to six notes of a chord every 5 ms
(`PolyTracker`) and keeps voices for them (`PolyAlloc`: matched, glided, started, released, restarted
on a new pick); `engine/core/fft.h` is the FFT under it, also handy for building wavetables in `init()`.
`voice/polyv.h` uses both.

Start from [`templates/module_template.h`](templates/module_template.h): a tremolo that was put in
`modulation/`, built, and run through every test. The shape:

```cpp
#pragma once
#include "../../core/module.h"
namespace rig {
inline constexpr ParamDesc TREM_PARAMS[] = {
  // symbol   label    min    max   default unit  curve        decimals sign
  {"depth", "DEPTH",  0.f,  100.f, 0.5f,  "%",  Curve::Lin, 0, false},
  {"rate",  "RATE",   0.5f, 16.f,  0.4f,  "Hz", Curve::Log, 1, false},
  {"shape", "SHAPE",  0.f,  1.f,   0.f,   "",   Curve::Lin, 2, false},
};
inline constexpr ModuleDesc TREM_DESC = {"trem", "TREMOLO", "sine to square", 3, TREM_PARAMS, 1, {"gain", nullptr}};
struct Trem : Module {
  const ModuleDesc& desc() const override { return TREM_DESC; }
  void init(float s) override { /* ... */ }
  void reset() override { /* ... */ }
  void process(const float* inL, const float* inR, float* oL, float* oR, int n,
               const float* e, const Context& ctx) override { /* ... */ }
};
}
```

The build checks the file and stops with what to fix if:
- it isn't in a folder listed in `categories.txt`;
- it doesn't hold exactly one `struct <Name> : Module`;
- its description's id isn't the file name (`trem.h` → `"trem"`);
- an id or struct name is already taken.

Rules:
- `e[i]` is the knob after modulation, 0..1. Use `map(i, e[i])` for real units.
- Smooth anything that would click when it jumps (`OnePole`, 20–30 ms), as the existing modules do.
- No memory allocation in `process`. Size buffers as members (fixed arrays).
- Use `undenorm()` in feedback paths and keep loop gain below 1.
- Up to 8 parameters (`MAXPAR`) and 2 display meters (`meters()` + names in the desc).
- `Context` gives `sr` and `bpm` for tempo-synced effects.

## 2. Run `./build.sh`

It makes `engine/modules/registry.h` from the folders and builds everything. The new effect appears
in its tab of the **Add an effect** panel. Adding it to the library doesn't add it to a fresh rig;
to do that, add its id to `default_chain.txt` (at most 12).

The registry holds the module *types*. A fresh chain holds the ones in `default_chain.txt`; after that the player
adds, removes and reorders instances freely (several of one type is fine: `holo`, `holo2`), and
presets save the chain. Your module is one instance: keep all its state in members, never `static`
(test 93 and the chain tests check two instances stay independent).

## 3. Add a test: `tests/native/test_<id>.cpp`

Start from [`templates/test_module_template.cpp`](templates/test_module_template.cpp):
`e.addModule("trem", nullptr, -1, false)` puts one in the chain and returns its slot, `only(e, slot)`
isolates it, and `riffSignal()` gives a synthetic guitar riff. Show it does its job, with numbers.

## Optional: a file slot (IRs, captures, samples)

Declare a slot in the description and implement four functions. The library, search, drag and
drop, presets and the protocol all pick it up automatically:

```cpp
inline constexpr ModuleDesc VERB_DESC = {"verb", "ROOM IR", "convolution", 2, VERB_PARAMS, 0, {nullptr, nullptr},
                                         {"room-ir", "Room IR", ".wav", true, irMaxBytes()}};   // kind, label, exts, ready, max bytes
std::vector<unsigned char> staging; std::string status;
unsigned char* assetBuffer(int bytes) override { staging.resize(bytes); return staging.data(); }
int assetCommit(int bytes) override { /* build from staging; set status; 0 = ok, negative = refused */ return 0; }
void assetClear() override { /* back to built-in */ }
const char* assetStatus() const override { return status.c_str(); }   // shown in the panel and log
```

The host reserves room with `assetBuffer(bytes)`, copies the file's payload in, then calls
`assetCommit`. These run on the control thread, never inside `process()`, so they may parse and
allocate. `.wav` files arrive decoded (the `RIR1` header and planar float32 channels described in
`engine/core/nam.h`, at the engine rate). Any other extension arrives as the file's raw bytes. Use
`ready = false` to accept files before the module can play them. The library gets a `room-ir/`
folder automatically.

**Swapping without clicks or audio-thread allocation:** build the new thing in `assetCommit`, wrap it
in a `LoadedModel` and hand it to a `ModelSlot` (`engine/core/model_slot.h`). In `process()`, call
`slot.begin(n)` once, run `slot.current()` and, while a swap is under way, `slot.next()`, mixed by
the weights `begin` returns. The slot warms the new model up silently, crossfades over 10 ms and
frees the old one back on the control thread. `amp.h` (NAM captures) and `cab.h` (IRs) both work
this way; the cab is the simplest example. For NAM captures and IRs, `engine/core/nam.h` already
has `loadCapture` and `parseIr`.

## Optional: a face screen, `ui/screens/<type>.js`

One screen per type: every instance of it uses the same screen (`m.id` tells them apart).

Without one, the face shows a bar per parameter. To draw something specific:

```js
screen('trem', (c, b, m, h) => {
  // c = canvas 2D context, b = {x, y, w, h} box, h.eff(i) = live knob 0..1,
  // h.param(i).real(v) = real units, h.meter(name, fallback), h.T = seconds
});
NOTES.trem = 'Shown in the focus panel.';
```

Then run `./build.sh`. The new effect appears on the stage, in the panel, in the matrix, as a
patch target for every source, and in the protocol (`param_set trem depth 0.8`).

`tests/native/test_conformance.cpp` (test 93) checks every registered module automatically: the
description's format (ids, symbols, ranges, units), finite output on loud noise at four knob
settings, and no allocation in `process()`. To grow an existing module, see [EXPANDING.md](EXPANDING.md).

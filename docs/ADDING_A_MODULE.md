# Adding an effect module

Three steps, all in new files except one line in the registry.

## 1. Write `engine/modules/<id>.h`

This tremolo was built and checked exactly this way (3 knobs, appears in the UI automatically):

```cpp
#pragma once
#include "../core/module.h"
namespace rig {

inline constexpr ParamDesc TREM_PARAMS[] = {
  // symbol   label    min    max   default unit  curve        decimals sign
  {"depth", "DEPTH",  0.f,  100.f, 0.5f,  "%",  Curve::Lin, 0, false},
  {"rate",  "RATE",   0.5f, 16.f,  0.4f,  "Hz", Curve::Log, 1, false},
  {"shape", "SHAPE",  0.f,  1.f,   0.f,   "",   Curve::Lin, 2, false},
};
inline constexpr ModuleDesc TREM_DESC = {"trem", "TREMOLO", "sine to square", 3, TREM_PARAMS, 0, {nullptr, nullptr}};

struct Trem : Module {
  float ph = 0;
  const ModuleDesc& desc() const override { return TREM_DESC; }
  void init(float s) override { sr = s; reset(); }
  void reset() override { ph = 0; }
  void process(const float* inL, const float* inR, float* oL, float* oR, int n,
               const float* e, const Context&) override {
    float depth = map(0, e[0]) * 0.01f, hz = map(1, e[1]), sh = e[2];
    for (int i = 0; i < n; i++) {
      ph += hz / sr; if (ph >= 1) ph -= 1;
      float s = sinf(2 * PI_F * ph), lfo = (1 - sh) * s + sh * (s > 0 ? 1.f : -1.f);
      float g = 1.f - depth * 0.5f * (1.f + lfo);
      oL[i] = inL[i] * g; oR[i] = inR[i] * g;
    }
  }
};
} // namespace rig
```

Rules:
- `e[i]` is the knob after modulation, 0..1. Use `map(i, e[i])` for real units.
- Smooth anything that would click when it jumps (`OnePole`, 20–30 ms), as the existing modules do.
- No memory allocation in `process`. Size buffers as members (fixed arrays).
- Use `undenorm()` in feedback paths and keep loop gain below 1.
- Up to 8 parameters (`MAXPAR`) and 2 display meters (`meters()` + names in the desc).
- `Context` gives `sr` and `bpm` for tempo-synced effects.

## 2. Register it in `engine/modules/registry.h`

```cpp
#include "trem.h"
...
  X(Tess, tess)        \
  X(Trem, trem)
```

The list is the module *types*. A fresh chain holds one of each, in this order; after that the player
adds, removes and reorders instances freely (several of one type is fine: `holo`, `holo2`), and
presets save the chain. Your module is one instance: keep all its state in members, never `static`
(test 93 and the chain tests check two instances stay independent).

## 3. Add a test: `tests/native/test_<id>.cpp`

```cpp
#include "framework.h"
using namespace rigtest;
TEST("70 trem: full depth dips the level") {
  Engine e; e.init(SR); only(e, mod(e, "trem"));
  ...
  CHECK(condition, "what you measured %.1f dB", value);
}
```

`only(e, index)` isolates one module. `riffSignal()` gives the synthetic guitar riff.

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

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

The position in the list is the position in the signal chain.

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

## Optional: a face screen, `ui/screens/<id>.js`

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

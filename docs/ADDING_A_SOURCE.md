# Adding a modulation source

A source turns anything into one number from −1 to 1 per block (about every 2.7 ms).

## 1. Write `engine/sources/<id>.h`

```cpp
#pragma once
#include "../core/source.h"
namespace rig {
inline constexpr ParamDesc RAND_PARAMS[] = {
  {"rate", "RATE", 1.f, 16.f, 0.5f, "per beat", Curve::Log, 1, false},   // 4 per beat at 0.5
};
inline constexpr SourceDesc RAND_DESC = {"rand", "RANDOM", "sample and hold", nullptr, 0, {nullptr, nullptr}, 1, RAND_PARAMS};
struct RandomSH : Source {
  float ph = 0, v = 0; unsigned seed = 1;
  const SourceDesc& desc() const override { return RAND_DESC; }
  float update(const SourceContext& c) override {
    ph += c.dt * c.bpm / 60.f * knob(c, 0);                // knob(c, i) = knob i in real units
    if (ph >= 1) { ph -= 1; seed = seed * 1664525u + 1013904223u; v = (seed >> 8) / 8388608.f - 1.f; }
    return v;
  }
};
}
```

`SourceContext` gives the dry input (`c.in`, `c.n`), `sr`, `dt`, `bpm` and the source's knobs.
**Knobs** are described exactly like a module's (`ParamDesc`, `engine/core/param.h`). The source's
panel, presets, scenes and the `src_set <id> <symbol> <0..1>` command come from that description
automatically. Pick each range so the default sits at 0.5 on a log curve (or the middle of a
linear one), and the default is exact. For outside data
(a sensor, a game controller), set `ext` in the desc to a short description and implement
`ext(float)`. The UI or the Linux host then sends `ext <id> <value>`.

## 2. Register it in `engine/sources/registry.h`

Add the `#include` and an `X(RandomSH, rand)` line. Order is the order of the orbs.

## 3. Test it in `tests/native/test_sources.cpp`

Optional: add a colour for it in `SOURCE_COLORS` in `ui/js/00-core.js`. Otherwise it gets the next fallback colour.

## Two tiers

Sources in `engine/sources/` run in the audio engine: they're sample-accurate and must be
real-time safe. Slow or outside-world sources (heart strap, gamepad, weather, a game) can
live outside the engine and just send `ext ctrl <value>` or their own `ext <id>` lines. That's
the easier place to start.

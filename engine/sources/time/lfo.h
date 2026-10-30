// engine/sources/time/lfo.h - tempo-synced LFO. RATE is beats per cycle (8 = 0.25 Hz at 120 bpm);
// SHAPE morphs sine (0%) -> triangle (50%) -> square (100%).
#pragma once
#include "../../core/source.h"

namespace rig {

// Ranges are centred on the defaults (0.5), so a fresh rig matches the fixed LFO it replaced.
inline constexpr ParamDesc LFO_PARAMS[] = {
  {"rate",  "RATE",  0.5f, 128.f, 0.5f, "beats", Curve::Log, 1, false},
  {"shape", "SHAPE", 0.f,  100.f, 0.f,  "%",     Curve::Lin, 0, false},
};
inline constexpr SourceDesc LFO_DESC = {"lfo", "LFO", "tempo synced · 8 beats", nullptr, 0, {nullptr, nullptr}, 2, LFO_PARAMS};

struct Lfo : Source {
  float ph = 0;
  const SourceDesc& desc() const override { return LFO_DESC; }
  float update(const SourceContext& c) override {
    ph += c.dt * c.bpm / 60.f / knob(c, 0); ph -= floorf(ph);
    const float s = knob(c, 1) * 0.01f, sine = sinf(2 * PI_F * ph);
    if (s <= 0.f) return sine;
    const float tri = ph < 0.25f ? 4 * ph : ph < 0.75f ? 2 - 4 * ph : 4 * ph - 4;   // same phase as the sine
    if (s <= 0.5f) return sine + (tri - sine) * 2 * s;
    const float sq = ph < 0.5f ? 1.f : -1.f;
    return tri + (sq - tri) * (2 * s - 1);
  }
};

} // namespace rig

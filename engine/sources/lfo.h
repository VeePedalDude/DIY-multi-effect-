// engine/sources/lfo.h - sine LFO, one cycle per 8 beats (0.25 Hz at 120 bpm).
#pragma once
#include "../core/source.h"

namespace rig {

inline constexpr SourceDesc LFO_DESC = {"lfo", "LFO", "tempo synced · 8 beats", nullptr, 0, {nullptr, nullptr}};

struct Lfo : Source {
  float ph = 0;
  const SourceDesc& desc() const override { return LFO_DESC; }
  float update(const SourceContext& c) override {
    ph += c.dt * c.bpm / 60.f / 8.f; ph -= floorf(ph);
    return sinf(2 * PI_F * ph);
  }
};

} // namespace rig

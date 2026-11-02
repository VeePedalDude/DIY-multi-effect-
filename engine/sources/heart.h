// engine/sources/heart.h - heartbeat (QRS-shaped). Uses a strap's bpm from `ext heart <bpm>`
// while fresh (< 5 s old), otherwise a simulated 72 bpm.
#pragma once
#include "../core/source.h"

namespace rig {

inline constexpr SourceDesc HEART_DESC = {"heart", "HEART RATE", "simulated 72 bpm", "beats per minute from a strap", 1, {"bpm", nullptr}};

struct Heart : Source {
  float ph = 0, extBpm = 0, extAge = 99, bpm = 72;
  const SourceDesc& desc() const override { return HEART_DESC; }
  static float shape(float u) {
    if (u > .27f && u < .30f) return -.45f;
    if (u >= .30f && u < .33f) return 1.f;
    if (u >= .33f && u < .36f) return -.85f;
    if (u > .50f && u < .62f) return -.15f + .3f * sinf(PI_F * (u - .5f) / .12f);
    return -.15f;
  }
  float update(const SourceContext& c) override {
    extAge += c.dt;
    bpm = (extAge < 5.f && extBpm > 30.f) ? extBpm : 72.f;
    ph += c.dt * bpm / 60.f; ph -= floorf(ph);
    return shape(ph);
  }
  void ext(float v) override { extBpm = v; extAge = 0; }
  void meters(float* out) const override { out[0] = bpm; }
};

} // namespace rig

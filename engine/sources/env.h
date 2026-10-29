// engine/sources/env.h - envelope follower on the dry input. FLOOR dBFS -> -1, 0 dBFS -> +1
// (default -60 dB). ATTACK and RELEASE set how fast it follows.
#pragma once
#include "../core/source.h"

namespace rig {

inline constexpr ParamDesc ENV_PARAMS[] = {
  {"attack",  "ATTACK",  0.25f, 100.f,  0.5f, "ms", Curve::Log, 1, false},   // 5 ms
  {"release", "RELEASE", 15.f,  1500.f, 0.5f, "ms", Curve::Log, 0, false},   // 150 ms
  {"floor",   "FLOOR",   -90.f, -30.f,  0.5f, "dB", Curve::Lin, 0, false},   // -60 dB
};
inline constexpr SourceDesc ENV_DESC = {"env", "ENV FOLLOW", "pick dynamics", nullptr, 0, {nullptr, nullptr}, 3, ENV_PARAMS};

struct EnvFollow : Source {
  float env = 0;
  const SourceDesc& desc() const override { return ENV_DESC; }
  float update(const SourceContext& c) override {
    float eA = 1.f - expf(-1.f / (knob(c, 0) / 1000.f * sr)), eR = 1.f - expf(-1.f / (knob(c, 1) / 1000.f * sr));
    for (int i = 0; i < c.n; i++) { float ax = fabsf(c.in[i]); env += (ax > env ? eA : eR) * (ax - env); }
    env = undenorm(env);
    float edb = 20.f * log10f(env + 1e-9f);
    const float floorDb = knob(c, 2);
    return clampf((edb - floorDb) / (-0.5f * floorDb) - 1.f, -1.f, 1.f);
  }
};

} // namespace rig

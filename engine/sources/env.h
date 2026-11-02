// engine/sources/env.h - envelope follower on the dry input: -60 dBFS -> -1, 0 dBFS -> +1.
#pragma once
#include "../core/source.h"

namespace rig {

inline constexpr SourceDesc ENV_DESC = {"env", "ENV FOLLOW", "pick dynamics", nullptr, 0, {nullptr, nullptr}};

struct EnvFollow : Source {
  float env = 0;
  const SourceDesc& desc() const override { return ENV_DESC; }
  float update(const SourceContext& c) override {
    float eA = 1.f - expf(-1.f / (0.005f * sr)), eR = 1.f - expf(-1.f / (0.150f * sr));
    for (int i = 0; i < c.n; i++) { float ax = fabsf(c.in[i]); env += (ax > env ? eA : eR) * (ax - env); }
    env = undenorm(env);
    float edb = 20.f * log10f(env + 1e-9f);
    return clampf((edb + 60.f) / 30.f - 1.f, -1.f, 1.f);
  }
};

} // namespace rig

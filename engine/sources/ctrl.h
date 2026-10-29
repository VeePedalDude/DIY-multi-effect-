// engine/sources/ctrl.h - external controller value (gamepad stick, slider, any sensor) via `ext ctrl <-1..1>`, smoothed.
#pragma once
#include "../core/source.h"

namespace rig {

inline constexpr ParamDesc CTRL_PARAMS[] = {
  {"smooth",   "SMOOTH",    1.f, 900.f, 0.5f, "ms", Curve::Log, 0, false},   // 30 ms
  {"deadzone", "DEAD ZONE", 0.f, 50.f,  0.f,  "%",  Curve::Lin, 0, false},   // a drifting stick's centre reads 0
};
inline constexpr SourceDesc CTRL_DESC = {"ctrl", "CONTROLLER", "gamepad stick / slider", "value -1..1", 0, {nullptr, nullptr}, 2, CTRL_PARAMS};

struct Ctrl : Source {
  float target = 0, value = 0;
  const SourceDesc& desc() const override { return CTRL_DESC; }
  float update(const SourceContext& c) override {
    const float dz = knob(c, 1) / 100.f, a = fabsf(target);
    const float t = a <= dz ? 0.f : (target > 0 ? 1.f : -1.f) * (a - dz) / (1.f - dz);   // rescaled past the dead zone
    value += (1.f - expf(-c.dt / (knob(c, 0) / 1000.f))) * (t - value);
    return clampf(value, -1.f, 1.f);
  }
  void ext(float v) override { target = clampf(v, -1.f, 1.f); }
};

} // namespace rig

// engine/sources/ctrl.h - external controller value (gamepad stick, slider, any sensor) via `ext ctrl <-1..1>`, smoothed.
#pragma once
#include "../core/source.h"

namespace rig {

inline constexpr SourceDesc CTRL_DESC = {"ctrl", "CONTROLLER", "gamepad stick / slider", "value -1..1", 0, {nullptr, nullptr}};

struct Ctrl : Source {
  float target = 0, value = 0;
  const SourceDesc& desc() const override { return CTRL_DESC; }
  float update(const SourceContext& c) override {
    value += (1.f - expf(-c.dt / 0.03f)) * (target - value);
    return clampf(value, -1.f, 1.f);
  }
  void ext(float v) override { target = clampf(v, -1.f, 1.f); }
};

} // namespace rig

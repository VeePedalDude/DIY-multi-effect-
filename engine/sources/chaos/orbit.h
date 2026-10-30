// engine/sources/chaos/orbit.h - three-body orbit (perturbed figure-eight); output = distance between bodies 0 and 1.
#pragma once
#include "../../core/source.h"

namespace rig {

inline constexpr ParamDesc ORBIT_PARAMS[] = {
  {"speed", "SPEED", 0.1f, 10.f, 0.5f, "x", Curve::Log, 2, false},   // 1x
};
inline constexpr SourceDesc ORBIT_DESC = {"orbit", "THREE-BODY", "chaotic orbit", nullptr, 0, {nullptr, nullptr}, 1, ORBIT_PARAMS};

struct Orbit : Source {
  float px[3], py[3], vx[3], vy[3];
  const SourceDesc& desc() const override { return ORBIT_DESC; }
  void init(float s) override { sr = s; resetBodies(); }
  void resetBodies() {
    const float x0 = 0.97000436f, y0 = -0.24308753f, vx3 = -0.93240737f, vy3 = -0.86473146f;
    px[0] = -x0 + 0.02f; py[0] = -y0; px[1] = x0; py[1] = y0; px[2] = 0; py[2] = 0;
    vx[2] = vx3; vy[2] = vy3; vx[0] = vx[1] = -vx3 / 2; vy[0] = vy[1] = -vy3 / 2;
  }
  void step(float dt) {
    float ax[3] = {0}, ay[3] = {0};
    for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) if (a != b) {
      float dx = px[b] - px[a], dy = py[b] - py[a], r2 = dx * dx + dy * dy + 0.0025f;
      float inv = 1.f / (r2 * sqrtf(r2));
      ax[a] += dx * inv; ay[a] += dy * inv;
    }
    for (int a = 0; a < 3; a++) { vx[a] += ax[a] * dt; vy[a] += ay[a] * dt; }
    for (int a = 0; a < 3; a++) { px[a] += vx[a] * dt; py[a] += vy[a] * dt; }
    float cx = (px[0] + px[1] + px[2]) / 3, cy = (py[0] + py[1] + py[2]) / 3;
    for (int a = 0; a < 3; a++) if (fabsf(px[a] - cx) > 4 || fabsf(py[a] - cy) > 4) { resetBodies(); break; }
  }
  float update(const SourceContext& c) override {
    const float span = c.dt * knob(c, 0);                     // SPEED scales simulated time
    for (float acc = 0; acc < span; acc += 0.002f) step(0.002f);
    float dx = px[0] - px[1], dy = py[0] - py[1];
    return tanhf((sqrtf(dx * dx + dy * dy) - 1.2f) * 1.5f);
  }
};

} // namespace rig

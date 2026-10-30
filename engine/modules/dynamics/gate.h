// engine/modules/dynamics/gate.h - noise gate with hysteresis and hold.
#pragma once
#include "../../core/module.h"

namespace rig {

inline constexpr ParamDesc GATE_PARAMS[] = {
  {"thresh",  "THRESH",  -80.f, -20.f, 0.35f, "dB", Curve::Lin, 0, false},
  {"release", "RELEASE",  20.f, 500.f, 0.40f, "ms", Curve::Lin, 0, false},
};
inline constexpr ModuleDesc GATE_DESC = {"gate", "NOISE GATE", "utility", 2, GATE_PARAMS, 1, {"open", nullptr}};

struct Gate : Module {
  float env = 0, gain = 0, hold = 0; bool open = false;
  OnePole thS;
  const ModuleDesc& desc() const override { return GATE_DESC; }
  void init(float s) override { sr = s; thS.setMs(sr, 30); thS.y = GATE_PARAMS[0].def; reset(); }
  void reset() override { env = 0; gain = 0; hold = 0; open = false; }
  void process(const float* inL, const float* inR, float* oL, float* oR, int n, const float* e, const Context&) override {
    float aAtt = 1.f - expf(-1.f / (0.0005f * sr)), aRel = 1.f - expf(-1.f / (0.050f * sr));
    float relMs = map(1, e[1]);
    float gUp = 1.f - expf(-1.f / (0.001f * sr)), gDn = 1.f - expf(-1.f / (0.001f * relMs * sr));
    for (int i = 0; i < n; i++) {
      float th = db2lin(map(0, thS.next(e[0])));
      float x = inL[i], ax = fabsf(x);
      env += (ax > env ? aAtt : aRel) * (ax - env);
      if (env > th) { open = true; hold = 0.010f * sr; }
      else if (env < th * 0.5f) { if (hold > 0) hold -= 1; else open = false; }   // 6 dB hysteresis
      float tgt = open ? 1.f : 0.f;
      gain += (tgt > gain ? gUp : gDn) * (tgt - gain);
      oL[i] = x * gain; oR[i] = inR[i] * gain;
    }
  }
  void meters(float* out) const override { out[0] = open ? 1.f : 0.f; }
};

} // namespace rig

// engine/modules/delay/holo.h - Hologram delay: tempo-synced dotted 8th with a diffused "halo" cloud.
#pragma once
#include "../../core/module.h"

namespace rig {

inline constexpr ParamDesc HOLO_PARAMS[] = {
  {"feedback", "FEEDBACK", 0.f,  95.f, 0.45f, "%", Curve::Lin, 0, false},
  {"halo",     "HALO",     0.f, 100.f, 0.60f, "%", Curve::Lin, 0, false},
};
inline constexpr ModuleDesc HOLO_DESC = {"holo", "HOLOGRAM", "delay · dotted 8th", 2, HOLO_PARAMS, 1, {"delay_ms", nullptr}};

struct Holo : Module {
  DelayLine<262144> line;
  Allpass ap1, ap2;
  Biquad lp, hp;
  OnePole timeS, fbS, haloS;
  float ph1 = 0, ph2 = 0, curMs = 375;
  const ModuleDesc& desc() const override { return HOLO_DESC; }
  void init(float s) override {
    sr = s; lp.lp(sr, 4500, 0.6f); hp.hp(sr, 120, 0.6f);
    timeS.setMs(sr, 150); fbS.setMs(sr, 30); haloS.setMs(sr, 30);
    timeS.y = 0.375f * sr; fbS.y = HOLO_PARAMS[0].def; haloS.y = HOLO_PARAMS[1].def;
    ap1.init((int)(0.0113f * sr), 0.6f); ap2.init((int)(0.0171f * sr), 0.55f);
    reset();
  }
  void reset() override { line.clear(); ap1.clear(); ap2.clear(); lp.reset(); hp.reset(); }
  void process(const float* inL, const float* inR, float* oL, float* oR, int n, const float* e, const Context& ctx) override {
    float secs = clampf(0.75f * 60.f / ctx.bpm, 0.05f, 2.4f);
    curMs = secs * 1000.f;
    float target = secs * sr, i1 = 0.31f / sr, i2 = 0.19f / sr;
    for (int i = 0; i < n; i++) {
      float d = timeS.next(target), fb = map(0, fbS.next(e[0])) * 0.01f, h = map(1, haloS.next(e[1])) * 0.01f;
      ph1 += i1; if (ph1 >= 1) ph1 -= 1;
      ph2 += i2; if (ph2 >= 1) ph2 -= 1;
      float s1 = sinf(2 * PI_F * ph1), s2 = sinf(2 * PI_F * ph2);
      float y = line.tap(d + 1.f + h * 0.0006f * sr * (1.f + s1));
      float a = line.tap(d * 1.5f + 1.f + h * 0.003f * sr * (1.f + s2));
      float b = line.tap(d * 0.5f + 1.f + h * 0.003f * sr * (1.f - s1));
      float diff = ap2.tick(ap1.tick(0.5f * (a + b)));
      float loop = hp.tick(lp.tick((1.f - h) * y + h * diff));
      float xin = 0.5f * (inL[i] + inR[i]);
      line.push(undenorm(xin + fb * tanhf(loop)));
      float wl = y + 0.6f * h * a, wr = y + 0.6f * h * b;
      oL[i] = inL[i] + 0.4f * wl;
      oR[i] = inR[i] + 0.4f * wr;
    }
  }
  void meters(float* out) const override { out[0] = curMs; }
};

} // namespace rig

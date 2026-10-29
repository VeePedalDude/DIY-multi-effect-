// engine/modules/amp.h - waveshaper amp, the stand-in for the NAM A2 slot.
// Shaper is oversampled (os x) and uses 1st-order antiderivative anti-aliasing (ADAA).
#pragma once
#include "../core/module.h"

namespace rig {

inline constexpr ParamDesc AMP_PARAMS[] = {
  {"input",  "INPUT",  -12.f, 24.f, 0.60f, "dB", Curve::Lin, 1, true},
  {"output", "OUTPUT", -18.f,  6.f, 0.50f, "dB", Curve::Lin, 1, true},
};
// NAM slot: captures can be chosen, searched and saved in presets now; ready = false until the NAM engine is compiled in.
inline constexpr ModuleDesc AMP_DESC = {"amp", "AMP", "stand-in · NAM A2 slot", 2, AMP_PARAMS, 0, {nullptr, nullptr},
                                        {"nam", "NAM capture", ".nam", false, 0}};

struct Amp : Module {
  Biquad hp, tight, scoop, post, up[3], dn[3];
  DCBlock dc;
  OnePole inS, outS;
  int os = 2; bool adaa = true;
  double xPrev = 0, fPrev = 0;
  static constexpr float BIAS = 0.15f;
  static constexpr float DRIVE_BASE_DB = 18.f;    // INPUT 0 dB = +18 dB into the shaper
  static constexpr float MAKEUP = 1.0f;
  const ModuleDesc& desc() const override { return AMP_DESC; }
  void init(float s) override {
    sr = s;
    hp.hp(sr, 90, 0.707f); tight.peak(sr, 900, 0.7f, 6.f);
    scoop.peak(sr, 420, 0.8f, -4.f); post.lp(sr, 7000, 0.707f);
    setOS(os);
    inS.setMs(sr, 20); outS.setMs(sr, 20); inS.y = AMP_PARAMS[0].def; outS.y = AMP_PARAMS[1].def;
    reset();
  }
  void setOS(int o) {
    os = o < 1 ? 1 : o;
    const float q[3] = {0.5176f, 0.7071f, 1.9319f};       // 6th-order Butterworth at os x rate
    for (int k = 0; k < 3; k++) { up[k].lp(os * sr, 19000, q[k]); dn[k].lp(os * sr, 19000, q[k]); }
  }
  void reset() override {
    hp.reset(); tight.reset(); scoop.reset(); post.reset(); dc.reset();
    for (int k = 0; k < 3; k++) { up[k].reset(); dn[k].reset(); }
    xPrev = 0; fPrev = F(0);
  }
  static float shape(float x) { return tanhf(x + BIAS) - tanhf(BIAS); }
  static double logcosh(double x) { double a = fabs(x); return a + log1p(exp(-2.0 * a)) - 0.69314718055994531; }
  static double F(double x) { return logcosh(x + BIAS) - x * tanh((double)BIAS); }   // antiderivative of shape
  float shapeAA(float xf) {
    double x = xf, dx = x - xPrev, Fx = F(x), y;
    if (fabs(dx) > 1e-5) y = (Fx - fPrev) / dx;
    else y = shape((float)(0.5 * (x + xPrev)));
    xPrev = x; fPrev = Fx;
    return (float)y;
  }
  void process(const float* inL, const float*, float* oL, float* oR, int n, const float* e, const Context&) override {
    for (int i = 0; i < n; i++) {
      float drive = db2lin(DRIVE_BASE_DB + map(0, inS.next(e[0])));
      float outG  = db2lin(map(1, outS.next(e[1])));
      float x = tight.tick(hp.tick(inL[i])) * drive;
      float y = 0;
      for (int ph = 0; ph < os; ph++) {                    // zero-stuff, filter, shape, filter, decimate
        float u = ph == 0 ? (float)os * x : 0.f;
        if (os > 1) for (int k = 0; k < 3; k++) u = up[k].tick(u);
        u = adaa ? shapeAA(u) : shape(u);
        if (os > 1) for (int k = 0; k < 3; k++) u = dn[k].tick(u);
        if (ph == 0) y = u;
      }
      y = post.tick(scoop.tick(dc.tick(y))) * MAKEUP * outG;
      oL[i] = y; oR[i] = y;
    }
  }
};

} // namespace rig

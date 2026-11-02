// engine/modules/cab.h - cabinet impulse response (direct FIR, up to 2048 taps).
#pragma once
#include "../core/module.h"

namespace rig {

inline constexpr ParamDesc CAB_PARAMS[] = {
  {"locut", "LO CUT", 20.f, 399.0525f, 0.25f, "Hz", Curve::Log, 0, false},
  {"mix",   "MIX",     0.f, 100.f,   0.90f, "%",  Curve::Lin, 0, false},
};
// Asset payload: mono float32 samples at the engine sample rate (the host decodes and resamples the .wav).
inline constexpr ModuleDesc CAB_DESC = {"cab", "CAB IR", "built-in cab", 2, CAB_PARAMS, 1, {"taps", nullptr},
                                        {"ir", "Cab IR", ".wav", true, 2048 * 4}};

struct Cab : Module {
  static constexpr int MAXIR = 2048;
  float ir[MAXIR], load[MAXIR], hist[2 * MAXIR];
  int len = 1, p = 0;
  Biquad lc; float lcHz = -1;
  OnePole mixS;
  const ModuleDesc& desc() const override { return CAB_DESC; }
  void init(float s) override { sr = s; mixS.setMs(sr, 20); mixS.y = CAB_PARAMS[1].def; makeDefault(); }
  void reset() override { memset(hist, 0, sizeof hist); p = 0; lc.reset(); }
  void makeDefault() {                                     // synthetic closed-back 4x12-ish response
    Biquad f[6];
    f[0].hp(sr, 75, 0.707f); f[1].peak(sr, 115, 1.0f, 5.f); f[2].peak(sr, 900, 1.0f, -4.f);
    f[3].peak(sr, 2600, 1.3f, 4.f); f[4].lp(sr, 5200, 0.6f); f[5].lp(sr, 6500, 1.1f);
    int n = (int)(0.021f * sr); if (n > MAXIR) n = MAXIR;
    int r1 = (int)(0.0006f * sr), r2 = (int)(0.0013f * sr);
    for (int i = 0; i < n; i++) {
      float x = (i == 0) ? 1.f : 0.f;
      if (i == r1) x += 0.25f;
      if (i == r2) x -= 0.15f;
      for (auto& b : f) x = b.tick(x);
      float fade = i > n - 64 ? (float)(n - i) / 64.f : 1.f;
      load[i] = x * fade;
    }
    commit(n);
  }
  void commit(int n) {                                     // energy-normalise so IRs swap at similar loudness
    if (n < 1) return;
    if (n > MAXIR) n = MAXIR;
    double e = 0; for (int i = 0; i < n; i++) e += (double)load[i] * load[i];
    float g = e > 1e-12 ? (float)(0.21 / sqrt(e)) : 0.f;
    for (int i = 0; i < n; i++) ir[i] = load[i] * g;
    len = n; reset();
  }
  void process(const float* inL, const float*, float* oL, float* oR, int n, const float* e, const Context&) override {
    float hz = map(0, e[0]);
    if (fabsf(hz - lcHz) > 0.5f) { lc.hp(sr, hz, 0.707f); lcHz = hz; }
    for (int i = 0; i < n; i++) {
      float x = inL[i];
      p = (p == 0) ? len - 1 : p - 1;                      // window hist[p..p+len) = newest..oldest
      hist[p] = x; hist[p + len] = x;
      const float* h = hist + p;
      float a0 = 0, a1 = 0, a2 = 0, a3 = 0;
      int k = 0;
      for (; k + 3 < len; k += 4) {
        a0 += ir[k] * h[k]; a1 += ir[k + 1] * h[k + 1];
        a2 += ir[k + 2] * h[k + 2]; a3 += ir[k + 3] * h[k + 3];
      }
      for (; k < len; k++) a0 += ir[k] * h[k];
      float wet = (a0 + a1) + (a2 + a3);
      float m = map(1, mixS.next(e[1])) * 0.01f;
      float y = lc.tick(wet * m + x * 0.3f * (1.f - m));
      oL[i] = y; oR[i] = y;
    }
  }
  void meters(float* out) const override { out[0] = (float)len; }
  unsigned char* assetBuffer() override { return reinterpret_cast<unsigned char*>(load); }
  int assetCommit(int bytes) override {
    int n = bytes / 4;
    if (n < 1) return -1;
    commit(n);
    return 0;
  }
  void assetClear() override { makeDefault(); }
};

} // namespace rig

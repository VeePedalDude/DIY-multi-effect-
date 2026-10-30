// engine/modules/reverb/tess.h - Tesseract reverb: 8-line FDN whose matrix rotates the two 4-D halves.
#pragma once
#include "../../core/module.h"

namespace rig {

inline constexpr ParamDesc TESS_PARAMS[] = {
  {"size", "SIZE",    0.5f, 7.5f, 0.70f, "s", Curve::Lin, 1, false},
  {"fold", "4D FOLD", 0.f,  1.f,  0.30f, "",  Curve::Lin, 2, false},
};
inline constexpr ModuleDesc TESS_DESC = {"tess", "TESSERACT", "reverb · 8-line FDN", 2, TESS_PARAMS, 0, {nullptr, nullptr}};

struct Tess : Module {
  static constexpr int L = 8;
  DelayLine<8192> line[L];
  DelayLine<8192> pre;
  Allpass dif1, dif2;
  float len[L], g[L], lpz[L], lpa = 0.5f, t = 0, lastRt = -1;
  OnePole foldS;
  const ModuleDesc& desc() const override { return TESS_DESC; }
  void init(float s) override {
    sr = s;
    const float ms[L] = {29.7f, 37.1f, 41.1f, 43.7f, 53.3f, 59.9f, 67.1f, 79.3f};
    for (int k = 0; k < L; k++) len[k] = ms[k] * 0.001f * sr;
    lpa = 1.f - expf(-2.f * PI_F * 7000.f / sr);
    dif1.init((int)(0.0047f * sr), 0.65f); dif2.init((int)(0.0083f * sr), 0.6f);
    foldS.setMs(sr, 50); foldS.y = TESS_PARAMS[1].def;
    reset();
  }
  void reset() override {
    for (auto& l : line) l.clear();
    pre.clear(); dif1.clear(); dif2.clear();
    memset(lpz, 0, sizeof lpz);
  }
  void setRt(float rt) {
    if (fabsf(rt - lastRt) < 1e-3f) return;
    for (int k = 0; k < L; k++) g[k] = powf(10.f, -3.f * (len[k] / sr) / rt);
    lastRt = rt;
  }
  static void wht8(float* v) {                         // fast Walsh-Hadamard, orthonormal
    for (int hlen = 1; hlen < 8; hlen <<= 1)
      for (int i = 0; i < 8; i += hlen << 1)
        for (int j = i; j < i + hlen; j++) { float a = v[j], b = v[j + hlen]; v[j] = a + b; v[j + hlen] = a - b; }
    const float s = 0.35355339f;
    for (int i = 0; i < 8; i++) v[i] *= s;
  }
  void process(const float* inL, const float* inR, float* oL, float* oR, int n, const float* e, const Context&) override {
    setRt(map(0, e[0]));
    float fold = map(1, foldS.y);
    for (int i = 0; i < n; i++) foldS.next(e[1]);
    t += (float)n / sr;
    float th = fold * (PI_F * 0.25f + 0.3f * sinf(2 * PI_F * 0.05f * t));
    float c = cosf(th), sn = sinf(th);
    float m0 = 0.0003f * sr * sinf(2 * PI_F * 0.7f * t), m5 = 0.0003f * sr * sinf(2 * PI_F * 0.53f * t + 1.f);
    const float sign[L] = {1, -1, 1, -1, 1, -1, 1, -1};
    for (int i = 0; i < n; i++) {
      float x = 0.5f * (inL[i] + inR[i]);
      pre.push(x);
      float d = dif2.tick(dif1.tick(pre.tap(0.012f * sr)));
      float v[L];
      for (int k = 0; k < L; k++) {
        float dl = len[k] + (k == 0 ? m0 : (k == 5 ? m5 : 0.f));
        float r = line[k].tap(dl);
        lpz[k] += lpa * (r - lpz[k]); lpz[k] = undenorm(lpz[k]);
        v[k] = lpz[k] * g[k];
      }
      float wl = 0.5f * (v[0] - v[2] + v[4] - v[6]);
      float wr = 0.5f * (v[1] - v[3] + v[5] - v[7]);
      for (int k = 0; k < 4; k++) {                      // rotate the two 4-D halves into each other
        float a = v[k], b = v[k + 4];
        v[k] = c * a - sn * b; v[k + 4] = sn * a + c * b;
      }
      wht8(v);
      for (int k = 0; k < L; k++) line[k].push(undenorm(v[k] + 0.35f * sign[k] * d));
      oL[i] = inL[i] + 0.3f * wl;
      oR[i] = inR[i] + 0.3f * wr;
    }
  }
};

} // namespace rig

// engine/modules/synth/sallen.h - SALLEN-KEY: a high-pass then a low-pass, each a two-pole
// Sallen-Key with its resonance fed back through clipping diodes, as in the Korg MS-20. Push a PEAK
// past 85% and it screams, and the clipping keeps the scream in check rather than running away.
// HP CUT 20 Hz-2 kHz and HP PEAK 0-100%, LP CUT 20 Hz-20 kHz and LP PEAK 0-100%, ENV +-100% (moves
// the low-pass, up to 4 octaves), DRIVE 0-24 dB, LEVEL +-12 dB. Default: high-pass open at 20 Hz,
// low-pass at 2 kHz with 30% peak. Meters: "cutoff" (the low-pass, Hz), "env".
//
// Each section: two one-poles with positive feedback k (y1 - y2) into the input, which gives
// LP 1/(s^2 + (2-k) s + 1) and HP s^2/(s^2 + (2-k) s + 1): it oscillates from k = 2. The feedback
// passes a soft clipper, solved together with the filter each sample (zero-delay feedback).
#pragma once
#include "../../core/module.h"
#include "../../core/vaf.h"

namespace rig {

inline constexpr ParamDesc SALLEN_PARAMS[] = {
  {"hpcut",  "HP CUT",  20.f, 2000.f,  0.f,    "Hz", Curve::Log, 0, false},
  {"hppeak", "HP PEAK", 0.f,  100.f,   0.f,    "%",  Curve::Lin, 0, false},
  {"lpcut",  "LP CUT",  20.f, 20000.f, 0.667f, "Hz", Curve::Log, 0, false},
  {"lppeak", "LP PEAK", 0.f,  100.f,   0.3f,   "%",  Curve::Lin, 0, false},
  {"env",    "ENV",    -100.f, 100.f,  0.5f,   "%",  Curve::Lin, 0, true},
  {"drive",  "DRIVE",   0.f,  24.f,    0.f,    "dB", Curve::Lin, 1, false},
  {"level",  "LEVEL",  -12.f, 12.f,    0.5f,   "dB", Curve::Lin, 1, true},
};
inline constexpr ModuleDesc SALLEN_DESC = {"sallen", "SALLEN-KEY", "synth filter · MS-20 style HP + LP", 7, SALLEN_PARAMS, 2, {"cutoff", "env"}};
inline constexpr float SALLEN_KMAX = 2.3f;            // past the k = 2 where it oscillates; the clipper holds it

struct SkSection {
  float G = 0.5f, s1[2] = {0, 0}, s2[2] = {0, 0};
  void set(float g) { G = g / (1.f + g); }
  // Returns {lp, hp} for one sample.
  void tick(int c, float x, float k, float& lp, float& hp) {
    const float b = 1.f - G, a = G - G * G, cc = b * (1.f - G) * s1[c] - b * s2[c];
    // u = x + sat(k (a u + cc)): Newton from the linear solution
    float u = (x + k * cc) / fmaxf(1.f - k * a, 0.05f);
    for (int it = 0; it < 3; it++) { const float arg = k * (a * u + cc), f = u - x - vaSat(arg), df = 1.f - vaSatSlope(arg) * k * a; u -= f / (fabsf(df) < 0.05f ? (df < 0 ? -0.05f : 0.05f) : df); }
    const float v1 = (u - s1[c]) * G, y1 = v1 + s1[c]; s1[c] = undenorm(y1 + v1);
    const float v2 = (y1 - s2[c]) * G, y2 = v2 + s2[c]; s2[c] = undenorm(y2 + v2);
    lp = y2; hp = u - 2.f * y1 + y2;
  }
  void reset() { s1[0] = s1[1] = s2[0] = s2[1] = 0; }
};

struct Sallen : Module {
  SkSection hpS, lpS; Os2 os; VaNoise nz; StepSmooth s[7]; EnvAmount env; float kh = 0, kl = 0, drv = 1, outG = 1, hz = 2000; int cnt = 0;
  const ModuleDesc& desc() const override { return SALLEN_DESC; }
  void init(float x) override { sr = x; os.init(sr); reset(); }
  void reset() override { for (int i = 0; i < 7; i++) s[i].init(sr, 20, SALLEN_PARAMS[i].def); env.init(sr); hpS.reset(); lpS.reset(); os.reset(); cnt = 0; }
  void process(const float* inL, const float* inR, float* oL, float* oR, int n, const float* e, const Context&) override {
    for (int i = 0; i < n; i++) {
      env.tick(inL[i], inR[i]);
      if (cnt-- <= 0) {
        cnt = EQ_STEP - 1; float v[7]; for (int j = 0; j < 7; j++) v[j] = s[j].next(clampf(e[j], 0, 1));
        env.update(); hz = clampf(map(2, v[2]) * env.scale(map(4, v[4])), 10.f, fminf(20000.f, 0.45f * sr));
        hpS.set(vaG2(sr, map(0, v[0]))); lpS.set(vaG2(sr, hz));
        kh = SALLEN_KMAX * v[1]; kl = SALLEN_KMAX * v[3];
        drv = db2lin(map(5, v[5])); outG = db2lin(map(6, v[6])) / sqrtf(drv);
      }
      const float in[2] = {inL[i], inR[i]}; float out[2];
      for (int c = 0; c < 2; c++) out[c] = outG * os.run(c, in[c], [&](float u) {
        float lp, hp; hpS.tick(c, vaSat(u * drv), kh, lp, hp);
        float lp2, hp2; lpS.tick(c, hp + nz.next(), kl, lp2, hp2); return lp2;
      });
      oL[i] = out[0]; oR[i] = out[1];
    }
  }
  void meters(float* out) const override { out[0] = hz; out[1] = env.e; }
};

} // namespace rig

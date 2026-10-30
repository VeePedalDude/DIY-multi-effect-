// engine/modules/synth/svf.h - STATE VARIABLE: the two-pole multimode filter of the Oberheim SEM and
// many polysynths, 12 dB/octave. MODE sweeps smoothly low-pass -> notch -> high-pass -> band-pass;
// RESO goes from flat (Q 0.5) to a sharp, singing peak (Q 25), and, like the SEM, it never quite
// self-oscillates. CUTOFF 20 Hz-20 kHz; ENV moves it up to 4 octaves either way with your picking;
// DRIVE 0-24 dB into a soft saturator first; LEVEL +-12 dB. Default: low-pass at 1 kHz, 30%.
// Meters: "cutoff" (Hz, where it is now) and "env" (0..1, the level ENV reads).
// Andrew Simper's trapezoidal SVF (Cytomic, 2013): all four outputs from one structure; 2x oversampled
// so DRIVE stays clean.
#pragma once
#include "../../core/module.h"
#include "../../core/vaf.h"

namespace rig {

inline constexpr ParamDesc SVF_PARAMS[] = {
  {"cutoff", "CUTOFF", 20.f, 20000.f, 0.566f, "Hz", Curve::Log, 0, false},
  {"reso",   "RESO",   0.f,  100.f,   0.3f,   "%",  Curve::Lin, 0, false},
  {"mode",   "MODE",   0.f,  3.f,     0.f,    "",   Curve::Lin, 2, false},
  {"env",    "ENV",   -100.f, 100.f,  0.5f,   "%",  Curve::Lin, 0, true},
  {"drive",  "DRIVE",  0.f,  24.f,    0.f,    "dB", Curve::Lin, 1, false},
  {"level",  "LEVEL", -12.f, 12.f,    0.5f,   "dB", Curve::Lin, 1, true},
};
inline constexpr ModuleDesc SVF_DESC = {"svf", "STATE VARIABLE", "synth filter · LP/notch/HP/BP, 12 dB", 6, SVF_PARAMS, 2, {"cutoff", "env"}};
inline float svfQ(float reso) { return 0.5f * powf(50.f, reso); }     // RESO 0..1 -> Q 0.5..25
// Weights of LP, notch, HP and BP for MODE 0..3 (each neighbour pair crossfades).
inline void svfMix(float mode, float w[4]) { for (int i = 0; i < 4; i++) w[i] = fmaxf(0.f, 1.f - fabsf(mode - (float)i)); }

struct SvfFilter : Module {
  Os2 os; StepSmooth s[6]; EnvAmount env; float a1 = 1, a2 = 0, a3 = 0, k = 2, w[4] = {1, 0, 0, 0}, drv = 1, outG = 1, hz = 1000;
  float ic1[2] = {0, 0}, ic2[2] = {0, 0}; int cnt = 0;
  const ModuleDesc& desc() const override { return SVF_DESC; }
  void init(float x) override { sr = x; os.init(sr); reset(); }
  void reset() override { for (int i = 0; i < 6; i++) s[i].init(sr, 20, SVF_PARAMS[i].def); env.init(sr); ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0; os.reset(); cnt = 0; }
  void process(const float* inL, const float* inR, float* oL, float* oR, int n, const float* e, const Context&) override {
    for (int i = 0; i < n; i++) {
      env.tick(inL[i], inR[i]);
      if (cnt-- <= 0) {
        cnt = EQ_STEP - 1; float v[6]; for (int j = 0; j < 6; j++) v[j] = s[j].next(clampf(e[j], 0, 1));
        env.update(); hz = map(0, v[0]) * env.scale(map(3, v[3]));
        const float g = vaG2(sr, hz); k = 1.f / svfQ(v[1]);
        a1 = 1.f / (1.f + g * (g + k)); a2 = g * a1; a3 = g * a2;
        svfMix(map(2, v[2]), w); drv = db2lin(map(4, v[4])); outG = db2lin(map(5, v[5])) / drv;
        hz = clampf(hz, 10.f, fminf(20000.f, 0.45f * sr));
      }
      const float in[2] = {inL[i], inR[i]}; float out[2];
      for (int c = 0; c < 2; c++) out[c] = os.run(c, in[c], [&](float x) {
        const float v0 = vaSat(x * drv), v3 = v0 - ic2[c], v1 = a1 * ic1[c] + a2 * v3, v2 = ic2[c] + a2 * ic1[c] + a3 * v3;
        ic1[c] = undenorm(2 * v1 - ic1[c]); ic2[c] = undenorm(2 * v2 - ic2[c]);
        const float lp = v2, bp = k * v1, hp = v0 - k * v1 - v2, notch = lp + hp;
        return outG * (w[0] * lp + w[1] * notch + w[2] * hp + w[3] * bp);
      });
      oL[i] = out[0]; oR[i] = out[1];
    }
  }
  void meters(float* out) const override { out[0] = hz; out[1] = env.e; }
};

} // namespace rig

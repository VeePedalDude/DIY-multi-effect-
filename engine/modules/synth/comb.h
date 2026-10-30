// engine/modules/synth/comb.h - COMB: the signal fed back through a short delay tuned to a pitch,
// so it rings at FREQ and every harmonic of it; negative FEEDBACK rings an octave lower with only the
// odd harmonics (hollow, clarinet-like). FREQ 30 Hz-3 kHz, FEEDBACK -95-+95%, DAMP 0-100% (darkens
// each pass, so high harmonics die first), ENV +-100% (moves FREQ up to 4 octaves), MIX 0-100%.
// Default: 220 Hz, +70%, 20% damping, half wet. Meters: "cutoff" (FREQ now, Hz), "env".
// The wet signal is scaled by (1 - |FEEDBACK|), so its peaks stay near the input's level.
#pragma once
#include "../../core/module.h"
#include "../../core/vaf.h"

namespace rig {

inline constexpr ParamDesc COMB_PARAMS[] = {
  {"freq",     "FREQ",     30.f,  3000.f, 0.433f, "Hz", Curve::Log, 0, false},
  {"feedback", "FEEDBACK", -95.f, 95.f,   0.868f, "%",  Curve::Lin, 0, true},
  {"damp",     "DAMP",     0.f,   100.f,  0.2f,   "%",  Curve::Lin, 0, false},
  {"env",      "ENV",     -100.f, 100.f,  0.5f,   "%",  Curve::Lin, 0, true},
  {"mix",      "MIX",      0.f,   100.f,  0.5f,   "%",  Curve::Lin, 0, false},
};
inline constexpr ModuleDesc COMB_DESC = {"comb", "COMB", "synth filter · rings at a pitch", 5, COMB_PARAMS, 2, {"cutoff", "env"}};

struct Comb : Module {
  DelayLine<8192> d[2]; float lp[2] = {0, 0}; StepSmooth s[5]; EnvAmount env;
  float per = 218, fb = 0.7f, da = 0.2f, wet = 0.5f, wetG = 0.15f, hz = 220; int cnt = 0;
  const ModuleDesc& desc() const override { return COMB_DESC; }
  void init(float x) override { sr = x; reset(); }
  void reset() override { for (int i = 0; i < 5; i++) s[i].init(sr, 20, COMB_PARAMS[i].def); env.init(sr); d[0].clear(); d[1].clear(); lp[0] = lp[1] = 0; cnt = 0; }
  void process(const float* inL, const float* inR, float* oL, float* oR, int n, const float* e, const Context&) override {
    for (int i = 0; i < n; i++) {
      env.tick(inL[i], inR[i]);
      if (cnt-- <= 0) {
        cnt = EQ_STEP - 1; float v[5]; for (int j = 0; j < 5; j++) v[j] = s[j].next(clampf(e[j], 0, 1));
        env.update(); hz = clampf(map(0, v[0]) * env.scale(map(3, v[3])), 8.f, 0.25f * sr);
        per = clampf(sr / hz, 2.f, 8000.f); fb = map(1, v[1]) * 0.01f; da = 0.9f * v[2]; wet = v[4]; wetG = 1.f - fabsf(fb);
      }
      const float in[2] = {inL[i], inR[i]}; float out[2];
      for (int c = 0; c < 2; c++) {
        lp[c] = undenorm(lp[c] + (1.f - da) * (d[c].tap(per) - lp[c]));    // one-pole damping in the loop
        const float y = in[c] + fb * lp[c];
        d[c].push(y);
        out[c] = (1.f - wet) * in[c] + wet * wetG * y;
      }
      oL[i] = out[0]; oR[i] = out[1];
    }
  }
  void meters(float* out) const override { out[0] = hz; out[1] = env.e; }
};

} // namespace rig

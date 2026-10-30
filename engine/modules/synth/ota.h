// engine/modules/synth/ota.h - OTA FILTER: four one-pole stages from transconductance amps with
// resonance around all four, as in the Roland Juno and Jupiter (IR3109) and the Prophet-5 (CEM3320).
// POLES taps the cascade after 1-4 stages (6 to 24 dB/octave, smoothly in between); the passband
// keeps its level as RESO rises (half the make-up before the stages, half after, so the drive
// stays clean), and at 100% it sings on its own. CUTOFF 20 Hz-20 kHz, RESO 0-100%,
// ENV +-100% (up to 4 octaves), DRIVE 0-24 dB, LEVEL +-12 dB. Default: 4 poles at 1 kHz, 30%.
// Meters: "cutoff", "env".
#pragma once
#include "../../core/module.h"
#include "../../core/vaf.h"

namespace rig {

inline constexpr ParamDesc OTA_PARAMS[] = {
  {"cutoff", "CUTOFF", 20.f, 20000.f, 0.566f, "Hz", Curve::Log, 0, false},
  {"reso",   "RESO",   0.f,  100.f,   0.3f,   "%",  Curve::Lin, 0, false},
  {"poles",  "POLES",  1.f,  4.f,     1.f,    "",   Curve::Lin, 1, false},
  {"env",    "ENV",   -100.f, 100.f,  0.5f,   "%",  Curve::Lin, 0, true},
  {"drive",  "DRIVE",  0.f,  24.f,    0.f,    "dB", Curve::Lin, 1, false},
  {"level",  "LEVEL", -12.f, 12.f,    0.5f,   "dB", Curve::Lin, 1, true},
};
inline constexpr ModuleDesc OTA_DESC = {"ota", "OTA FILTER", "synth filter · 1-4 poles, Juno style", 6, OTA_PARAMS, 2, {"cutoff", "env"}};
inline constexpr float OTA_KMAX = 4.4f;                // a cascade of four oscillates from k = 4; 4.4 starts it quickly

struct Ota : Module {
  VaLoop<4> f; Os2 os; VaNoise nz; StepSmooth s[6]; EnvAmount env; float k = 0, comp = 1, drv = 1, outG = 1, poles = 4, hz = 1000; int cnt = 0;
  const ModuleDesc& desc() const override { return OTA_DESC; }
  void init(float x) override { sr = x; os.init(sr); vaCascade4(f); reset(); }
  void reset() override { for (int i = 0; i < 6; i++) s[i].init(sr, 20, OTA_PARAMS[i].def); env.init(sr); f.reset(); os.reset(); cnt = 0; }
  void process(const float* inL, const float* inR, float* oL, float* oR, int n, const float* e, const Context&) override {
    for (int i = 0; i < n; i++) {
      env.tick(inL[i], inR[i]);
      if (cnt-- <= 0) {
        cnt = EQ_STEP - 1; float v[6]; for (int j = 0; j < 6; j++) v[j] = s[j].next(clampf(e[j], 0, 1));
        env.update(); hz = clampf(map(0, v[0]) * env.scale(map(3, v[3])), 10.f, fminf(20000.f, 0.45f * sr)); f.set(vaG2(sr, hz));
        k = OTA_KMAX * v[1]; comp = sqrtf(1.f + k); poles = map(2, v[2]);   // half the make-up before the loop, half after
        drv = db2lin(map(4, v[4])); outG = db2lin(map(5, v[5])) / sqrtf(drv) * comp;
      }
      const int a = clampf(poles, 1.f, 4.f) >= 4.f ? 2 : (int)poles - 1; const float t = poles - 1.f - (float)a;
      const float ig = drv * comp;
      oL[i] = outG * os.run(0, inL[i], [&](float u) { const float* x = f.tick(0, u * ig + nz.next(), k); return x[a] + t * (x[a + 1] - x[a]); });
      oR[i] = outG * os.run(1, inR[i], [&](float u) { const float* x = f.tick(1, u * ig + nz.next(), k); return x[a] + t * (x[a + 1] - x[a]); });
    }
  }
  void meters(float* out) const override { out[0] = hz; out[1] = env.e; }
};

} // namespace rig

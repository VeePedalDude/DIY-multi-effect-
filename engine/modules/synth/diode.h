// engine/modules/synth/diode.h - DIODE LADDER: the filter of the TB-303 and the EMS synths. Its
// stages load each other, so it's softer below the peak than the Moog ladder and the resonance
// squelches; with ENV and a little DRIVE it's acid. From 100% RESO it self-oscillates.
// CUTOFF 20 Hz-20 kHz (where the peak sits), RESO 0-100%, ENV +-100% (up to 4 octaves), DRIVE
// 0-24 dB, LEVEL +-12 dB. Default: 1 kHz, 30%. Half the passband's loss at high RESO is made up
// (in dB) after the filter, so the bass still thins a little, like the original. Meters: "cutoff", "env".
// Linear model: T. Stinchcombe 2008 (see vaDiode4 in core/vaf.h); saturated zero-delay feedback.
#pragma once
#include "../../core/module.h"
#include "../../core/vaf.h"

namespace rig {

inline constexpr ParamDesc DIODE_PARAMS[] = {
  {"cutoff", "CUTOFF", 20.f, 20000.f, 0.566f, "Hz", Curve::Log, 0, false},
  {"reso",   "RESO",   0.f,  100.f,   0.3f,   "%",  Curve::Lin, 0, false},
  {"env",    "ENV",   -100.f, 100.f,  0.5f,   "%",  Curve::Lin, 0, true},
  {"drive",  "DRIVE",  0.f,  24.f,    0.f,    "dB", Curve::Lin, 1, false},
  {"level",  "LEVEL", -12.f, 12.f,    0.5f,   "dB", Curve::Lin, 1, true},
};
inline constexpr ModuleDesc DIODE_DESC = {"diode", "DIODE LADDER", "synth filter · 303 style squelch", 5, DIODE_PARAMS, 2, {"cutoff", "env"}};
inline constexpr float DIODE_KMAX = 1.08f * VA_DIODE_K;   // oscillates from RESO 93%

struct Diode : Module {
  VaLoop<4> f; Os2 os; VaNoise nz; StepSmooth s[5]; EnvAmount env; float k = 0, inG = 1, outG = 1, hz = 1000; int cnt = 0;
  const ModuleDesc& desc() const override { return DIODE_DESC; }
  void init(float x) override { sr = x; os.init(sr); vaDiode4(f); reset(); }
  void reset() override { for (int i = 0; i < 5; i++) s[i].init(sr, 20, DIODE_PARAMS[i].def); env.init(sr); f.reset(); os.reset(); cnt = 0; }
  void process(const float* inL, const float* inR, float* oL, float* oR, int n, const float* e, const Context&) override {
    for (int i = 0; i < n; i++) {
      env.tick(inL[i], inR[i]);
      if (cnt-- <= 0) {
        cnt = EQ_STEP - 1; float v[5]; for (int j = 0; j < 5; j++) v[j] = s[j].next(clampf(e[j], 0, 1));
        env.update(); hz = clampf(map(0, v[0]) * env.scale(map(2, v[2])), 10.f, fminf(20000.f, 0.45f * sr));
        f.set(vaG2(sr, hz) / VA_DIODE_W);                 // so the peak lands on CUTOFF
        k = DIODE_KMAX * v[1]; const float drv = db2lin(map(3, v[3]));
        inG = drv; outG = db2lin(map(4, v[4])) / sqrtf(drv) * sqrtf(1.f + k);
      }
      oL[i] = outG * os.run(0, inL[i], [&](float u) { return f.tick(0, u * inG + nz.next(), k)[3]; });
      oR[i] = outG * os.run(1, inR[i], [&](float u) { return f.tick(1, u * inG + nz.next(), k)[3]; });
    }
  }
  void meters(float* out) const override { out[0] = hz; out[1] = env.e; }
};

} // namespace rig

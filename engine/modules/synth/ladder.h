// engine/modules/synth/ladder.h - LADDER: the Moog transistor ladder, 24 dB/octave low-pass with the
// feedback squashed by the transistors, so the resonance growls rather than whistles and, from 100%,
// self-oscillates at CUTOFF. Like the original, the bass thins as RESO rises; COMP puts it back.
// CUTOFF 20 Hz-20 kHz, RESO 0-100%, ENV +-100% (up to 4 octaves), DRIVE 0-24 dB, COMP 0-100%,
// LEVEL +-12 dB. Default: 1 kHz, 30%, no compensation. Meters: "cutoff", "env".
// Four one-poles with saturated feedback, zero-delay feedback solved per sample, 2x oversampled (core/vaf.h).
#pragma once
#include "../../core/module.h"
#include "../../core/vaf.h"

namespace rig {

inline constexpr ParamDesc LADDER_PARAMS[] = {
  {"cutoff", "CUTOFF", 20.f, 20000.f, 0.566f, "Hz", Curve::Log, 0, false},
  {"reso",   "RESO",   0.f,  100.f,   0.3f,   "%",  Curve::Lin, 0, false},
  {"env",    "ENV",   -100.f, 100.f,  0.5f,   "%",  Curve::Lin, 0, true},
  {"drive",  "DRIVE",  0.f,  24.f,    0.f,    "dB", Curve::Lin, 1, false},
  {"comp",   "COMP",   0.f,  100.f,   0.f,    "%",  Curve::Lin, 0, false},
  {"level",  "LEVEL", -12.f, 12.f,    0.5f,   "dB", Curve::Lin, 1, true},
};
inline constexpr ModuleDesc LADDER_DESC = {"ladder", "LADDER", "synth filter · Moog style, 24 dB", 6, LADDER_PARAMS, 2, {"cutoff", "env"}};
inline constexpr float LADDER_KMAX = 4.4f;             // oscillates from k = 4 (RESO 91%); 4.4 starts it quickly

struct Ladder : Module {
  VaLoop<4> f; Os2 os; VaNoise nz; StepSmooth s[6]; EnvAmount env; float k = 0, inG = 1, outG = 1, hz = 1000; int cnt = 0;
  const ModuleDesc& desc() const override { return LADDER_DESC; }
  void init(float x) override { sr = x; os.init(sr); vaCascade4(f); reset(); }
  void reset() override { for (int i = 0; i < 6; i++) s[i].init(sr, 20, LADDER_PARAMS[i].def); env.init(sr); f.reset(); os.reset(); cnt = 0; }
  void process(const float* inL, const float* inR, float* oL, float* oR, int n, const float* e, const Context&) override {
    for (int i = 0; i < n; i++) {
      env.tick(inL[i], inR[i]);
      if (cnt-- <= 0) {
        cnt = EQ_STEP - 1; float v[6]; for (int j = 0; j < 6; j++) v[j] = s[j].next(clampf(e[j], 0, 1));
        env.update(); hz = clampf(map(0, v[0]) * env.scale(map(2, v[2])), 10.f, fminf(20000.f, 0.45f * sr)); f.set(vaG2(sr, hz));
        k = LADDER_KMAX * v[1]; const float drv = db2lin(map(3, v[3]));
        inG = drv * (1.f + k * map(4, v[4]) * 0.01f); outG = db2lin(map(5, v[5])) / sqrtf(drv);
      }
      oL[i] = outG * os.run(0, inL[i], [&](float u) { return f.tick(0, u * inG + nz.next(), k)[3]; });
      oR[i] = outG * os.run(1, inR[i], [&](float u) { return f.tick(1, u * inG + nz.next(), k)[3]; });
    }
  }
  void meters(float* out) const override { out[0] = hz; out[1] = env.e; }
};

} // namespace rig

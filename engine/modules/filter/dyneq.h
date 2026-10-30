// engine/modules/filter/dyneq.h - DYNAMIC EQ: one bell that only moves when that band gets loud.
// Tames the harsh 2-4 kHz bite of a hard pick attack, or the boom of a palm mute, and leaves the
// tone alone the rest of the time; or, with RANGE above 0, lifts a band when you dig in.
//  - FREQ 40 Hz-8 kHz and Q 0.5-8: the band, both for listening and for acting.
//  - THRESH -60-0 dB: the band's level where it starts to act (6 dB soft knee).
//  - RANGE -18-+18 dB: the most it will cut (below 0) or boost (above 0).
//  - RATIO 1-10: how hard it acts above the threshold (4 = 4 dB over gives 3 dB of change).
//  - ATTACK 0.5-50 ms, RELEASE 10-500 ms.
// Default: 3 kHz, Q 1.5, -24 dB, up to -6 dB, 4:1, 5 ms, 80 ms. Meters: "gain" (the dB applied
// now) and "level" (the band's level, dB).
// Feed-forward: the band's level is measured on the input with a band-pass at FREQ and Q.
#pragma once
#include "../../core/module.h"
#include "../../core/eq.h"

namespace rig {

inline constexpr ParamDesc DYNEQ_PARAMS[] = {
  {"freq",    "FREQ",    40.f,  8000.f, 0.815f, "Hz", Curve::Log, 0, false},
  {"q",       "Q",       0.5f,  8.f,    0.396f, "",   Curve::Log, 2, false},
  {"thresh",  "THRESH", -60.f,  0.f,    0.6f,   "dB", Curve::Lin, 0, false},
  {"range",   "RANGE",  -18.f,  18.f,   0.333f, "dB", Curve::Lin, 1, true},
  {"ratio",   "RATIO",   1.f,   10.f,   0.333f, "x",  Curve::Lin, 1, false},
  {"attack",  "ATTACK",  0.5f,  50.f,   0.5f,   "ms", Curve::Log, 1, false},
  {"release", "RELEASE", 10.f,  500.f,  0.532f, "ms", Curve::Log, 0, false},
};
inline constexpr ModuleDesc DYNEQ_DESC = {"dyneq", "DYNAMIC EQ", "eq · a band that reacts to you", 7, DYNEQ_PARAMS, 2, {"gain", "level"}};
inline constexpr float DYNEQ_KNEE = 6.f;
// The change (dB) for a band level `over` dB above the threshold.
inline float dyneqGain(float over, float range, float ratio) {
  const float w = DYNEQ_KNEE, x = over <= -w / 2 ? 0.f : over < w / 2 ? (over + w / 2) * (over + w / 2) / (2 * w) : over;
  const float amt = x * (1.f - 1.f / ratio);
  return range < 0 ? fmaxf(range, -amt) : fminf(range, amt);
}

struct Dyneq : Module {
  Svf side, bell; StepSmooth s[7]; float env = 0, gainDb = 0, levelDb = -120; int cnt = 0;
  const ModuleDesc& desc() const override { return DYNEQ_DESC; }
  void init(float x) override { sr = x; reset(); }
  void reset() override { for (int k = 0; k < 7; k++) s[k].init(sr, 20, DYNEQ_PARAMS[k].def); side.reset(); bell.reset(); env = 0; gainDb = 0; levelDb = -120; cnt = 0; }
  void process(const float* inL, const float* inR, float* oL, float* oR, int n, const float* e, const Context&) override {
    const float att = 1.f - expf(-1.f / (0.001f * map(5, clampf(e[5], 0, 1)) * sr)), rel = 1.f - expf(-1.f / (0.001f * map(6, clampf(e[6], 0, 1)) * sr));
    for (int i = 0; i < n; i++) {
      if (cnt-- <= 0) {
        cnt = EQ_STEP - 1; float v[5]; for (int k = 0; k < 5; k++) v[k] = map(k, s[k].next(clampf(e[k], 0, 1)));
        levelDb = 20.f * log10f(env + 1e-6f);
        gainDb = dyneqGain(levelDb - v[2], v[3], v[4]);
        side.set(Svf::BP, sr, v[0], v[1]); bell.set(Svf::BELL, sr, v[0], v[1], gainDb);
      }
      const float d = fmaxf(fabsf(side.tick(0, inL[i])), fabsf(side.tick(1, inR[i])));
      env = undenorm(env + (d > env ? att : rel) * (d - env));
      oL[i] = bell.tick(0, inL[i]); oR[i] = bell.tick(1, inR[i]);
    }
  }
  void meters(float* out) const override { out[0] = gainDb; out[1] = levelDb; }
};

} // namespace rig

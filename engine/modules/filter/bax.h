// engine/modules/filter/bax.h - BAXANDALL: the classic two-knob bass and treble (Peter Baxandall,
// 1952), as on hi-fi amps and many pedals. BASS and TREBLE +-15 dB, each a first-order shelf whose
// halfway point is BASS FREQ (50-400 Hz) and TREB FREQ (1-8 kHz); LEVEL +-12 dB. Default flat.
// The shelves keep rising at 6 dB/octave until they reach their gain, like the circuit.
#pragma once
#include "../../core/module.h"
#include "../../core/eq.h"

namespace rig {

inline constexpr ParamDesc BAX_PARAMS[] = {
  {"bass",   "BASS",   -15.f, 15.f,   0.5f,   "dB", Curve::Lin, 1, true},
  {"treble", "TREBLE", -15.f, 15.f,   0.5f,   "dB", Curve::Lin, 1, true},
  {"bfreq",  "B FREQ", 50.f,  400.f,  0.421f, "Hz", Curve::Log, 0, false},
  {"tfreq",  "T FREQ", 1000.f, 8000.f, 0.528f, "Hz", Curve::Log, 0, false},
  {"level",  "LEVEL",  -12.f, 12.f,   0.5f,   "dB", Curve::Lin, 1, true},
};
inline constexpr ModuleDesc BAX_DESC = {"bax", "BAXANDALL", "eq · bass & treble shelves", 5, BAX_PARAMS, 0, {nullptr, nullptr}};

struct Bax : Module {
  Shelf1 lo, hi; StepSmooth s[5]; float level = 1; int cnt = 0;
  const ModuleDesc& desc() const override { return BAX_DESC; }
  void init(float x) override { sr = x; reset(); }
  void reset() override { for (int k = 0; k < 5; k++) s[k].init(sr, 20, BAX_PARAMS[k].def); lo.reset(); hi.reset(); cnt = 0; }
  void process(const float* inL, const float* inR, float* oL, float* oR, int n, const float* e, const Context&) override {
    for (int i = 0; i < n; i++) {
      if (cnt-- <= 0) {
        cnt = EQ_STEP - 1; float v[5]; for (int k = 0; k < 5; k++) v[k] = map(k, s[k].next(clampf(e[k], 0, 1)));
        lo.lowShelf(sr, v[2], v[0]); hi.highShelf(sr, v[3], v[1]); level = db2lin(v[4]);
      }
      oL[i] = level * hi.tick(0, lo.tick(0, inL[i])); oR[i] = level * hi.tick(1, lo.tick(1, inR[i]));
    }
  }
};

} // namespace rig

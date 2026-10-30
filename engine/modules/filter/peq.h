// engine/modules/filter/peq.h - PARAMETRIC: a studio-style four-band EQ. LOW shelf at 100 Hz and
// HIGH shelf at 5 kHz (+-15 dB), and two fully parametric mid bands: MID 1 (60 Hz-4 kHz) and
// MID 2 (300 Hz-12 kHz), each with its own gain (+-15 dB) and Q (0.3 wide to 10 narrow). Default
// flat, mids at 400 Hz and 2.5 kHz, Q 1.
// Second-order bands on a state-variable filter, so sweeping a mid while you play doesn't zipper.
#pragma once
#include "../../core/module.h"
#include "../../core/eq.h"

namespace rig {

inline constexpr ParamDesc PEQ_PARAMS[] = {
  {"low",  "LOW",   -15.f, 15.f,    0.5f,   "dB", Curve::Lin, 1, true},
  {"f1",   "MID 1", 60.f,  4000.f,  0.452f, "Hz", Curve::Log, 0, false},
  {"g1",   "GAIN 1", -15.f, 15.f,   0.5f,   "dB", Curve::Lin, 1, true},
  {"q1",   "Q 1",   0.3f,  10.f,    0.343f, "",   Curve::Log, 2, false},
  {"f2",   "MID 2", 300.f, 12000.f, 0.575f, "Hz", Curve::Log, 0, false},
  {"g2",   "GAIN 2", -15.f, 15.f,   0.5f,   "dB", Curve::Lin, 1, true},
  {"q2",   "Q 2",   0.3f,  10.f,    0.343f, "",   Curve::Log, 2, false},
  {"high", "HIGH",  -15.f, 15.f,    0.5f,   "dB", Curve::Lin, 1, true},
};
inline constexpr ModuleDesc PEQ_DESC = {"peq", "PARAMETRIC", "eq · 2 shelves, 2 sweepable mids", 8, PEQ_PARAMS, 0, {nullptr, nullptr}};
inline constexpr float PEQ_LOW_HZ = 100.f, PEQ_HIGH_HZ = 5000.f, PEQ_SHELF_Q = 0.7071f;

struct Peq : Module {
  Svf lo, m1, m2, hi; StepSmooth s[8]; int cnt = 0;
  const ModuleDesc& desc() const override { return PEQ_DESC; }
  void init(float x) override { sr = x; reset(); }
  void reset() override { for (int k = 0; k < 8; k++) s[k].init(sr, 20, PEQ_PARAMS[k].def); lo.reset(); m1.reset(); m2.reset(); hi.reset(); cnt = 0; }
  void process(const float* inL, const float* inR, float* oL, float* oR, int n, const float* e, const Context&) override {
    for (int i = 0; i < n; i++) {
      if (cnt-- <= 0) {
        cnt = EQ_STEP - 1; float v[8]; for (int k = 0; k < 8; k++) v[k] = map(k, s[k].next(clampf(e[k], 0, 1)));
        lo.set(Svf::LSHELF, sr, PEQ_LOW_HZ, PEQ_SHELF_Q, v[0]);
        m1.set(Svf::BELL, sr, v[1], v[3], v[2]);
        m2.set(Svf::BELL, sr, v[4], v[6], v[5]);
        hi.set(Svf::HSHELF, sr, PEQ_HIGH_HZ, PEQ_SHELF_Q, v[7]);
      }
      oL[i] = hi.tick(0, m2.tick(0, m1.tick(0, lo.tick(0, inL[i]))));
      oR[i] = hi.tick(1, m2.tick(1, m1.tick(1, lo.tick(1, inR[i]))));
    }
  }
};

} // namespace rig

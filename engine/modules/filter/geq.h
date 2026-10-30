// engine/modules/filter/geq.h - GRAPHIC EQ: seven sliders an octave apart at the bands of the
// classic seven-band guitar graphic (100, 200, 400, 800 Hz, 1.6, 3.2, 6.4 kHz), each +-15 dB, and
// LEVEL +-15 dB. Default flat (the output is the input).
// Each band is a bell whose width narrows as it's pushed further (proportional Q, as in analog
// graphics): gentle moves blend into a smooth curve, big ones stay on their band.
#pragma once
#include "../../core/module.h"
#include "../../core/eq.h"

namespace rig {

inline constexpr float GEQ_BANDS[7] = {100.f, 200.f, 400.f, 800.f, 1600.f, 3200.f, 6400.f};
inline constexpr ParamDesc GEQ_PARAMS[] = {
  {"b100",  "100",   -15.f, 15.f, 0.5f, "dB", Curve::Lin, 1, true},
  {"b200",  "200",   -15.f, 15.f, 0.5f, "dB", Curve::Lin, 1, true},
  {"b400",  "400",   -15.f, 15.f, 0.5f, "dB", Curve::Lin, 1, true},
  {"b800",  "800",   -15.f, 15.f, 0.5f, "dB", Curve::Lin, 1, true},
  {"b1k6",  "1.6K",  -15.f, 15.f, 0.5f, "dB", Curve::Lin, 1, true},
  {"b3k2",  "3.2K",  -15.f, 15.f, 0.5f, "dB", Curve::Lin, 1, true},
  {"b6k4",  "6.4K",  -15.f, 15.f, 0.5f, "dB", Curve::Lin, 1, true},
  {"level", "LEVEL", -15.f, 15.f, 0.5f, "dB", Curve::Lin, 1, true},
};
inline constexpr ModuleDesc GEQ_DESC = {"geq", "GRAPHIC EQ", "eq · 7 bands, an octave apart", 8, GEQ_PARAMS, 0, {nullptr, nullptr}};
// The width of a band at this boost or cut: Q 0.9 (wide) near 0 dB up to 2.4 at +-15 dB.
inline float geqQ(float db) { return 0.9f + 1.5f * fabsf(db) / 15.f; }

struct Geq : Module {
  Svf band[7]; StepSmooth s[8]; float level = 1; int cnt = 0;
  const ModuleDesc& desc() const override { return GEQ_DESC; }
  void init(float x) override { sr = x; reset(); }
  void reset() override { for (int k = 0; k < 8; k++) s[k].init(sr, 20, GEQ_PARAMS[k].def); for (auto& b : band) b.reset(); cnt = 0; }
  void process(const float* inL, const float* inR, float* oL, float* oR, int n, const float* e, const Context&) override {
    for (int i = 0; i < n; i++) {
      if (cnt-- <= 0) {
        cnt = EQ_STEP - 1;
        for (int k = 0; k < 7; k++) { const float db = map(k, s[k].next(clampf(e[k], 0, 1))); band[k].set(Svf::BELL, sr, GEQ_BANDS[k], geqQ(db), db); }
        level = db2lin(map(7, s[7].next(clampf(e[7], 0, 1))));
      }
      float l = inL[i], r = inR[i];
      for (int k = 0; k < 7; k++) { l = band[k].tick(0, l); r = band[k].tick(1, r); }
      oL[i] = level * l; oR[i] = level * r;
    }
  }
};

} // namespace rig

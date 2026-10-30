// engine/modules/filter/tilt.h - TILT EQ: one knob leans the whole spectrum about a pivot, darker
// one way, brighter the other (the Tonelux / Quad tilt control). TILT +-6 dB (the top end goes up
// by TILT and the bottom down by the same), PIVOT 200 Hz-3 kHz (the frequency left alone), LEVEL
// +-12 dB. Default flat: the output is the input.
// First-order, so the slope is gentle: about 3 dB per octave around the pivot at full tilt.
#pragma once
#include "../../core/module.h"
#include "../../core/eq.h"

namespace rig {

inline constexpr ParamDesc TILT_PARAMS[] = {
  {"tilt",  "TILT",  -6.f,  6.f,    0.5f,   "dB", Curve::Lin, 1, true},
  {"pivot", "PIVOT", 200.f, 3000.f, 0.512f, "Hz", Curve::Log, 0, false},
  {"level", "LEVEL", -12.f, 12.f,   0.5f,   "dB", Curve::Lin, 1, true},
};
inline constexpr ModuleDesc TILT_DESC = {"tilt", "TILT EQ", "eq · one knob, dark to bright", 3, TILT_PARAMS, 0, {nullptr, nullptr}};

struct Tilt : Module {
  Shelf1 sh; StepSmooth tS, pS, lS; float level = 1; int cnt = 0;
  const ModuleDesc& desc() const override { return TILT_DESC; }
  void init(float s) override { sr = s; reset(); }
  void reset() override {
    tS.init(sr, 20, TILT_PARAMS[0].def); pS.init(sr, 20, TILT_PARAMS[1].def); lS.init(sr, 20, TILT_PARAMS[2].def);
    sh.reset(); cnt = 0;
  }
  void process(const float* inL, const float* inR, float* oL, float* oR, int n, const float* e, const Context&) override {
    for (int i = 0; i < n; i++) {
      if (cnt-- <= 0) {
        cnt = EQ_STEP - 1;
        const float t = map(0, tS.next(clampf(e[0], 0, 1))), G = db2lin(t);
        sh.set(sr, map(1, pS.next(clampf(e[1], 0, 1))) * G, 1.f / G, G);
        level = db2lin(map(2, lS.next(clampf(e[2], 0, 1))));
      }
      oL[i] = level * sh.tick(0, inL[i]); oR[i] = level * sh.tick(1, inR[i]);
    }
  }
};

} // namespace rig

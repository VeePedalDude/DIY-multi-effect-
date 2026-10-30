// engine/modules/modulation/trem.h - TREMOLO: volume swells from sine to square. DEPTH 0-100%,
// RATE 0.5-16 Hz, SHAPE sine (0) to square (1). Default 50% at 3 Hz, sine.
//
// TEMPLATE for a new effect (docs/ADDING_A_MODULE.md). Copy to engine/modules/<category>/<id>.h
// (a folder from engine/modules/categories.txt: the tab it shows in), rename TREM/Trem/"trem", and
// run ./build.sh: the registry is made from the folders, so there is no list to edit.
#pragma once
#include "../../core/module.h"

namespace rig {

// The knobs. Defaults are normalised (0..1). Symbols are the names in presets and the protocol.
//                 symbol   label    min    max    default unit  curve        decimals sign
inline constexpr ParamDesc TREM_PARAMS[] = {
  {"depth", "DEPTH",  0.f,  100.f, 0.5f,  "%",  Curve::Lin, 0, false},
  {"rate",  "RATE",   0.5f, 16.f,  0.4f,  "Hz", Curve::Log, 1, false},
  {"shape", "SHAPE",  0.f,  1.f,   0.f,   "",   Curve::Lin, 2, false},
};
//                                       id      face title  subtitle          knobs            meters
inline constexpr ModuleDesc TREM_DESC = {"trem", "TREMOLO", "sine to square", 3, TREM_PARAMS, 1, {"gain", nullptr}};

struct Trem : Module {
  // All state in members: the chain can hold several of these, and nothing in process() may allocate.
  float ph = 0, gain = 1; OnePole depthS, rateS;
  const ModuleDesc& desc() const override { return TREM_DESC; }
  void init(float s) override { sr = s; depthS.setMs(sr, 20); rateS.setMs(sr, 20); reset(); }
  void reset() override { ph = 0; gain = 1; depthS.y = 0.5f; rateS.y = 3.f; }
  // e[i] = knob i after modulation (0..1), for this block; map(i, e[i]) gives real units.
  void process(const float* inL, const float* inR, float* oL, float* oR, int n,
               const float* e, const Context&) override {
    const float depthT = map(0, e[0]) * 0.01f, rateT = map(1, e[1]), sh = e[2];
    for (int i = 0; i < n; i++) {
      const float depth = depthS.next(depthT), hz = rateS.next(rateT);    // smoothed: a jumping knob never clicks
      ph += hz / sr; if (ph >= 1) ph -= 1;
      const float s = sinf(2 * PI_F * ph), lfo = (1 - sh) * s + sh * (s > 0 ? 1.f : -1.f);
      gain = 1.f - depth * 0.5f * (1.f + lfo);
      oL[i] = inL[i] * gain; oR[i] = inR[i] * gain;
    }
  }
  void meters(float* out) const override { out[0] = gain; }        // up to 2 display values, named in the desc
};

} // namespace rig

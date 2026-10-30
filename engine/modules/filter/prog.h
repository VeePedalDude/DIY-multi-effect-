// engine/modules/filter/prog.h - PROGRAM EQ: a passive tube program equaliser in the style of the
// Pultec EQP-1A, with its famous trick: boost and cut the same low frequency together and you get
// a big low end with a dip just above it, tight rather than muddy.
//  - LOW FREQ 20-100 Hz. LOW BOOST 0-10 (a resonant shelf, up to +15 dB) and LOW ATTEN 0-10 (a
//    gentler shelf whose turn sits three times higher, up to -10 dB). Both at 10 on 60 Hz:
//    about +5.5 dB at 20-30 Hz and a -7 dB dip near 120 Hz.
//  - HIGH FREQ 3-16 kHz, HIGH BOOST 0-10 (a bell, up to +16 dB), BANDWIDTH 0-10 (sharp to broad).
//  - HIGH ATTEN 0-10: a shelf at 10 kHz, down to -16 dB.
//  - DRIVE 0-100%: the make-up tube stage, from clean to warm (mostly second harmonic).
// Default flat with 20% drive. "In the style of": the shapes are tuned to the published behaviour,
// not a model of the original's circuit.
#pragma once
#include "../../core/module.h"
#include "../../core/eq.h"

namespace rig {

inline constexpr ParamDesc PROG_PARAMS[] = {
  {"lfreq",  "LOW FREQ",  20.f,   100.f,   0.683f, "Hz", Curve::Log, 0, false},
  {"lboost", "LOW BOOST", 0.f,    10.f,    0.f,    "",   Curve::Lin, 1, false},
  {"latten", "LOW ATTEN", 0.f,    10.f,    0.f,    "",   Curve::Lin, 1, false},
  {"hfreq",  "HIGH FREQ", 3000.f, 16000.f, 0.586f, "Hz", Curve::Log, 0, false},
  {"hboost", "HIGH BOOST", 0.f,   10.f,    0.f,    "",   Curve::Lin, 1, false},
  {"bw",     "BANDWIDTH", 0.f,    10.f,    0.5f,   "",   Curve::Lin, 1, false},
  {"hatten", "HIGH ATTEN", 0.f,   10.f,    0.f,    "",   Curve::Lin, 1, false},
  {"drive",  "DRIVE",     0.f,    100.f,   0.2f,   "%",  Curve::Lin, 0, false},
};
inline constexpr ModuleDesc PROG_DESC = {"prog", "PROGRAM EQ", "eq · passive boost & cut, tube", 8, PROG_PARAMS, 0, {nullptr, nullptr}};
// The shapes, shared with the face screen (ui/screens/prog.js).
inline constexpr float PROG_LBOOST_DB = 1.5f, PROG_LBOOST_Q = 0.9f, PROG_LATTEN_DB = 1.0f, PROG_LATTEN_AT = 3.f;
inline constexpr float PROG_HBOOST_DB = 1.6f, PROG_HATTEN_DB = 1.6f, PROG_HATTEN_HZ = 10000.f, PROG_HATTEN_Q = 0.6f;
inline float progBwQ(float bw) { return 2.5f * powf(0.2f, bw / 10.f); }       // BANDWIDTH 0..10 -> Q 2.5..0.5
inline constexpr float PROG_BIAS = 0.25f;

struct Prog : Module {
  Svf lb, hb, ha; Shelf1 la; StepSmooth s[8]; DCBlock dc[2]; float k = 0.2f, kb = 0, norm = 1; int cnt = 0;
  const ModuleDesc& desc() const override { return PROG_DESC; }
  void init(float x) override { sr = x; for (auto& d : dc) d.r = 1.f - 2 * PI_F * 5.f / sr; reset(); }   // DC blocker at 5 Hz
  void reset() override {
    for (int i = 0; i < 8; i++) s[i].init(sr, 20, PROG_PARAMS[i].def);
    lb.reset(); hb.reset(); ha.reset(); la.reset(); dc[0].reset(); dc[1].reset(); cnt = 0;
  }
  // The tube stage: an offset tanh, unity gain and zero output for small signals.
  float tube(float x) const { return (tanhf(k * (x + PROG_BIAS)) - kb) * norm; }
  void process(const float* inL, const float* inR, float* oL, float* oR, int n, const float* e, const Context&) override {
    for (int i = 0; i < n; i++) {
      if (cnt-- <= 0) {
        cnt = EQ_STEP - 1; float v[8]; for (int j = 0; j < 8; j++) v[j] = map(j, s[j].next(clampf(e[j], 0, 1)));
        lb.set(Svf::LSHELF, sr, v[0], PROG_LBOOST_Q, PROG_LBOOST_DB * v[1]);
        la.lowShelf(sr, v[0] * PROG_LATTEN_AT, -PROG_LATTEN_DB * v[2]);
        hb.set(Svf::BELL, sr, v[3], progBwQ(v[5]), PROG_HBOOST_DB * v[4]);
        ha.set(Svf::HSHELF, sr, PROG_HATTEN_HZ, PROG_HATTEN_Q, -PROG_HATTEN_DB * v[6]);
        const float d = v[7] * 0.01f; k = 0.05f + 3.5f * d * d; kb = tanhf(k * PROG_BIAS); norm = 1.f / (k * (1.f - kb * kb));
      }
      const float l = ha.tick(0, hb.tick(0, la.tick(0, lb.tick(0, inL[i])))), r = ha.tick(1, hb.tick(1, la.tick(1, lb.tick(1, inR[i]))));
      oL[i] = dc[0].tick(tube(l)); oR[i] = dc[1].tick(tube(r));
    }
  }
};

} // namespace rig

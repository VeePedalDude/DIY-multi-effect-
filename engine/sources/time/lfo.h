// engine/sources/time/lfo.h - LFO, bipolar -1..1 (or unipolar with POLAR).
//  RATE   beats per cycle when SYNC is on (default 8 beats = 0.25 Hz at 120 bpm); the knob stops on
//         note values (1/8 ... 32 bars), cross-modulation still glides it.
//  SHAPE  morphs sine (0%) -> triangle (50%) -> square (100%); it is the MORPH wave.
//  WAVE   MORPH (SHAPE above), SINE, TRIANGLE, SQUARE, SAW UP, SAW DOWN, STEP RANDOM, SMOOTH RANDOM,
//         EXP RISE. A change crossfades over 10 ms.
//  DEPTH  0-100% of the output, for every destination (default 100%).
//  STEPS  OFF, or 2-32 steps per cycle: holds the wave for 1/N of a cycle, a staircase. Crossfades.
//  HZ     the rate when SYNC is FREE, 0.05-20 Hz (default 1 Hz).
//  SYNC   SYNC (RATE, follows the tempo; default) or FREE (HZ). The phase carries on: no jump.
//  POLAR  BIPOLAR -1..1 (default), UNI + 0..1, UNI - -1..0. Crossfades over 10 ms.
// Each patch from an LFO can also have its own PHASE and DIV (core/tap.h): one LFO drives several
// knobs at different offsets and multiples of its cycle, locked together.
// Defaults reproduce the LFO before WAVE...POLAR existed, bit for bit (test 200).
// Meter: "hz", the rate it runs at now.
#pragma once
#include "../../core/source.h"
#include "../../core/notes.h"

namespace rig {

inline constexpr ParamStop LFO_WAVE_STOPS[] = {
  {0, "MORPH"}, {1, "SINE"}, {2, "TRIANGLE"}, {3, "SQUARE"}, {4, "SAW UP"}, {5, "SAW DOWN"},
  {6, "STEP RANDOM"}, {7, "SMOOTH RANDOM"}, {8, "EXP RISE"}};
inline constexpr ParamStop LFO_STEPS_STOPS[] = {
  {0, "OFF"}, {2, "2"}, {3, "3"}, {4, "4"}, {6, "6"}, {8, "8"}, {12, "12"}, {16, "16"}, {24, "24"}, {32, "32"}};
inline constexpr ParamStop LFO_POLAR_STOPS[] = {{0, "BIPOLAR"}, {1, "UNI +"}, {2, "UNI -"}};

// Ranges are centred on the defaults (0.5), so a fresh rig matches the fixed LFO it replaced.
inline constexpr ParamDesc LFO_PARAMS[] = {
  {"rate",  "RATE",  0.5f,  128.f, 0.5f, "beats", Curve::Log, 1, false, NOTE_STOPS, NNOTE_STOPS},
  {"shape", "SHAPE", 0.f,   100.f, 0.f,  "%",     Curve::Lin, 0, false},
  {"wave",  "WAVE",  0.f,   8.f,   0.f,  "",      Curve::Lin, 0, false, LFO_WAVE_STOPS, 9},
  {"depth", "DEPTH", 0.f,   100.f, 1.f,  "%",     Curve::Lin, 0, false},
  {"steps", "STEPS", 0.f,   32.f,  0.f,  "",      Curve::Lin, 0, false, LFO_STEPS_STOPS, 10},
  {"hz",    "HZ",    0.05f, 20.f,  0.5f, "Hz",    Curve::Log, 2, false},   // 1 Hz
  {"sync",  "SYNC",  0.f,   1.f,   1.f,  "",      Curve::Lin, 0, false, SYNC_STOPS, 2},
  {"polar", "POLAR", 0.f,   2.f,   0.f,  "",      Curve::Lin, 0, false, LFO_POLAR_STOPS, 3},
};
inline constexpr SourceDesc LFO_DESC = {"lfo", "LFO", "tempo synced or free · 9 waves", nullptr, 1, {"hz", nullptr}, 8, LFO_PARAMS, true};

struct Lfo : Source {
  float ph = 0; unsigned cyc = 0;                    // phase 0..1 and whole cycles done
  float shape = 0, depth = 1, wxf = 1, sxf = 1, pxf = 1, hz = 0;   // this block's settings, and crossfades
  int wave = 0, wavePrev = 0, steps = 0, stepsPrev = 0, pol = 0, polPrev = 0;
  const SourceDesc& desc() const override { return LFO_DESC; }

  // The MORPH wave: exactly the LFO's original shape.
  static float morph(float ph, float s) {
    const float sine = sinf(2 * PI_F * ph);
    if (s <= 0.f) return sine;
    const float tri = ph < 0.25f ? 4 * ph : ph < 0.75f ? 2 - 4 * ph : 4 * ph - 4;   // same phase as the sine
    if (s <= 0.5f) return sine + (tri - sine) * 2 * s;
    const float sq = ph < 0.5f ? 1.f : -1.f;
    return tri + (sq - tri) * (2 * s - 1);
  }
  static float rnd(unsigned n) {                        // a fixed random value per cycle number, -1..1
    n ^= 0x9E3779B9u; n ^= n >> 16; n *= 0x7FEB352Du; n ^= n >> 15; n *= 0x846CA68Bu; n ^= n >> 16;
    return (float)(n >> 8) * (2.f / 16777215.f) - 1.f;
  }
  float waveAt(int w, float p, unsigned n) const {
    switch (w) {
      case 1: return sinf(2 * PI_F * p);
      case 2: return p < 0.25f ? 4 * p : p < 0.75f ? 2 - 4 * p : 4 * p - 4;
      case 3: return p < 0.5f ? 1.f : -1.f;
      case 4: return 2 * p - 1;
      case 5: return 1 - 2 * p;
      case 6: return rnd(n);
      case 7: { const float a = rnd(n), b = rnd(n + 1), t = 0.5f - 0.5f * cosf(PI_F * p); return a + (b - a) * t; }
      case 8: return (expf(4.f * p) - 1.f) * (2.f / 53.59815f) - 1.f;      // e^4 - 1 = 53.59815
      default: return morph(p, shape);
    }
  }
  static float quant(float p, int st) { return st >= 2 ? floorf(p * (float)st) / (float)st : p; }
  float shaped(int w, float p, unsigned n) const {      // one wave, with the step crossfade
    const float a = waveAt(w, quant(p, steps), n);
    if (sxf >= 1.f) return a;
    return waveAt(w, quant(p, stepsPrev), n) + (a - waveAt(w, quant(p, stepsPrev), n)) * sxf;
  }
  float at(float p, unsigned n) const override {
    float v = shaped(wave, p, n);
    if (wxf < 1.f) { const float o = shaped(wavePrev, p, n); v = o + (v - o) * wxf; }
    if (depth != 1.f) v *= depth;
    if (pol == 0 && pxf >= 1.f) return v;
    const float now = polar(v, pol);
    return pxf >= 1.f ? now : polar(v, polPrev) + (now - polar(v, polPrev)) * pxf;
  }
  float polar(float v, int p) const { return p == 0 ? v : p == 1 ? 0.5f * v + 0.5f * depth : 0.5f * v - 0.5f * depth; }   // BIPOLAR, UNI +, UNI -
  void cycle(unsigned& n, float& p) const override { n = cyc; p = ph; }

  float update(const SourceContext& c) override {
    const bool sync = c.p[6] >= 0.5f;                   // SYNC is a two-stop switch: 0 FREE, 1 SYNC
    if (sync) { const float beats = knob(c, 0); hz = c.bpm / 60.f / beats; ph += c.dt * c.bpm / 60.f / beats; }   // the original expression: bit-exact defaults
    else { hz = knob(c, 5); ph += c.dt * hz; }
    const float whole = floorf(ph); cyc += (unsigned)whole; ph -= whole;
    shape = knob(c, 1) * 0.01f;
    const float step = c.dt / 0.010f;                   // 10 ms crossfades
    const float dT = knob(c, 3) / 100.f;
    if (depth != dT) { depth += (dT - depth) * (1.f - expf(-step)); if (fabsf(depth - dT) < 1e-6f) depth = dT; }
    const int w = (int)lrintf(clampf(knob(c, 2), 0.f, 8.f));
    if (w != wave && wxf >= 1.f) { wavePrev = wave; wave = w; wxf = 0.f; }   // one fade at a time: never a jump
    if (wxf < 1.f) wxf = fminf(1.f, wxf + step);
    const int st = (int)lrintf(clampf(knob(c, 4), 0.f, 32.f));
    if (st != steps && sxf >= 1.f) { stepsPrev = steps; steps = st; sxf = 0.f; }
    if (sxf < 1.f) sxf = fminf(1.f, sxf + step);
    const int pt = (int)lrintf(clampf(knob(c, 7), 0.f, 2.f));
    if (pt != pol && pxf >= 1.f) { polPrev = pol; pol = pt; pxf = 0.f; }
    if (pxf < 1.f) pxf = fminf(1.f, pxf + step);
    return at(ph, cyc);
  }
  void meters(float* out) const override { out[0] = hz; }
};

} // namespace rig

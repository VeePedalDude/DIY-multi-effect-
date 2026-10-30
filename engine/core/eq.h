// engine/core/eq.h - building blocks for the equalisers in engine/modules/filter/.
// Everything is allocation-free, stereo (two channels of state) and safe to retune while audio
// runs: the EQ modules recompute coefficients every EQ_STEP samples from smoothed knobs.
//
//  - Svf: Andrew Simper's linear trapezoidal state-variable filter ("Linear trap integrated SVF",
//    Cytomic technical paper, 2013). Bell, shelves, low/high/band pass from one structure. It
//    matches the analog prototype's response (bilinear, prewarped at the centre) and, unlike a
//    direct-form biquad, stays clean when its frequency or gain moves every few samples.
//  - Shelf1: first-order shelves and tilt from a topology-preserving one-pole split
//    (Zavalishin, "The Art of VA Filter Design", ch. 3): y = lp*gLow + hp*gHigh.
//  - Iir3: a third-order filter from s-domain coefficients by the bilinear transform, in double
//    precision (the amp tone stack's poles sit low in frequency).
#pragma once
#include "dsp.h"

namespace rig {

constexpr int EQ_STEP = 16;               // samples between coefficient updates

struct Svf {
  enum Mode { LP, HP, BP, BELL, LSHELF, HSHELF };
  float a1 = 1, a2 = 0, a3 = 0, m0 = 1, m1 = 0, m2 = 0;
  float ic1[2] = {0, 0}, ic2[2] = {0, 0};
  // f in Hz, q the analog Q, db the gain (bell and shelves). BP has 0 dB at the centre.
  void set(Mode mode, float sr, float f, float q, float db = 0.f) {
    const float w = PI_F * clampf(f, 5.f, 0.45f * sr) / sr, A = powf(10.f, db / 40.f);
    float g = tanf(w), k = 1.f / clampf(q, 0.05f, 50.f);
    switch (mode) {
      case LP:     m0 = 0; m1 = 0; m2 = 1; break;
      case HP:     m0 = 1; m1 = -k; m2 = -1; break;
      case BP:     m0 = 0; m1 = k; m2 = 0; break;
      case BELL:   k = k / A; m0 = 1; m1 = k * (A * A - 1); m2 = 0; break;
      case LSHELF: g = g / sqrtf(A); m0 = 1; m1 = k * (A - 1); m2 = A * A - 1; break;
      case HSHELF: g = g * sqrtf(A); m0 = A * A; m1 = k * (1 - A) * A; m2 = 1 - A * A; break;
    }
    a1 = 1.f / (1.f + g * (g + k)); a2 = g * a1; a3 = g * a2;
  }
  float tick(int ch, float v0) {
    const float v3 = v0 - ic2[ch], v1 = a1 * ic1[ch] + a2 * v3, v2 = ic2[ch] + a2 * ic1[ch] + a3 * v3;
    ic1[ch] = undenorm(2 * v1 - ic1[ch]); ic2[ch] = undenorm(2 * v2 - ic2[ch]);
    return m0 * v0 + m1 * v1 + m2 * v2;
  }
  void reset() { ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0; }
};

// First-order split at fc: y = gLow * lowpass + gHigh * highpass.
//  - low shelf, gain G at DC, half the dB at f:   split at f / sqrt(G), gLow = G, gHigh = 1
//  - high shelf, gain G at the top, half at f:    split at f * sqrt(G), gLow = 1, gHigh = G
//  - tilt +-T dB about a pivot p (0 dB at p):     split at p * 10^(T/20), gLow = 10^(-T/20), gHigh = 10^(T/20)
struct Shelf1 {
  float G = 0, gLo = 1, gHi = 1, s[2] = {0, 0};
  void set(float sr, float fc, float lo, float hi) {
    const float g = tanf(PI_F * clampf(fc, 5.f, 0.45f * sr) / sr); G = g / (1.f + g); gLo = lo; gHi = hi;
  }
  void lowShelf(float sr, float f, float db)  { const float A = db2lin(db); set(sr, f / sqrtf(A), A, 1.f); }
  void highShelf(float sr, float f, float db) { const float A = db2lin(db); set(sr, f * sqrtf(A), 1.f, A); }
  float tick(int ch, float x) {
    const float v = (x - s[ch]) * G, lp = v + s[ch]; s[ch] = undenorm(lp + v);
    return gLo * lp + gHi * (x - lp);
  }
  void reset() { s[0] = s[1] = 0; }
};

// H(s) = (B0 + B1 s + B2 s^2 + B3 s^3) / (A0 + A1 s + A2 s^2 + A3 s^3), bilinear with s = 2 sr (1 - 1/z) / (1 + 1/z).
struct Iir3 {
  double b[4] = {1, 0, 0, 0}, a[4] = {1, 0, 0, 0}, z[2][3] = {{0, 0, 0}, {0, 0, 0}};
  void set(double sr, const double B[4], const double A[4]) {
    const double c = 2 * sr, c2 = c * c, c3 = c2 * c;
    double nb[4] = { B[0] + B[1] * c + B[2] * c2 + B[3] * c3, 3 * B[0] + B[1] * c - B[2] * c2 - 3 * B[3] * c3,
                     3 * B[0] - B[1] * c - B[2] * c2 + 3 * B[3] * c3, B[0] - B[1] * c + B[2] * c2 - B[3] * c3 };
    double na[4] = { A[0] + A[1] * c + A[2] * c2 + A[3] * c3, 3 * A[0] + A[1] * c - A[2] * c2 - 3 * A[3] * c3,
                     3 * A[0] - A[1] * c - A[2] * c2 + 3 * A[3] * c3, A[0] - A[1] * c + A[2] * c2 - A[3] * c3 };
    for (int i = 0; i < 4; i++) { b[i] = nb[i] / na[0]; a[i] = na[i] / na[0]; }
  }
  float tick(int ch, float xf) {                    // transposed direct form II
    double* s = z[ch]; const double x = xf, y = b[0] * x + s[0];
    s[0] = b[1] * x - a[1] * y + s[1]; s[1] = b[2] * x - a[2] * y + s[2]; s[2] = b[3] * x - a[3] * y;
    if (fabs(s[0]) < 1e-30) s[0] = 0;
    return (float)y;
  }
  void reset() { for (auto& c : z) c[0] = c[1] = c[2] = 0; }
};

// A knob smoother that advances once per EQ_STEP samples.
struct StepSmooth {
  float y = 0, a = 1;
  void init(float sr, float ms, float v) { a = 1.f - expf(-(float)EQ_STEP / (0.001f * ms * sr)); y = v; }
  float next(float t) { y += a * (t - y); return y; }
};

} // namespace rig

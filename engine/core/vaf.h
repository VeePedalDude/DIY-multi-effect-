// engine/core/vaf.h - building blocks for the synth filters in engine/modules/synth/ (virtual analog).
// Allocation-free, stereo, retuned every EQ_STEP samples from smoothed knobs (see eq.h).
//
//  - vaSat: a fast, smooth saturator (a rational tanh), and its slope.
//  - VaLoop<N>: an N-state linear filter dx/dt = w (A x + b u) with saturated feedback
//    u = sat(in - k y), integrated by the trapezoidal rule with zero-delay feedback: each sample,
//    the one unknown u is solved by Newton's method (Zavalishin, "The Art of VA Filter Design",
//    ch. 5-6). The ladders use it: the Moog ladder, the OTA cascade and the diode ladder differ
//    only in A and in where the outputs are taken.
//  - EnvAmount: the input's level, 0..1 from -48 to 0 dBFS, for a filter's ENV knob.
#pragma once
#include "eq.h"

namespace rig {

// tanh by a rational fit (within 2% of tanh, exact slope 1 at 0), clipped at +-3 where it reaches +-1.
inline float vaSat(float x) { x = clampf(x, -3.f, 3.f); const float x2 = x * x; return x * (27.f + x2) / (27.f + 9.f * x2); }
inline float vaSatSlope(float x) {
  if (x <= -3.f || x >= 3.f) return 0.f;
  const float x2 = x * x, d = 27.f + 9.f * x2; return ((27.f + 3.f * x2) * d - x * (27.f + x2) * 18.f * x) / (d * d);
}

template <int N> struct VaLoop {
  float A[N][N] = {}, b[N] = {}, c[N] = {};         // the analog system, w = 1 at the frequency set()
  float M[N][N] = {}, p[N] = {};                     // the trapezoidal update for the current g
  float x[2][N] = {}, uPrev[2] = {0, 0};
  // g = tan(pi f / sr) for w = 1 at f.
  void set(float g) {
    double L[N][2 * N];                              // [I - gA | I + gA]: invert the left, apply to the right
    for (int i = 0; i < N; i++) for (int j = 0; j < N; j++) { L[i][j] = (i == j) - g * A[i][j]; L[i][N + j] = (i == j) + g * A[i][j]; }
    double q[N]; for (int i = 0; i < N; i++) q[i] = g * b[i];
    for (int col = 0; col < N; col++) {               // Gauss-Jordan (I - gA is diagonally dominant here)
      const double piv = L[col][col];
      for (int j = 0; j < 2 * N; j++) L[col][j] /= piv;
      q[col] /= piv;
      for (int r = 0; r < N; r++) if (r != col) { const double f = L[r][col]; if (f == 0) continue; for (int j = 0; j < 2 * N; j++) L[r][j] -= f * L[col][j]; q[r] -= f * q[col]; }
    }
    for (int i = 0; i < N; i++) { p[i] = (float)q[i]; for (int j = 0; j < N; j++) M[i][j] = (float)L[i][N + j]; }
  }
  // One sample: input `in` (already scaled by the drive), feedback k. Returns the new state.
  const float* tick(int ch, float in, float k) {
    float* s = x[ch]; float m[N], alpha = 0, beta = 0;
    for (int i = 0; i < N; i++) { float a = p[i] * uPrev[ch]; for (int j = 0; j < N; j++) a += M[i][j] * s[j]; m[i] = a; alpha += c[i] * p[i]; beta += c[i] * a; }
    // y = alpha u + beta; solve u = sat(in - k y) by Newton, from the linear solution.
    float u = clampf((in - k * beta) / (1.f + k * alpha), -3.f, 3.f);
    for (int it = 0; it < 3; it++) {
      const float arg = in - k * (alpha * u + beta), f = u - vaSat(arg), df = 1.f + vaSatSlope(arg) * k * alpha;
      u -= f / df;
    }
    for (int i = 0; i < N; i++) s[i] = undenorm(m[i] + p[i] * u);
    uPrev[ch] = u;
    return s;
  }
  void reset() { for (auto& ch : x) for (auto& v : ch) v = 0; uPrev[0] = uPrev[1] = 0; }
};

// Four one-poles in a row (Moog ladder, OTA cascade): dx1 = w (u - x1), dxi = w (x(i-1) - xi). Feedback from x4.
inline void vaCascade4(VaLoop<4>& f) {
  for (int i = 0; i < 4; i++) { f.A[i][i] = -1.f; if (i) f.A[i][i - 1] = 1.f; }
  f.b[0] = 1.f; f.c[3] = 1.f;
}
// The diode ladder (EMS, TB-303), linearised (T. Stinchcombe, "Analysis of the Moog Transistor Ladder
// and Derivative Filters", 2008): each capacitor is loaded by the next, so the stages couple both ways.
// Characteristic polynomial s^4 + 7 s^3 + 15 s^2 + 10 s + 1; it resonates at w = sqrt(10/7) and
// oscillates from k = 18.39.
inline void vaDiode4(VaLoop<4>& f) {
  const float a[4][4] = {{-2, 1, 0, 0}, {1, -2, 1, 0}, {0, 1, -2, 1}, {0, 0, 1, -1}};
  for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) f.A[i][j] = a[i][j];
  f.b[0] = 1.f; f.c[3] = 1.f;
}
inline constexpr float VA_DIODE_W = 1.19522861f;     // sqrt(10/7): its resonance, relative to w = 1
inline constexpr float VA_DIODE_K = 18.3877551f;     // 15 * 10/7 - (10/7)^2 - 1: where it starts to oscillate

// The input's level for a filter's ENV knob: a peak follower (3 ms attack, 120 ms release), as 0..1
// over -48..0 dBFS. ENV +-100% moves the cutoff up to 4 octaves either way at full level.
struct EnvAmount {
  float env = 0, aA = 1, aR = 1, e = 0;
  void init(float sr) { aA = 1.f - expf(-1.f / (0.003f * sr)); aR = 1.f - expf(-1.f / (0.120f * sr)); env = 0; e = 0; }
  void tick(float l, float r) { const float d = fmaxf(fabsf(l), fabsf(r)); env = undenorm(env + (d > env ? aA : aR) * (d - env)); }
  float update() { e = clampf((20.f * log10f(env + 1e-6f) + 48.f) / 48.f, 0.f, 1.f); return e; }
  float scale(float amountPct) const { return exp2f(4.f * amountPct * 0.01f * e); }
};
inline constexpr float VA_ENV_OCTAVES = 4.f;

// 2x oversampling for the saturating filters, the way the amp does it (drive/amp.h): zero-stuff, a
// 6th-order Butterworth at 19 kHz on the way up and again on the way down (minimum phase: about
// 6 samples of group delay at 48 kHz). run(ch, x, f) calls f twice at the doubled rate.
struct Os2 {
  Biquad up[2][3], dn[2][3];
  void init(float sr) {
    const float q[3] = {0.5176f, 0.7071f, 1.9319f};
    for (int c = 0; c < 2; c++) for (int k = 0; k < 3; k++) { up[c][k].lp(2 * sr, 19000, q[k]); dn[c][k].lp(2 * sr, 19000, q[k]); }
  }
  template <class F> float run(int c, float x, F&& f) {
    float out = 0;
    for (int ph = 0; ph < 2; ph++) {
      float u = ph == 0 ? 2.f * x : 0.f;
      for (int k = 0; k < 3; k++) u = up[c][k].tick(u);
      u = f(u);
      for (int k = 0; k < 3; k++) u = dn[c][k].tick(u);
      out = u;
    }
    return out;
  }
  void reset() { for (auto& c : up) for (auto& b : c) b.reset(); for (auto& c : dn) for (auto& b : c) b.reset(); }
};

// A whisper of noise (-120 dBFS) for the resonant loops, standing in for an analog circuit's own
// noise: it lets a filter at full resonance start singing from silence, as a synth's does.
struct VaNoise {
  unsigned s = 22222u;
  float next() { s = s * 1664525u + 1013904223u; return 1e-6f * ((float)(s >> 8) / 8388608.f - 1.f); }
};

// A cutoff in Hz to g, kept below Nyquist.
inline float vaG(float sr, float hz) { return tanf(PI_F * clampf(hz, 10.f, fminf(20000.f, 0.42f * sr)) / sr); }
// The same at the 2x rate (Os2): the cutoff can go to 20 kHz at any engine rate.
inline float vaG2(float sr, float hz) { return tanf(PI_F * clampf(hz, 10.f, fminf(20000.f, 0.45f * sr)) / (2 * sr)); }

} // namespace rig

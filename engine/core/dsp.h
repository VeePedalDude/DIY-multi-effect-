// engine/core/dsp.h - shared DSP building blocks (filters, delay lines, smoothing).
// Everything here is allocation-free and safe to use inside process().
#pragma once
#include <math.h>
#include <string.h>

namespace rig {

constexpr int   MAXBLOCK = 128;
constexpr float PI_F = 3.14159265358979f;

inline float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
inline float db2lin(float d) { return powf(10.f, d * 0.05f); }
inline float undenorm(float x) { return fabsf(x) < 1e-15f ? 0.f : x; }

// ------------------------------------------------------------------ utilities
struct OnePole {
  float y = 0, a = 1;
  void setMs(float sr, float ms) { a = 1.f - expf(-1.f / (0.001f * ms * sr)); }
  float next(float t) { y += a * (t - y); return y; }
};

struct Biquad {                         // RBJ cookbook, transposed DF-II
  float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
  void lp(float sr, float f, float q) { pass(sr, f, q, false); }
  void hp(float sr, float f, float q) { pass(sr, f, q, true); }
  void pass(float sr, float f, float q, bool high) {
    float w = 2 * PI_F * clampf(f, 5.f, 0.49f * sr) / sr, al = sinf(w) / (2 * q), c = cosf(w), a0 = 1 + al;
    if (!high) { b0 = (1 - c) / 2 / a0; b1 = (1 - c) / a0; }
    else       { b0 = (1 + c) / 2 / a0; b1 = -(1 + c) / a0; }
    b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
  }
  void peak(float sr, float f, float q, float db) {
    float A = powf(10.f, db / 40.f), w = 2 * PI_F * f / sr, al = sinf(w) / (2 * q), c = cosf(w);
    float a0 = 1 + al / A;
    b0 = (1 + al * A) / a0; b1 = -2 * c / a0; b2 = (1 - al * A) / a0; a1 = b1; a2 = (1 - al / A) / a0;
  }
  float tick(float x) {
    float y = b0 * x + z1;
    z1 = undenorm(b1 * x - a1 * y + z2);
    z2 = undenorm(b2 * x - a2 * y);
    return y;
  }
  void reset() { z1 = z2 = 0; }
};

struct DCBlock {
  float x1 = 0, y1 = 0, r = 0.995f;
  float tick(float x) { float y = x - x1 + r * y1; x1 = x; y1 = undenorm(y); return y; }
  void reset() { x1 = y1 = 0; }
};

template <int N> struct DelayLine {     // power-of-two ring, linear-interp fractional read
  static_assert((N & (N - 1)) == 0, "N must be a power of two");
  float b[N]; int w = 0;
  void clear() { memset(b, 0, sizeof b); w = 0; }
  void push(float x) { b[w] = x; w = (w + 1) & (N - 1); }
  float tap(float d) const {             // value written d samples ago (d >= 1)
    float rp = (float)w - d;
    while (rp < 0) rp += (float)N;
    int i0 = (int)rp; float fr = rp - (float)i0;
    float a = b[i0 & (N - 1)], c = b[(i0 + 1) & (N - 1)];
    return a + fr * (c - a);
  }
};

struct Allpass {
  float buf[4096]; int len = 1, i = 0; float g = 0.6f;
  void init(int l, float gain) { len = l < 1 ? 1 : (l > 4095 ? 4095 : l); g = gain; clear(); }
  void clear() { memset(buf, 0, sizeof buf); i = 0; }
  float tick(float x) {
    float d = buf[i], v = x + g * d;
    buf[i] = undenorm(v); if (++i >= len) i = 0;
    return d - g * v;
  }
};

} // namespace rig

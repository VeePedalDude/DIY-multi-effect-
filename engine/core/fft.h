// engine/core/fft.h - a small radix-2 complex FFT, allocation-free after init(). For analysis in
// process() (core/polytrack.h) and for building wavetables in init().
#pragma once
#include "dsp.h"

namespace rig {

template <int MAX> struct Fft {
  int n = 0;
  float cs[MAX / 2], sn[MAX / 2]; int rev[MAX];
  void init(int size) {                              // size: a power of two, <= MAX (control thread)
    n = size; int lg = 0; while ((1 << lg) < n) lg++;
    for (int i = 0; i < n; i++) { int r = 0; for (int b = 0; b < lg; b++) if (i & (1 << b)) r |= 1 << (lg - 1 - b); rev[i] = r; }
    for (int i = 0; i < n / 2; i++) { cs[i] = (float)cos(2.0 * M_PI * i / n); sn[i] = (float)sin(2.0 * M_PI * i / n); }
  }
  // In place. Forward: X[k] = sum x[t] e^(-2 pi i k t / n); inverse without the 1/n.
  void run(float* re, float* im, bool inverse = false) const {
    for (int i = 0; i < n; i++) if (i < rev[i]) { float t = re[i]; re[i] = re[rev[i]]; re[rev[i]] = t; t = im[i]; im[i] = im[rev[i]]; im[rev[i]] = t; }
    for (int len = 2; len <= n; len <<= 1) {
      const int half = len >> 1, step = n / len;
      for (int i = 0; i < n; i += len) for (int j = 0; j < half; j++) {
        const float wr = cs[j * step], wi = inverse ? sn[j * step] : -sn[j * step];
        const int a = i + j, b = a + half;
        const float xr = re[b] * wr - im[b] * wi, xi = re[b] * wi + im[b] * wr;
        re[b] = re[a] - xr; im[b] = im[a] - xi; re[a] += xr; im[a] += xi;
      }
    }
  }
};

} // namespace rig

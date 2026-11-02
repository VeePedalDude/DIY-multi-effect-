#include "framework.h"
using namespace rigtest;

TEST("50 tess: RT60 tracks SIZE (Schroeder T20, broadband)") {
  for (float size : {0.f, 0.5f, 1.f}) {
    Engine e; e.init(SR); int t = mod(e, "tess"); only(e, t); e.base[t][0] = size;
    std::vector<float> in((size_t)(12 * SR), 0.f); in[4800] = 0.5f;
    auto r = render(e, in);
    std::vector<double> edc(r.L.size()); double acc = 0;
    for (size_t i = r.L.size(); i-- > 4801;) { acc += (double)r.L[i] * r.L[i] + (double)r.R[i] * r.R[i]; edc[i] = acc; }
    double e0 = edc[4801]; size_t i5 = 0, i25 = 0;
    for (size_t i = 4801; i < r.L.size(); i++) { double d = 10 * log10(edc[i] / e0); if (!i5 && d <= -5) i5 = i; if (!i25 && d <= -25) { i25 = i; break; } }
    float rt = i25 ? 3.f * (i25 - i5) / SR : -1;
    float target = e.tess.map(0, size);
    CHECK(rt > 0 && fabsf(rt - target) / target < 0.35f, "size %.1f: RT60 %.2f s (label %.1f s)", size, rt, target);
  }
}

TEST("51 tess: max size + max 4D fold stays lossless-stable and decays") {
  Engine e; e.init(SR); int t = mod(e, "tess"); only(e, t); e.base[t][0] = 1; e.base[t][1] = 1; e.tess.foldS.y = 1;
  std::vector<float> in((size_t)(30 * SR), 0.f); for (size_t i = 0; i < (size_t)SR; i++) in[i] = riffSignal()[(size_t)(0.2f * SR) + i];
  auto r = render(e, in);
  CHECK(e.nanResets == 0 && rms(r.L, 25 * SR, 30 * SR) < rms(r.L, 1 * SR, 3 * SR) * 0.01, "%.1f -> %.1f dBFS", dbf(rms(r.L, SR, 3 * SR)), dbf(rms(r.L, 25 * SR, 30 * SR)));
}

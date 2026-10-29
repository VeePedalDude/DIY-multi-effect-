#include "framework.h"
using namespace rigtest;

TEST("40 holo: stable at 95% feedback + full halo, repeats decay") {
  Engine e; e.init(SR); int h = mod(e, "holo"); only(e, h); e.base[h][0] = 1; e.base[h][1] = 1; fx<Holo>(e, "holo").fbS.y = 1; fx<Holo>(e, "holo").haloS.y = 1;
  std::vector<float> in((size_t)(40 * SR), 0.f);
  for (size_t i = 0; i < (size_t)SR; i++) in[i] = riffSignal()[(size_t)(0.2f * SR) + i];
  auto r = render(e, in);
  double w1 = rms(r.L, 1 * SR, 6 * SR), w2 = rms(r.L, 15 * SR, 20 * SR), w3 = rms(r.L, 35 * SR, 40 * SR);
  CHECK(e.nanResets == 0 && peak(r.L) <= 0.98f, "no blow-up (peak %.3f)", peak(r.L));
  CHECK(w3 < w1 * 0.1, "repeats decay: %.1f -> %.1f -> %.1f dBFS", dbf(w1), dbf(w2), dbf(w3));
}

TEST("41 holo: first repeat lands on the dotted 8th") {
  Engine e; e.init(SR); int h = mod(e, "holo"); only(e, h); e.base[h][1] = 0; fx<Holo>(e, "holo").haloS.y = 0;
  std::vector<float> im((size_t)SR, 0.f); im[64] = 1.f;
  auto r = render(e, im);
  size_t best = 0; float bv = 0; for (size_t i = 1000; i < r.L.size(); i++) if (fabsf(r.L[i]) > bv) { bv = fabsf(r.L[i]); best = i; }
  float d1 = (best - 64) * 1000.f / SR;
  CHECK(fabsf(d1 - 375.f) < 1.5f, "first repeat at %.1f ms (dotted 8th @120 = 375 ms)", d1);
}

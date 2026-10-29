#include "framework.h"
using namespace rigtest;

TEST("30 cab: built-in IR response (dB rel 1 kHz)") {
  Engine e; e.init(SR); int c = mod(e, "cab"); only(e, c); e.base[c][0] = 0; e.cab.mixS.y = 1; e.base[c][1] = 1;
  std::vector<float> imp(8192, 0.f); imp[100] = 0.5f;
  auto r = render(e, imp);
  auto mag = [&](float f) { std::complex<double> a = 0; for (int i = 0; i < 8192; i++) a += (double)r.L[i] * std::polar(1.0, -2 * M_PI * f * i / SR); return std::abs(a); };
  double ref = mag(1000);
  printf("      ");
  for (float f : {80.f, 120.f, 400.f, 1000.f, 2600.f, 5000.f, 8000.f, 12000.f}) printf("%gHz %+.1f  ", f, dbf(mag(f) / ref));
  printf("\n");
  CHECK(dbf(mag(12000) / ref) < -20, "high end rolled off like a guitar speaker (12 kHz %.1f dB)", dbf(mag(12000) / ref));
}

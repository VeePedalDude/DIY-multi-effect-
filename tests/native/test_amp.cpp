#include "framework.h"
using namespace rigtest;

static double aliasDb(float f0, float amp, float drive, int os, bool adaa) {
  const int N = 65536; std::vector<float> s(N + 8192);
  for (size_t i = 0; i < s.size(); i++) s[i] = amp * sinf(2 * (float)M_PI * f0 * i / SR);
  Engine e; e.init(SR); int a = mod(e, "amp"); only(e, a); e.amp.adaa = adaa; e.amp.setOS(os); e.amp.reset();
  e.base[a][0] = drive; e.amp.inS.y = drive;
  auto r = render(e, s);
  std::vector<std::complex<double>> X(N);
  for (int i = 0; i < N; i++) X[i] = r.L[8192 + i] * (0.5 - 0.5 * cos(2 * M_PI * i / (N - 1)));
  fft(X);
  double harm = 0, other = 0;
  for (int k = 2; k < N / 2; k++) {
    double f = (double)k * SR / N, p = std::norm(X[k]);
    if (f < 30) continue;
    double h = f / f0, near = fabs(h - round(h)) * f0;
    if (near < 25 && round(h) >= 1) harm += p; else other += p;
  }
  return -10 * log10(other / harm);
}

TEST("20 amp: aliasing (non-harmonic energy below harmonics; bigger is cleaner)") {
  struct V { int os; bool adaa; const char* name; } vs[] = {{1,false,"1x"},{2,false,"2x"},{4,false,"4x"},{1,true,"1x+ADAA"},{2,true,"2x+ADAA"},{4,true,"4x+ADAA"}};
  printf("      %-9s %22s %22s %22s\n", "variant", "2.5k sine, max drive", "1234 Hz sine, max drive", "330 Hz, +9.6 dB input");
  for (auto& v : vs) printf("      %-9s %19.1f dB %19.1f dB %19.1f dB\n", v.name, aliasDb(2500, .25f, 1.f, v.os, v.adaa), aliasDb(1234, .25f, 1.f, v.os, v.adaa), aliasDb(330, .25f, .6f, v.os, v.adaa));
  Engine d; d.init(SR);
  double shipped = aliasDb(2500, .25f, 1.f, d.amp.os, d.amp.adaa);
  CHECK(shipped > 50, "shipped setting (%dx%s): %.1f dB at the worst case", d.amp.os, d.amp.adaa ? "+ADAA" : "", shipped);
}

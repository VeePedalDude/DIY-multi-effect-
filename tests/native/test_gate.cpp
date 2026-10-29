#include "framework.h"
using namespace rigtest;

TEST("10 gate: mutes a -75 dBFS floor, passes guitar") {
  std::vector<float> noise((size_t)(3 * SR)); srand(3);
  for (auto& v : noise) v = 0.000178f * ((rand() / (float)RAND_MAX) * 2 - 1) * 1.732f;
  Engine e; e.init(SR); only(e, mod(e, "gate"));
  auto r = render(e, noise);
  double inDb = dbf(rms(noise, SR, 3 * SR)), outDb = dbf(rms(r.L, SR, 3 * SR));
  CHECK(outDb < inDb - 40, "noise in %.1f dBFS -> out %.1f dBFS (%.1f dB attenuation)", inDb, outDb, inDb - outDb);
  Engine e2; e2.init(SR); only(e2, mod(e2, "gate"));
  auto r2 = render(e2, riffSignal());
  double ratio = rms(r2.L, 0, r2.L.size()) / rms(riffSignal(), 0, riffSignal().size());
  CHECK(ratio > 0.97, "guitar passes at %.2f dB", dbf(ratio));
}

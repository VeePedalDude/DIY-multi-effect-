// TEMPLATE for a new effect's own test (docs/ADDING_A_MODULE.md): copy to tests/native/test_<id>.cpp.
// The conformance and chain tests already check the format, finite output and allocation for every
// effect in the folders; this file checks that it does its job, with numbers.
#include "framework.h"
using namespace rigtest;

TEST("110 trem: full depth dips the level, at RATE") {
  Engine e; e.init(SR);
  const int t = e.addModule("trem", nullptr, -1, false);            // a fresh chain doesn't include it
  only(e, t); e.base[t][0] = 1.f; e.base[t][1] = 0.4f;              // 100% depth, 3 Hz
  std::vector<float> s((size_t)(2 * SR)); for (size_t i = 0; i < s.size(); i++) s[i] = 0.3f * sinf(2 * (float)M_PI * 440 * i / SR);
  auto r = render(e, s);
  float lo = 1, hi = 0; for (size_t i = SR; i + 480 < r.L.size(); i += 480) { float p = peak(r.L, i, i + 480); lo = fminf(lo, p); hi = fmaxf(hi, p); }
  CHECK(lo < 0.02f && hi > 0.25f, "level swings %.3f..%.3f at full depth", lo, hi);
}

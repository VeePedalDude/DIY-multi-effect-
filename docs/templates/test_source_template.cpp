// TEMPLATE for a new source's own test (docs/ADDING_A_SOURCE.md): copy to
// tests/native/test_<id>.cpp. The conformance tests (test_conformance.cpp) already check the
// format, the -1..1 range, independence and allocation for every registered source; this file
// checks that it does its job, with numbers.
#include "framework.h"
using namespace rigtest;

TEST("100 rand: steps at RATE per beat") {
  Engine e; e.init(SR);
  const int k = e.addSource("rand", nullptr);                       // or src(e, "rand") once it's in the registry
  e.rack.slot[k].base[0] = 0.5f;                                    // 2 per beat; at 120 bpm, 4 per second
  for (int i = 0; i < (int)(10 * SR / 128); i++) { memset(e.inBuf, 0, sizeof e.inBuf); e.process(128); }
  float m[MAXSRCMETERS]; e.rack.meters(k, m);
  CHECK(fabsf(m[0] - 40.f) <= 1.f, "%.0f steps in 10 s at 120 bpm (expect 40)", m[0]);
}

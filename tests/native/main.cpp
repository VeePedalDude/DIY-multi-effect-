// tests/native/main.cpp - runs every registered test, in name order. Optional arg filters by substring.
#include "framework.h"
#include <cstring>
int main(int argc, char** argv) {
  auto& R = rigtest::Registry::get();
  std::sort(R.tests.begin(), R.tests.end(), [](auto& a, auto& b) { return a.first < b.first; });
  printf("rig engine native tests  (sr %.0f, block 128, %d modules, %d sources)\n\n", rigtest::SR, rig::NMODTYPES, rig::NSRCTYPES);
  int ran = 0;
  for (auto& t : R.tests) {
    if (argc > 1 && !strstr(t.first.c_str(), argv[1])) continue;
    printf("%s\n", t.first.c_str()); t.second(); ran++;
  }
  printf("\n%s (%d test groups, %d failure%s)\n", R.fails ? "SOME TESTS FAILED" : "ALL TESTS PASSED", ran, R.fails, R.fails == 1 ? "" : "s");
  return R.fails ? 1 : 0;
}

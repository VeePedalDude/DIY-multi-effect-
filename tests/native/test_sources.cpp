// test_sources.cpp - modulation sources and routing.
#include "framework.h"
#include <cstring>
using namespace rigtest;

TEST("60 mod: LFO -> HOLOGRAM feedback swings base 0.45 by +/-0.22") {
  Engine e; e.init(SR); int h = mod(e, "holo"); e.conn[h][0] = src(e, "lfo");
  float lo = 1, hi = 0;
  for (int k = 0; k < (int)(8 * SR / 128); k++) { memset(e.inBuf, 0, sizeof e.inBuf); e.process(128); lo = fminf(lo, e.eff[h][0]); hi = fmaxf(hi, e.eff[h][0]); }
  CHECK(fabsf(lo - 0.23f) < 0.01f && fabsf(hi - 0.67f) < 0.01f, "effective range %.3f..%.3f over one 8 s LFO cycle", lo, hi);
}

TEST("61 mod: controller ext value reaches its target (clamped at 1)") {
  Engine e; e.init(SR); int c = mod(e, "cab"); e.src.list[src(e, "ctrl")]->ext(0.5f); e.conn[c][1] = src(e, "ctrl");
  for (int k = 0; k < 400; k++) { memset(e.inBuf, 0, sizeof e.inBuf); e.process(128); }
  CHECK(fabsf(e.eff[c][1] - 1.f) < 0.01f, "cab mix %.3f (0.90 + 0.22 x 0.5, clamped to 1)", e.eff[c][1]);
}

TEST("62 adsr: pick onsets on the riff") {
  Engine e; e.init(SR);
  render(e, riffWithNoise());
  int truth = countOnsetGroups(riff());
  CHECK(abs(e.src.adsr.onsets - truth) <= 2, "detected %d onsets, riff has %d pick events", e.src.adsr.onsets, truth);
}

TEST("63 orbit: bounded and moving over 10 minutes") {
  Engine e; e.init(SR); int o = src(e, "orbit"); float lo = 1, hi = -1; int jumps = 0; float last = 0;
  for (int k = 0; k < (int)(600 * SR / 128); k++) {
    memset(e.inBuf, 0, sizeof e.inBuf); e.process(128);
    float v = e.src.val[o]; lo = fminf(lo, v); hi = fmaxf(hi, v);
    if (fabsf(v - last) > 0.5f) jumps++;
    last = v;
  }
  CHECK(std::isfinite(lo) && hi - lo > 1.0f, "range %.2f..%.2f, %d jumps from resets", lo, hi, jumps);
}

TEST("64 heart: strap bpm used while fresh, falls back to 72 after 5 s") {
  Engine e; e.init(SR); int hs = src(e, "heart");
  e.src.list[hs]->ext(120.f);
  for (int k = 0; k < 100; k++) { memset(e.inBuf, 0, sizeof e.inBuf); e.process(128); }
  float live = e.src.heart.bpm;
  for (int k = 0; k < (int)(6 * SR / 128); k++) { memset(e.inBuf, 0, sizeof e.inBuf); e.process(128); }
  CHECK(live == 120.f && e.src.heart.bpm == 72.f, "live %.0f bpm, after silence %.0f bpm", live, e.src.heart.bpm);
}

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
  Engine e; e.init(SR); int c = mod(e, "cab"); e.rack.ext(src(e, "ctrl"), 0.5f); e.conn[c][1] = src(e, "ctrl");
  for (int k = 0; k < 400; k++) { memset(e.inBuf, 0, sizeof e.inBuf); e.process(128); }
  CHECK(fabsf(e.eff[c][1] - 1.f) < 0.01f, "cab mix %.3f (0.90 + 0.22 x 0.5, clamped to 1)", e.eff[c][1]);
}

TEST("62 adsr: pick onsets on the riff") {
  Engine e; e.init(SR);
  render(e, riffWithNoise());
  int truth = countOnsetGroups(riff());
  CHECK(abs(inst<PickAdsr>(e, "adsr").onsets - truth) <= 2, "detected %d onsets, riff has %d pick events", inst<PickAdsr>(e, "adsr").onsets, truth);
}

TEST("63 orbit: bounded and moving over 10 minutes") {
  Engine e; e.init(SR); int o = src(e, "orbit"); float lo = 1, hi = -1; int jumps = 0; float last = 0;
  for (int k = 0; k < (int)(600 * SR / 128); k++) {
    memset(e.inBuf, 0, sizeof e.inBuf); e.process(128);
    float v = e.rack.slot[o].val; lo = fminf(lo, v); hi = fmaxf(hi, v);
    if (fabsf(v - last) > 0.5f) jumps++;
    last = v;
  }
  CHECK(std::isfinite(lo) && hi - lo > 1.0f, "range %.2f..%.2f, %d jumps from resets", lo, hi, jumps);
}

TEST("64 heart: strap bpm used while fresh, falls back to 72 after 5 s") {
  Engine e; e.init(SR); int hs = src(e, "heart");
  e.rack.ext(hs, 120.f);
  for (int k = 0; k < 100; k++) { memset(e.inBuf, 0, sizeof e.inBuf); e.process(128); }
  float live = inst<Heart>(e, "heart").bpm;
  for (int k = 0; k < (int)(6 * SR / 128); k++) { memset(e.inBuf, 0, sizeof e.inBuf); e.process(128); }
  CHECK(live == 120.f && inst<Heart>(e, "heart").bpm == 72.f, "live %.0f bpm, after silence %.0f bpm", live, inst<Heart>(e, "heart").bpm);
}

// ----------------------------------------------------------------- source knobs (src_set)
static int srcKnob(Engine& e, int k, const char* sym) { int p = e.srcParamIndex(k, sym); if (p < 0) { printf("no knob %s\n", sym); exit(2); } return p; }
static void setReal(Engine& e, int k, const char* sym, float real) {           // set a source knob by its real value
  const ParamDesc& q = sourceTypeDesc(e.rack.slot[k].type).params[srcKnob(e, k, sym)];
  e.rack.slot[k].base[srcKnob(e, k, sym)] = q.curve == Curve::Log ? logf(real / q.min) / logf(q.max / q.min) : (real - q.min) / (q.max - q.min);
}
static std::vector<float> trace(Engine& e, int k, const std::vector<float>& in) {   // source value per block
  std::vector<float> v; for (size_t i = 0; i + 128 <= in.size(); i += 128) { memcpy(e.inBuf, &in[i], 512); e.process(128); v.push_back(e.rack.slot[k].val); } return v;
}

TEST("65 sources: every source describes its knobs, with defaults in range") {
  Engine e; e.init(SR); int total = 0; bool ok = true;
  for (int k = 0; k < NSRCTYPES; k++) {
    const SourceDesc& d = sourceTypeDesc(e.rack.slot[k].type); total += d.nparams;
    for (int p = 0; p < d.nparams; p++) { const ParamDesc& q = d.params[p]; ok = ok && q.def >= 0 && q.def <= 1 && q.max > q.min && (q.curve == Curve::Lin || q.min > 0); }
  }
  CHECK(ok && total >= NSRCTYPES && strstr(e.descJson, "\"symbol\":\"rate\""), "%d sources, %d knobs, all listed in describe()", NSRCTYPES, total);
}

TEST("66 sources: LFO rate and shape") {
  std::vector<float> z((size_t)(8 * SR), 0.f);
  Engine e; e.init(SR); int k = src(e, "lfo"); setReal(e, k, "rate", 2.f);      // 2 beats at 120 bpm = 1 Hz
  auto v = trace(e, k, z); int ups = 0; for (size_t i = 1; i < v.size(); i++) ups += v[i - 1] < 0 && v[i] >= 0;
  CHECK(ups >= 7 && ups <= 8, "RATE 2 beats at 120 bpm: %d cycles in 8 s (1 Hz)", ups);
  Engine e2; e2.init(SR); setReal(e2, k, "shape", 100.f); auto q = trace(e2, k, z);
  int full = 0; for (float x : q) full += fabsf(x) > 0.999f;
  CHECK(full > (int)(0.98 * q.size()), "SHAPE 100%%: square (%.1f%% of blocks at +/-1)", 100.0 * full / q.size());
}

TEST("67 sources: envelope release, ADSR attack, orbit speed, heart rest rate, controller dead zone") {
  // a 300 ms burst, then silence: value 120 ms after it ends
  std::vector<float> b((size_t)SR, 0.f); for (size_t i = 0; i < (size_t)(0.3f * SR); i++) b[i] = 0.5f * sinf(2 * (float)M_PI * 220 * i / SR);
  auto envAfter = [&](float rel) { Engine e; e.init(SR); int k = src(e, "env"); setReal(e, k, "release", rel); auto v = trace(e, k, b); return v[(size_t)(0.42f * SR / 128)]; };
  float fast = envAfter(15.f), slow = envAfter(1500.f);
  CHECK(fast < -0.9f && slow > 0.5f, "ENV RELEASE 15 ms -> %.2f, 1500 ms -> %.2f, 120 ms after the note", fast, slow);
  // ADSR: blocks from the first pick to the peak
  auto peakAt = [&](float att) { Engine e; e.init(SR); int k = src(e, "adsr"); setReal(e, k, "attack", att); auto v = trace(e, k, riffSignal()); size_t on = 0; while (on < v.size() && v[on] <= -0.999f) on++; size_t pk = on; while (pk < v.size() && v[pk] < 0.999f) pk++; return (pk - on) * 128 / SR * 1000; };
  float a2 = peakAt(2.f), a400 = peakAt(400.f);
  CHECK(a2 < 10 && a400 > 350 && a400 < 450, "ADSR ATTACK 2 ms peaks after %.0f ms, 400 ms after %.0f ms", a2, a400);
  // orbit: how much it moves in 20 s
  auto travel = [&](float sp) { Engine e; e.init(SR); int k = src(e, "orbit"); setReal(e, k, "speed", sp); auto v = trace(e, k, std::vector<float>((size_t)(20 * SR), 0.f)); double t = 0; for (size_t i = 1; i < v.size(); i++) t += fabs(v[i] - v[i - 1]); return t; };
  double t01 = travel(0.1f), t1 = travel(1.f), t5 = travel(5.f);
  CHECK(t01 < t1 && t1 < t5, "ORBIT SPEED 0.1x / 1x / 5x: travel %.1f / %.1f / %.1f in 20 s", t01, t1, t5);
  // heart: simulated rate
  { Engine e; e.init(SR); int k = src(e, "heart"); setReal(e, k, "rest", 110.f); trace(e, k, std::vector<float>(1280, 0.f)); float m[2]; e.rack.meters(k, m);
    CHECK(fabsf(m[0] - 110.f) < 0.5f, "HEART REST 110 bpm -> meter %.1f bpm", m[0]); }
  // controller: dead zone swallows a drifting stick, full throw still reaches 1
  { Engine e; e.init(SR); int k = src(e, "ctrl"); setReal(e, k, "deadzone", 20.f); e.rack.ext(k, 0.15f); auto v = trace(e, k, std::vector<float>((size_t)(0.5f * SR), 0.f));
    float drift = v.back(); e.rack.ext(k, 1.f); auto w = trace(e, k, std::vector<float>((size_t)(0.5f * SR), 0.f));
    CHECK(drift == 0.f && w.back() > 0.99f, "CTRL DEAD ZONE 20%%: stick at 0.15 -> %.3f, full throw -> %.3f", drift, w.back()); }
}

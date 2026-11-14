// test_levels.cpp - the patch level and the master after the chain, and the level meters (engine.h LevelMeter).
#include "framework.h"
using namespace rigtest;

static float sine(Engine& e, float db, float secs, float hz = 220.f) {        // feed a sine; returns the output RMS (dBFS) over the last half
  const float a = powf(10.f, db / 20.f); std::vector<float> s((size_t)(secs * SR));
  for (size_t i = 0; i < s.size(); i++) s[i] = a * sinf(2.f * (float)M_PI * hz * (float)i / SR);
  auto r = render(e, s); return 20.f * log10f(rms(r.L, s.size() / 2, s.size()));
}
static void dry(Engine& e) { e.init(SR); for (int m = 0; m < MAXMOD; m++) if (e.used(m)) { e.byp[m] = true; e.wet[m] = 0; } }

TEST("179 levels: patch level and master add, the meters read the patch before the master and the output after") {
  Engine e; dry(e); e.masterDb = 0; e.patchDb = 0;
  const float base = sine(e, -20.f, 1.f);
  CHECK(fabsf(base - (-23.01f)) < 0.2f, "a -20 dBFS sine passes the dry chain at about -23.0 dB RMS (got %.2f)", base);
  CHECK(fabsf(e.meters[M_LVL + LV_PATCH_RMS] - base) < 0.2f && fabsf(e.meters[M_LVL + LV_OUT_RMS] - base) < 0.2f, "patch and output RMS meters read it (%.2f, %.2f)", e.meters[M_LVL + LV_PATCH_RMS], e.meters[M_LVL + LV_OUT_RMS]);
  CHECK(fabsf(e.meters[M_LVL + LV_OUT_PK] - (-20.f)) < 0.2f, "the output peak meter reads -20 dBFS (%.2f)", e.meters[M_LVL + LV_OUT_PK]);
  Engine p; dry(p); p.patchDb = 9; p.masterDb = -4; p.masterS.y = powf(10.f, 5.f / 20.f);
  const float got = sine(p, -20.f, 1.f);
  CHECK(fabsf(got - (base + 5.f)) < 0.1f, "patch +9 dB and master -4 dB: +5 dB out (%.2f vs %.2f)", got, base);
  CHECK(fabsf(p.meters[M_LVL + LV_PATCH_RMS] - (base + 9.f)) < 0.2f, "the patch meter shows the patch's +9 dB, not the master's -4 (%.2f)", p.meters[M_LVL + LV_PATCH_RMS]);
  CHECK(p.meters[M_LVL + LV_LIMIT] == 0.f, "no limiting at -18 dBFS peaks");
  Engine h; dry(h); h.patchDb = 24; h.masterDb = 12; h.masterS.y = powf(10.f, 36.f / 20.f);
  sine(h, -20.f, 1.f);
  CHECK(h.meters[M_LVL + LV_LIMIT] > 6.f && h.meters[M_LVL + LV_OUT_PK] <= -0.17f, "+36 dB into the limiter: the meter shows it pulling %.1f dB, output peaks at %.2f dBFS", h.meters[M_LVL + LV_LIMIT], h.meters[M_LVL + LV_OUT_PK]);
  Engine q; dry(q); q.masterDb = 0; std::vector<float> z((size_t)SR, 0.f); render(q, z);
  CHECK(q.meters[M_LVL + LV_OUT_RMS] <= -119.f && q.meters[M_LVL + LV_PATCH_PK] <= -119.f, "silence reads -120 dB (the floor)");
}

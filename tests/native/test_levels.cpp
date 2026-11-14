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

TEST("180 hold: a preset change fades out, resets the modules while silent, and fades back in without a step") {
  Engine e; e.init(SR); e.masterDb = 0; e.masterS.y = 1;
  const auto& g = riffSignal(); const int B = 128, n = 48000 * 3;
  std::vector<float> out; int silentAt = -1, backAt = -1, holdAt = 48000, releaseAt = 48000 + 38 * 128;   // block boundaries
  for (int i = 0; i + B <= n; i += B) {
    if (i == holdAt) { e.hold(true); }
    if (i == holdAt + 1920) { e.patchDb = 18; for (int m = 0; m < MAXMOD; m++) if (e.used(m) && !strcmp(e.mid[m], "amp")) { e.base[m][0] = 1.f; e.base[m][1] = 1.f; } }   // hot amp, +18 dB, while silent
    if (i == releaseAt) e.hold(false);
    memcpy(e.inBuf, &g[i], B * sizeof(float)); e.process(B);
    for (int k = 0; k < B; k++) out.push_back(e.outL[k]);
    if (silentAt < 0 && e.holdState == Engine::HOLD_SILENT) silentAt = i + B;
    if (backAt < 0 && i > releaseAt && e.holdState == Engine::HOLD_NONE) backAt = i + B;
  }
  double gap = 0; for (int i = silentAt; i < releaseAt; i++) gap = fmax(gap, fabs(out[i]));
  CHECK(silentAt > 0 && silentAt - holdAt <= 0.012f * SR, "silent %.1f ms after hold 1 (fade 10 ms)", (silentAt - holdAt) * 1000.f / SR);
  CHECK(gap == 0.0, "nothing at all comes out while it's held (peak %.2g), even with the hot amp and +18 dB set meanwhile", gap);
  CHECK(backAt > 0 && fabsf((backAt - releaseAt) * 1000.f / SR - 100.f) < 6.f, "back to full level %.1f ms after hold 0 (80 ms settle + 20 ms fade)", (backAt - releaseAt) * 1000.f / SR);
  double step = 0; for (int i = releaseAt + 1; i < backAt; i++) step = fmax(step, fabs(out[i] - out[i - 1]));
  double steady = 0; for (int i = backAt + 4800; i < backAt + 48000 && i < (int)out.size(); i++) steady = fmax(steady, fabs(out[i] - out[i - 1]));
  CHECK(step <= steady, "the fade back in steps no more than the new preset's own playing (%.3f vs %.3f)", step, steady);
  Engine t; t.init(SR); t.hold(true); std::vector<float> z((size_t)(3.2f * SR), 0.f); render(t, z);
  CHECK(t.holdState != Engine::HOLD_SILENT && t.holdState != Engine::HOLD_FADING, "a hold nobody releases ends by itself after 3 s");
}

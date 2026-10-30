// test_orbit.cpp - THREE-BODY: the catalogue of periodic orbits, OUT, RATE/SYNC and CHAOS, with numbers.
#include "framework.h"
#include <chrono>
using namespace rigtest;

namespace {
const float DT = 128.f / SR;
// The source as it was before ORBIT...CHAOS (SPEED only), to prove the defaults bit-identical.
struct OldOrbit {
  float px[3], py[3], vx[3], vy[3];
  OldOrbit() { reset(); }
  void reset() { const float x0 = 0.97000436f, y0 = -0.24308753f, vx3 = -0.93240737f, vy3 = -0.86473146f;
    px[0] = -x0 + 0.02f; py[0] = -y0; px[1] = x0; py[1] = y0; px[2] = 0; py[2] = 0; vx[2] = vx3; vy[2] = vy3; vx[0] = vx[1] = -vx3 / 2; vy[0] = vy[1] = -vy3 / 2; }
  void step(float dt) {
    float ax[3] = {0}, ay[3] = {0};
    for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) if (a != b) { float dx = px[b] - px[a], dy = py[b] - py[a], r2 = dx * dx + dy * dy + 0.0025f; float inv = 1.f / (r2 * sqrtf(r2)); ax[a] += dx * inv; ay[a] += dy * inv; }
    for (int a = 0; a < 3; a++) { vx[a] += ax[a] * dt; vy[a] += ay[a] * dt; }
    for (int a = 0; a < 3; a++) { px[a] += vx[a] * dt; py[a] += vy[a] * dt; }
    float cx = (px[0] + px[1] + px[2]) / 3, cy = (py[0] + py[1] + py[2]) / 3;
    for (int a = 0; a < 3; a++) if (fabsf(px[a] - cx) > 4 || fabsf(py[a] - cy) > 4) { reset(); break; }
  }
  float update(float dt, float speed) { const float span = dt * speed; for (float acc = 0; acc < span; acc += 0.002f) step(0.002f);
    float dx = px[0] - px[1], dy = py[0] - py[1]; return tanhf((sqrtf(dx * dx + dy * dy) - 1.2f) * 1.5f); }
};
int ki(const char* sym) { for (int i = 0; i < ORBIT_DESC.nparams; i++) if (!strcmp(ORBIT_PARAMS[i].symbol, sym)) return i; printf("no knob %s\n", sym); exit(2); }
float norm(const char* sym, float real) { const ParamDesc& q = ORBIT_PARAMS[ki(sym)]; return q.curve == Curve::Log ? logf(real / q.min) / logf(q.max / q.min) : (real - q.min) / (q.max - q.min); }
struct Bench {
  Orbit o; float p[MAXPAR]; float bpm = 120.f; float in[128] = {0};
  Bench() { o.init(SR); for (int i = 0; i < ORBIT_DESC.nparams; i++) p[i] = ORBIT_PARAMS[i].def; }
  Bench& set(const char* sym, float real) { p[ki(sym)] = norm(sym, real); return *this; }
  float step() { SourceContext c{in, 128, SR, DT, bpm, p}; return o.update(c); }
  std::vector<float> run(float secs) { std::vector<float> v; for (int i = 0; i < (int)lrintf(secs / DT); i++) v.push_back(step()); return v; }
  float meter(int m) { float x[2]; o.meters(x); return x[m]; }
};
float maxJump(const std::vector<float>& v, size_t a = 1, size_t b = SIZE_MAX) { float m = 0; for (size_t i = std::max<size_t>(a, 1); i < std::min(b, v.size()); i++) m = fmaxf(m, fabsf(v[i] - v[i - 1])); return m; }
// One orbit synced to exactly 2 bars (4 s = 1500 blocks at 120 bpm), so cycle k and cycle k+1 line up block for block.
std::vector<float> synced(int orbit, int out, float chaos, float secs) { Bench b; b.set("orbit", (float)orbit).set("out", (float)out).set("sync", 1).set("rate", 8).set("chaos", chaos); return b.run(secs); }
const int CYC = 1500;
float cycleDiff(const std::vector<float>& v, int a, int b) { float d = 0; for (int i = 0; i < CYC; i++) d = fmaxf(d, fabsf(v[a * CYC + i] - v[b * CYC + i])); return d; }
}

TEST("130 orbit: the new knobs at their defaults sound exactly like the original THREE-BODY") {
  Bench b; OldOrbit o; long same = 0, total = 0; unsigned r = 9;
  for (int i = 0; i < (int)(120.f / DT); i++) {                 // 2 minutes, SPEED jumping every block (resets included)
    r = r * 1664525u + 1013904223u; b.p[0] = (r >> 8) / 16777216.f;
    same += b.step() == o.update(DT, ORBIT_PARAMS[0].map(b.p[0])); total++;
  }
  CHECK(same == total, "%ld of %ld blocks identical (==) with SPEED jumping every block", same, total);
}

TEST("131 orbit: every catalogued orbit repeats, cycle after cycle") {
  for (int k = 1; k <= NORBITS; k++) {
    auto v = synced(k, 0, 0.f, 6 * 4.f); float lo = 1, hi = -1; for (float x : v) { lo = fminf(lo, x); hi = fmaxf(hi, x); }
    const float d = cycleDiff(v, 1, 5);
    CHECK(d < 1e-3f && hi - lo > 1.5f, "%-13s m3 %.1f, T %6.3f: cycle 2 vs cycle 6 differ by at most %.1e; DISTANCE spans %+.2f..%+.2f",
          ORBITS[k - 1].name, ORBITS[k - 1].m3, ORBITS[k - 1].T, d, lo, hi);
  }
}

TEST("132 orbit: CHAOS 100% lets them run free: some hold their loop, the less stable ones leave it") {
  for (int k = 1; k <= NORBITS; k++) {
    auto exact = synced(k, 0, 0.f, 20 * 4.f), free = synced(k, 0, 100.f, 20 * 4.f);
    int left = -1; for (int c = 0; c < 20 && left < 0; c++) for (int i = 0; i < CYC; i++) if (fabsf(exact[c * CYC + i] - free[c * CYC + i]) > 0.1f) { left = c + 1; break; }
    bool finite = true; for (float x : free) finite = finite && std::isfinite(x) && x >= -1 && x <= 1;
    CHECK(finite, "%-13s run free for 20 cycles: %s", ORBITS[k - 1].name, left < 0 ? "still on its loop (within 0.1)" : (std::string("off its loop in cycle ") + std::to_string(left)).c_str());
  }
}

TEST("133 orbit: CHAOS nudges each cycle; the restart fades instead of jumping") {
  for (int k : {1, 4, 8}) {
    auto v0 = synced(k, 0, 0.f, 6 * 4.f), v = synced(k, 0, 40.f, 6 * 4.f);
    const float own = maxJump(v0, 2), j = maxJump(v, 2), d = cycleDiff(v, 2, 3);
    CHECK(d > 0.01f && j < 2.5f * own + 0.02f, "%-13s CHAOS 40%%: cycles 3 and 4 differ by up to %.3f; largest block step %.3f (exact loop: %.3f)", ORBITS[k - 1].name, d, j, own);
  }
}

TEST("134 orbit: OUT reads distance, body x, speed or closeness") {
  const char* nm[] = {"DISTANCE", "BODY X", "SPEED", "CLOSENESS"};
  for (int q = 0; q < 4; q++) {
    float worst = 3; const char* who = "";
    for (int k = 1; k <= NORBITS; k++) { auto v = synced(k, q, 0.f, 8.f); float lo = 1, hi = -1; for (size_t i = 10; i < v.size(); i++) { lo = fminf(lo, v[i]); hi = fmaxf(hi, v[i]); } if (hi - lo < worst) { worst = hi - lo; who = ORBITS[k - 1].name; } }
    CHECK(worst > 1.4f, "%-9s spans at least %.2f of -1..1 on every orbit (narrowest: %s)", nm[q], worst, who);
  }
  // CLOSENESS peaks at the near-collision: on the DRAGONFLY bodies pass within 0.007
  auto c = synced(7, 3, 0.f, 8.f); int peaks = 0; for (size_t i = CYC + 1; i < c.size(); i++) peaks += c[i - 1] < 0.9f && c[i] >= 0.9f;
  CHECK(peaks >= 1, "DRAGONFLY CLOSENESS: rises past 0.9 %d times per cycle, as bodies swing close", peaks);
  Bench b; b.set("orbit", 1); auto a = b.run(2.f); b.set("out", 3); auto z = b.run(0.2f); a.insert(a.end(), z.begin(), z.end());
  CHECK(maxJump(a, a.size() - z.size()) < 0.25f, "DISTANCE -> CLOSENESS on the FIGURE-8: largest block step %.3f (a 30 ms fade)", maxJump(a, a.size() - z.size()));
}

TEST("135 orbit: SYNC fits one orbit to RATE, whatever the orbit's own period") {
  for (int k : {1, 8, 9}) {
    Bench b; b.set("orbit", (float)k).set("sync", 1).set("rate", 4); b.run(1.f); const float p120 = b.meter(0); b.bpm = 90; b.run(1.f); const float p90 = b.meter(0);
    Bench f; f.set("orbit", (float)k).set("speed", 2); f.run(0.1f);
    CHECK(fabsf(p120 - 2.f) < 1e-3f && fabsf(p90 - 4 * 60 / 90.f) < 1e-3f && fabsf(f.meter(0) - ORBITS[k - 1].T / 2) < 1e-3f,
          "%-9s RATE 1 bar: %.3f s at 120 bpm, %.3f s at 90; FREE at SPEED 2: %.3f s (T %.3f / 2)", ORBITS[k - 1].name, p120, p90, f.meter(0), ORBITS[k - 1].T);
  }
  Bench b; b.set("orbit", 4).set("sync", 1).set("rate", 1); auto v = b.run(20.f); const float cyc = b.meter(1);
  CHECK(fabsf(cyc - 40.f) <= 1.f, "MOTH I at 1/4 note, 120 bpm: %.0f cycles in 20 s (40)", cyc);
  Bench t; t.set("orbit", 1).set("sync", 1).set("rate", 8); auto x = t.run(1.f); t.bpm = 60; auto y = t.run(1.f); x.insert(x.end(), y.begin(), y.end());
  CHECK(maxJump(x, x.size() - y.size() - 1, x.size() - y.size() + 2) <= maxJump(x, 2) * 1.01f, "a tempo change slows the orbit without a jump");
}

TEST("136 orbit: changing ORBIT crossfades over 30 ms") {
  float worst = 0; int worstFrom = 0, worstTo = 0;
  for (int from = 0; from <= NORBITS; from += 3) for (int to = 1; to <= NORBITS; to += 4) if (to != from) {
    Bench b; b.set("orbit", (float)from); auto a = b.run(1.3f); const float own = maxJump(a, 2); b.set("orbit", (float)to); auto z = b.run(0.1f);
    const float j = fmaxf(fabsf(z[0] - a.back()), maxJump(z)) - own; if (j > worst) { worst = j; worstFrom = from; worstTo = to; }
  }
  CHECK(worst < 0.25f, "worst extra block step at a change: %.3f (%s -> %s); an instant switch can be up to 2", worst, ORBIT_NAMES[worstFrom].name, ORBIT_NAMES[worstTo].name);
}

TEST("137 orbit: CPU per block") {
  for (int k : {0, 1, 8, 10}) for (float rate : {8.f, 0.5f}) {
    Bench b; b.set("orbit", (float)k).set("sync", 1).set("rate", rate); b.run(0.5f);
    auto t0 = std::chrono::steady_clock::now(); const int n = 20000; volatile float s = 0; for (int i = 0; i < n; i++) s += b.step();
    const double ns = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / n * 1e9;
    CHECK(ns < 50000, "%-11s one orbit per %-7s %7.0f ns per block (%.3f%% of one core)", ORBIT_NAMES[k].name, rate == 8.f ? "2 bars:" : "1/8:", ns, ns / (DT * 1e9) * 100);
  }
}

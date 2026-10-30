// test_geo.cpp - GEOMETRIC PULSE: shapes, star cycles, geometry-driven timing, the hyperbolic metric,
// clock sync, seeds, polymeter, parameter jumps, transport, outputs and CPU, with numbers.
#include "framework.h"
#include <chrono>
#include <array>
using namespace rigtest;

namespace {
int kidx(const char* s) { for (int i = 0; i < GEO_DESC.nparams; i++) if (!strcmp(GEO_PARAMS[i].symbol, s)) return i; printf("no knob %s\n", s); exit(2); }
int sidx(const char* s) { for (int i = 0; i < GEO_DESC.nsettings; i++) if (!strcmp(GEO_SETTINGS[i].symbol, s)) return i; printf("no setting %s\n", s); exit(2); }
float normOf(const ParamDesc& q, float r) { return q.curve == Curve::Log ? logf(r / q.min) / logf(q.max / q.min) : (r - q.min) / (q.max - q.min); }
struct Bench {
  GeoPulse g; float p[MAXPAR], cfg[MAXCFG] = {0}, tab[MAXROWS][MAXCOLS] = {{0}}; float bpm = 120.f; float in[128] = {0}; int block = 128;
  Bench() { g.init(SR); for (int i = 0; i < 8; i++) p[i] = GEO_PARAMS[i].def; for (int i = 0; i < GS_COUNT; i++) cfg[i] = GEO_SETTINGS[i].def;
    for (int r = 0; r < MAXROWS; r++) for (int c = 0; c < 5; c++) tab[r][c] = GEO_COLS[c].def; }
  Bench& k(const char* s, float r) { p[kidx(s)] = normOf(GEO_PARAMS[kidx(s)], r); return *this; }
  Bench& s(const char* n, float r) { cfg[sidx(n)] = normOf(GEO_SETTINGS[sidx(n)], r); return *this; }
  Bench& cell(int row, int col, float r) { tab[row][col] = normOf(GEO_COLS[col], r); return *this; }
  float step() { SourceContext c{in, block, SR, block / SR, bpm, p, cfg, tab}; return g.update(c); }
  std::vector<float> run(double secs) { std::vector<float> v; const long n = lrint(secs * SR / block); for (long i = 0; i < n; i++) v.push_back(step()); return v; }
  std::vector<GeoPulse::Ev> events() const { std::vector<GeoPulse::Ev> e; const unsigned from = g.nlog > 256 ? g.nlog - 256 : 0; for (unsigned i = from; i < g.nlog; i++) e.push_back(g.log[i & 255]); return e; }
};
// Run in chunks, collecting every step from the log (it holds the latest 256).
std::vector<GeoPulse::Ev> collect(Bench& b, double secs) {
  std::vector<GeoPulse::Ev> all; unsigned seen = b.g.nlog; const long n = lrint(secs * SR / b.block);
  for (long i = 0; i < n; i++) { b.step(); while (seen < b.g.nlog) all.push_back(b.g.log[seen++ & 255]); }
  return all;
}
std::string seqText(const std::vector<GeoPulse::Ev>& e, size_t n) { std::string s; for (size_t i = 0; i < std::min(n, e.size()); i++) s += (i ? "," : "") + std::to_string(e[i].vertex); return s; }
}

TEST("140 geo: a seven-vertex polygon plays its cycle, each step on the sample") {
  Bench b; auto e = collect(b, 100 * 7 * 0.125);                 // 100 cycles of 7 x 1/16 at 120 bpm (0.125 s each step)
  bool order = true; double worst = 0;
  for (size_t i = 0; i < e.size(); i++) { order = order && e[i].vertex == (int)(i % 7) && e[i].fired; worst = fmax(worst, fabs(e[i].sample - i * 6000.0)); }
  CHECK(e.size() == 700 && order && worst < 1e-3, "%zu steps in 100 cycles, vertices %s... in order; worst distance from the 6000-sample grid %.1e samples", e.size(), seqText(e, 9).c_str(), worst);
  Bench c; c.block = 37; auto f = collect(c, 7 * 0.125 * 10); double w2 = 0; for (size_t i = 0; i < f.size(); i++) w2 = fmax(w2, fabs(f[i].sample - i * 6000.0));
  CHECK(f.size() == 70 && w2 < 1e-6, "with 37-sample blocks: %zu steps, worst %.1e samples off the grid (sample-indexed, not block-quantised)", f.size(), w2);
}

TEST("141 geo: star polygons, including VERTICES and SKIP with a common factor") {
  struct C { int n, k, cyc; const char* want; } cs[] = {
    {7, 3, 0, "0,3,6,2,5,1,4"}, {12, 5, 0, "0,5,10,3,8,1,6,11,4,9,2,7"}, {12, 4, 0, "0,4,8,0,4,8"}, {12, 4, 1, "0,4,8,1,5,9,2,6,10,3,7,11"},
    {10, 4, 0, "0,4,8,2,6,0"}, {10, 4, 1, "0,4,8,2,6,1,5,9,3,7"}, {9, 3, 1, "0,3,6,1,4,7,2,5,8"}};
  for (auto& x : cs) {
    Bench b; b.s("shape", GEO_STAR).s("verts", x.n).s("skip", x.k).s("cycles", x.cyc);
    auto e = collect(b, 0.125 * 12.5); const int len = (int)std::count(x.want, x.want + strlen(x.want), ',') + 1;
    const int g = geoGcd(x.n, x.k);
    CHECK(seqText(e, len) == x.want, "{%d/%d} gcd %d, %s: %s (%d steps per cycle)", x.n, x.k, g, x.cyc ? "ALL cycles" : "ONE cycle", seqText(e, len).c_str(), b.g.geo.steps);
  }
  { Bench b; b.s("verts", 5).s("dir", 1); auto e = collect(b, 0.125 * 10); CHECK(seqText(e, 10) == "4,3,2,1,0,4,3,2,1,0", "REVERSE: %s", seqText(e, 10).c_str()); }
  { Bench b; b.s("verts", 5).s("dir", 2); auto e = collect(b, 0.125 * 12); CHECK(seqText(e, 12) == "0,1,2,3,4,3,2,1,0,1,2,3", "PING-PONG (ends not repeated): %s", seqText(e, 12).c_str()); }
  { Bench b; b.s("verts", 5).s("dir", 3); auto e = collect(b, 0.125 * 5000); int hist[5] = {0}; for (auto& x : e) hist[x.vertex]++;
    int lo = 99999, hi = 0; for (int h : hist) { lo = std::min(lo, h); hi = std::max(hi, h); }
    CHECK(e.size() == 5000 && lo > 920 && hi < 1080, "RANDOM: %s... ; over 5000 steps each vertex %d-%d times (1000 expected)", seqText(e, 12).c_str(), lo, hi); }
  { Bench b; b.s("shape", GEO_CUBE); auto e = collect(b, 0.125 * 8); bool edges = true;
    for (size_t i = 0; i + 1 < e.size(); i++) { const float* a = GEO_POLY[4 + e[i].vertex]; const float* c = GEO_POLY[4 + e[i + 1].vertex];
      edges = edges && fabsf((a[0] - c[0]) * (a[0] - c[0]) + (a[1] - c[1]) * (a[1] - c[1]) + (a[2] - c[2]) * (a[2] - c[2]) - 4.f / 3.f) < 1e-4f; }
    CHECK(edges && b.g.geo.n == 8, "CUBE: 8 vertices, every step travels along one of its edges"); }
}

TEST("142 geo: geometry-driven timing changes the steps, never the cycle") {
  struct C { const char* name; int shape; float warp, curve, depth, timing; } cs[] = {
    {"WARPED 60%, ANGULAR", GEO_WARPED, 60, 0, 0, 1}, {"WARPED 60%, GEODESIC", GEO_WARPED, 60, 0, 0, 2}, {"POLYGON warp 80%, half way", GEO_POLYGON, 80, 0, 0, 0.5f},
    {"HYPERBOLIC curve 80% warp 50%, GEODESIC", GEO_HYPERBOLIC, 50, 80, 0, 2}, {"POLYGON depth 100% (helix), GEODESIC", GEO_POLYGON, 0, 0, 100, 2},
    {"ICOSAHEDRON depth 60% warp 30%, GEODESIC", GEO_ICOSA, 30, 0, 60, 2}};
  for (auto& x : cs) {
    Bench b; b.s("shape", x.shape).s("verts", 9).k("warp", x.warp).k("curve", x.curve).k("depth", x.depth).k("timing", x.timing);
    auto e = collect(b, 30.0); const int L = b.g.geo.steps; const double C = L * 0.25 * 0.5 * SR;   // samples per cycle
    double worst = 0, lo = 1e18, hi = 0;
    for (size_t i = 0; i + L < e.size(); i++) { worst = fmax(worst, fabs(e[i + L].sample - e[i].sample - C)); const double d = e[i + 1].sample - e[i].sample; lo = fmin(lo, d); hi = fmax(hi, d); }
    CHECK(worst < 1e-6 && hi / lo > 1.15, "%-42s every cycle %.0f samples (worst %.0e off); steps %.0f-%.0f samples (x%.2f)", x.name, C, worst, lo, hi, hi / lo);
  }
  Bench u; u.s("verts", 9); auto e = collect(u, 5.0); double lo = 1e18, hi = 0; for (size_t i = 0; i + 1 < e.size(); i++) { const double d = e[i + 1].sample - e[i].sample; lo = fmin(lo, d); hi = fmax(hi, d); }
  CHECK(hi - lo < 1e-6, "UNIFORM: every step %.0f samples", lo);
}

TEST("143 geo: HYPERBOLIC measures with the hyperbolic metric") {
  // a regular hyperbolic n-gon with circumradius rho has sides s: sinh(s/2) = sinh(rho) sin(pi/n)
  double worst = 0; int checked = 0;
  for (int n : {3, 5, 7, 12, 32}) for (float curve : {10.f, 50.f, 100.f}) {
    float k[MAXPAR], cfg[MAXCFG]; for (int i = 0; i < 8; i++) k[i] = GEO_PARAMS[i].def; for (int i = 0; i < GS_COUNT; i++) cfg[i] = GEO_SETTINGS[i].def;
    k[GK_CURVE] = curve / 100.f; cfg[GS_SHAPE] = normOf(GEO_SETTINGS[GS_SHAPE], GEO_HYPERBOLIC); cfg[GS_VERTS] = normOf(GEO_SETTINGS[GS_VERTS], (float)n);
    GeoShapeData g; const GeoIn in = GeoShapeData::read(k, cfg); g.build(in);
    const double rho = 0.02 + 3.98 * in.curve / 1024.0, want = 2 * asinh(sinh(rho) * sin(M_PI / n));
    for (int i = 0; i < n; i++) { worst = fmax(worst, fabs(g.dist(i, (i + 1) % n) / want - 1)); checked++; }
  }
  CHECK(worst < 2e-4, "%d sides of regular hyperbolic polygons (3-32 sides, curve 10-100%%): within %.1e of sinh(s/2) = sinh(rho) sin(pi/n)", checked, worst);
  // an irregular polygon: CURVE reshapes the rhythm. Near 0 the steps follow the straight edges; far out,
  // a hyperbolic edge is about the sum of its ends' distances from the centre, so the steps follow how far
  // each vertex reaches out instead.
  auto steps = [](float curve) { Bench b; b.s("shape", GEO_HYPERBOLIC).s("verts", 8).k("warp", 60).k("timing", 2).k("curve", curve);
    auto e = collect(b, 1.0); std::vector<double> d; for (int i = 0; i < 8; i++) d.push_back((e[i + 1].sample - e[i].sample) / 6000.0); return d; };
  auto s0 = steps(0), s50 = steps(50), s100 = steps(100); double c50 = 0, c100 = 0;
  for (int i = 0; i < 8; i++) { c50 = fmax(c50, fabs(s50[i] - s0[i])); c100 = fmax(c100, fabs(s100[i] - s0[i])); }
  auto txt = [](const std::vector<double>& v) { std::string s; for (double x : v) { char b[8]; snprintf(b, 8, "%.2f ", x); s += b; } return s; };
  CHECK(c50 > 0.1 && c100 > 0.2, "warp 60%%, GEODESIC, steps as multiples of 1/16: curve 0: %s| 100: %s(up to %.2f and %.2f of a step moved at 50 and 100%%)", txt(s0).c_str(), txt(s100).c_str(), c50, c100);
  { Bench b; b.s("shape", GEO_HYPERBOLIC).s("verts", 8).k("timing", 2).k("curve", 100); auto e = collect(b, 4.0); double lo = 1e18, hi = 0;
    for (size_t i = 0; i + 1 < e.size(); i++) { const double d = e[i + 1].sample - e[i].sample; lo = fmin(lo, d); hi = fmax(hi, d); }
    CHECK(hi - lo < 1.0, "a regular polygon has equal sides in any geometry: curve 100%%, no warp -> steps %.1f-%.1f samples", lo, hi); }
}

TEST("144 geo: gates and triggers stay on the master clock through tempo changes") {
  Bench b; b.s("verts", 5); double beats = 0, worst = 0; long blocks = 0; unsigned seen = 0; int n = 0; double gateWorst = 0;
  for (int seg = 0; seg < 6; seg++) {
    b.bpm = 90.f + 23.f * seg;                                  // a new tapped tempo every 2 s
    for (int i = 0; i < (int)(2 * SR / 128); i++) {
      const double startBeats = beats; b.step(); blocks++;
      while (seen < b.g.nlog) {
        const auto& ev = b.g.log[seen++ & 255];
        const double at = startBeats + (ev.sample - (blocks - 1) * 128.0) * b.bpm / 60.0 / SR;   // its beat position
        worst = fmax(worst, fabs(at / 0.25 - lrint(at / 0.25))); n++;
        gateWorst = fmax(gateWorst, fabs((b.g.gateEnd - ev.sample) - 0.5 * 0.25 * 60.0 / b.bpm * SR));
      }
      beats += 128.0 * b.bpm / 60.0 / SR;
    }
  }
  CHECK(worst * 0.25 < 1e-6, "%d steps at 90-205 bpm: every one within %.1e beats of the 1/16 grid", n, worst * 0.25);
  CHECK(gateWorst < 1e-3, "every gate lasts 50%% of its step at the tempo it fired in (worst %.1e samples off)", gateWorst);
}

TEST("145 geo: the same seed plays the same pattern, whatever the block size") {
  auto play = [](int seed, int block, int dir) { Bench b; b.block = block; b.s("seed", seed).s("verts", 11).s("dir", dir).k("prob", 50);
    for (int r = 0; r < 11; r++) b.cell(r, GC_PROB, 40.f + 5.f * r);
    auto e = collect(b, 30.0); std::vector<std::array<int, 4>> v;
    for (auto& x : e) v.push_back({x.vertex, x.step, (int)x.cycle, x.fired ? 1 : 0});
    return v; };
  auto a = play(7, 128, 0), b = play(7, 37, 0), c = play(7, 1, 0), d = play(8, 128, 0);
  auto trim = [](std::vector<std::array<int, 4>>& x, size_t n) { if (x.size() > n) x.resize(n); };   // runs end within a block of each other
  const size_t common = std::min({a.size(), b.size(), c.size()}); trim(a, common); trim(b, common); trim(c, common); trim(d, common);
  int fired = 0; for (auto& x : a) fired += x[3];
  const double expect = 0.5 * (40 + 90) / 2 / 100.0;
  CHECK(a == b && a == c && a != d, "seed 7: %zu steps identical at 128-, 37- and 1-sample blocks; seed 8 differs", a.size());
  CHECK(fabs((double)fired / a.size() - expect) < 0.05, "fired %d of %zu (%.1f%%; PROB 50%% x per-vertex 40-90%% = %.1f%%)", fired, a.size(), 100.0 * fired / a.size(), 100 * expect);
  auto r1 = play(3, 128, 3), r2 = play(3, 37, 3); trim(r1, std::min(r1.size(), r2.size())); trim(r2, r1.size());
  CHECK(r1 == r2, "RANDOM direction, seed 3: the same %zu steps at both block sizes", r1.size());
}

TEST("146 geo: polymeter and polyrhythm against the same clock") {
  Bench a, b; a.s("verts", 7); b.s("verts", 16); std::vector<double> a0, b0; unsigned sa = 0, sb = 0;
  for (int i = 0; i < (int)(56 * 0.5 * SR / 128) + 1; i++) {     // 56 beats at 120 bpm
    a.step(); b.step();
    while (sa < a.g.nlog) { auto& e = a.g.log[sa++ & 255]; if (e.step == 0) a0.push_back(e.sample); }
    while (sb < b.g.nlog) { auto& e = b.g.log[sb++ & 255]; if (e.step == 0) b0.push_back(e.sample); }
  }
  std::vector<double> both; for (double x : a0) for (double y : b0) if (fabs(x - y) < 1e-6) both.push_back(x);
  CHECK(both.size() == 3 && fabs(both[1] - 28 * 0.5 * SR) < 1e-6, "7 steps against 16 at 1/16: %zu and %zu cycles in 56 beats; they start together %zu times (beats %.0f, %.0f, %.0f)",
        a0.size(), b0.size(), both.size(), both.size() > 0 ? both[0] / SR * 2 : -1, both.size() > 1 ? both[1] / SR * 2 : -1, both.size() > 2 ? both[2] / SR * 2 : -1);
  Bench p; p.s("verts", 7).s("length", 4); auto e = collect(p, 4.0 - 128 / SR); double worst = 0;
  for (size_t i = 0; i < e.size(); i++) worst = fmax(worst, fabs(e[i].sample - i * (4 * 0.5 * SR / 7)));
  CHECK(e.size() == 14 && worst < 1e-6, "LENGTH 1 bar: 7 steps in every 4 beats (7 against 4), %zu steps in 2 bars, worst %.0e samples off", e.size(), worst);
}

TEST("147 geo: knobs jumping every block never fire extra steps; transport stop, play and reset") {
  Bench b; unsigned r = 5; int perBlockMax = 0; unsigned seen = 0; long total = 0;
  for (int i = 0; i < (int)(60 * SR / 128); i++) {
    for (const char* s : {"phase", "timing", "warp", "gate", "depth"}) { r = r * 1664525u + 1013904223u; b.p[kidx(s)] = (r >> 8) / 16777216.f; }
    b.step(); int n = (int)(b.g.nlog - seen); seen = b.g.nlog; total += n; perBlockMax = std::max(perBlockMax, n);
  }
  CHECK(fabs(total / 480.0 - 1) < 0.05 && perBlockMax <= 2, "PHASE, TIMING, WARP, GATE, DEPTH random every block for 60 s: %ld steps (480 without jumps), at most %d in one block", total, perBlockMax);
  // a naive version (window from last block's knobs) for comparison
  Bench s; s.s("verts", 7); s.run(1.0); s.g.ext(0.f); s.run(0.01); const unsigned at = s.g.nlog; s.run(2.0);
  const unsigned stopped = s.g.nlog - at; s.g.ext(1.f); s.run(1.01); const unsigned played = s.g.nlog - at;
  s.g.ext(2.f); const unsigned before = s.g.nlog; s.step(); const auto first = s.g.log[before & 255];
  CHECK(stopped == 0 && played == 8 && s.g.nlog > before && first.step == 0 && first.cycle == 0 && fabs(first.sample - (s.g.now - 128)) < 1e-9,
        "STOP: %u steps in 2 s; PLAY: %u in the next second; back to the start: step 0 fires on the first sample of that block", stopped, played);
}

TEST("148 geo: the six outputs, polarity, accents and smoothing") {
  auto range = [](const std::vector<float>& v) { float lo = 1, hi = -1; for (float x : v) { lo = fminf(lo, x); hi = fmaxf(hi, x); } return std::make_pair(lo, hi); };
  { Bench b; b.s("verts", 4).k("rate", 1).k("gate", 25); auto v = b.run(8.0); int hi = 0; for (float x : v) hi += x > 0;
    CHECK(fabs(hi / (double)v.size() - 0.25) < 0.01, "GATE 25%% at 1/4 notes: high %.1f%% of blocks", 100.0 * hi / v.size()); }
  { Bench b; b.s("verts", 4).s("out", 1); auto v = b.run(4.0 - 128 / SR); int hi = 0, rises = 0, longest = 0, run = 0; for (size_t i = 0; i < v.size(); i++) { hi += v[i] > 0; rises += v[i] > 0 && (i == 0 || v[i - 1] <= 0); run = v[i] > 0 ? run + 1 : 0; longest = std::max(longest, run); }
    CHECK(rises == 32 && longest <= 2, "TRIGGER: 32 steps -> %d pulses, each 1-2 blocks (5 ms; never shorter than a block)", rises); }
  { Bench b; b.s("verts", 4).k("rate", 4).s("out", 2).s("polar", 1); b.cell(1, GC_ACCENT, 1); auto v = b.run(8.0); float pk0 = 0, pk1 = 0;
    for (size_t i = 0; i < v.size(); i++) { const int stepIdx = (int)(i * 128 / SR / 2) % 4; if (stepIdx == 0) pk0 = fmaxf(pk0, v[i]); if (stepIdx == 1) pk1 = fmaxf(pk1, v[i]); }
    size_t at = 0; while (at < v.size() && v[at] < pk0 * 0.99f) at++; size_t dec = at; while (dec < v.size() && v[dec] > pk0 * 0.01f) dec++;
    CHECK(fabsf(pk0 - 0.8f) < 0.02f && fabsf(pk1 - 1.f) < 0.025f && fabs((dec - at) * 128 / SR * 1000 - 200) < 10,
          "ENVELOPE: peak %.2f (VEL 80%%), %.2f on the accented vertex; falls to 1%% in %.0f ms (DECAY 200)", pk0, pk1, (dec - at) * 128 / SR * 1000); }
  { Bench b; b.s("verts", 4).s("out", 3).s("polar", 1); b.cell(2, GC_VEL, 30); auto v = b.run(1.0);
    CHECK(fabsf(v[(int)(0.3 * SR / 128)] - 0.3f) < 1e-6f && fabsf(v[(int)(0.1 * SR / 128)] - 0.8f) < 1e-6f, "VELOCITY: held per step (0.80, then 0.30 on vertex 2)"); }
  { Bench b; b.s("verts", 8).s("out", 4); auto v = b.run(4.0 - 128 / SR); auto rg = range(v); int wraps = 0; for (size_t i = 1; i < v.size(); i++) wraps += v[i] < v[i - 1] - 1;
    CHECK(rg.first < -0.99f && rg.second > 0.99f && wraps == 3, "PHASE: ramps %.2f..%.2f, %d wraps in 4 s (one per 1-s cycle)", rg.first, rg.second, wraps); }
  { Bench b; b.s("verts", 5).s("out", 5).s("polar", 1); auto v = b.run(0.625); std::vector<float> lv; for (float x : v) if (lv.empty() || x != lv.back()) lv.push_back(x);
    CHECK(lv.size() == 5 && lv[0] == 0.f && lv[4] == 1.f, "VERTEX: %zu levels 0, 0.25 .. 1 for 5 vertices", lv.size()); }
  for (int pl = 0; pl < 3; pl++) { Bench b; b.s("out", 4).s("polar", pl); auto rg = range(b.run(3.0));
    CHECK(fabsf(rg.first - (pl == 0 ? -1 : pl == 1 ? 0 : -1)) < 0.01f && fabsf(rg.second - (pl == 2 ? 0 : 1)) < 0.01f, "POLARITY %s: %.2f..%.2f", GEO_POLARS[pl].name, rg.first, rg.second); }
  { Bench b; b.s("verts", 4).k("rate", 4).s("out", 4); auto a = b.run(0.5); const float before = a.back(); b.s("out", 0); auto z = b.run(0.02);   // PHASE about -0.5, the gate high (+1)
    float j = fabsf(z[0] - before); for (size_t i = 1; i < z.size(); i++) j = fmaxf(j, fabsf(z[i] - z[i - 1]));
    CHECK(j <= fabsf(1.f - before) * (128 / SR) / 0.010f * 1.01f && fabsf(z.back() - 1.f) < 1e-6f, "OUT PHASE -> GATE (%.2f -> +1): largest block step %.2f, a 10 ms fade", before, j); }
  { Bench b; b.s("smooth", 20); auto v = b.run(1.0); float j = 0; for (size_t i = 1; i < v.size(); i++) j = fmaxf(j, fabsf(v[i] - v[i - 1]));
    CHECK(j < 0.26f, "SMOOTH 20 ms on the gate: largest block step %.3f (2.0 without)", j); }
}

TEST("149 geo: CPU") {
  struct C { const char* name; int shape, n, dir; bool mod; } cs[] = {
    {"POLYGON 7", GEO_POLYGON, 7, 0, false}, {"HYPERBOLIC 32, GEODESIC", GEO_HYPERBOLIC, 32, 0, false},
    {"HYPERBOLIC 32, WARP moved every block", GEO_HYPERBOLIC, 32, 0, true}, {"DODECAHEDRON, RANDOM direction", GEO_DODECA, 0, 3, false}};
  for (auto& x : cs) {
    Bench b; b.s("shape", x.shape).s("dir", x.dir).k("timing", 2).k("rate", 0.125f); if (x.n) b.s("verts", x.n); b.run(0.5);
    const int n = 20000; volatile float sink = 0; auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < n; i++) { if (x.mod) b.p[GK_WARP] = (i % 997) / 997.f; sink += b.step(); }
    const double ns = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / n * 1e9;
    CHECK(ns < 20000, "%-38s %6.0f ns per block (%.3f%% of one core), 1/32 steps", x.name, ns, ns / (128 / SR * 1e9) * 100);
  }
}

// test_seq.cpp - GATE SEQ: squares, step length, swing, ratchets, Euclidean fill, shift, chance and
// seeds, directions, resync, transport, outputs and CPU, with numbers.
#include "framework.h"
#include <chrono>
#include <array>
using namespace rigtest;

namespace {
int kx(const char* s) { for (int i = 0; i < SEQ_DESC.nparams; i++) if (!strcmp(SEQ_PARAMS[i].symbol, s)) return i; printf("no knob %s\n", s); exit(2); }
int sx(const char* s) { for (int i = 0; i < SEQ_DESC.nsettings; i++) if (!strcmp(SEQ_SETTINGS[i].symbol, s)) return i; printf("no setting %s\n", s); exit(2); }
float nrm(const ParamDesc& q, float r) { return q.curve == Curve::Log ? logf(r / q.min) / logf(q.max / q.min) : (r - q.min) / (q.max - q.min); }
struct Seq {
  GateSeq g; float p[MAXPAR] = {0}, cfg[MAXCFG] = {0}, tab[MAXROWS][MAXCOLS] = {{0}}; float bpm = 120.f, in[128] = {0}; int block = 128;
  Seq() { g.init(SR); for (int i = 0; i < SEQ_DESC.nparams; i++) p[i] = SEQ_PARAMS[i].def; for (int i = 0; i < SS_COUNT; i++) cfg[i] = SEQ_SETTINGS[i].def;
    for (int r = 0; r < MAXROWS; r++) for (int c = 0; c < 6; c++) tab[r][c] = SEQ_COLS[c].def; }
  Seq& k(const char* s, float r) { p[kx(s)] = nrm(SEQ_PARAMS[kx(s)], r); return *this; }
  Seq& s(const char* n, float r) { cfg[sx(n)] = nrm(SEQ_SETTINGS[sx(n)], r); return *this; }
  Seq& cell(int row, int col, float r) { tab[row][col] = nrm(SEQ_COLS[col], r); return *this; }
  Seq& only(std::initializer_list<int> on) { for (int r = 0; r < 32; r++) tab[r][SC_ON] = 0; for (int r : on) tab[r][SC_ON] = 1; return *this; }
  float step() { SourceContext c{in, block, SR, block / SR, bpm, p, cfg, tab}; return g.update(c); }
  std::vector<float> run(double secs) { std::vector<float> v; const long n = lrint(secs * SR / block); for (long i = 0; i < n; i++) v.push_back(step()); return v; }
};
std::vector<GateSeq::Ev> collect(Seq& b, double secs) {
  std::vector<GateSeq::Ev> all; unsigned seen = b.g.nlog; const long n = lrint(secs * SR / b.block);
  for (long i = 0; i < n; i++) { b.step(); while (seen < b.g.nlog) all.push_back(b.g.log[seen++ & 255]); }
  return all;
}
std::vector<GateSeq::Ev> firedOf(const std::vector<GateSeq::Ev>& e) { std::vector<GateSeq::Ev> f; for (auto& x : e) if (x.fired) f.push_back(x); return f; }
std::string squares(const std::vector<GateSeq::Ev>& e, size_t n) { std::string s; for (size_t i = 0; i < std::min(n, e.size()); i++) s += (i ? "," : "") + std::to_string(e[i].square); return s; }
const double S16 = 0.25 * 0.5 * 48000;                           // samples in a 1/16 step at 120 bpm (6000)
}

TEST("150 seq: 16 squares at 1/16, every hit on its sample") {
  Seq b; auto e = collect(b, 8.0 - 128 / SR); auto f = firedOf(e); double worst = 0; bool order = true;
  for (size_t i = 0; i < f.size(); i++) { worst = fmax(worst, fabs(f[i].sample - i * S16)); order = order && f[i].square == (int)(i % 16); }
  CHECK(f.size() == 64 && order && worst < 1e-3, "%zu hits in 4 bars, squares 0..15 in turn, worst %.1e samples off the 6000-sample grid", f.size(), worst);
  Seq c; c.block = 37; auto g = firedOf(collect(c, 2.0 - 37 / SR)); double w2 = 0; for (size_t i = 0; i < g.size(); i++) w2 = fmax(w2, fabs(g[i].sample - i * S16));
  CHECK(g.size() == 16 && w2 < 1e-3, "37-sample blocks: %zu hits, worst %.1e samples off (sample-indexed)", g.size(), w2);
}

TEST("151 seq: the squares you switch on are the ones that play") {
  Seq b; b.only({0, 3, 6, 10, 12}); auto f = firedOf(collect(b, 4.0 - 128 / SR));
  CHECK(squares(f, 10) == "0,3,6,10,12,0,3,6,10,12" && fabs(f[1].sample - 3 * S16) < 1e-3, "squares 0, 3, 6, 10, 12 on: plays %s ... (square 3 at sample %.0f)", squares(f, 10).c_str(), f[1].sample);
  Seq c; c.s("steps", 5); auto g = firedOf(collect(c, 0.125 * 11));
  CHECK(squares(g, 11) == "0,1,2,3,4,0,1,2,3,4,0", "STEPS 5: %s (5 against the bar)", squares(g, 11).c_str());
  Seq d; d.s("steps", 4).s("dir", 1); auto h = collect(d, 0.125 * 8); Seq pp; pp.s("steps", 4).s("dir", 2); auto q = collect(pp, 0.125 * 10);
  CHECK(squares(h, 8) == "3,2,1,0,3,2,1,0" && squares(q, 10) == "0,1,2,3,2,1,0,1,2,3", "REVERSE %s; PING-PONG %s", squares(h, 8).c_str(), squares(q, 10).c_str());
  Seq r; r.s("steps", 8).s("dir", 3); auto x = collect(r, 0.125 * 4000); int hist[8] = {0}; for (auto& v : x) hist[v.square]++; int lo = 9999, hi = 0; for (int v : hist) { lo = std::min(lo, v); hi = std::max(hi, v); }
  CHECK(lo > 440 && hi < 560, "RANDOM over 4000 steps: each square %d-%d times (500 expected)", lo, hi);
}

TEST("152 seq: step length (RATE), tempo changes and swing") {
  for (float rate : {0.125f, 1.f / 3.f, 1.f, 4.f}) {
    Seq b; b.k("rate", rate); auto f = firedOf(collect(b, rate * 0.5 * 8 + 0.01)); const double want = rate * 0.5 * SR; double worst = 0;
    for (size_t i = 0; i < f.size(); i++) worst = fmax(worst, fabs(f[i].sample - i * want));
    CHECK(f.size() == 9 && worst < 1e-6, "RATE %-5s: hits every %.0f samples (%zu in 8 steps), worst %.1e off", SEQ_PARAMS[0].stops[SEQ_PARAMS[0].nearestStop(rate)].name, want, f.size(), worst);
  }
  Seq b; double beats = 0, worst = 0; long blocks = 0; unsigned seen = 0; int n = 0;
  for (int seg = 0; seg < 5; seg++) { b.bpm = 84.f + 31.f * seg;
    for (int i = 0; i < (int)(1.5 * SR / 128); i++) { const double st = beats; b.step(); blocks++;
      while (seen < b.g.nlog) { auto& ev = b.g.log[seen++ & 255]; const double at = st + (ev.sample - (blocks - 1) * 128.0) * b.bpm / 60.0 / SR; worst = fmax(worst, fabs(at / 0.25 - lrint(at / 0.25))); n++; }
      beats += 128.0 * b.bpm / 60.0 / SR; } }
  CHECK(worst * 0.25 < 1e-9, "%d steps across 84-208 bpm: each within %.0e beats of the 1/16 grid", n, worst * 0.25);
  Seq s; s.k("swing", 33.333f); auto f = firedOf(collect(s, 1.0));
  CHECK(fabs((f[1].sample - f[0].sample) / S16 - 4.0 / 3) < 1e-3 && fabs((f[2].sample - f[1].sample) / S16 - 2.0 / 3) < 1e-3 && fabs(f[2].sample - 2 * S16) < 1e-3,
        "SWING 33%%: pairs split %.3f / %.3f of two steps (a triplet feel); every second hit late, the beat itself on time", (f[1].sample - f[0].sample) / S16 / 2, (f[2].sample - f[1].sample) / S16 / 2);
}

TEST("153 seq: ratchets and per-step gate length") {
  Seq b; b.only({0, 4}).cell(0, SC_RATCHET, 3).cell(4, SC_RATCHET, 2); auto f = firedOf(collect(b, 0.125 * 6));
  CHECK(f.size() == 5 && fabs(f[1].sample - S16 / 3) < 1e-3 && fabs(f[2].sample - 2 * S16 / 3) < 1e-3 && fabs(f[4].sample - 4.5 * S16) < 1e-3,
        "square 0 ratchet 3: hits at 0, %.0f, %.0f samples; square 4 ratchet 2: at %.0f and %.0f", f[1].sample, f[2].sample, f[3].sample, f[4].sample);
  Seq g; g.only({0}).k("gate", 50).cell(0, SC_RATCHET, 3); g.step(); const double len = g.g.gateEnd;   // the first hit's gate
  Seq h; h.only({0}).k("gate", 60).cell(0, SC_GATE, 150); h.step(); const double l2 = h.g.gateEnd;
  CHECK(fabs(len - S16 / 6) < 1e-3, "ratchet 3, GATE 50%%: each gate %.0f samples (half of a third of the step)", len);
  CHECK(fabs(l2 - S16 * 0.9) < 1e-3, "GATE 60%% x the step's own GATE 150%%: %.0f samples (0.9 of the step)", l2);
}

TEST("154 seq: Euclidean FILL and SHIFT") {
  struct C { int steps, hits; const char* want; } cs[] = {{8, 3, "x..x..x."}, {8, 5, "x.xx.xx."}, {16, 4, "x...x...x...x..."}, {12, 5, "x..x.x..x.x."}, {7, 3, "x..x.x."}};
  for (auto& x : cs) {
    Seq b; b.s("steps", x.steps).s("pattern", 1).k("fill", x.hits); auto e = collect(b, 0.125 * x.steps - 0.001);
    std::string got(x.steps, '.'); for (auto& v : e) if (v.fired) got[v.square] = 'x';
    char w[40]; snprintf(w, sizeof w, "%s", x.want); for (int i = 0; i < x.steps; i++) w[i] = seqEuclid(i, x.hits, x.steps) ? 'x' : '.';
    CHECK(got == w, "%2d hits over %2d steps: %s", x.hits, x.steps, got.c_str());
  }
  Seq m; m.s("pattern", 1); unsigned r = 3; long total = 0; int maxBlock = 0; unsigned seen = 0;
  for (int i = 0; i < (int)(30 * SR / 128); i++) { r = r * 1664525u + 1013904223u; m.p[SK_FILL] = (r >> 8) / 16777216.f; r = r * 1664525u + 1013904223u; m.p[SK_SHIFT] = (r >> 8) / 16777216.f;
    m.step(); int n = (int)(m.g.nlog - seen); seen = m.g.nlog; total += n; maxBlock = std::max(maxBlock, n); }
  CHECK(maxBlock <= 1 && labs(total - 240) <= 1, "FILL and SHIFT random every block for 30 s: %ld steps (240), never more than %d in a block", total, maxBlock);
  Seq s; s.only({0}).k("shift", 3); auto f = firedOf(collect(s, 0.125 * 16 - 0.001));
  CHECK(f.size() == 1 && fabs(f[0].sample - 13 * S16) < 1e-3, "SHIFT 3: square 0 plays on step 14 of 16 (sample %.0f)", f.size() ? f[0].sample : -1);
}

TEST("155 seq: chance and seeds") {
  auto play = [](int seed, int block) { Seq b; b.block = block; b.s("seed", seed).k("prob", 70); for (int r = 0; r < 16; r++) b.cell(r, SC_PROB, 30.f + 4.f * r);
    auto e = collect(b, 30.0); std::vector<std::array<long long, 3>> v;
    for (auto& x : e) v.push_back({x.square, x.step, x.fired ? 1 : 0});
    return v; };
  auto a = play(9, 128), b = play(9, 37), c = play(9, 1), d = play(10, 128);
  const size_t n = std::min({a.size(), b.size(), c.size()}); a.resize(n); b.resize(n); c.resize(n); d.resize(n);
  int fired = 0; for (auto& x : a) fired += (int)x[2];
  CHECK(a == b && a == c && a != d, "seed 9: %zu steps identical at 128-, 37- and 1-sample blocks; seed 10 differs", n);
  CHECK(fabs(fired / (double)n - 0.7 * 0.6) < 0.04, "fired %.1f%% (PROB 70%% x per-square 30-90%% = 42%%)", 100.0 * fired / n);
}

TEST("156 seq: RESYNC, transport") {
  Seq b; b.s("steps", 5).s("resync", 1); auto e = collect(b, 4.0 - 128 / SR); std::string s; for (auto& v : e) s += std::to_string(v.square);
  CHECK(s == "0123401234012340" "0123401234012340" && e.size() == 32, "STEPS 5, RESYNC 1 BAR: %s (back to square 1 every bar, no double hit)", s.substr(0, 16).c_str());
  Seq t; t.run(1.0); t.g.ext(0); t.run(0.01); const unsigned at = t.g.nlog; t.run(2.0); const unsigned stopped = t.g.nlog - at;
  t.g.ext(1); t.run(1.01); const unsigned played = t.g.nlog - at - stopped; t.g.ext(2); const unsigned before = t.g.nlog; t.step(); const auto first = t.g.log[before & 255];
  CHECK(stopped == 0 && played == 8 && first.square == 0 && fabs(first.sample - (t.g.now - 128)) < 1e-9, "STOP: %u steps in 2 s; PLAY: %u in a second; back to step 1 fires on that block's first sample", stopped, played);
}

TEST("157 seq: outputs, VALUE and SLEW, polarity") {
  { Seq b; b.k("gate", 25); auto v = b.run(8.0); int hi = 0; for (float x : v) hi += x > 0; CHECK(fabs(hi / (double)v.size() - 0.25) < 0.02, "GATE 25%%: high %.1f%% of blocks", 100.0 * hi / v.size()); }
  { Seq b; b.s("out", 4).k("rate", 1); for (int r = 0; r < 16; r++) b.cell(r, SC_VALUE, -100.f + 13.f * r); auto v = b.run(2.0);
    CHECK(fabsf(v[10] + 1.f) < 1e-6f && fabsf(v[(int)(0.6 * SR / 128)] + 0.87f) < 1e-3f, "VALUE: steps -1.00, -0.87 ... from the squares' values, held per step"); }
  { Seq b; b.s("out", 4).k("rate", 4).k("slew", 100); b.cell(0, SC_VALUE, -100).cell(1, SC_VALUE, 100); auto v = b.run(2.6);
    size_t at = (size_t)(2.0 * SR / 128); size_t up = at; while (up < v.size() && v[up] < 1.f - 2 * 0.3679f) up++;
    CHECK(fabs((up - at) * 128 / SR * 1000 - 100) < 6, "SLEW 100 ms: VALUE -1 -> +1 is 63%% of the way after %.0f ms", (up - at) * 128 / SR * 1000); }
  { Seq b; b.s("out", 3).s("polar", 1).only({0, 1}).cell(1, SC_VEL, 35); auto v = b.run(0.2);
    CHECK(fabsf(v[(int)(0.05 * SR / 128)] - 0.8f) < 1e-6f && fabsf(v[(int)(0.18 * SR / 128)] - 0.35f) < 1e-6f, "VELOCITY: 0.80, then 0.35 on square 2"); }
  { Seq b; b.s("out", 2).s("polar", 1).k("rate", 4); auto v = b.run(1.0); float pk = 0; for (float x : v) pk = fmaxf(pk, x);
    CHECK(fabsf(pk - 0.8f) < 0.02f, "ENVELOPE: peak %.2f (VEL 80%%)", pk); }
  { Seq b; b.s("out", 5).s("polar", 1).s("steps", 5); auto v = b.run(0.62); std::vector<float> lv; for (float x : v) if (lv.empty() || x != lv.back()) lv.push_back(x);
    CHECK(lv.size() == 5 && lv[4] == 1.f, "STEP: %zu levels 0 .. 1 for 5 steps", lv.size()); }
  { Seq b; b.s("out", 1); auto v = b.run(2.0 - 128 / SR); int rises = 0; for (size_t i = 1; i < v.size(); i++) rises += v[i] > 0 && v[i - 1] <= 0; rises += v[0] > 0;
    CHECK(rises == 16, "TRIGGER: %d pulses for 16 steps", rises); }
}

TEST("158 seq: CPU") {
  struct C { const char* name; int steps; bool euclid, rat; } cs[] = {{"16 squares", 16, false, false}, {"32 squares, ratchet 4 everywhere, 1/32", 32, false, true}, {"EUCLID, FILL moved every block", 16, true, false}};
  for (auto& x : cs) {
    Seq b; b.s("steps", x.steps).s("pattern", x.euclid ? 1 : 0); if (x.rat) { b.k("rate", 0.125f); for (int r = 0; r < 32; r++) b.cell(r, SC_RATCHET, 4); }
    b.run(0.5); const int n = 20000; volatile float sink = 0; auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < n; i++) { if (x.euclid) b.p[SK_FILL] = (i % 33) / 32.f; sink += b.step(); }
    const double ns = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / n * 1e9;
    CHECK(ns < 20000, "%-40s %5.0f ns per block (%.3f%% of one core)", x.name, ns, ns / (128 / SR * 1e9) * 100);
  }
}

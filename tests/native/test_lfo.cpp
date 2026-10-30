// test_lfo.cpp - the LFO's knobs (WAVE, DEPTH, STEPS, HZ, SYNC, POLAR) and per-destination PHASE and
// DIV (core/tap.h), with measured numbers.
#include "framework.h"
using namespace rigtest;

namespace {
const float DT = 128.f / SR;
// The LFO as it was before WAVE...POLAR (RATE and SHAPE only), to prove the defaults bit-identical.
struct OldLfo {
  float ph = 0;
  float update(float dt, float bpm, float rate, float shapePct) {
    ph += dt * bpm / 60.f / rate; ph -= floorf(ph);
    const float s = shapePct * 0.01f, sine = sinf(2 * PI_F * ph);
    if (s <= 0.f) return sine;
    const float tri = ph < 0.25f ? 4 * ph : ph < 0.75f ? 2 - 4 * ph : 4 * ph - 4;
    if (s <= 0.5f) return sine + (tri - sine) * 2 * s;
    const float sq = ph < 0.5f ? 1.f : -1.f;
    return tri + (sq - tri) * (2 * s - 1);
  }
};
int knobIndex(const char* sym) { for (int i = 0; i < LFO_DESC.nparams; i++) if (!strcmp(LFO_PARAMS[i].symbol, sym)) return i; printf("no knob %s\n", sym); exit(2); }
float norm(const char* sym, float real) { const ParamDesc& q = LFO_PARAMS[knobIndex(sym)]; return q.curve == Curve::Log ? logf(real / q.min) / logf(q.max / q.min) : (real - q.min) / (q.max - q.min); }
// A bare LFO with its default knobs, then any changes; run() gives its value per block.
struct Bench {
  Lfo l; float p[MAXPAR]; float bpm = 120.f; float in[128] = {0};
  Bench() { l.init(SR); for (int i = 0; i < LFO_DESC.nparams; i++) p[i] = LFO_PARAMS[i].def; }
  Bench& set(const char* sym, float real) { p[knobIndex(sym)] = norm(sym, real); return *this; }
  float step() { SourceContext c{in, 128, SR, DT, bpm, p}; return l.update(c); }
  std::vector<float> run(float secs) { std::vector<float> v; for (int i = 0; i < (int)(secs / DT); i++) v.push_back(step()); return v; }
};
float maxJump(const std::vector<float>& v, size_t a = 1, size_t b = SIZE_MAX) { float m = 0; for (size_t i = std::max<size_t>(a, 1); i < std::min(b, v.size()); i++) m = fmaxf(m, fabsf(v[i] - v[i - 1])); return m; }
int upCrossings(const std::vector<float>& v, float at = 0.f) { int n = 0; for (size_t i = 1; i < v.size(); i++) n += v[i - 1] < at && v[i] >= at; return n; }
}

TEST("120 lfo: the new knobs at their defaults sound exactly like the original LFO") {
  // 60 s with RATE, SHAPE and the tempo moving every block (as cross-modulation moves them)
  Bench b; OldLfo o; long same = 0, total = 0; unsigned r = 5;
  for (int i = 0; i < (int)(60.f / DT); i++) {
    r = r * 1664525u + 1013904223u; b.p[0] = (r >> 8) / 16777216.f;
    r = r * 1664525u + 1013904223u; b.p[1] = (r >> 8) / 16777216.f;
    b.bpm = 60.f + 120.f * ((i / 400) % 3) / 2.f;
    const float n = b.step(), w = o.update(DT, b.bpm, LFO_PARAMS[0].map(b.p[0]), LFO_PARAMS[1].map(b.p[1]));
    same += n == w; total++;
  }
  CHECK(same == total, "%ld of %ld blocks identical (==) with RATE, SHAPE and tempo jumping every block", same, total);
}

TEST("121 lfo: WAVE, with a 10 ms crossfade on every change") {
  const char* names[] = {"MORPH", "SINE", "TRIANGLE", "SQUARE", "SAW UP", "SAW DOWN", "STEP RANDOM", "SMOOTH RANDOM", "EXP RISE"};
  for (int w = 1; w <= 8; w++) {
    Bench b; b.set("sync", 0).set("hz", 1.f).set("wave", (float)w); auto v = b.run(10.f);
    float lo = 1, hi = -1, rise = 0, fall = 0; int changes = 0, below = 0;
    for (size_t i = 8; i < v.size(); i++) { lo = fminf(lo, v[i]); hi = fmaxf(hi, v[i]); const float d = v[i] - v[i - 1]; if (d > 0) rise += d; else fall -= d; changes += d != 0.f; below += v[i] < 0.f; }
    bool ok = lo >= -1.f && hi <= 1.f && hi - lo > 1.f;
    char extra[96] = "";
    int up = 0, dn = 0; for (size_t i = 8; i < v.size(); i++) { up += v[i] > v[i - 1]; dn += v[i] < v[i - 1]; }
    if (w == 4) { ok = ok && dn >= 9 && dn <= 11; snprintf(extra, sizeof extra, "rises for %d blocks, drops in %d (once a cycle)", up, dn); }
    if (w == 5) { ok = ok && up >= 9 && up <= 11; snprintf(extra, sizeof extra, "falls for %d blocks, jumps up in %d (once a cycle)", dn, up); }
    if (w == 6) { ok = ok && changes >= 9 && changes <= 11; snprintf(extra, sizeof extra, "%d new values in 10 s (1 per cycle)", changes); }
    if (w == 7) { const float j = maxJump(v, 8); ok = ok && j < 0.05f; snprintf(extra, sizeof extra, "glides: largest block step %.3f", j); }
    if (w == 8) { const float f = (float)below / (v.size() - 8); ok = ok && fabsf(f - 0.83f) < 0.02f; snprintf(extra, sizeof extra, "below 0 for %.0f%% of the cycle (swells, then drops)", 100 * f); }
    CHECK(ok, "%-13s range %+.2f..%+.2f %s", names[w], lo, hi, extra);
  }
  // SINE -> SQUARE at a point where they differ by 0.4: the change fades, it doesn't jump
  Bench b; b.set("sync", 0).set("hz", 1.f).set("wave", 1); auto pre = b.run(0.1f);
  b.set("wave", 3); auto post = b.run(0.03f); pre.insert(pre.end(), post.begin(), post.end());
  const float j = maxJump(pre, pre.size() - post.size() - 1); int fade = 0; for (float x : post) if (x < 0.999f) fade++;
  CHECK(j < 0.15f && fade >= 3 && fade <= 5, "SINE -> SQUARE at 36 degrees (0.77 -> 1): largest block step %.3f, settles in %.1f ms", j, fade * DT * 1000);
}

TEST("122 lfo: DEPTH scales every destination, smoothed") {
  Bench b; b.set("depth", 50.f); auto v = b.run(16.f); float pk = 0; for (float x : v) pk = fmaxf(pk, fabsf(x));
  CHECK(fabsf(pk - 0.5f) < 0.005f, "DEPTH 50%%: peak %.3f", pk);
  Bench c; c.set("sync", 0).set("hz", 1.f); auto a = c.run(0.26f); c.set("depth", 0.f); auto z = c.run(0.1f); a.insert(a.end(), z.begin(), z.end());
  float settle = 0; for (size_t i = 0; i < z.size(); i++) if (fabsf(z[i]) > 0.01f) settle = (i + 1) * DT * 1000;
  CHECK(maxJump(a) < 0.25f && settle < 50.f, "DEPTH 100%% -> 0 at the peak: largest block step %.3f (from 1.0), under 1%% after %.0f ms", maxJump(a), settle);
}

TEST("123 lfo: STEPS turns any wave into a staircase") {
  for (int n : {4, 8, 16}) {
    Bench b; b.set("sync", 0).set("hz", 1.f).set("steps", (float)n); auto v = b.run(10.f);
    int changes = 0; float flat = 0; for (size_t i = 9; i < v.size(); i++) if (v[i] != v[i - 1]) changes++;
    const float hold = 10.f * 1000.f / changes; (void)flat;
    CHECK(fabsf(hold - 1000.f / n) < 1000.f / n * 0.05f, "STEPS %2d on a 1 Hz sine: %d steps in 10 s, each held %.1f ms (%.1f expected)", n, changes, hold, 1000.f / n);
  }
  Bench b; b.set("sync", 0).set("hz", 1.f).set("wave", 4).set("steps", 8); auto v = b.run(2.f); std::vector<float> lv;
  v.erase(v.begin(), v.begin() + 8);                   // past the 10 ms fade in from OFF
  for (float x : v) { bool seen = false; for (float y : lv) seen = seen || fabsf(x - y) < 1e-6f; if (!seen) lv.push_back(x); }
  CHECK(lv.size() == 8, "STEPS 8 on SAW UP: %zu levels (a rising ladder)", lv.size());
  Bench c; c.set("sync", 0).set("hz", 1.f); auto a = c.run(0.6f); c.set("steps", 2); auto z = c.run(0.1f); a.insert(a.end(), z.begin(), z.end());
  CHECK(maxJump(a, 2) < 0.2f, "STEPS OFF -> 2 at 216 degrees (-0.95 -> 0): largest block step %.3f", maxJump(a, 2));
}

TEST("124 lfo: SYNC follows the tempo, FREE runs at HZ") {
  auto cycles = [](float bpm, bool sync) { Bench b; b.bpm = bpm; b.set("sync", sync ? 1.f : 0.f).set("rate", 1.f).set("hz", 3.f); return upCrossings(b.run(10.f)); };
  const int s60 = cycles(60, true), s120 = cycles(120, true), f60 = cycles(60, false), f180 = cycles(180, false);
  CHECK(abs(s60 - 10) <= 1 && abs(s120 - 20) <= 1, "SYNC, RATE 1/4: %d cycles in 10 s at 60 bpm, %d at 120 bpm (1 and 2 Hz)", s60, s120);
  CHECK(abs(f60 - 30) <= 1 && f60 == f180, "FREE, HZ 3: %d cycles in 10 s at 60 bpm, %d at 180 bpm (ignores the tempo)", f60, f180);
  Bench b; b.set("rate", 8.f).set("hz", 0.5f); auto v = b.run(1.f); b.set("sync", 0.f); auto w = b.run(1.f); v.insert(v.end(), w.begin(), w.end());
  float m[2]; b.l.meters(m); const size_t sw = v.size() - w.size();
  const float own = maxJump(v, sw + 2), at = fabsf(v[sw] - v[sw - 1]);
  CHECK(at <= own * 1.01f && fabsf(m[0] - 0.5f) < 0.001f, "SYNC -> FREE (0.25 Hz -> 0.5 Hz): step at the switch %.4f, no bigger than its own %.4f (the phase carries on); meter %.2f Hz", at, own, m[0]);
  int rate = knobIndex("rate"); const ParamDesc& q = LFO_PARAMS[rate];
  CHECK(q.nstops == 17 && !strcmp(q.stops[q.nearestStop(1.55f)].name, "1/4.") && !strcmp(q.stops[q.nearestStop(8.f)].name, "2 bars"),
        "RATE stops: %d note values, 1.55 beats -> %s, 8 beats (default) -> %s", q.nstops, q.stops[q.nearestStop(1.55f)].name, q.stops[q.nearestStop(8.f)].name);
}

TEST("125 lfo: POLAR bipolar, UNI + and UNI -, crossfaded") {
  const char* nm[] = {"BIPOLAR", "UNI +", "UNI -"}; const float wl[] = {-1, 0, -1}, wh[] = {1, 1, 0};
  for (int p = 0; p < 3; p++) {
    Bench b; b.set("polar", (float)p).set("sync", 0).set("hz", 1.f); auto v = b.run(5.f); float lo = 1, hi = -1; for (size_t i = 8; i < v.size(); i++) { lo = fminf(lo, v[i]); hi = fmaxf(hi, v[i]); }
    CHECK(fabsf(lo - wl[p]) < 0.01f && fabsf(hi - wh[p]) < 0.01f, "%-7s range %+.2f..%+.2f", nm[p], lo, hi);
  }
  Bench b; b.set("sync", 0).set("hz", 1.f); auto a = b.run(0.75f); b.set("polar", 2); auto z = b.run(0.1f); a.insert(a.end(), z.begin(), z.end());
  CHECK(maxJump(a, 2) < 0.02f, "BIPOLAR -> UNI - at the trough (both -1): largest block step %.4f", maxJump(a, 2));
  Bench c; c.set("sync", 0).set("hz", 1.f); auto x = c.run(0.75f); c.set("polar", 1); auto y = c.run(0.1f); x.insert(x.end(), y.begin(), y.end());
  CHECK(maxJump(x, 2) < 0.3f, "BIPOLAR -> UNI + at the trough (-1 -> 0): largest block step %.3f, a 10 ms fade", maxJump(x, 2));
}

// ----------------------------------------------------------------- per-destination PHASE and DIV
namespace {
struct TapRig {                    // the default rig, LFO free at 1 Hz, patched to 4 HOLOGRAM knobs at base 0.5, depth 0.4
  Engine e; int l, h;
  TapRig() {
    e.init(SR); l = src(e, "lfo"); h = e.addModule("peq", nullptr, -1);   // 8 knobs to patch
    e.rack.slot[l].base[knobIndex("sync")] = 0.f; e.rack.slot[l].base[knobIndex("hz")] = norm("hz", 1.f);
    for (int p = 0; p < 4; p++) { e.base[h][p] = 0.5f; e.conn[h][p] = l; e.depth[h][p] = 0.4f; }
  }
  std::vector<std::vector<float>> run(float secs) {
    std::vector<std::vector<float>> v(4);
    for (int k = 0; k < (int)(secs / DT); k++) { memset(e.inBuf, 0, sizeof e.inBuf); e.process(128); for (int p = 0; p < 4; p++) v[p].push_back((e.eff[h][p] - 0.5f) / 0.4f); }
    return v;
  }
};
// Phase of x relative to y in degrees, from the correlations with y and with y a quarter cycle later.
// How far x runs ahead of y, in degrees (x(t) = y(t + lead)), from correlations with y now and with y a quarter cycle back.
float leadDeg(const std::vector<float>& x, const std::vector<float>& y, int quarter) {
  double c0 = 0, c1 = 0; for (size_t i = quarter; i < x.size(); i++) { c0 += x[i] * y[i]; c1 += x[i] * y[i - quarter]; }
  float d = -atan2f((float)c1, (float)c0) * 180.f / PI_F; return d < 0 ? d + 360.f : d;
}
}

TEST("126 taps: one LFO, four destinations at 0 / 90 / 180 / 270 degrees") {
  TapRig r; for (int p = 1; p < 4; p++) r.e.tap[r.h][p].set(90.f * p, 1.f);
  auto v = r.run(10.f); for (auto& x : v) x.erase(x.begin(), x.begin() + 200);   // past the 100 ms glide in
  const int q = (int)lrintf(0.25f / DT);
  float lead[4]; for (int p = 0; p < 4; p++) lead[p] = leadDeg(v[p], v[0], q);
  bool ok = true; for (int p = 1; p < 4; p++) ok = ok && fabsf(lead[p] - 90.f * p) < 3.f;
  CHECK(ok, "ahead of the first: %.1f / %.1f / %.1f degrees (90 / 180 / 270)", lead[1], lead[2], lead[3]);
}

TEST("127 taps: DIV multiplies the cycle and stays locked to it") {
  const float divs[] = {0.5f, 1.5f, 2.f, 3.f};
  TapRig r; for (int p = 0; p < 4; p++) r.e.tap[r.h][p].set(0.f, divs[p]);
  auto v = r.run(60.f);
  // locked: each patch against an ideal sine at DIV x the LFO's own cycles
  bool ok = true; char line[200]; int at = 0;
  for (int p = 0; p < 4; p++) {
    const int n = upCrossings(v[p]);
    at += snprintf(line + at, sizeof line - at, "%s%gx: %d cycles", p ? ", " : "", divs[p], n);
    ok = ok && abs(n - (int)lrintf(60.f * divs[p])) <= 1;
  }
  CHECK(ok, "in 60 s at 1 Hz: %s", line);
  TapRig s; s.e.tap[s.h][1].set(0.f, 1.5f); double worst = 0; s.run(0.5f);
  for (int k = 0; k < (int)(120.f / DT); k++) {
    memset(s.e.inBuf, 0, sizeof s.e.inBuf); s.e.process(128);
    unsigned n; float ph; s.e.rack.slot[s.l].s.load()->cycle(n, ph);
    const double ideal = fmod(((double)n + ph) * 1.5, 1.0), got = asin(fmax(-1.0, fmin(1.0, (s.e.eff[s.h][1] - 0.5f) / 0.4f)));
    const double want = asin(sin(2 * M_PI * ideal)); worst = fmax(worst, fabs(got - want));
  }
  CHECK(worst * 180 / M_PI < 1.0, "1.5x over 2 minutes (180 of its cycles): worst phase error %.3f degrees", worst * 180 / M_PI);
}

TEST("128 taps: turning PHASE or DIV glides, never jumps") {
  TapRig r; auto a = r.run(1.f);
  float normal = 0; for (int p = 0; p < 1; p++) normal = maxJump(a[p], 10);
  r.e.tap[r.h][0].set(180.f, 1.f); r.e.tap[r.h][1].set(0.f, 3.f);
  auto b = r.run(1.f);
  float j0 = fmaxf(fabsf(b[0][0] - a[0].back()), maxJump(b[0])), j1 = fmaxf(fabsf(b[1][0] - a[1].back()), maxJump(b[1]));
  // settled: within 1 degree of the new phase, i.e. the mirror of the untouched 0 degree patch (knob 4)
  int settle = 0; for (size_t i = 0; i < b[0].size(); i++) if (fabsf(b[0][i] + b[3][i]) > 2 * PI_F / 360.f) settle = (int)i + 1;
  CHECK(j0 < 0.15f && j1 < 0.15f, "PHASE 0 -> 180 and DIV 1 -> 3 at once: largest block step %.3f and %.3f (an instant switch: up to 2; the 1 Hz LFO's own: %.3f)", j0, j1, normal);
  CHECK(settle * DT < 0.6f, "PHASE on its new setting (within 1 degree) after %.0f ms", settle * DT * 1000);
}

TEST("129 taps: cross-modulation patches tap too; non-tapping sources ignore PHASE and DIV") {
  Engine e; e.init(SR); int l = src(e, "lfo"), o = src(e, "orbit"), env = src(e, "env");
  e.rack.slot[l].base[knobIndex("sync")] = 0.f; e.rack.slot[l].base[knobIndex("hz")] = norm("hz", 1.f);
  for (int p : {0}) { e.rack.patch(o, p, l, 0.3f); e.rack.tapSet(o, p, 180.f, 1.f); }
  float sum = 0, prev = 0; int n = 0;
  for (int k = 0; k < (int)(4.f / DT); k++) { memset(e.inBuf, 0, sizeof e.inBuf); e.process(128); if (k > 400) { sum = fmaxf(sum, fabsf((e.rack.slot[o].eff[0] - e.rack.slot[o].base[0]) / 0.3f + prev)); n++; } prev = e.rack.slot[l].val; }
  CHECK(sum < 0.002f, "LFO -> ORBIT speed at 180 degrees: the knob moves opposite the LFO (worst mismatch %.3f over %d blocks, one block late as all cross-mod)", sum, n);
  Engine f; f.init(SR); int h = mod(f, "holo"), envf = src(f, "env"); (void)env;
  f.conn[h][0] = envf; f.depth[h][0] = 0.3f; f.tap[h][0].set(90.f, 2.f); long same = 0, tot = 0;
  const auto& g = riffSignal();
  for (size_t i = 0; i + 128 <= g.size(); i += 128) { memcpy(f.inBuf, &g[i], 512); f.process(128); same += f.eff[h][0] == clampf(f.base[h][0] + 0.3f * f.rack.slot[envf].val, 0.f, 1.f); tot++; }
  CHECK(same == tot, "ENV FOLLOW (no taps) with PHASE 90, DIV 2 set: %ld of %ld blocks use its plain value", same, tot);
}

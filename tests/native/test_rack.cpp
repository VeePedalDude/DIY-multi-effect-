// test_rack.cpp - the modulation rack (engine/mod_rack.h): adding and removing source instances,
// cross-modulation, real-time safety and cost.
#include "framework.h"
#include <chrono>
using namespace rigtest;

static void block(Engine& e, const float* in = nullptr) { if (in) memcpy(e.inBuf, in, 512); else memset(e.inBuf, 0, sizeof e.inBuf); e.process(128); }
static int knobOf(const Engine& e, int k, const char* sym) { int p = e.srcParamIndex(k, sym); if (p < 0) { printf("no knob %s\n", sym); exit(2); } return p; }
static int add(Engine& e, const char* type, const char* id = nullptr) { return e.addSource(type, id); }
// upward zero crossings per second of a source's output
static float rateOf(Engine& e, int k, const std::vector<float>& in, float secs) {
  int cross = 0, blocks = (int)(secs * SR / 128); float last = e.rack.slot[k].val;
  for (int b = 0; b < blocks; b++) { block(e, in.empty() ? nullptr : &in[(b * 128) % (in.size() - 128)]); const float v = e.rack.slot[k].val; if (last < 0 && v >= 0) cross++; last = v; }
  return cross / secs;
}
static std::vector<float> sine(float hz, float amp, float secs) { std::vector<float> s((size_t)(secs * SR)); for (size_t i = 0; i < s.size(); i++) s[i] = amp * sinf(2 * PI_F * hz * i / SR); return s; }
static void patchDefaults(Engine& e) {          // the rig's default patches (as in the parity scenario)
  e.conn[mod(e, "amp")][0] = src(e, "env"); e.conn[mod(e, "holo")][0] = src(e, "lfo"); e.conn[mod(e, "holo")][1] = src(e, "orbit"); e.conn[mod(e, "tess")][1] = src(e, "adsr");
}

TEST("80 rack: a fresh rack is one of each type; adding sources nobody listens to changes nothing") {
  Engine e; e.init(SR); int nd = 0; const char* const* want = defaultRack(nd); bool ids = e.rack.count() == nd;
  for (int i = 0; i < nd; i++) ids = ids && e.rack.slot[i].type == sourceTypeIndex(want[i]) && !strcmp(e.rack.slot[i].id, want[i]) && strstr(e.rackJson, want[i]);
  CHECK(ids && strstr(e.descJson, "\"sourceTypes\":[") && strstr(e.descJson, "\"maxSources\":16") && strstr(e.descJson, "\"sourceCategories\":["), "%d instances from default_rack.txt, ids = type ids in slots 0..%d; describe() lists %d types in %d categories", e.rack.count(), nd - 1, NSRCTYPES, NSRCCATS);
  Engine* a = new Engine(); a->init(SR); patchDefaults(*a);
  Engine* b = new Engine(); b->init(SR); patchDefaults(*b); add(*b, "lfo"); add(*b, "orbit"); add(*b, "adsr");
  auto ra = render(*a, riffSignal()), rb = render(*b, riffSignal());
  CHECK(ra.L == rb.L && ra.R == rb.R, "three extra unpatched sources: output bit-identical over %.0f s", riffSignal().size() / SR);
  delete a; delete b;
}

TEST("81 rack: add and remove (ids, limits, clean-up)") {
  Engine e; e.init(SR);
  int l2 = add(e, "lfo"), l3 = add(e, "lfo"), w = add(e, "orbit", "wobble");
  CHECK(l2 >= 0 && !strcmp(e.rack.slot[l2].id, "lfo2") && !strcmp(e.rack.slot[l3].id, "lfo3") && !strcmp(e.rack.slot[w].id, "wobble"), "automatic ids lfo2, lfo3; a chosen id “wobble”");
  int dup = add(e, "lfo", "wobble"), caps = add(e, "lfo", "Bad"), modId = add(e, "lfo", "amp"), longId = add(e, "lfo", "abcdefghijklmnop"), noType = add(e, "theremin");
  CHECK(dup == RACK_BAD_ID && caps == RACK_BAD_ID && modId == RACK_BAD_ID && longId == RACK_BAD_ID && noType == RACK_BAD_TYPE, "refused: a taken id, capitals, a module's id, 16 characters, an unknown type");
  int last = 0; while (e.rack.count() < MAXSRC) last = add(e, "env");
  CHECK(last >= 0 && add(e, "env") == RACK_FULL && e.rack.count() == MAXSRC, "%d instances fit; one more is refused (rack full)", MAXSRC);
  // patch lfo2 to a module knob and to another source's knob, then remove it
  const int h = mod(e, "holo"), envK = src(e, "env");
  e.conn[h][0] = l2; e.rack.patch(envK, knobOf(e, envK, "release"), l2, 0.4f); e.rack.patch(l3, 0, l2, 0.4f);
  for (int i = 0; i < 20; i++) block(e);
  CHECK(e.removeSource(l2) == 0 && e.conn[h][0] == -1 && e.rack.slot[envK].conn[knobOf(e, envK, "release")] == -1 && e.rack.slot[l3].conn[0] == -1 && e.sourceIndex("lfo2") < 0,
    "removing lfo2 unpatches the module knob and both source knobs it moved");
  block(e);
  CHECK(e.meters[M_SRC + l2] == 0.f && e.meters[M_SEFF + l2 * MAXPAR] == 0.f && !strstr(e.rackJson, "\"lfo2\""), "its slot reads 0 and it's gone from the rack list");
  CHECK(add(e, "heart") == l2 && !strcmp(e.rack.slot[l2].id, "heart2"), "the freed slot is reused (heart2)");
  e.removeSource(l3); block(e); add(e, "lfo");
  CHECK(e.rack.nRetired == 0, "removed instances are freed once the audio thread has moved on (%d waiting)", e.rack.nRetired);
  CHECK(e.removeSource(l2 + 100) == RACK_NO_SUCH && e.removeSource(-1) == RACK_NO_SUCH, "removing a slot that isn't there: refused");
}

TEST("82 rack: instances are independent; the same settings give the same output") {
  Engine e; e.init(SR);
  const int a = src(e, "lfo"), b = add(e, "lfo"), c = add(e, "lfo");
  e.rack.slot[a].base[0] = e.rack.slot[b].base[0] = 0.25f;                        // 2 beats per cycle
  e.rack.slot[c].base[0] = 0.125f;                                                // 1 beat per cycle
  bool same = true; int crossA = 0, crossC = 0; float la = 0, lc = 0;
  for (int i = 0; i < (int)(8 * SR / 128); i++) {
    block(e); same = same && e.rack.slot[a].val == e.rack.slot[b].val;
    crossA += la < 0 && e.rack.slot[a].val >= 0; crossC += lc < 0 && e.rack.slot[c].val >= 0;
    la = e.rack.slot[a].val; lc = e.rack.slot[c].val;
  }
  CHECK(same && abs(crossA - 8) <= 1 && abs(crossC - 16) <= 1, "lfo and lfo2 at 2 beats: identical; lfo3 at 1 beat: %d cycles in 8 s vs %d (120 bpm)", crossC, crossA);
  const int h2 = add(e, "heart"); e.rack.ext(h2, 100.f);
  for (int i = 0; i < 40; i++) block(e);
  CHECK(inst<Heart>(e, "heart").bpm == 72.f && inst<Heart>(e, "heart2").bpm == 100.f, "a strap on heart2 (100 bpm) leaves heart on its own rate (%.0f bpm)", inst<Heart>(e, "heart").bpm);
}

TEST("83 cross-mod: an envelope moves an LFO's rate while you play") {
  Engine e; e.init(SR);
  const int l = src(e, "lfo"), en = src(e, "env");
  e.rack.patch(l, knobOf(e, l, "rate"), en, 0.5f);                                // RATE knob 0.5 +/- 0.5, from the envelope
  const float quiet = rateOf(e, l, {}, 4.f);                                       // silence: envelope -1, knob 0 = 0.5 beats per cycle
  const float knobQuiet = e.meters[M_SEFF + l * MAXPAR + 0];
  const float loud = rateOf(e, l, sine(220, 0.5f, 1.f), 30.f);                    // -6 dBFS: envelope ~0.8, knob ~0.9 = ~75 beats
  const float knobLoud = e.meters[M_SEFF + l * MAXPAR + 0];
  CHECK(fabsf(quiet - 4.f) < 0.3f && loud < 0.1f, "LFO %.2f Hz in silence, %.3f Hz while a note plays (120 bpm)", quiet, loud);
  CHECK(knobQuiet == 0.f && fabsf(knobLoud - 0.9f) < 0.03f, "its effective RATE knob (published per slot): %.2f in silence, %.2f playing", knobQuiet, knobLoud);
  // depth 0 changes nothing
  Engine* p = new Engine(); p->init(SR); patchDefaults(*p); p->rack.patch(src(*p, "lfo"), 0, src(*p, "orbit"), 0.f);
  Engine* q = new Engine(); q->init(SR); patchDefaults(*q);
  auto rp = render(*p, riffSignal()), rq = render(*q, riffSignal());
  CHECK(rp.L == rq.L, "a cross patch at depth 0: output bit-identical to no patch");
  delete p; delete q;
  // the knob follows the modulator's value from the block before, exactly
  Engine f; f.init(SR); const int o = src(f, "orbit"), l2 = add(f, "lfo");
  f.rack.patch(l2, 0, o, 0.3f); f.rack.patch(o, 0, l2, 0.3f);                      // a loop, both directions
  bool exact = true; float prevO = 0, prevL = 0;
  for (int i = 0; i < 2000; i++) {
    block(f);
    exact = exact && f.rack.slot[l2].eff[0] == clampf(f.rack.slot[l2].base[0] + 0.3f * prevO, 0.f, 1.f) && f.rack.slot[o].eff[0] == clampf(f.rack.slot[o].base[0] + 0.3f * prevL, 0.f, 1.f);
    prevO = f.rack.slot[o].val; prevL = f.rack.slot[l2].val;
  }
  CHECK(exact, "in a loop (orbit <-> lfo2) each knob = base + depth x the other's value one block earlier, exactly");
}

TEST("84 cross-mod: loops and self-patches stay bounded for 10 minutes") {
  Engine* e = new Engine(); e->init(SR); patchDefaults(*e);
  const int l = src(*e, "lfo"), o = src(*e, "orbit"), en = src(*e, "env"), ad = src(*e, "adsr"), l2 = add(*e, "lfo");
  e->rack.patch(l, 0, l, 1.f);                       // an LFO moving its own rate, full depth
  e->rack.patch(o, 0, l, 1.f); e->rack.patch(l2, 0, o, 1.f); e->rack.patch(l, 1, l2, 1.f);
  e->rack.patch(en, 1, ad, 1.f); e->rack.patch(ad, 0, en, 1.f);    // envelope <-> ADSR
  e->rack.patch(ad, 3, ad, 1.f);
  float lo[MAXSRC], hi[MAXSRC]; for (int k = 0; k < MAXSRC; k++) { lo[k] = 1; hi[k] = -1; }
  bool finite = true; const auto& g = riffWithNoise();
  for (int i = 0; i < (int)(600 * SR / 128); i++) {
    block(*e, &g[(i * 128) % (g.size() - 128)]);
    for (int k = 0; k < MAXSRC; k++) if (e->rack.used(k)) { const float v = e->rack.slot[k].val; finite = finite && std::isfinite(v) && v >= -1 && v <= 1; lo[k] = fminf(lo[k], v); hi[k] = fmaxf(hi[k], v);
      for (int p = 0; p < MAXPAR; p++) finite = finite && e->rack.slot[k].eff[p] >= 0 && e->rack.slot[k].eff[p] <= 1; }
  }
  CHECK(finite && e->nanResets == 0 && hi[l] - lo[l] > 1.5f && hi[l2] - lo[l2] > 1.5f && hi[o] - lo[o] > 0.5f,
    "every value finite and in range, no NaN resets; still moving: lfo %.2f..%.2f, lfo2 %.2f..%.2f, orbit %.2f..%.2f", lo[l], hi[l], lo[l2], hi[l2], lo[o], hi[o]);
  delete e;
}

TEST("85 rack: never allocates while it plays, with sources coming and going") {
  Engine* e = new Engine(); e->init(SR); patchDefaults(*e);
  const auto& g = riffSignal(); long during = 0; unsigned rnd = 5; int adds = 0, removes = 0, patches = 0;
  auto next = [&] { rnd = rnd * 1664525u + 1013904223u; return rnd >> 8; };
  for (size_t i = 0, b = 0; i + 128 <= g.size(); i += 128, b++) {
    if (b % 40 == 0) { if (add(*e, sourceTypeDesc((int)(next() % NSRCTYPES)).id) >= 0) adds++; }          // control thread
    if (b % 55 == 0) { int k = (int)(next() % MAXSRC); if (e->rack.used(k) && e->removeSource(k) == 0) removes++; }
    if (b % 13 == 0) { int k = (int)(next() % MAXSRC), f = (int)(next() % MAXSRC); if (e->rack.used(k) && e->rack.used(f) && e->rack.slot[k].nparams() > 0) { e->rack.patch(k, (int)(next() % e->rack.slot[k].nparams()), f, 0.5f); patches++; }
      int m = e->order.s[next() % e->order.n]; if (e->rack.used(f)) e->conn[m][0] = f; }
    memcpy(e->inBuf, &g[i], 512);
    during += heapCallsDuring([&] { e->process(128); });                                                    // audio thread
  }
  if (heapCounting()) CHECK(during == 0, "heap calls inside process() over %.0f s with %d adds, %d removes, %d cross patches: %ld", g.size() / SR, adds, removes, patches, during);
  else CHECK(true, "heap counting needs glibc; skipped");
  delete e;                                                                                                 // ASan / leak check: everything freed
}

TEST("86 rack: what a full rack costs") {
  const auto& g = riffSignal();
  auto cpu = [&](bool full) {
    Engine* e = new Engine(); e->init(SR); patchDefaults(*e);
    if (full) { for (int t = 0; e->rack.count() < MAXSRC; t++) add(*e, sourceTypeDesc(t % NSRCTYPES).id);
      for (int k = 1; k < MAXSRC; k++) e->rack.patch(k, 0, k - 1, 0.3f); }
    double best = 1e9;
    for (int r = 0; r < 3; r++) { auto t0 = std::chrono::steady_clock::now(); render(*e, g); best = fmin(best, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count()); }
    delete e; return 100 * best / (g.size() / SR);
  };
  const double d = cpu(false), f = cpu(true);
  printf("      whole engine: %.2f%% of one core with the default 6 sources, %.2f%% with %d (3 orbits) all cross-patched\n", d, f, MAXSRC);
  CHECK(f - d < 3.0, "a full rack adds %.2f%% of one core", f - d);
}

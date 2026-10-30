// test_fxchain.cpp - the dynamic effect chain (engine.h, fx_chain.h): adding, removing and moving
// module instances while it plays, without clicks or allocation on the audio thread.
#include "framework.h"
#include <chrono>
using namespace rigtest;

static std::vector<float> sine(float hz, float amp, float secs) { std::vector<float> s((size_t)(secs * SR)); for (size_t i = 0; i < s.size(); i++) s[i] = amp * sinf(2 * (float)M_PI * hz * i / SR); return s; }
static std::string chainIds(const Engine& e) { std::string s; for (int i = 0; i < e.order.n; i++) { if (i) s += ' '; s += e.mid[e.order.s[i]]; } return s; }

TEST("04 fx chain: a fresh chain is one of each type; a module added bypassed changes nothing") {
  Engine e; e.init(SR); int nd = 0; const char* const* want = defaultChain(nd); bool ok = e.order.n == nd;
  for (int i = 0; i < nd; i++) ok = ok && e.order.s[i] == i && !strcmp(e.mid[i], want[i]) && e.mtype[i] == moduleTypeIndex(want[i]);
  CHECK(ok && strstr(e.descJson, "\"moduleTypes\":[") && strstr(e.descJson, "\"maxModules\":12") && strstr(e.descJson, "\"moduleCategories\":["),
    "chain: %s (default_chain.txt, slots 0..%d); describe() lists %d types in %d categories", chainIds(e).c_str(), nd - 1, NMODTYPES, NMODCATS);
  Engine* a = new Engine(); a->init(SR); Engine* b = new Engine(); b->init(SR);
  const int h = b->addModule("holo", nullptr, 1); b->byp[h] = true;                    // added, but bypassed before it ever sounds
  auto ra = render(*a, riffSignal()), rb = render(*b, riffSignal());
  CHECK(ra.L == rb.L && ra.R == rb.R, "a second HOLOGRAM added (bypassed) after the gate: output bit-identical over %.0f s", riffSignal().size() / SR);
  delete a; delete b;
}

TEST("05 fx chain: add, remove and move (ids, positions, limits, clean-up)") {
  Engine e; e.init(SR);
  const int h2 = e.addModule("holo", nullptr, -1), t2 = e.addModule("tess", "room", 0);
  CHECK(h2 >= 0 && !strcmp(e.mid[h2], "holo2") && !strcmp(e.mid[t2], "room") && chainIds(e) == "room gate amp cab holo tess holo2", "added holo2 at the end and “room” (a TESSERACT) first: %s", chainIds(e).c_str());
  const int dup = e.addModule("holo", "room"), srcId = e.addModule("holo", "lfo"), caps = e.addModule("holo", "Big"), none = e.addModule("flanger", nullptr);
  CHECK(dup == CHAIN_BAD_ID && srcId == CHAIN_BAD_ID && caps == CHAIN_BAD_ID && none == CHAIN_BAD_TYPE, "refused: a taken id, a source's id, capitals, an unknown type");
  CHECK(e.addSource("lfo", "room") == RACK_BAD_ID, "and a source can't take a module's id");
  e.moveModule(h2, 1);
  CHECK(chainIds(e) == "room holo2 gate amp cab holo tess", "moved holo2 to position 1: %s", chainIds(e).c_str());
  while (e.order.n < MAXMOD) e.addModule("gate", nullptr, -1);
  CHECK(e.addModule("gate", nullptr, -1) == CHAIN_FULL, "%d modules fit; one more is refused", MAXMOD);
  // patch a source to holo2, then remove it
  e.conn[h2][0] = src(e, "lfo");
  for (int i = 0; i < 10; i++) { memset(e.inBuf, 0, sizeof e.inBuf); e.process(128); }
  CHECK(e.removeModule(h2) == 0 && e.moduleIndex("holo2") < 0 && !strstr(e.chainJson, "holo2") && e.conn[h2][0] == -1, "removing holo2: gone from the chain and its patch dropped at once");
  CHECK(e.mods[h2] != nullptr && Order::unpack(e.wantOrder.load()).indexOf(h2) >= 0, "but it stays in the audio path while it fades out");
  for (int i = 0; i < 10; i++) { memset(e.inBuf, 0, sizeof e.inBuf); e.process(128); e.collect(false); }
  CHECK(e.mods[h2] == nullptr && Order::unpack(e.curOrder.load()).indexOf(h2) < 0, "faded (10 ms), out of the audio order, then freed");
  e.removeModule(e.moduleIndex("gate2")); e.collect(true);
  CHECK(e.addModule("cab", nullptr, -1) >= 0 && e.moduleIndex("cab2") >= 0, "the freed slot takes a new module (cab2)");
  CHECK(e.removeModule(99) == CHAIN_NO_SUCH && e.moveModule(-1, 0) == CHAIN_NO_SUCH, "removing or moving a slot that isn't there: refused");
}

// a 220 Hz sine through the chain; the largest sample-to-sample step shows any click
TEST("06 fx chain: no clicks when modules are added, removed or moved while it plays") {
  const auto s = sine(220, 0.25f, 6.f);
  auto setup = [](Engine& x) { x.init(SR); x.masterDb = 0; x.masterS.y = 1; x.byp[x.moduleIndex("gate")] = true; x.wet[x.moduleIndex("gate")] = 0; };
  Engine* a = new Engine(); setup(*a);
  auto steady = render(*a, s);
  const float base = maxStep(steady.L, 2 * SR, 6 * SR);
  Engine* b = new Engine(); setup(*b);
  auto r = render(*b, s, 128, [](Engine& x, int i) {
    if (i == 2 * 48000) x.addModule("holo", nullptr, 2);                               // a delay in, mid-note
    if (i == 3 * 48000) x.removeModule(x.moduleIndex("amp"));                          // the amp out
    if (i == 4 * 48000) x.moveModule(x.moduleIndex("tess"), 0);                        // the reverb to the front
    if (i == 5 * 48000) x.moveModule(x.moduleIndex("holo2"), -1);
    x.collect(false);
  });
  const float worst = maxStep(r.L, 1 * SR, 6 * SR), ampOut = rms(r.L, (size_t)(3.5f * SR), (size_t)(3.9f * SR)), before = rms(r.L, (size_t)(2.5f * SR), (size_t)(2.9f * SR));
  printf("      max step %.4f with an add, a remove and two moves; %.4f playing steadily\n", worst, base);
  CHECK(worst < base * 3.f, "no clicks: largest step %.2fx the steady signal's", worst / base);
  CHECK(fabsf(ampOut - before) > 0.01f && chainIds(*b) == "tess gate cab holo holo2", "the changes took effect (level %.3f -> %.3f with the amp out; chain %s)", before, ampOut, chainIds(*b).c_str());
  delete a; delete b;
}

TEST("07 fx chain: instances of one type are independent") {
  // HOLOGRAM then a second HOLOGRAM, nothing else; different feedback on each
  auto build = [](Engine& x) {
    x.init(SR); const int a = x.moduleIndex("holo"), b = x.addModule("holo", nullptr, -1, false);
    only(x, a); x.byp[b] = false; x.wet[b] = 1.f;
    x.base[a][0] = 0.2f; x.base[b][0] = 0.9f; fx<Holo>(x, "holo").fbS.y = 0.2f; fx<Holo>(x, "holo2").fbS.y = 0.9f;
  };
  Engine* x = new Engine(); build(*x); auto r1 = render(*x, riffSignal());
  Engine* y = new Engine(); build(*y); auto r2 = render(*y, riffSignal());
  const float f1 = fx<Holo>(*x, "holo").fbS.y, f2 = fx<Holo>(*x, "holo2").fbS.y;
  CHECK(&fx<Holo>(*x, "holo") != &fx<Holo>(*x, "holo2") && fabsf(f1 - 0.2f) < 0.01f && fabsf(f2 - 0.9f) < 0.01f, "two HOLOGRAMs in series keep their own state (feedback %.2f and %.2f)", f1, f2);
  CHECK(r1.L == r2.L && peak(r1.L) > 0.01f && peak(r1.L) <= 1.f, "the same chain twice gives the same output (peak %.2f)", peak(r1.L));
  delete x; delete y;
}

TEST("08 fx chain: never allocates while it plays, with modules coming, going and moving") {
  Engine* e = new Engine(); e->init(SR);
  const auto& g = riffSignal(); long during = 0; unsigned rnd = 3; int adds = 0, removes = 0, moves = 0;
  auto next = [&] { rnd = rnd * 1664525u + 1013904223u; return rnd >> 8; };
  for (size_t i = 0, b = 0; i + 128 <= g.size(); i += 128, b++) {
    if (b % 45 == 0 && e->addModule(moduleTypeDesc((int)(next() % NMODTYPES)).id, nullptr, (int)(next() % (e->order.n + 1))) >= 0) adds++;   // control thread
    if (b % 60 == 0 && e->order.n > 2 && e->removeModule(e->order.s[next() % e->order.n]) == 0) removes++;
    if (b % 25 == 0 && e->order.n > 1 && e->moveModule(e->order.s[next() % e->order.n], (int)(next() % e->order.n)) == 0) moves++;
    e->collect(false);
    memcpy(e->inBuf, &g[i], 512);
    during += heapCallsDuring([&] { e->process(128); });                                                          // audio thread
  }
  bool finite = true; for (int k = 0; k < 128; k++) finite = finite && std::isfinite(e->outL[k]);
  if (heapCounting()) CHECK(during == 0 && finite, "heap calls inside process() over %.0f s with %d adds, %d removes, %d moves: %ld", g.size() / SR, adds, removes, moves, during);
  else CHECK(finite, "heap counting needs glibc; output finite");
  delete e;                                                                                                       // ASan / leak check: everything freed
}

TEST("09 fx chain: what a longer chain costs") {
  const auto& g = riffSignal();
  auto cpu = [&](int extra) {
    Engine* e = new Engine(); e->init(SR);
    const char* more[] = {"holo", "tess", "amp", "cab", "holo", "tess", "gate"};
    for (int k = 0; k < extra; k++) e->addModule(more[k % 7], nullptr, -1, false);
    double best = 1e9;
    for (int r = 0; r < 3; r++) { auto t0 = std::chrono::steady_clock::now(); render(*e, g); best = fmin(best, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count()); }
    delete e; return 100 * best / (g.size() / SR);
  };
  const double d = cpu(0), f = cpu(MAXMOD - NMODTYPES);
  printf("      whole engine: %.2f%% of one core with the default %d modules, %.2f%% with %d (2 more delays, 2 more reverbs...)\n", d, NMODTYPES, f, MAXMOD);
  CHECK(f < 100, "a full chain of %d runs at %.1f%% of one core", MAXMOD, f);
}

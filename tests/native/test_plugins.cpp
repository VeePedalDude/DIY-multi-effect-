// Plugins (core/plugin.h, core/plugin_clap.h): the example .rigfx bundles' native Linux .clap loaded at
// run time, as the rig-host loads them, and played like built-ins.
#include "framework.h"
#include "../../engine/plugins.h"
#include <cstdlib>
#include <sys/stat.h>
using namespace rigtest;

static int rig_plugin_load(const char* path) { return rig::loadClapPlugin(path); }
static const char* rig_plugin_status() { return rig::pluginStatus().c_str(); }

namespace {
// The bundle's Linux .clap, unpacked (tests run on Linux): build/plugins/<id><tag>.clap. A loaded library
// is never written over (that changes the code under it): another copy gets another tag, as rig-host
// names its copies by their content.
std::string unpack(const char* id, const char* tag = "") {
  mkdir("build/plugins", 0755);
  const std::string out = std::string("build/plugins/") + id + tag + ".clap";
  const std::string cmd = std::string("unzip -p dist/plugins/") + id + ".rigfx clap/x86_64-linux.clap > " + out + " 2>/dev/null";
  return system(cmd.c_str()) == 0 ? out : std::string();
}
}

TEST("170 plugins: a .rigfx effect loads at run time and plays like the built-in it was copied from") {
  const std::string path = unpack("trem");
  CHECK(!path.empty(), "dist/plugins/trem.rigfx unpacked (tools/build_plugin.py plugins/trem makes it)");
  const int t = rig_plugin_load(path.c_str());
  CHECK(t >= NMODTYPES, "loaded: type %d, \"%s\"", t, rig_plugin_status());
  if (t < 0) return;
  Engine e; e.init(SR);
  CHECK(strstr(e.descJson, "\"type\":\"trem\"") && strstr(e.descJson, "\"plugin\":{") && strstr(e.descJson, "\"tremolo\""), "describe() lists it, with its plugin details and tags");
  const int m = e.addModule("trem", nullptr, -1, false); only(e, m);
  CHECK(m >= 0 && !strcmp(e.mods[m]->desc().params[1].unit, "Hz") && e.mods[m]->desc().params[1].curve == Curve::Log, "in the chain; its RATE knob is the template's (Hz, log)");
  e.base[m][0] = 1.f; e.base[m][1] = 0.4f;                         // DEPTH 100%, RATE 3 Hz
  std::vector<float> s((size_t)(2 * SR)); for (size_t i = 0; i < s.size(); i++) s[i] = 0.3f * sinf(2 * (float)M_PI * 440 * i / SR);
  auto r = render(e, s);
  float lo = 1, hi = 0; for (size_t i = SR; i + 480 < r.L.size(); i += 480) { float p = peak(r.L, i, i + 480); lo = fminf(lo, p); hi = fmaxf(hi, p); }
  CHECK(lo < 0.02f && hi > 0.25f, "full depth swings the level %.3f..%.3f (the template's own test: < 0.02 and > 0.25)", lo, hi);
  float g[2] = {0, 0}; e.mods[m]->meters(g);
  CHECK(g[0] >= 0.f && g[0] <= 1.f, "its meter (gain) comes through: %.3f", g[0]);
  const long heap = heapCallsDuring([&] { for (int b = 0; b < 200; b++) { e.base[m][1] = (b % 50) / 50.f; e.process(128); } });
  CHECK(!heapCounting() || heap == 0, "heap calls while it plays with RATE moving: %ld", heap);
  const int m2 = e.addModule("trem", nullptr, -1, false);
  CHECK(m2 >= 0 && !strcmp(e.mid[m2], "trem2"), "a second instance: %s", m2 >= 0 ? e.mid[m2] : "-");
}

TEST("171 plugins: a .rigfx modulation source loads and moves a knob") {
  const std::string path = unpack("rand");
  const int t = rig_plugin_load(path.c_str());
  CHECK(t >= NSRCTYPES, "loaded: source type %d, \"%s\"", t, rig_plugin_status());
  if (t < 0) return;
  Engine e; e.init(SR);
  const int k = e.addSource("rand", nullptr);
  CHECK(k >= 0, "in the rack");
  if (k < 0) return;
  float lo = 1, hi = -1; int changes = 0; float last = 0;
  for (int b = 0; b < 2000; b++) { e.process(128); const float v = e.rack.slot[k].val; lo = fminf(lo, v); hi = fmaxf(hi, v); if (fabsf(v - last) > 1e-6f) changes++; last = v; }
  CHECK(lo < -0.3f && hi > 0.3f && changes > 4 && changes < 200, "a new value 2 per beat (120 bpm, 5.3 s): range %.2f..%.2f, %d steps", lo, hi, changes);
}

// How many types called `id` describe() lists (the type lists only, not the chain or rack).
static int listed(const Engine& e, const char* list, const char* id) {
  const char* a = strstr(e.descJson, list); if (!a) return -1;
  const char* b = strstr(a, "],\"modules\""); const char* b2 = strstr(a, "],\"sources\""); if (!b || (b2 && b2 < b)) b = b2;
  const std::string want = std::string("\"type\":\"") + id + "\""; int n = 0;
  for (const char* p = strstr(a, want.c_str()); p && (!b || p < b); p = strstr(p + 1, want.c_str())) n++;
  return n;
}
TEST("178 plugins: a newer file replaces the type, a removed one leaves the lists; what's playing keeps playing") {
  const std::string path = unpack("trem", "-178");
  if (!rig::livePlugin("trem")) rig_plugin_load(path.c_str());
  Engine e; e.init(SR);
  const int m1 = e.addModule("trem", nullptr, -1, false); only(e, m1);
  CHECK(m1 >= 0, "an instance of the first version");
  if (m1 < 0) return;
  e.base[m1][0] = 1.f; e.base[m1][1] = 0.4f;
  std::vector<float> s((size_t)SR); for (size_t i = 0; i < s.size(); i++) s[i] = 0.3f * sinf(2 * (float)M_PI * 440 * i / SR);
  auto swing = [&](Engine& en) { auto r = render(en, s); float lo = 1, hi = 0; for (size_t i = SR / 2; i + 480 < r.L.size(); i += 480) { float p = peak(r.L, i, i + 480); lo = fminf(lo, p); hi = fmaxf(hi, p); } return hi - lo; };
  const int t = rig_plugin_load(path.c_str());                   // the same id again: an update
  e.describe();
  CHECK(t >= 0 && strstr(rig_plugin_status(), "replaced"), "loading it again replaces it: \"%s\"", rig_plugin_status());
  CHECK(listed(e, "\"moduleTypes\"", "trem") == 1, "the effect list has it once (%d)", listed(e, "\"moduleTypes\"", "trem"));
  const float sw1 = swing(e);
  CHECK(sw1 > 0.2f, "the instance made before still plays its own code: level swings by %.3f", sw1);
  const int m2 = e.addModule("trem", nullptr, -1, false);
  CHECK(m2 >= 0 && e.mtype[m2] == t && e.mtype[m1] != t, "a new instance is the new version (type %d; the old instance keeps %d)", m2 >= 0 ? e.mtype[m2] : -1, e.mtype[m1]);
  CHECK(rig::retirePlugin("trem") == 0 && strstr(rig_plugin_status(), "removed"), "removed: \"%s\"", rig_plugin_status());
  e.describe();
  CHECK(listed(e, "\"moduleTypes\"", "trem") == 0, "it's gone from the effect list");
  CHECK(e.addModule("trem", nullptr, -1, false) == CHAIN_BAD_TYPE, "and can't be added");
  float sw2 = 0; { e.removeModule(m2); e.collect(true); sw2 = swing(e); }
  CHECK(sw2 > 0.2f, "the one in the chain plays on until it's taken out (swing %.3f)", sw2);
  CHECK(rig::retirePlugin("trem") == -1 && rig::retirePlugin("holo") == -1, "removing it twice, or a built-in, is refused");
  const int t3 = rig_plugin_load(path.c_str());
  CHECK(t3 >= 0 && strstr(rig_plugin_status(), "added"), "put back: \"%s\"", rig_plugin_status());
  const std::string rp = unpack("rand", "-178");
  if (!rig::livePlugin("rand")) rig_plugin_load(rp.c_str());
  Engine e2; e2.init(SR);
  const int k = e2.addSource("rand", nullptr);
  const int ts = rig_plugin_load(rp.c_str()); e2.describe();
  CHECK(k >= 0 && ts >= 0 && listed(e2, "\"sourceTypes\"", "rand") == 1, "a source is replaced the same way (listed once)");
  for (int b = 0; b < 50; b++) e2.process(128);
  CHECK(e2.rack.slot[k].type >= 0 && e2.rack.slot[k].nparams() == sourceTypeDesc(e2.rack.slot[k].type).nparams, "the old source keeps its own layout and runs");
}

TEST("181 plugins: a preset change (hold) resets a plugin where it stands: no heap calls on the audio thread") {
  const std::string path = unpack("trem", "-181");
  if (rig_plugin_load(path.c_str()) < 0) { CHECK(false, "trem loads: %s", rig_plugin_status()); return; }
  Engine e; e.init(SR); const int m = e.addModule("trem", nullptr, -1, false);
  CHECK(m >= 0, "trem in the chain");
  if (m < 0) return;
  const auto& g = riffSignal(); int at = 0;
  auto run = [&](int blocks) { for (int b = 0; b < blocks; b++) { memcpy(e.inBuf, &g[at], 64 * sizeof(float)); at += 64; e.process(64); } };
  run(200);
  bool silent = false;
  const long heap = heapCallsDuring([&] { e.hold(true); run(40); silent = e.holdState == Engine::HOLD_SILENT; e.hold(false); run(60); });
  CHECK(silent && e.holdState == Engine::HOLD_NONE, "the hold went silent and came back");
  CHECK(!heapCounting() || heap == 0, "heap calls across the change (64-frame blocks): %ld", heap);
  float pk = 0; run(100); for (int i = 0; i < 64; i++) pk = fmaxf(pk, fabsf(e.outL[i]));
  CHECK(std::isfinite(pk), "it plays on");
}

TEST("183 plugins: after a preset change a plugin's knob smoothers start on the new preset, as built-in effects' do") {
  // A plugin is its own binary with its own copy of g_snapSmoothing (dsp.h), which the engine can't set;
  // the SDK (sdk/rigfx.h) sets it for the 10 ms after a reset. TREMOLO resets its smoothers to 50% at 3 Hz.
  const std::string path = unpack("trem", "-183");
  if (rig_plugin_load(path.c_str()) < 0) { CHECK(false, "trem loads: %s", rig_plugin_status()); return; }
  Engine e; e.init(SR); const int m = e.addModule("trem", nullptr, -1, false);
  if (m < 0) { CHECK(false, "trem in the chain"); return; }
  auto run = [&](int blocks) { for (int b = 0; b < blocks; b++) { for (int i = 0; i < 64; i++) e.inBuf[i] = 0.1f; e.process(64); } };
  e.base[m][0] = 0.2f; e.base[m][1] = 0.9f; run(400);
  e.hold(true); run(30); e.base[m][0] = 0.9f; e.base[m][1] = 0.f; e.hold(false);   // DEPTH 90%, RATE 0.5 Hz, SINE
  double worst = 0; float g[4];
  for (int b = 0; b < 47; b++) {                 // 62.7 ms: the gain should follow 1 - 0.45 (1 + sin(2 pi 0.5 t)) from the reset
    run(1); e.mods[m]->meters(g);
    const double t = (b + 1) * 64 / (double)SR, want = 1 - 0.45 * (1 + sin(2 * M_PI * 0.5 * t));
    worst = fmax(worst, fabs(20 * log10(g[0] / want)));
  }
  CHECK(worst < 0.05, "TREMOLO's gain is the new preset's from the reset on (worst %.2f dB off in 63 ms)", worst);
}

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
// The bundle's Linux .clap, unpacked (tests run on Linux): build/plugins/<id>.clap
std::string unpack(const char* id) {
  mkdir("build/plugins", 0755);
  const std::string out = std::string("build/plugins/") + id + ".clap";
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
  CHECK(rig_plugin_load(path.c_str()) == -2, "loading it again is refused: \"%s\"", rig_plugin_status());
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

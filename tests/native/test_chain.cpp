// test_chain.cpp - whole engine: stability, output ceiling, CPU, clicks, self-description.
#include "framework.h"
#include <chrono>
#include <cstring>
using namespace rigtest;

TEST("01 chain: full chain, defaults, 4 modulations patched") {
  Engine e; e.init(SR);
  e.conn[mod(e, "amp")][0] = src(e, "env"); e.conn[mod(e, "holo")][0] = src(e, "lfo");
  e.conn[mod(e, "holo")][1] = src(e, "orbit"); e.conn[mod(e, "tess")][1] = src(e, "heart");
  std::vector<float> long60; const auto& g = riffWithNoise();
  while (long60.size() < 60 * SR) long60.insert(long60.end(), g.begin(), g.end());
  long60.resize((size_t)(60 * SR));
  auto t0 = std::chrono::steady_clock::now();
  auto r = render(e, long60);
  double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  float pk = fmaxf(peak(r.L), peak(r.R));
  CHECK(e.nanResets == 0, "no NaN/Inf resets (%d)", e.nanResets);
  CHECK(pk <= 0.98f, "output peak %.3f (%.1f dBFS) <= 0.98 ceiling", pk, dbf(pk));
  printf("      output RMS %.1f dBFS for input RMS %.1f dBFS\n", dbf(rms(r.L, 0, r.L.size())), dbf(rms(long60, 0, long60.size())));
  printf("      native CPU: %.2f s for 60 s audio -> %.2f%% of one core (x%.0f realtime)\n", secs, 100 * secs / 60, 60 / secs);
}

TEST("02 chain: no clicks from bypass toggles or a tempo jump (220 Hz sine)") {
  std::vector<float> s((size_t)(6 * SR)); for (size_t i = 0; i < s.size(); i++) s[i] = 0.25f * sinf(2 * (float)M_PI * 220 * i / SR);
  Engine e; e.init(SR); int g = mod(e, "gate"); e.byp[g] = true; e.wet[g] = 0; e.masterDb = 0; e.masterS.y = 1;
  auto steady = render(e, s);
  float base = maxStep(steady.L, 2 * SR, 3 * SR);
  Engine e2; e2.init(SR); e2.byp[g] = true; e2.wet[g] = 0; e2.masterDb = 0; e2.masterS.y = 1;
  auto r = render(e2, s, 128, [](Engine& x, int i) {
    int a = x.moduleIndex("amp");
    if (i % 24000 < 128 && i > 48000) x.byp[a] = !x.byp[a];          // toggle amp every 0.5 s
    if (i == 4 * 48000) x.bpm = 70;                                    // tempo jump
  });
  float worst = maxStep(r.L, 1 * SR, 6 * SR);
  CHECK(worst < base * 3.f, "max step %.4f vs steady %.4f", worst, base);
}

TEST("03 chain: self-description matches the registries") {
  Engine e; e.init(SR);
  const char* j = e.descJson; size_t len = strlen(j);
  CHECK(len > 100 && len < sizeof(e.descJson) - 1, "JSON is %zu bytes (buffer %zu)", len, sizeof(e.descJson));
  int braces = 0, brackets = 0; bool inStr = false;
  for (size_t i = 0; i < len; i++) { char c = j[i]; if (c == '"' && (i == 0 || j[i - 1] != '\\')) inStr = !inStr; if (inStr) continue;
    braces += c == '{' ? 1 : c == '}' ? -1 : 0; brackets += c == '[' ? 1 : c == ']' ? -1 : 0; }
  CHECK(!inStr && braces == 0 && brackets == 0, "brackets and quotes balance");
  bool all = true;
  for (int m = 0; m < NMODTYPES; m++) { const auto& d = moduleTypeDesc(m);
    std::string k = std::string("\"type\":\"") + d.id + "\""; if (!strstr(j, k.c_str())) all = false;
    if (d.nparams < 1 || d.nparams > MAXPAR) all = false;
    for (int p = 0; p < d.nparams; p++) { std::string s = std::string("\"symbol\":\"") + d.params[p].symbol + "\""; if (!strstr(j, s.c_str())) all = false;
      if (d.params[p].def < 0 || d.params[p].def > 1) all = false;
      if (d.params[p].curve == Curve::Log && d.params[p].min <= 0) all = false; } }
  for (int k = 0; k < NSRCTYPES; k++) { std::string s = std::string("\"type\":\"") + sourceTypeDesc(k).id + "\""; if (!strstr(j, s.c_str())) all = false; }
  CHECK(all, "all %d module types and %d source types listed with valid parameter ranges", NMODTYPES, NSRCTYPES);
}

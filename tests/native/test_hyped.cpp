// HYPED FUZZ (engine/modules/drive/hyped.h): the circuit model from third_party/hypedfuzz, in the chain.
#include "framework.h"
#include "HypedFuzzRealtime.h"
#include <chrono>
using namespace rigtest;

namespace {
int addHyped(Engine& e) { const int m = e.addModule("hyped", nullptr, -1, false); only(e, m); return m; }
std::vector<float> slice(const std::vector<float>& x, double secs) { return std::vector<float>(x.begin(), x.begin() + (size_t)(secs * SR)); }
// set a stepped knob (MODE, ENGINE) to stop k of 3
float stop3(int k) { return k / 2.f; }
}

TEST("166 hyped: on 2x REF it plays exactly what the package plays on its own") {
  const auto in = slice(riffSignal(), 2.0);
  Hyped h; h.init(SR);                                                       // the module itself, outside the chain's fade-in
  float e[MAXPAR]; for (int k = 0; k < 8; k++) e[k] = HYPED_PARAMS[k].def;
  e[7] = 0.f;                                                                // ENGINE 2x REF: the package's own defaults
  h.engine = h.wantEngine = 0; Hyped::solverFor(*h.os2, 0); h.live = h.os2.get();
  std::vector<float> L(in.size()), R(in.size()); Context ctx;
  for (size_t i = 0; i < in.size(); i += 128) h.process(&in[i], &in[i], &L[i], &R[i], 128, e, ctx);
  hyped::Realtime ref(SR, 2); hyped::Controls c; ref.setControls(c);       // the package's defaults = the module's
  std::vector<float> y(in.size()); ref.processBlock(in.data(), y.data(), in.size());
  float d = 0; for (size_t i = 0; i < y.size(); i++) d = fmaxf(d, fabsf(y[i] - L[i]));
  // (not bit-exact: the module builds its circuit on 2x FAST, the default, and the solver's cached
  // factorisations differ in the last bits; 0 when built on REF)
  CHECK(d < 1e-6f, "largest difference from hyped::Realtime run directly: %.2g (output peak %.3f)", d, peak(L));
  CHECK(L == R, "both outputs carry it (a mono pedal)");
}

TEST("167 hyped: FULL, SCOOP and BOOST sound different; GAIN drives it harder") {
  const auto in = slice(riffSignal(), 2.0);
  double rm[3], crest[3];
  for (int k = 0; k < 3; k++) {
    Engine e; e.init(SR); const int m = addHyped(e); e.base[m][4] = stop3(k);
    auto r = render(e, in);
    rm[k] = rms(r.L, (size_t)(0.3 * SR), r.L.size()); crest[k] = peak(r.L, (size_t)(0.3 * SR)) / rm[k];
    CHECK(rm[k] > 0.005 && peak(r.L) < 2.f, "%s: rms %.3f, peak %.3f, crest %.1f", HYPED_MODES[k].name, rm[k], peak(r.L), crest[k]);
  }
  CHECK(crest[0] < crest[2] && crest[1] < crest[2], "the fuzz modes are squarer than BOOST (crest %.1f, %.1f vs %.1f)", crest[0], crest[1], crest[2]);
  double g[2];
  for (int k = 0; k < 2; k++) {
    Engine e; e.init(SR); const int m = addHyped(e); e.base[m][0] = k ? 1.f : 0.1f; e.base[m][3] = 0.5f;
    auto r = render(e, in); const size_t a = (size_t)(0.3 * SR);
    g[k] = peak(r.L, a) / rms(r.L, a, r.L.size());
  }
  CHECK(g[1] < g[0], "GAIN 10 flattens the riff more than GAIN 1 (crest %.2f vs %.2f)", g[1], g[0]);
}

TEST("168 hyped: knobs swept and MODE switched while playing: no allocation, no click, no failed solve") {
  Engine e; e.init(SR); const int m = addHyped(e);
  const auto in = slice(riffSignal(), 3.0);
  Run r; r.L.resize(in.size()); r.R.resize(in.size());                     // (sized here: only process() is counted)
  const long heap = heapCallsDuring([&] {
    for (size_t i = 0; i + 128 <= in.size(); i += 128) {
      const float t = (float)i / SR;
      e.base[m][0] = 0.5f + 0.5f * sinf(2.f * t); e.base[m][2] = 0.5f + 0.4f * sinf(3.1f * t);
      e.base[m][5] = 0.5f + 0.5f * sinf(1.3f * t); e.base[m][6] = 0.5f + 0.5f * sinf(0.7f * t);
      e.base[m][4] = stop3((int)(t / 0.75f) % 3);                           // a new MODE every 0.75 s
      memcpy(e.inBuf, &in[i], 128 * sizeof(float)); e.process(128);
      memcpy(&r.L[i], e.outL, 128 * sizeof(float)); memcpy(&r.R[i], e.outR, 128 * sizeof(float));
    }
  });
  CHECK(!heapCounting() || heap == 0, "heap calls in process while every knob moves: %ld", heap);
  bool finite = true; for (float v : r.L) finite = finite && std::isfinite(v);
  CHECK(finite, "output stays finite");
  // around each switch the output dips 5 ms each way: the steepest step there is no worse than in normal playing
  float atSwitch = 0, elsewhere = 0;
  for (int s = 1; s < 4; s++) { const size_t c = (size_t)(s * 0.75 * SR); atSwitch = fmaxf(atSwitch, maxStep(r.L, c, c + (size_t)(0.03 * SR))); }
  elsewhere = maxStep(r.L, (size_t)(0.1 * SR), (size_t)(0.7 * SR));
  CHECK(atSwitch <= elsewhere * 1.5f, "steepest sample step across MODE switches %.3f; while playing %.3f", atSwitch, elsewhere);
  auto& h = fx<Hyped>(e, "hyped");
  CHECK(h.live->failedSolves() == 0, "failed Newton solves: %llu", h.live->failedSolves());
}

TEST("169 hyped: ENGINE: 2x FAST matches 2x REF; what each costs per 128-sample block") {
  const auto in = slice(riffSignal(), 2.0);
  std::vector<float> out[3]; double ms[3];
  for (int k = 0; k < 3; k++) {
    Engine e; e.init(SR); const int m = addHyped(e); e.base[m][7] = stop3(k);
    render(e, slice(riffSignal(), 0.1));                                  // past the switch dip
    e.mods[m]->reset();
    const auto t0 = std::chrono::steady_clock::now();
    out[k] = render(e, in).L;
    ms[k] = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / (in.size() / 128.0);
  }
  float d = 0; for (size_t i = 0; i < in.size(); i++) d = fmaxf(d, fabsf(out[0][i] - out[1][i]));
  CHECK(d < 1e-3f, "2x FAST vs 2x REF: largest difference %.2g, %.0f dB below the peak (%.3f)", d, 20 * log10f(d / peak(out[0]) + 1e-30f), peak(out[0]));
  CHECK(rms(out[2], (size_t)(0.3 * SR), in.size()) > 0.005, "1x FAST plays");
  printf("      per 128-sample block (2.67 ms at 48 kHz): 2x REF %.2f ms (%.0f%%), 2x FAST %.2f ms (%.0f%%), 1x FAST %.2f ms (%.0f%%)\n",
         ms[0], 100 * ms[0] / 2.667, ms[1], 100 * ms[1] / 2.667, ms[2], 100 * ms[2] / 2.667);
}

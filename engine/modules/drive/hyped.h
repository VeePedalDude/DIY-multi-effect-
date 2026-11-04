// engine/modules/drive/hyped.h - HYPED FUZZ: PedalPCB's Hyped Fuzz (a Boss Hyper Fuzz FZ-2 circuit)
// as a circuit model: every resistor, capacitor, transistor, diode and op-amp of the schematic solved
// by modified nodal analysis with Newton's method, sample by sample, oversampled (third_party/hypedfuzz,
// v8, used unchanged). It's derived from the schematic with estimated semiconductor and op-amp
// parameters, not measured from a pedal: a model, not a verified clone.
//
// Knobs: the pedal's four (GAIN, BASS, TREBLE, LEVEL) and its three-way switch (MODE: FULL, SCOOP,
// BOOST); two of the package's circuit macros (OCTAVE moves R14 12k..8k, the octave-up character of
// the fuzz modes; BITE shrinks C11 15n..4.95n, FULL mode's low end); and ENGINE, how it's solved:
//   2x REF   2x oversampled, the package's reference solver (LinearSolver::Auto) and exact tanh/exp
//   2x FAST  2x oversampled, its CompactNewton solver and fast tanh (falls back to the reference
//            solver when a step doesn't converge): 18% cheaper natively, 10% in WebAssembly, and within
//            0.00025 of 2x REF (58 dB under the peak) on the test riff. The default: at 128-sample
//            buffers 2x REF alone takes 68% of the block natively and 87% in the browser
//   1x FAST  not oversampled: half the cost again, with the aliasing that brings
// It is the heaviest module in the rig: see docs and the test for measured CPU.
//
// The circuit models only move between audio blocks, on the audio thread (the package's setters
// aren't safe beside its process()): knob changes are smoothed and applied at block edges when they
// move enough to matter, and MODE and ENGINE switches dip the output for 5 ms each way so the jump
// can't click. Two circuit instances are built at init (2x and 1x), so nothing is allocated later.
#pragma once
#include <memory>
#include "../../core/module.h"
#include "HypedFuzzRealtime.h"
#include "HypedFuzzPerformance.h"

namespace rig {

inline constexpr ParamStop HYPED_MODES[] = {{0, "FULL"}, {1, "SCOOP"}, {2, "BOOST"}};
inline constexpr ParamStop HYPED_ENGINES[] = {{0, "2x REF"}, {1, "2x FAST"}, {2, "1x FAST"}};
inline constexpr ParamDesc HYPED_PARAMS[] = {
  {"gain",   "GAIN",   0.f, 10.f, 0.5f, "",  Curve::Lin, 1, false},
  {"bass",   "BASS",   0.f, 10.f, 0.5f, "",  Curve::Lin, 1, false},
  {"treble", "TREBLE", 0.f, 10.f, 0.5f, "",  Curve::Lin, 1, false},
  {"level",  "LEVEL",  0.f, 10.f, 0.5f, "",  Curve::Lin, 1, false},
  {"mode",   "MODE",   0.f, 2.f,  0.f,  "",  Curve::Lin, 0, false, HYPED_MODES, 3},
  {"octave", "OCTAVE", 0.f, 100.f, 0.5f, "%", Curve::Lin, 0, false},
  {"bite",   "BITE",   0.f, 100.f, 0.f,  "%", Curve::Lin, 0, false},
  {"engine", "ENGINE", 0.f, 2.f,  0.5f, "",  Curve::Lin, 0, false, HYPED_ENGINES, 3},
};
inline constexpr ModuleDesc HYPED_DESC = {"hyped", "HYPED FUZZ", "circuit model · Hyper Fuzz", 8, HYPED_PARAMS, 2, {"drive", "solver"}};

struct Hyped : Module {
  std::unique_ptr<hyped::Realtime> os2, os1;       // built in init(), never in process()
  hyped::Realtime* live = nullptr;                // the one playing
  float sm[4] = {0.5f, 0.5f, 0.5f, 0.5f};         // GAIN BASS TREBLE LEVEL, smoothed per block
  float applied[6] = {-1, -1, -1, -1, -1, -1};    // what the circuit has (gain bass treble level octave bite)
  int mode = 0, engine = 0, wantMode = 0, wantEngine = 0;
  float dip = 1.f, dipStep = 0.f; int dipDir = 0; // the 5 ms dip around a MODE or ENGINE switch
  float drive = 0.f, solverMeter = 0.f;
  float in[MAXBLOCK], out[MAXBLOCK];
  const ModuleDesc& desc() const override { return HYPED_DESC; }

  void init(float s) override {
    sr = s;
    os2 = std::make_unique<hyped::Realtime>(sr, 2);
    os1 = std::make_unique<hyped::Realtime>(sr, 1);
    dipStep = 1.f / (0.005f * sr);
    mode = wantMode = HYPED_PARAMS[4].nearestStop(map(4, HYPED_PARAMS[4].def));
    engine = wantEngine = HYPED_PARAMS[7].nearestStop(map(7, HYPED_PARAMS[7].def));
    solverFor(*os2, engine == 2 ? 0 : engine); solverFor(*os1, 2);
    live = engine == 2 ? os1.get() : os2.get();
    for (float& a : applied) a = -1;
    for (int k = 0; k < 4; k++) sm[k] = HYPED_PARAMS[k].def;
    reset();
  }
  void reset() override {
    if (os2) os2->reset();
    if (os1) os1->reset();
    dip = 1.f; dipDir = 0; drive = 0;
  }
  static void solverFor(hyped::Realtime& r, int eng) {
    r.setLinearSolver(eng == 0 ? hyped::LinearSolver::Auto : hyped::LinearSolver::CompactNewton);
    r.setFastNonlinearMath(eng != 0);
  }
  // Push the knobs into the circuit: only what moved, and only when it moved enough to hear.
  void apply(hyped::Realtime& r, const float* k6, bool force) {
    bool controls = force;
    for (int i = 0; i < 4; i++) if (fabsf(k6[i] - applied[i]) > 0.002f) controls = true;
    if (controls) {
      hyped::Controls c;
      c.gain = k6[0]; c.bass = k6[1]; c.treble = k6[2]; c.level = k6[3];
      c.mode = mode == 1 ? hyped::Mode::Scooped : mode == 2 ? hyped::Mode::Boost : hyped::Mode::Full;
      r.setControls(c);
      for (int i = 0; i < 4; i++) applied[i] = k6[i];
    }
    hyped::PerformanceControls macro(r);
    if (force || fabsf(k6[4] - applied[4]) > 0.005f) { macro.octaveCharacter(k6[4]); applied[4] = k6[4]; }
    if (force || fabsf(k6[5] - applied[5]) > 0.005f) { macro.fullBite(k6[5]); applied[5] = k6[5]; }
  }
  void process(const float* inL, const float*, float* oL, float* oR, int n, const float* e, const Context&) override {
    // Block-rate smoothing (about 20 ms) of the four pots; the macros follow the knob directly.
    const float a = 1.f - expf(-(float)n / (0.020f * sr));
    for (int k = 0; k < 4; k++) sm[k] += a * (clamp01(e[k]) - sm[k]);
    const float k6[6] = {sm[0], sm[1], sm[2], sm[3], clamp01(e[5]), clamp01(e[6])};
    wantMode = HYPED_PARAMS[4].nearestStop(map(4, e[4]));
    wantEngine = HYPED_PARAMS[7].nearestStop(map(7, e[7]));
    if ((wantMode != mode || wantEngine != engine) && dipDir == 0) dipDir = -1;   // dip, then switch at the bottom
    for (int i = 0; i < n; i++) in[i] = std::isfinite(inL[i]) ? inL[i] : 0.f;
    apply(*live, k6, false);
    live->processBlock(in, out, (std::size_t)n);
    float pk = 0;
    for (int i = 0; i < n; i++) {
      if (dipDir < 0) { dip -= dipStep; if (dip <= 0.f) { dip = 0.f; dipDir = 2; } }
      else if (dipDir > 0 && dipDir != 2) { dip += dipStep; if (dip >= 1.f) { dip = 1.f; dipDir = 0; } }
      const float y = out[i] * dip;
      oL[i] = y; oR[i] = y; pk = fmaxf(pk, fabsf(out[i]));
    }
    if (dipDir == 2) {                             // silent now: switch, and come back up next block
      const bool engineChanged = wantEngine != engine;
      mode = wantMode; engine = wantEngine;
      hyped::Realtime* next = engine == 2 ? os1.get() : os2.get();
      solverFor(*next, engine);
      if (next != live || engineChanged) next->reset();
      live = next;
      apply(*live, k6, true);
      dipDir = 1;
    }
    drive += 0.3f * (pk - drive);
    solverMeter = (float)engine;
  }
  void meters(float* o) const override { o[0] = drive; o[1] = solverMeter; }
  static float clamp01(float v) { return v < 0.f ? 0.f : v > 1.f ? 1.f : v; }
};

}  // namespace rig

// engine/modules/cab.h - impulse responses of any length, mono or stereo: a cabinet, a room, a
// convolution reverb. Runs on NeuralAmpModelerCore's zero-latency partitioned FFT convolver
// (nam::Linear: direct convolution up to 1024 taps, FFT partitions above; see core/nam.h).
#pragma once
#include <string>
#include <vector>
#include "../core/module.h"
#include "../core/nam.h"

namespace rig {

inline constexpr ParamDesc CAB_PARAMS[] = {
  {"locut", "LO CUT", 20.f, 399.0525f, 0.25f, "Hz", Curve::Log, 0, false},
  {"mix",   "MIX",     0.f, 100.f,   0.90f, "%",  Curve::Lin, 0, false},
};
// Payload: the RIR1 header + planar float32 samples (see core/nam.h). Up to 20 s, 1 or 2 channels.
inline constexpr ModuleDesc CAB_DESC = {"cab", "CAB IR", "built-in cab", 2, CAB_PARAMS, 2, {"taps", "chans"},
                                    {"ir", "Cab IR", ".wav", true, irMaxBytes()}};

struct Cab : Module {
  static constexpr float LEVEL = 0.21f;            // RMS gain every IR is normalised to
  ModelSlot slot;
  std::vector<unsigned char> staging;
  std::string status;
  Biquad lcL, lcR; float lcHz = -1;
  OnePole mixS;
  float x[MAXBLOCK], wL[MAXBLOCK], wR[MAXBLOCK], accL[MAXBLOCK], accR[MAXBLOCK];
  const ModuleDesc& desc() const override { return CAB_DESC; }
  void init(float s) override {
    sr = s; mixS.setMs(sr, 20); mixS.y = CAB_PARAMS[1].def; slot.setFade((int)(0.010f * sr));
    slot.replaceNow(makeDefault()); builtIn = true;      // hosts re-send their files after init
    reset();
  }
  void reset() override { lcL.reset(); lcR.reset(); }
  bool builtIn = true;
  std::unique_ptr<LoadedModel> makeDefault() {             // synthetic closed-back 4x12-ish response
    Biquad f[6];
    f[0].hp(sr, 75, 0.707f); f[1].peak(sr, 115, 1.0f, 5.f); f[2].peak(sr, 900, 1.0f, -4.f);
    f[3].peak(sr, 2600, 1.3f, 4.f); f[4].lp(sr, 5200, 0.6f); f[5].lp(sr, 6500, 1.1f);
    int n = (int)(0.021f * sr);
    int r1 = (int)(0.0006f * sr), r2 = (int)(0.0013f * sr);
    std::vector<float> ir(n);
    for (int i = 0; i < n; i++) {
      float v = (i == 0) ? 1.f : 0.f;
      if (i == r1) v += 0.25f;
      if (i == r2) v -= 0.15f;
      for (auto& b : f) v = b.tick(v);
      float fade = i > n - 64 ? (float)(n - i) / 64.f : 1.f;
      ir[i] = v * fade;
    }
    std::string note;
    auto lm = makeIr(ir.data(), 1, n, sr, sr, MAXBLOCK, LEVEL, note);
    status = "built-in cab";
    return lm;
  }
  void process(const float* inL, const float* inR, float* oL, float* oR, int n, const float* e, const Context&) override {
    float hz = map(0, e[0]);
    if (fabsf(hz - lcHz) > 0.5f) { lcL.hp(sr, hz, 0.707f); lcR.hp(sr, hz, 0.707f); lcHz = hz; }
    const Fade f = slot.begin(n);
    for (int i = 0; i < n; i++) { float v = 0.5f * (inL[i] + inR[i]); x[i] = std::isfinite(v) ? v : 0.f; }   // keep NaN out of the IR's history
    convolve(slot.current(), f, n, false);
    if (slot.next()) convolve(slot.next(), f.other(), n, true);
    for (int i = 0; i < n; i++) {
      float m = map(1, mixS.next(e[1])) * 0.01f, dry = x[i] * 0.3f * (1.f - m);
      oL[i] = lcL.tick(accL[i] * m + dry);
      oR[i] = lcR.tick(accR[i] * m + dry);
    }
  }
  // Wet signal of one IR at weight w (normalisation gain included), written or added to acc.
  void convolve(const LoadedModel* lm, const Fade& w, int n, bool add) {
    if (!add) { memset(accL, 0, n * sizeof(float)); memset(accR, 0, n * sizeof(float)); }
    if (!lm || !lm->dsp) return;
    float* ins[1] = {x};
    float* outL[1] = {wL};
    float* outR[1] = {wR};
    lm->dsp->process(ins, outL, n);
    if (lm->dsp2) lm->dsp2->process(ins, outR, n);
    const float* r = lm->dsp2 ? wR : wL;
    for (int i = 0; i < n; i++) { float g = lm->gain * w.at(i, n); accL[i] += wL[i] * g; accR[i] += r[i] * g; }
  }
  void meters(float* out) const override {
    const LoadedModel* lm = slot.current();
    out[0] = lm ? (float)lm->frames : 0.f;
    out[1] = lm && lm->dsp ? (lm->dsp2 ? 2.f : 1.f) : 0.f;
  }
  unsigned char* assetBuffer(int bytes) override { staging.resize(bytes > 0 ? bytes : 1); return staging.data(); }
  int assetCommit(int bytes) override {
    if (bytes < 1 || bytes > (int)staging.size()) { status = "empty file"; return -1; }
    auto lm = parseIr(staging.data(), bytes, sr, MAXBLOCK, LEVEL, status);
    std::vector<unsigned char>().swap(staging);
    if (!lm) return -1;
    slot.stage(std::move(lm)); builtIn = false;
    return 0;
  }
  void assetClear() override { auto lm = makeDefault(); slot.stage(std::move(lm)); builtIn = true; }
  const char* assetStatus() const override { return status.c_str(); }
};

} // namespace rig

// engine/modules/drive/amp.h - NAM capture player, with a built-in waveshaper amp when no capture is loaded.
// Captures run through NeuralAmpModelerCore (any .nam architecture, see core/nam.h).
// The built-in shaper is oversampled (os x) and uses 1st-order antiderivative anti-aliasing (ADAA).
#pragma once
#include <string>
#include <vector>
#include "../../core/module.h"
#include "../../core/nam.h"

namespace rig {

// INPUT is the gain into whatever is loaded: 0 dB = unity into a capture; the built-in shaper adds +18 dB.
inline constexpr ParamDesc AMP_PARAMS[] = {
  {"input",  "INPUT",  -12.f, 24.f, 1.f / 3.f, "dB", Curve::Lin, 1, true},
  {"output", "OUTPUT", -18.f,  6.f, 0.50f,     "dB", Curve::Lin, 1, true},
};
// Payload: the .nam file's JSON text, as-is.
inline constexpr ModuleDesc AMP_DESC = {"amp", "AMP", "NAM capture player", 2, AMP_PARAMS, 1, {"rate", nullptr},
                                        {"nam", "NAM capture", ".nam", true, 32 << 20}};

struct Amp : Module {
  Biquad hp, tight, scoop, post, up[3], dn[3];
  DCBlock dc;
  OnePole inS, outS;
  int os = 2; bool adaa = true;
  double xPrev = 0, fPrev = 0;
  ModelSlot slot;                                  // the loaded capture (nullptr dsp = built-in shaper)
  std::vector<unsigned char> staging;
  std::string status;
  float inDb[MAXBLOCK], outG[MAXBLOCK], xin[MAXBLOCK], yL[MAXBLOCK], yR[MAXBLOCK];
  static constexpr float BIAS = 0.15f;
  static constexpr float DRIVE_BASE_DB = 18.f;    // INPUT 0 dB = +18 dB into the shaper
  static constexpr float MAKEUP = 1.0f;
  const ModuleDesc& desc() const override { return AMP_DESC; }
  void init(float s) override {
    sr = s;
    hp.hp(sr, 90, 0.707f); tight.peak(sr, 900, 0.7f, 6.f);
    scoop.peak(sr, 420, 0.8f, -4.f); post.lp(sr, 7000, 0.707f);
    setOS(os);
    inS.setMs(sr, 20); outS.setMs(sr, 20); inS.y = AMP_PARAMS[0].def; outS.y = AMP_PARAMS[1].def;
    slot.setFade((int)(0.010f * sr));
    reset();
    if (nam::DSP* d = slot.dsp()) d->ResetAndPrewarm(sr, MAXBLOCK);   // re-init at a new rate keeps the capture
  }
  void setOS(int o) {
    os = o < 1 ? 1 : o;
    const float q[3] = {0.5176f, 0.7071f, 1.9319f};       // 6th-order Butterworth at os x rate
    for (int k = 0; k < 3; k++) { up[k].lp(os * sr, 19000, q[k]); dn[k].lp(os * sr, 19000, q[k]); }
  }
  void reset() override {
    hp.reset(); tight.reset(); scoop.reset(); post.reset(); dc.reset();
    for (int k = 0; k < 3; k++) { up[k].reset(); dn[k].reset(); }
    xPrev = 0; fPrev = F(0);
  }
  static float shape(float x) { return tanhf(x + BIAS) - tanhf(BIAS); }
  static double logcosh(double x) { double a = fabs(x); return a + log1p(exp(-2.0 * a)) - 0.69314718055994531; }
  static double F(double x) { return logcosh(x + BIAS) - x * tanh((double)BIAS); }   // antiderivative of shape
  float shapeAA(float xf) {
    double x = xf, dx = x - xPrev, Fx = F(x), y;
    if (fabs(dx) > 1e-5) y = (Fx - fPrev) / dx;
    else y = shape((float)(0.5 * (x + xPrev)));
    xPrev = x; fPrev = Fx;
    return (float)y;
  }
  void process(const float* inL, const float*, float* oL, float* oR, int n, const float* e, const Context&) override {
    for (int i = 0; i < n; i++) { inDb[i] = map(0, inS.next(e[0])); outG[i] = db2lin(map(1, outS.next(e[1]))); }
    Fade f = slot.begin(n);
    const LoadedModel *a = slot.current(), *b = slot.next();
    const bool twoBuiltIns = b && !(a && a->dsp) && !b->dsp;      // one shaper, so don't run it twice
    if (twoBuiltIns) f = {1.f, 1.f};
    render(a, f, inL, oL, oR, n, false);
    if (b && !twoBuiltIns) render(b, f.other(), inL, oL, oR, n, true);
  }
  // One source (a capture, or the built-in shaper when lm has no dsp) at weight w, written or added.
  void render(const LoadedModel* lm, const Fade& w, const float* inL, float* oL, float* oR, int n, bool add) {
    if (lm && lm->dsp) {
      // Non-finite input never reaches the capture: its state can't be cleared without allocating.
      for (int i = 0; i < n; i++) { float v = inL[i] * db2lin(inDb[i]); xin[i] = std::isfinite(v) ? v : 0.f; }
      float* ins[2] = {xin, xin};
      float* outs[2] = {yL, yR};
      lm->dsp->process(ins, outs, n);
      if (lm->dsp->NumOutputChannels() < 2) memcpy(yR, yL, n * sizeof(float));
      for (int i = 0; i < n; i++) yL[i] *= lm->gain, yR[i] *= lm->gain;
    } else {
      for (int i = 0; i < n; i++) {
        float x = tight.tick(hp.tick(inL[i])) * db2lin(DRIVE_BASE_DB + inDb[i]);
        float y = 0;
        for (int ph = 0; ph < os; ph++) {                  // zero-stuff, filter, shape, filter, decimate
          float u = ph == 0 ? (float)os * x : 0.f;
          if (os > 1) for (int k = 0; k < 3; k++) u = up[k].tick(u);
          u = adaa ? shapeAA(u) : shape(u);
          if (os > 1) for (int k = 0; k < 3; k++) u = dn[k].tick(u);
          if (ph == 0) y = u;
        }
        yL[i] = yR[i] = post.tick(scoop.tick(dc.tick(y))) * MAKEUP;
      }
    }
    for (int i = 0; i < n; i++) {
      float g = outG[i] * w.at(i, n);
      if (add) { oL[i] += yL[i] * g; oR[i] += yR[i] * g; } else { oL[i] = yL[i] * g; oR[i] = yR[i] * g; }
    }
  }
  void meters(float* out) const override { const LoadedModel* lm = slot.current(); out[0] = lm && lm->dsp ? (float)lm->rate : 0.f; }
  unsigned char* assetBuffer(int bytes) override { staging.resize(bytes > 0 ? bytes : 1); return staging.data(); }
  int assetCommit(int bytes) override {
    if (bytes < 1 || bytes > (int)staging.size()) { status = "empty file"; return -1; }
    auto lm = loadCapture(staging.data(), bytes, sr, MAXBLOCK, status);
    std::vector<unsigned char>().swap(staging);
    if (!lm) return -1;
    slot.stage(std::move(lm));
    return 0;
  }
  void assetClear() override {
    auto lm = std::make_unique<LoadedModel>(); lm->warm = (int)(0.02f * sr);   // flush the shaper's filters before it's heard
    slot.stage(std::move(lm)); status = "built-in amp";
  }
  const char* assetStatus() const override { return status.c_str(); }
};

} // namespace rig

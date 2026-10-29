// engine/sources/adsr.h - pick-triggered ADSR. Onsets are detected in the >1.8 kHz band,
// where pick transients live and ringing strings do not (24/24 on the test riff).
// A 40 ms, D 300 ms to S 0.4, R 600 ms once the input drops below -45 dBFS.
#pragma once
#include "../core/source.h"

namespace rig {

inline constexpr SourceDesc ADSR_DESC = {"adsr", "PICK ADSR", "per-note swell", nullptr, 1, {"onsets", nullptr}};

struct PickAdsr : Source {
  float fast = 0, refr = 0, adsr = 0; int stage = 0; int onsets = 0;
  float hfFast = 0, hfSlow = 0; Biquad hf1, hf2;
  float ONSET_FLOOR = 0.001f, ONSET_RATIO = 2.f;
  const SourceDesc& desc() const override { return ADSR_DESC; }
  void init(float s) override { sr = s; hf1.hp(sr, 1800, 0.5412f); hf2.hp(sr, 1800, 1.3066f); }
  float update(const SourceContext& c) override {
    float fA = 1.f - expf(-1.f / (0.001f * sr)), fR = 1.f - expf(-1.f / (0.030f * sr));
    float hA = 1.f - expf(-1.f / (0.0005f * sr)), hR = 1.f - expf(-1.f / (0.010f * sr));
    float hsA = 1.f - expf(-1.f / (0.025f * sr)), hsR = 1.f - expf(-1.f / (0.150f * sr));
    bool trig = false;
    for (int i = 0; i < c.n; i++) {
      float ax = fabsf(c.in[i]);
      fast += (ax > fast ? fA : fR) * (ax - fast);
      float hx = fabsf(hf2.tick(hf1.tick(c.in[i])));
      hfFast += (hx > hfFast ? hA : hR) * (hx - hfFast);
      hfSlow += (hx > hfSlow ? hsA : hsR) * (hx - hfSlow);
      if (refr > 0) refr -= 1.f;
      else if (hfFast > ONSET_FLOOR && hfFast > ONSET_RATIO * hfSlow) { trig = true; refr = 0.060f * sr; onsets++; }
    }
    hfFast = undenorm(hfFast); hfSlow = undenorm(hfSlow); fast = undenorm(fast);
    float dt = c.dt;
    if (trig) stage = 1;
    if (stage == 1) { adsr += dt / 0.040f; if (adsr >= 1) { adsr = 1; stage = 2; } }
    else if (stage == 2) { adsr -= dt / 0.300f * 0.6f; if (adsr <= 0.4f) { adsr = 0.4f; stage = 3; } }
    if (stage == 3 && fast < 0.0056f) stage = 4;
    if (stage == 4) { adsr -= dt / 0.600f; if (adsr <= 0) { adsr = 0; stage = 0; } }
    return 2.f * adsr - 1.f;
  }
  void meters(float* out) const override { out[0] = (float)onsets; }
};

} // namespace rig

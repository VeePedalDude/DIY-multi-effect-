// engine/modules/synth/formant.h - FORMANT: three resonances placed where a voice puts them, so the
// guitar says a vowel. VOWEL sweeps A-E-I-O-U (0-4, blending in between); SHIFT moves every
// resonance -12-+12 semitones (smaller or bigger "mouth"); RESO from broad to sharp; ENV +-100%
// moves along the vowels with your picking (talk-box wah); MIX 0-100%. Default: "A", no shift,
// 50%, fully wet. Meters: "cutoff" (the first formant now, Hz) and "env".
// Formants: the first three of each vowel (bass voice) from the Csound manual's formant table,
// with the higher ones' level differences halved (in dB) so the guitar doesn't vanish into the first.
#pragma once
#include "../../core/module.h"
#include "../../core/vaf.h"

namespace rig {

inline constexpr ParamDesc FORMANT_PARAMS[] = {
  {"vowel", "VOWEL",  0.f,   4.f,   0.f,  "",   Curve::Lin, 2, false},
  {"shift", "SHIFT", -12.f,  12.f,  0.5f, "st", Curve::Lin, 1, true},
  {"reso",  "RESO",   0.f,   100.f, 0.5f, "%",  Curve::Lin, 0, false},
  {"env",   "ENV",   -100.f, 100.f, 0.5f, "%",  Curve::Lin, 0, true},
  {"mix",   "MIX",    0.f,   100.f, 1.f,  "%",  Curve::Lin, 0, false},
};
inline constexpr ModuleDesc FORMANT_DESC = {"formant", "FORMANT", "synth filter · vowels A E I O U", 5, FORMANT_PARAMS, 2, {"cutoff", "env"}};
// Per vowel: formant Hz, bandwidth Hz, level dB (F1..F3).
struct Vowel { float f[3], bw[3], db[3]; };
inline constexpr Vowel FORMANT_VOWELS[5] = {
  {{600, 1040, 2250}, {60, 70, 110}, {0, -7, -9}},     // A
  {{400, 1620, 2400}, {40, 80, 100}, {0, -12, -9}},    // E
  {{250, 1750, 2600}, {60, 90, 100}, {0, -15, -20}},   // I
  {{400, 750, 2400},  {40, 80, 100}, {0, -11, -21}},   // O
  {{350, 600, 2400},  {40, 80, 100}, {0, -20, -32}},   // U
};
// One vowel position 0..4 (blended), shift in semitones, Q scale: three bands' Hz, Q and gain.
inline void formantBands(float pos, float shiftSt, float qScale, float hz[3], float q[3], float gain[3]) {
  pos = clampf(pos, 0.f, 4.f); const int a = pos >= 4.f ? 3 : (int)pos; const float t = pos - (float)a, sh = exp2f(shiftSt / 12.f);
  const Vowel &x = FORMANT_VOWELS[a], &y = FORMANT_VOWELS[a + 1];
  for (int b = 0; b < 3; b++) {
    hz[b] = x.f[b] * powf(y.f[b] / x.f[b], t) * sh;
    const float bw = (x.bw[b] + t * (y.bw[b] - x.bw[b])) * sh;
    q[b] = hz[b] / bw * qScale; gain[b] = db2lin(0.5f * (x.db[b] + t * (y.db[b] - x.db[b])));
  }
}

struct Formant : Module {
  Svf band[3]; StepSmooth s[5]; EnvAmount env; float gain[3] = {1, 1, 1}, wet = 1, norm = 1, hz = 600; int cnt = 0;
  const ModuleDesc& desc() const override { return FORMANT_DESC; }
  void init(float x) override { sr = x; reset(); }
  void reset() override { for (int i = 0; i < 5; i++) s[i].init(sr, 20, FORMANT_PARAMS[i].def); env.init(sr); for (auto& b : band) b.reset(); cnt = 0; }
  void process(const float* inL, const float* inR, float* oL, float* oR, int n, const float* e, const Context&) override {
    for (int i = 0; i < n; i++) {
      env.tick(inL[i], inR[i]);
      if (cnt-- <= 0) {
        cnt = EQ_STEP - 1; float v[5]; for (int j = 0; j < 5; j++) v[j] = s[j].next(clampf(e[j], 0, 1));
        env.update();
        const float pos = clampf(map(0, v[0]) + VA_ENV_OCTAVES * map(3, v[3]) * 0.01f * env.e, 0.f, 4.f);
        float f[3], q[3]; formantBands(pos, map(1, v[1]), 0.35f * powf(8.f, v[2]), f, q, gain);   // RESO -> Q x0.35..2.8
        for (int b = 0; b < 3; b++) band[b].set(Svf::BP, sr, f[b], q[b]);
        hz = f[0]; wet = map(4, v[4]) * 0.01f; norm = 5.f / (gain[0] + gain[1] + gain[2]);   // narrow bands pass little of a whole chord: make it up
      }
      const float in[2] = {inL[i], inR[i]}; float out[2];
      for (int c = 0; c < 2; c++) {
        float y = 0; for (int b = 0; b < 3; b++) y += gain[b] * band[b].tick(c, in[c]);
        out[c] = (1.f - wet) * in[c] + wet * norm * y;
      }
      oL[i] = out[0]; oR[i] = out[1];
    }
  }
  void meters(float* out) const override { out[0] = hz; out[1] = env.e; }
};

} // namespace rig

// engine/modules/filter/stack.h - TONE STACK: the passive bass/mid/treble network inside guitar amps,
// modelled from the circuit. The knobs interact as they do on the amp (turning up BASS also
// changes the mids). BASS, MID, TREBLE 0-10; MODEL 0-2 morphs between three amps' part values:
// 0 = '59 Fender Bassman, 1 = Marshall JCM800, 2 = Fender Twin Reverb (in between = the parts
// blended); LEVEL +-12 dB. Default: every knob at 5 on the Bassman. The stack's own loss is made
// up so that with every knob at 5 its average level from 80 Hz to 5 kHz is 0 dB.
//
// Transfer function: D. T. Yeh and J. O. Smith, "Discretization of the '59 Fender Bassman Tone
// Stack", DAFx 2006 (the third-order H(s) of the FMV stack, bilinear transform). The formula was
// checked against an independent nodal-analysis solve of the circuit: within 1e-12 dB for all
// three models, every corner of the knobs, 20 Hz-20 kHz. BASS has an audio taper like the amps'.
#pragma once
#include <complex>
#include "../../core/module.h"
#include "../../core/eq.h"

namespace rig {

inline constexpr ParamDesc STACK_PARAMS[] = {
  {"bass",   "BASS",   0.f,  10.f, 0.5f, "",   Curve::Lin, 1, false},
  {"mid",    "MID",    0.f,  10.f, 0.5f, "",   Curve::Lin, 1, false},
  {"treble", "TREBLE", 0.f,  10.f, 0.5f, "",   Curve::Lin, 1, false},
  {"model",  "MODEL",  0.f,  2.f,  0.f,  "",   Curve::Lin, 1, false},
  {"level",  "LEVEL", -12.f, 12.f, 0.5f, "dB", Curve::Lin, 1, true},
};
inline constexpr ModuleDesc STACK_DESC = {"stack", "TONE STACK", "eq · amp bass, mid, treble", 5, STACK_PARAMS, 0, {nullptr, nullptr}};

// Part values: R1 treble pot, R2 bass pot, R3 mid pot, R4 slope resistor, C1 treble cap, C2 bass cap, C3 mid cap.
struct StackParts { double R1, R2, R3, R4, C1, C2, C3; };
inline constexpr StackParts STACK_MODELS[3] = {
  {250e3, 1e6,   25e3, 56e3,  250e-12, 20e-9,  20e-9},    // Fender '59 Bassman 5F6-A
  {220e3, 1e6,   22e3, 33e3,  470e-12, 22e-9,  22e-9},    // Marshall JCM800
  {250e3, 250e3, 10e3, 100e3, 250e-12, 100e-9, 47e-9},    // Fender Twin Reverb (blackface)
};
inline double stackBassTaper(double k) { return (pow(10.0, 2.0 * k) - 1.0) / 99.0; }   // audio taper, 0..1

// s-domain coefficients for knob positions l (bass), m (mid), t (treble), each 0..1.
inline void stackCoeffs(const StackParts& p, double l, double m, double t, double B[4], double A[4]) {
  const double R1 = p.R1, R2 = p.R2, R3 = p.R3, R4 = p.R4, C1 = p.C1, C2 = p.C2, C3 = p.C3, C123 = C1 * C2 * C3;
  B[0] = 0;
  B[1] = t * C1 * R1 + m * C3 * R3 + l * (C1 * R2 + C2 * R2) + (C1 * R3 + C2 * R3);
  B[2] = t * (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4) - m * m * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
       + m * (C1 * C3 * R1 * R3 + C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
       + l * (C1 * C2 * R1 * R2 + C1 * C2 * R2 * R4 + C1 * C3 * R2 * R4) + l * m * (C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3)
       + (C1 * C2 * R1 * R3 + C1 * C2 * R3 * R4 + C1 * C3 * R3 * R4);
  B[3] = l * m * C123 * (R1 * R2 * R3 + R2 * R3 * R4) - m * m * C123 * (R1 * R3 * R3 + R3 * R3 * R4)
       + m * C123 * (R1 * R3 * R3 + R3 * R3 * R4) + t * C123 * R1 * R3 * R4 - t * m * C123 * R1 * R3 * R4 + t * l * C123 * R1 * R2 * R4;
  A[0] = 1;
  A[1] = (C1 * R1 + C1 * R3 + C2 * R3 + C2 * R4 + C3 * R4) + m * C3 * R3 + l * (C1 * R2 + C2 * R2);
  A[2] = m * (C1 * C3 * R1 * R3 - C2 * C3 * R3 * R4 + C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3) + l * m * (C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3)
       - m * m * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3) + l * (C1 * C2 * R2 * R4 + C1 * C2 * R1 * R2 + C1 * C3 * R2 * R4 + C2 * C3 * R2 * R4)
       + (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4 + C1 * C2 * R3 * R4 + C1 * C2 * R1 * R3 + C1 * C3 * R3 * R4 + C2 * C3 * R3 * R4);
  A[3] = l * m * C123 * (R1 * R2 * R3 + R2 * R3 * R4) - m * m * C123 * (R1 * R3 * R3 + R3 * R3 * R4)
       + m * C123 * (R3 * R3 * R4 + R1 * R3 * R3 - R1 * R3 * R4) + l * C123 * R1 * R2 * R4 + C123 * R1 * R3 * R4;
}
// The analog response's magnitude at f Hz.
inline double stackMag(const double B[4], const double A[4], double f) {
  const std::complex<double> s(0, 2 * M_PI * f);
  return std::abs((B[0] + s * (B[1] + s * (B[2] + s * B[3]))) / (A[0] + s * (A[1] + s * (A[2] + s * A[3]))));
}
// Parts for a MODEL position 0..2: neighbouring amps blended (in log terms, like swapping in values between).
inline StackParts stackParts(double pos) {
  pos = pos < 0 ? 0 : pos > 2 ? 2 : pos; const int i = pos >= 2 ? 1 : (int)pos; const double f = pos - i;
  const StackParts &a = STACK_MODELS[i], &b = STACK_MODELS[i + 1];
  auto mix = [f](double x, double y) { return x * pow(y / x, f); };
  return {mix(a.R1, b.R1), mix(a.R2, b.R2), mix(a.R3, b.R3), mix(a.R4, b.R4), mix(a.C1, b.C1), mix(a.C2, b.C2), mix(a.C3, b.C3)};
}
// Makeup for a model: the gain that brings the every-knob-at-5 response to 0 dB on average over 80 Hz-5 kHz.
inline double stackMakeupDb(const StackParts& p) {
  double B[4], A[4], sum = 0; stackCoeffs(p, stackBassTaper(0.5), 0.5, 0.5, B, A);
  const int N = 32; for (int i = 0; i < N; i++) sum += 20 * log10(stackMag(B, A, 80.0 * pow(5000.0 / 80.0, i / (N - 1.0))));
  return -sum / N;
}

struct Stack : Module {
  Iir3 f; StepSmooth s[5]; double makeup[3] = {0, 0, 0}; float gain = 1; int cnt = 0;
  const ModuleDesc& desc() const override { return STACK_DESC; }
  void init(float x) override { sr = x; for (int k = 0; k < 3; k++) makeup[k] = stackMakeupDb(STACK_MODELS[k]); reset(); }
  void reset() override { for (int k = 0; k < 5; k++) s[k].init(sr, 20, STACK_PARAMS[k].def); f.reset(); cnt = 0; }
  void process(const float* inL, const float* inR, float* oL, float* oR, int n, const float* e, const Context&) override {
    for (int i = 0; i < n; i++) {
      if (cnt-- <= 0) {
        cnt = EQ_STEP - 1; float v[5]; for (int k = 0; k < 5; k++) v[k] = s[k].next(clampf(e[k], 0, 1));
        const double pos = map(3, v[3]); double B[4], A[4];
        stackCoeffs(stackParts(pos), stackBassTaper(v[0]), v[1], v[2], B, A); f.set(sr, B, A);
        const int mi = pos >= 2 ? 1 : (int)pos; const double fr = pos - mi;
        gain = db2lin((float)(makeup[mi] + fr * (makeup[mi + 1] - makeup[mi])) + map(4, v[4]));
      }
      oL[i] = gain * f.tick(0, inL[i]); oR[i] = gain * f.tick(1, inR[i]);
    }
  }
};

} // namespace rig

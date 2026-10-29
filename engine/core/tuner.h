// engine/core/tuner.h - a strobe tuner on the dry input.
//
// Two parts:
//  - The note: cycfi Q's bit-stream autocorrelation pitch detector (third_party/q, MIT,
//    https://github.com/cycfi/q). A reading in 30-60 ms. One detector covers about four octaves (with
//    a 40 Hz floor, notes above ~750 Hz come back as a sub-multiple), so there are two: 40-700 Hz and
//    250-2100 Hz. Above SPLIT_HZ the high one is believed; it gives no reading at all on low notes.
//    The detectors pick the note and give the first cents reading.
//  - Fine cents: one more heterodyne, anchored at the detector's first reading of the note, so its
//    beat is near 0 Hz however far off the string is. Its turning rate is exactly (string - anchor)
//    Hz; once a note has held for FINE_AFTER seconds, cents come from that: ±0.01 cent from B1 to C7.
//    If a bend moves the pitch away from the anchor, it re-anchors.
//  - The strobe: like a mechanical strobe tuner, the input is compared with reference oscillators
//    at the target note's pitch and at 2x and 4x (the bands). Each band is mixed down to a slowly
//    turning phase: still when the string is exactly in tune, turning at h x (string - reference) Hz
//    otherwise, forwards when sharp. The UI draws stripes at that phase, so their motion is the
//    string's actual tuning error, not a needle animation of a number.
// Off by default; when off it costs nothing. Nothing here allocates after init().
#pragma once
#include <cmath>
#include <memory>
#include "dsp.h"
// GCC's flow analysis reports a false "maybe uninitialized" inside Q's period detector once it is
// inlined here (the fields are set before use on every path). third_party/ stays unedited, so the
// warning is switched off around the include only.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include <q/pitch/pitch_detector.hpp>
#include <q/support/literals.hpp>
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace rig {

constexpr int TUN_BANDS = 3;
constexpr float FINE_AFTER = 0.25f;  // seconds a note holds before cents come from the strobe
constexpr double LOW_HZ = 40.0, LOW_TOP_HZ = 700.0;     // the low detector: 8-string F#1 / drop A to F5
constexpr double HIGH_LOW_HZ = 250.0, TOP_HZ = 2100.0;  // the high detector, up to C7 (past the 24th fret of a high E)
constexpr float SPLIT_HZ = 680.f;
constexpr int TUN_HARM[TUN_BANDS] = {1, 2, 4};
// Meter layout for the tuner (published in describe() as meters.tun).
enum TunerMeter { T_ON, T_LEVEL, T_FREQ, T_NOTE, T_CENTS, T_TARGET, T_PH0, T_AMP0 = T_PH0 + TUN_BANDS, T_A4 = T_AMP0 + TUN_BANDS, T_MUTE, T_CONF, T_LEN };

struct Tuner {
  float sr = 48000, a4 = 440.f;
  bool on = false, mute = true, wasOn = false;
  std::unique_ptr<cycfi::q::pitch_detector> pd, pdHi;
  int hiAge = 1 << 30;           // samples since the high detector last read a note above SPLIT_HZ
  float freq = 0, level = 0, conf = 0, cents = 0, target = 0, qFreq = 0, qCents = 0;
  int note = -1, silentFor = 0, held = 0;
  double beat = 0, lastPh = 0;   // the fine heterodyne's turning rate (Hz), and its phase at the last block
  double fref = 0, fph = 0, fr[4] = {0}, fi[4] = {0};   // the fine heterodyne: anchor (Hz), oscillator phase, filter
  static constexpr int POLES = 4;
  double ph[TUN_BANDS] = {0}, zr[TUN_BANDS][POLES] = {{0}}, zi[TUN_BANDS][POLES] = {{0}};
  float lpA = 0, envA = 0, envR = 0;

  void init(float s) {
    using namespace cycfi::q::literals;
    sr = s;
    pd = std::make_unique<cycfi::q::pitch_detector>(cycfi::q::frequency(LOW_HZ), cycfi::q::frequency(LOW_TOP_HZ), s, -45_dB);
    pdHi = std::make_unique<cycfi::q::pitch_detector>(cycfi::q::frequency(HIGH_LOW_HZ), cycfi::q::frequency(TOP_HZ), s, -45_dB);
    lpA = 1.f - expf(-2.f * PI_F * 8.f / sr);           // four one-pole stages at 8 Hz: keeps the beat, drops the neighbouring harmonics (>= 40 Hz away, -57 dB)
    envA = 1.f - expf(-1.f / (0.002f * sr)); envR = 1.f - expf(-1.f / (0.120f * sr));
    clear();
  }
  void clear() {
    if (pd) pd->reset();
    if (pdHi) pdHi->reset();
    hiAge = 1 << 30;
    freq = level = conf = cents = target = qFreq = qCents = 0; note = -1; silentFor = held = 0; beat = lastPh = fref = fph = 0; for (int k = 0; k < POLES; k++) fr[k] = fi[k] = 0;
    for (int b = 0; b < TUN_BANDS; b++) { ph[b] = 0; for (int k = 0; k < POLES; k++) zr[b][k] = zi[b][k] = 0; }
  }
  static float midiOf(float f, float a4) { return 69.f + 12.f * log2f(f / a4); }

  void process(const float* in, int n) {
    if (!on) { wasOn = false; return; }
    if (!wasOn) { clear(); wasOn = true; }                 // fresh start each time it's switched on
    const double w = 2.0 * M_PI / sr;
    for (int i = 0; i < n; i++) {
      const float x = in[i], ax = fabsf(x);
      level += (ax > level ? envA : envR) * (ax - level);
      float f = 0, p = 0;
      if ((*pdHi)(x)) { const float h = pdHi->get_frequency(); if (h > SPLIT_HZ) { f = h; p = pdHi->periodicity(); hiAge = 0; } }
      if (hiAge < (1 << 30)) hiAge++;
      if ((*pd)(x) && hiAge > (int)(0.05f * sr)) { f = pd->get_frequency(); p = pd->periodicity(); }   // the low one, unless a high note is sounding
      {
        if (f > 0) {
          qFreq = f; conf = p; silentFor = 0;
          const float m = midiOf(f, a4);
          if (note < 0 || fabsf(m - (float)note) > 0.6f) { note = (int)lrintf(m); held = 0; }   // a little hysteresis between notes
          target = a4 * powf(2.f, (note - 69) / 12.f);
          qCents = 100.f * (m - (float)note);
        }
      }
      if (fref > 0) {                                       // the fine heterodyne
        fph += w * fref; if (fph > 2 * M_PI) fph -= 2 * M_PI;
        double r = x * cos(fph), q = -x * sin(fph);
        for (int k = 0; k < POLES; k++) { fr[k] += lpA * (r - fr[k]); fi[k] += lpA * (q - fi[k]); r = fr[k]; q = fi[k]; }
      }
      if (target > 0) {                                     // the strobe bands
        for (int b = 0; b < TUN_BANDS; b++) {
          ph[b] += w * TUN_HARM[b] * target; if (ph[b] > 2 * M_PI) ph[b] -= 2 * M_PI;
          const double re = x * cos(ph[b]), im = -x * sin(ph[b]);
          double r = re, q = im;
          for (int k = 0; k < POLES; k++) { zr[b][k] += lpA * (r - zr[b][k]); zi[b][k] += lpA * (q - zi[b][k]); r = zr[b][k]; q = zi[b][k]; }
        }
      }
    }
    level = undenorm(level);
    if (target > 0 && qFreq > 0) {                          // fine cents
      const double p = atan2(fi[POLES - 1], fr[POLES - 1]);
      if (held == 0) { fref = qFreq; beat = 0; }            // anchor at the detector's reading
      else {
        double d = p - lastPh; if (d > M_PI) d -= 2 * M_PI; if (d < -M_PI) d += 2 * M_PI;
        beat += (1.0 - exp(-n / (0.06 * sr))) * (d * sr / (2 * M_PI * n) - beat);
      }
      lastPh = p; held += n;
      const float fAmp = level > 1e-5f ? 2.f * (float)hypot(fr[POLES - 1], fi[POLES - 1]) / level : 0.f;
      const float fineCents = (float)(1200.0 * log2((fref + beat) / target));
      bool fine = held > (int)(FINE_AFTER * sr) && fAmp > 0.05f;
      if (held > (int)(FINE_AFTER * sr) && (!fine || fabsf(fineCents - qCents) > 5.f)) { held = 0; fine = false; }   // moved away (a bend): re-anchor
      freq = fine ? (float)(fref + beat) : qFreq;
      cents = fine ? fineCents : qCents;
    }
    if (level < 0.0015f) { if ((silentFor += n) > (int)(0.4f * sr)) { note = -1; freq = qFreq = 0; cents = qCents = 0; target = 0; held = 0; pd->reset(); pdHi->reset(); hiAge = 1 << 30; } }   // quiet for 0.4 s: no note
    else silentFor = 0;
  }
  // Band phase (radians), and its strength relative to the input level (0..1).
  float phase(int b) const { return (float)atan2(zi[b][POLES - 1], zr[b][POLES - 1]); }
  float amp(int b) const { return level > 1e-5f ? clampf(2.f * (float)hypot(zr[b][POLES - 1], zi[b][POLES - 1]) / level, 0.f, 1.f) : 0.f; }
  void meters(float* m) const {
    m[T_ON] = on ? 1.f : 0.f; m[T_LEVEL] = 20.f * log10f(level + 1e-9f); m[T_FREQ] = freq; m[T_NOTE] = (float)note;
    m[T_CENTS] = cents; m[T_TARGET] = target; m[T_A4] = a4; m[T_MUTE] = mute ? 1.f : 0.f; m[T_CONF] = conf;
    for (int b = 0; b < TUN_BANDS; b++) { m[T_PH0 + b] = note >= 0 ? phase(b) : 0.f; m[T_AMP0 + b] = note >= 0 ? amp(b) : 0.f; }
  }
};

} // namespace rig

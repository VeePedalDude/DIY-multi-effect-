// engine/core/polytrack.h - polyphonic note tracking for guitar, and voice allocation, for modules that
// turn chords into keyboard or synth voices (modules/voice/). No per-string pickup: every hop (about
// 5 ms) it takes a spectrum of the input and finds the notes in it.
//
//  1. The input is low-passed (6th-order Butterworth, 4.5 kHz) and every D-th sample kept (about 12 kHz
//     at any rate), so a 2048-point Hann FFT spans 170 ms with 5.9 Hz bins: wide enough apart to tell a
//     low E from the F#, G or B next to it in a chord. Hop 64 samples there (5.3 ms).
//  2. Peaks above both 40 dB below the loudest and an absolute floor (-56 dBFS), each at the frequency
//     the phase advance between hops says (a phase vocoder: a cent or two).
//  3. Notes one at a time, best evidence first (after Klapuri 2006): every peak from 65 Hz to 1.6 kHz is
//     a candidate, scored by the peaks on its harmonics up to 4.4 kHz, each weighted (f0 + 52) /
//     (h f0 + 320), so an octave down only wins if its odd harmonics are there too. The winner's
//     harmonics are taken out down to the average of each and its neighbours (spectral smoothness:
//     whatever sticks out above a string's smooth envelope is left for another note, a fifth above say).
//     A further note needs 18% of the first one's score and a harmonic of its own (2nd to 4th), not one
//     stray partial. An octave above a played note mostly vanishes into it: a known limit of finding
//     notes in one spectrum.
//  4. Voices (PolyAlloc): a note is matched to the nearest sounding voice within 70 cents (it glides);
//     a new note starts a voice once it's been seen in three hops in a row; a voice whose note has been
//     gone for two hops is released. A pick heard in the input (a quick rise in level) restarts a sounding
//     voice whose note got louder with it: the same note picked again.
// Allocation-free after init().
#pragma once
#include "fft.h"
#include <algorithm>

namespace rig {

struct PolyNote { float hz, amp; };

struct PolyTracker {
  static constexpr int MAXN = 2048, MAXPEAKS = 48, MAXNOTES = 6, MAXH = 40;
  static constexpr float SKEW = 0.8f;      // the window rises over its first 80%, falls over the last 20%
  static constexpr float TOP = 3800.f;      // harmonics are looked at up to here
  static constexpr float STOP = 0.18f;      // a further note needs this much of the first one's score
  static constexpr int SMW = 1;             // smoothness: each harmonic against its neighbours either side      // a further note needs this much of the first one's salience
  static constexpr float FLOOR = 0.0015f;   // peaks below -56 dBFS are not looked at
  Fft<MAXN> fft;
  int N = 2048, H = 64, D = 4, dpos = 0, pos = 0, since = 0; float sr = 12000, inSr = 48000;
  Biquad aa[4];                                              // anti-alias lowpass before taking every D-th sample
  float in[MAXN] = {0}, win[MAXN], re[MAXN], im[MAXN], mag[MAXN / 2 + 1], ph[MAXN / 2 + 1], prevPh[MAXN / 2 + 1] = {0};
  float pf[MAXPEAKS], pa[MAXPEAKS]; int pb[MAXPEAKS], npk = 0; int pk[MAXN / 2 + 1];              // this hop's peaks: frequency, amplitude
  PolyNote notes[MAXNOTES]; int nnotes = 0; unsigned frames = 0;
  void init(float s) {
    inSr = s; D = (int)fmaxf(1.f, roundf(s / 12000.f)); sr = s / D; N = 2048; H = 64; fft.init(N);
    const float q[4] = {0.5098f, 0.6013f, 0.9000f, 2.5629f};     // 8th-order Butterworth at 3.8 kHz: -45 dB where
    for (int i = 0; i < 4; i++) aa[i].lp(inSr, fminf(3800.f, 0.32f * sr), q[i]);   // aliases start landing on what's looked at
    const int R = (int)(SKEW * N);
    for (int i = 0; i < N; i++) win[i] = i < R ? 0.5f - 0.5f * cosf(PI_F * i / R) : 0.5f + 0.5f * cosf(PI_F * (i - R) / (N - R));
    reset();
  }
  void reset() { memset(in, 0, sizeof in); for (auto& b : aa) b.reset(); dpos = 0; for (int& x : pk) x = -1; memset(prevPh, 0, sizeof prevPh); pos = 0; since = 0; nnotes = 0; npk = 0; }
  // One input sample; true when a hop was analysed (notes[] is new).
  bool push(float x) {
    x = aa[3].tick(aa[2].tick(aa[1].tick(aa[0].tick(x))));
    if (++dpos < D) return false;
    dpos = 0; in[pos] = x; pos = (pos + 1) & (N - 1);
    if (++since < H) return false;
    since = 0; analyse(); return true;
  }
  static float cents(float a, float b) { return 1200.f * log2f(a / b); }

  void analyse() {
    frames++;
    for (int i = 0; i < N; i++) { re[i] = in[(pos + i) & (N - 1)] * win[i]; im[i] = 0.f; }
    fft.run(re, im);
    const int kmin = (int)(55.f * N / sr), kmax = (int)fminf(TOP * N / sr, (float)(N / 2 - 2));
    const float scale = 4.f / N;                           // a sine of amplitude A peaks at A (the window sums to N/2)
    float mx = 0.f;
    for (int k = kmin - 1; k <= kmax + 1; k++) {
      mag[k] = sqrtf(re[k] * re[k] + im[k] * im[k]) * scale; mx = fmaxf(mx, mag[k]);
      ph[k] = atan2f(im[k], re[k]);
    }
    const float thr = fmaxf(FLOOR, mx * 0.01f);
    npk = 0;
    for (int k = kmin; k <= kmax; k++) {
      if (!(mag[k] > thr && mag[k] > mag[k - 1] && mag[k] >= mag[k + 1])) continue;
      // phase vocoder: the frequency that turns the phase by what it turned since the last hop
      float d = ph[k] - prevPh[k] - 2 * PI_F * k * H / N;
      d -= 2 * PI_F * floorf(d / (2 * PI_F) + 0.5f);
      float f = (k + d * N / (2 * PI_F * H)) * sr / N;
      if (!(fabsf(f * N / sr - k) <= 1.f)) {                // not believable: the bin's own parabola
        const float a = mag[k - 1], b = mag[k], c = mag[k + 1], den = a - 2 * b + c;
        f = (k + (den != 0.f ? 0.5f * (a - c) / den : 0.f)) * sr / N;
      }
      if (npk < MAXPEAKS) { pf[npk] = f; pb[npk] = k; pa[npk++] = mag[k]; }
      else { int w = 0; for (int i = 1; i < MAXPEAKS; i++) if (pa[i] < pa[w]) w = i; if (mag[k] > pa[w]) { pf[w] = f; pb[w] = k; pa[w] = mag[k]; } }
    }
    for (int k = kmin - 1; k <= kmax + 1; k++) prevPh[k] = ph[k];
    for (int k = 0; k <= N / 2; k++) pk[k] = -1;
    for (int p = 0; p < npk; p++) pk[pb[p]] = p;
    findNotes();
  }
  // Peak indices by FFT bin (-1: none), so a harmonic's peak is found in O(1).
  int peakOf(float t, const float* a, float tolC) const {
    const int b = (int)lrintf(t * N / sr), r = 1 + (int)(t * (exp2f(tolC / 1200.f) - 1.f) * N / sr); int w = -1; float bc = tolC;
    for (int k = b - r; k <= b + r; k++) { if (k < 0 || k > N / 2) continue; const int p = pk[k]; if (p < 0 || a[p] <= 0.f) continue; const float c = fabsf(cents(pf[p], t)); if (c < bc) { bc = c; w = p; } }
    return w;
  }
  static float tol(int h) { return fminf(35.f + 1.5f * h, 60.f); }   // a little wider up high (strings stretch their partials)
  // How well f0 is explained by the peaks: each harmonic's amplitude, weighted by (f0 + 52) / (h f0 + 320)
  // (Klapuri 2006), so a note an octave down only wins with its odd harmonics there too.
  float salience(float f0, const float* a) const {
    float s = 0.f;
    for (int h = 1; h <= MAXH; h++) {
      const float t = f0 * h; if (t > TOP) break;
      const int p = peakOf(t, a, tol(h)); if (p >= 0) s += a[p] * (f0 + 52.f) / (t + 320.f);
    }
    return s;
  }
  void findNotes() {
    float a[MAXPEAKS]; for (int p = 0; p < npk; p++) a[p] = pa[p];
    nnotes = 0; float first = 0.f;
    for (int it = 0; it < MAXNOTES; it++) {
      int best = -1; float bs = 0.f;
      for (int p = 0; p < npk; p++) {                       // candidates: the peaks themselves
        if (a[p] <= 0.f || pf[p] < 65.f || pf[p] > 1600.f) continue;
        float s = salience(pf[p], a);
        if (nnotes > 0) {                                    // a further note needs harmonics of its own, not one stray partial
          int own = 0; for (int h = 2; h <= 4; h++) own += peakOf(pf[p] * h, a, tol(h)) >= 0;
          if (own < 1) continue;
        }
        if (s > bs) { bs = s; best = p; }
      }
      if (best < 0 || (it > 0 && bs < STOP * first)) break;
      if (it == 0) first = bs;
      const float bf = pf[best];
      // take its harmonics out, each down to the average of it and its neighbours (spectral smoothness:
      // what sticks out above a string's smooth envelope belongs to another note, an octave or a fifth up)
      int hp[MAXH + 2]; float ah[MAXH + 2]; int nh = 0;
      for (int h = 1; h <= MAXH; h++) { const float t = bf * h; if (t > TOP) break; hp[h] = peakOf(t, a, tol(h)); ah[h] = hp[h] >= 0 ? a[hp[h]] : 0.f; nh = h; }
      ah[0] = ah[1]; ah[nh + 1] = 0.f; if (nh + 2 <= MAXH + 1) ah[nh + 2] = 0.f;
      float e2 = 0.f;
      for (int h = 1; h <= nh; h++) {
        if (hp[h] < 0) continue;
        float sm = 0.f; int cnt = 0; for (int q = h - SMW; q <= h + SMW; q++) if (q >= 0 && q <= nh + 1) { sm += ah[q]; cnt++; } sm /= cnt;
        const float take = fminf(ah[h], fmaxf(sm, h == 1 ? ah[1] : 0.f));
        e2 += take * take; a[hp[h]] -= take; if (a[hp[h]] < 1e-6f) a[hp[h]] = 0.f;
      }
      notes[nnotes++] = {bf, sqrtf(e2)};
    }
  }
};

// Voices for the notes the tracker finds: matched, started, glided and released (see the top).
struct PolyAlloc {
  static constexpr int MAXV = 8;
  struct V { bool on = false, fresh = false; float hz = 0, level = 0; int miss = 0; unsigned seq = 0; } v[MAXV];
  static constexpr int CONF = 3;                              // hops in a row a new note must be seen (16 ms)
  PolyNote pending[PolyTracker::MAXNOTES]; int pendN[PolyTracker::MAXNOTES] = {0}, npend = 0; unsigned seq = 0;
  float pre[MAXV] = {0}; int onsetIn = 0;                      // a pick: levels then, and hops until it's judged
  void reset() { for (auto& x : v) x = V(); npend = 0; onsetIn = 0; }
  // A pick was heard: three hops on (the note is in the spectrum by then), a sounding voice whose note got
  // 30% louder starts again (the same note picked again: the tape restarts, the filter opens).
  void onset() { if (onsetIn > 0) return; for (int k = 0; k < MAXV; k++) pre[k] = v[k].on ? v[k].level : 0.f; onsetIn = 3; }
  static float cents(float a, float b) { return 1200.f * log2f(a / b); }
  // quantise: snap each note to the nearest equal-tempered semitone (keys) or leave it free (bends).
  void update(const PolyNote* notes, int n, bool quantise, const float* envOf) {
    bool used[MAXV] = {false};
    PolyNote seen[PolyTracker::MAXNOTES]; int ns = 0;
    for (int i = 0; i < n; i++) { float hz = notes[i].hz; if (quantise) hz = 440.f * exp2f(roundf(12.f * log2f(hz / 440.f)) / 12.f); seen[ns++] = {hz, notes[i].amp}; }
    bool matched[PolyTracker::MAXNOTES] = {false};
    for (int i = 0; i < ns; i++) {                          // a sounding voice within 70 cents keeps going
      int w = -1; float bc = 70.f;
      for (int k = 0; k < MAXV; k++) if (v[k].on && !used[k]) { const float c = fabsf(cents(seen[i].hz, v[k].hz)); if (c < bc) { bc = c; w = k; } }
      if (w >= 0) { used[w] = true; matched[i] = true; v[w].hz = seen[i].hz; v[w].level = seen[i].amp; v[w].miss = 0; v[w].fresh = false; }
    }
    PolyNote next[PolyTracker::MAXNOTES]; int nextN[PolyTracker::MAXNOTES], nn = 0;
    for (int i = 0; i < ns; i++) {
      if (matched[i]) continue;
      int count = 1;                                         // seen in the hops before too? enough of them: start a voice
      for (int j = 0; j < npend; j++) if (fabsf(cents(seen[i].hz, pending[j].hz)) < 70.f) count = std::max(count, pendN[j] + 1);
      if (count < CONF) { nextN[nn] = count; next[nn++] = seen[i]; continue; }
      int w = -1; float quiet = 1e9f;
      for (int k = 0; k < MAXV; k++) if (!v[k].on && !used[k] && envOf[k] < quiet) { quiet = envOf[k]; w = k; }   // the quietest released voice
      if (w < 0) for (int k = 0; k < MAXV; k++) if (!used[k] && v[k].level < quiet) { quiet = v[k].level; w = k; }   // or steal the quietest
      if (w < 0) continue;
      used[w] = true; v[w].on = true; v[w].fresh = true; v[w].hz = seen[i].hz; v[w].level = seen[i].amp; v[w].miss = 0; v[w].seq = ++seq;
    }
    if (onsetIn > 0 && --onsetIn == 0)
      for (int k = 0; k < MAXV; k++) if (v[k].on && used[k] && !v[k].fresh && pre[k] > 0.f && v[k].level > 1.3f * pre[k]) { v[k].fresh = true; v[k].seq = ++seq; }
    npend = nn; for (int i = 0; i < nn; i++) { pending[i] = next[i]; pendN[i] = nextN[i]; }
    for (int k = 0; k < MAXV; k++) if (v[k].on && !used[k] && ++v[k].miss >= 2) v[k].on = false;   // gone two hops: release
  }
};

} // namespace rig

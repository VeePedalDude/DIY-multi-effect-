// harness.cpp - native tests for the rig engine. Build: see build.sh
#include "../dsp/rig.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <complex>
#include <chrono>

using namespace rig;
static const float SR = 48000.f;
static int fails = 0;
#define CHECK(cond, ...) do { bool _ok = (cond); printf("  [%s] ", _ok ? "PASS" : "FAIL"); printf(__VA_ARGS__); printf("\n"); if (!_ok) fails++; } while (0)

static float dbf(double x) { return 20.f * (float)log10(x + 1e-12); }

// ---------------------------------------------------------- synthetic guitar
struct Note { float t, f, vel; bool mute; };
static std::vector<float> guitar(float secs, std::vector<Note>& notes, unsigned seed = 1) {
  int N = (int)(secs * SR);
  std::vector<float> out(N, 0.f);
  srand(seed);
  for (auto& nt : notes) {
    int start = (int)(nt.t * SR), period = (int)(SR / nt.f);
    std::vector<float> line(period);
    for (auto& v : line) v = nt.vel * ((rand() / (float)RAND_MAX) * 2.f - 1.f);
    float damp = nt.mute ? 0.93f : 0.996f;
    int dur = (int)((nt.mute ? 0.18f : 2.2f) * SR);
    int idx = 0;
    for (int i = 0; i < dur && start + i < N; i++) {
      float a = line[idx], b = line[(idx + 1) % period];
      float y = 0.5f * (a + b) * damp;
      line[idx] = y; idx = (idx + 1) % period;
      float fade = i > dur - 480 ? (float)(dur - i) / 480.f : 1.f;
      out[start + i] += a * fade;
    }
  }
  float pk = 0; for (float v : out) pk = fmaxf(pk, fabsf(v));
  for (auto& v : out) v *= 0.25f / pk;                 // peaks at -12 dBFS (typical 2i2 INST level)
  return out;
}
static std::vector<Note> riff() {
  std::vector<Note> n;
  float E2 = 82.41f, A2 = 110.f, B2 = 123.47f, D3 = 146.83f;
  float t = 0.2f;
  for (int bar = 0; bar < 4; bar++) {
    float root = bar % 2 ? A2 : E2;
    n.push_back({t, root, 1.f, false}); n.push_back({t + 0.008f, root * 1.4983f, .8f, false}); n.push_back({t + 0.016f, root * 2, .6f, false});
    t += 0.75f;
    for (int k = 0; k < 4; k++) { n.push_back({t, root, .7f, true}); t += 0.125f; }
    n.push_back({t, bar % 2 ? D3 : B2, .9f, false}); t += 0.75f;
  }
  return n;
}
static int countOnsetGroups(const std::vector<Note>& n) {     // chord strums within 30 ms = one pick event
  int c = 0; float last = -1;
  for (auto& x : n) { if (x.t - last > 0.03f) c++; last = x.t; }
  return c;
}

// ---------------------------------------------------------- engine runner
struct Run { std::vector<float> L, R; std::vector<float> m0; };
static Run render(Engine& e, const std::vector<float>& in, int block = 128, void (*hook)(Engine&, int) = nullptr) {
  Run r; r.L.resize(in.size()); r.R.resize(in.size());
  for (size_t i = 0; i < in.size(); i += block) {
    int n = (int)std::min<size_t>(block, in.size() - i);
    if (hook) hook(e, (int)i);
    memcpy(e.inBuf, &in[i], n * sizeof(float));
    e.process(n);
    memcpy(&r.L[i], e.outL, n * sizeof(float)); memcpy(&r.R[i], e.outR, n * sizeof(float));
  }
  return r;
}
static double rms(const std::vector<float>& x, size_t a, size_t b) {
  double s = 0; b = std::min(b, x.size()); for (size_t i = a; i < b; i++) s += (double)x[i] * x[i];
  return b > a ? sqrt(s / (b - a)) : 0;
}
static float peak(const std::vector<float>& x, size_t a = 0, size_t b = SIZE_MAX) {
  float p = 0; b = std::min(b, x.size()); for (size_t i = a; i < b; i++) p = fmaxf(p, fabsf(x[i])); return p;
}
static float maxStep(const std::vector<float>& x, size_t a, size_t b) {
  float m = 0; b = std::min(b, x.size()); for (size_t i = a + 1; i < b; i++) m = fmaxf(m, fabsf(x[i] - x[i - 1])); return m;
}
static void only(Engine& e, int keep) { for (int m = 0; m < NMOD; m++) { e.byp[m] = (m != keep); e.wet[m] = (m != keep) ? 0.f : 1.f; } }

// ---------------------------------------------------------- FFT
static void fft(std::vector<std::complex<double>>& a) {
  size_t n = a.size();
  for (size_t i = 1, j = 0; i < n; i++) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap(a[i], a[j]); }
  for (size_t len = 2; len <= n; len <<= 1) {
    double ang = -2 * M_PI / len; std::complex<double> wl(cos(ang), sin(ang));
    for (size_t i = 0; i < n; i += len) { std::complex<double> w(1); for (size_t j = 0; j < len / 2; j++) { auto u = a[i + j], v = a[i + j + len / 2] * w; a[i + j] = u + v; a[i + j + len / 2] = u - v; w *= wl; } }
  }
}

int main() {
  printf("rig engine native tests  (sr %.0f, block 128)\n\n", SR);
  auto notes = riff();
  auto gtr = guitar(14.f, notes);
  // add a -75 dBFS noise floor (pickup hiss / hum stand-in)
  std::vector<float> gtrN = gtr; srand(7);
  for (auto& v : gtrN) v += 0.000178f * ((rand() / (float)RAND_MAX) * 2 - 1) * 1.732f;

  // 1. full chain sanity + CPU
  {
    printf("1. Full chain, default settings, 4 modulations patched\n");
    Engine e; e.init(SR);
    e.conn[1][0] = 1; e.conn[3][0] = 0; e.conn[3][1] = 3; e.conn[4][1] = 4;
    std::vector<float> long60; while (long60.size() < 60 * SR) long60.insert(long60.end(), gtrN.begin(), gtrN.end());
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
  // 2. Gate
  {
    printf("2. Noise gate (threshold -59 dBFS, input floor -75 dBFS)\n");
    std::vector<float> noise((size_t)(3 * SR)); srand(3);
    for (auto& v : noise) v = 0.000178f * ((rand() / (float)RAND_MAX) * 2 - 1) * 1.732f;
    Engine e; e.init(SR); only(e, 0); e.masterDb = 0; e.masterS.y = 1;
    auto r = render(e, noise);
    double inDb = dbf(rms(noise, SR, 3 * SR)), outDb = dbf(rms(r.L, SR, 3 * SR));
    CHECK(outDb < inDb - 40, "noise in %.1f dBFS -> out %.1f dBFS (%.1f dB attenuation)", inDb, outDb, inDb - outDb);
    e.init(SR); only(e, 0); e.masterDb = 0; e.masterS.y = 1;
    auto r2 = render(e, gtr);
    double ratio = rms(r2.L, 0, r2.L.size()) / rms(gtr, 0, gtr.size());
    CHECK(ratio > 0.97, "guitar passes the gate at %.2f dB", dbf(ratio));
  }
  // 3. Amp aliasing
  {
    printf("3. Amp stand-in aliasing: non-harmonic energy below harmonic energy (bigger = cleaner)\n");
    auto measure = [&](float f0, float amp, float drive, int os, bool adaa) {
      const int N = 65536; std::vector<float> s(N + 8192);
      for (size_t i = 0; i < s.size(); i++) s[i] = amp * sinf(2 * (float)M_PI * f0 * i / SR);
      Engine e; e.init(SR); only(e, 1); e.amp.adaa = adaa; e.amp.setOS(os); e.amp.reset();
      e.base[1][0] = drive; e.amp.inS.y = drive; e.masterDb = 0; e.masterS.y = 1;
      auto r = render(e, s);
      std::vector<std::complex<double>> X(N);
      for (int i = 0; i < N; i++) X[i] = r.L[8192 + i] * (0.5 - 0.5 * cos(2 * M_PI * i / (N - 1)));
      fft(X);
      double harm = 0, other = 0;
      for (int k = 2; k < N / 2; k++) {
        double f = (double)k * SR / N, p = std::norm(X[k]);
        if (f < 30) continue;
        double h = f / f0, near = fabs(h - round(h)) * f0;
        if (near < 25 && round(h) >= 1) harm += p; else other += p;
      }
      return -10 * log10(other / harm);
    };
    struct V { int os; bool adaa; const char* name; } vs[] = {{1,false,"1x"},{2,false,"2x"},{4,false,"4x"},{1,true,"1x+ADAA"},{2,true,"2x+ADAA"},{4,true,"4x+ADAA"}};
    printf("      %-9s %22s %22s %22s\n", "variant", "2.5k sine, max drive", "1234 Hz sine, max drive", "330 Hz, default drive");
    for (auto& v : vs) printf("      %-9s %19.1f dB %19.1f dB %19.1f dB\n", v.name, measure(2500, .25f, 1.f, v.os, v.adaa), measure(1234, .25f, 1.f, v.os, v.adaa), measure(330, .25f, .6f, v.os, v.adaa));
    Engine d; d.init(SR);
    float shipped = measure(2500, .25f, 1.f, d.amp.os, d.amp.adaa);
    CHECK(shipped > 50, "shipped setting (%dx%s): %.1f dB at the worst case", d.amp.os, d.amp.adaa ? "+ADAA" : "", shipped);
  }
  // 4. Cab IR default response
  {
    printf("4. Cab IR default response (impulse through cab only, dB rel 1 kHz)\n");
    Engine e; e.init(SR); only(e, 2); e.masterDb = 0; e.masterS.y = 1; e.base[2][0] = 0; e.cab.mixS.y = 1; e.base[2][1] = 1;
    std::vector<float> imp(8192, 0.f); imp[100] = 0.5f;
    auto r = render(e, imp);
    auto mag = [&](float f) { std::complex<double> a = 0; for (int i = 0; i < 8192; i++) a += (double)r.L[i] * std::polar(1.0, -2 * M_PI * f * i / SR); return std::abs(a); };
    double ref = mag(1000);
    printf("      ");
    for (float f : {80.f, 120.f, 400.f, 1000.f, 2600.f, 5000.f, 8000.f, 12000.f}) printf("%gHz %+.1f  ", f, dbf(mag(f) / ref));
    printf("\n");
    CHECK(dbf(mag(12000) / ref) < -20, "high end rolled off like a guitar speaker (12 kHz %.1f dB)", dbf(mag(12000) / ref));
  }
  // 5. Delay stability at max feedback + halo
  {
    printf("5. Hologram delay stability (feedback 95%%, halo 1.0, 40 s)\n");
    Engine e; e.init(SR); only(e, 3); e.masterDb = 0; e.masterS.y = 1; e.base[3][0] = 1; e.base[3][1] = 1; e.holo.fbS.y = 1; e.holo.haloS.y = 1;
    std::vector<float> in((size_t)(40 * SR), 0.f);
    for (size_t i = 0; i < (size_t)SR; i++) in[i] = gtr[(size_t)(0.2f * SR) + i];
    auto r = render(e, in);
    double w1 = rms(r.L, 1 * SR, 6 * SR), w2 = rms(r.L, 15 * SR, 20 * SR), w3 = rms(r.L, 35 * SR, 40 * SR);
    CHECK(e.nanResets == 0 && peak(r.L) <= 0.98f, "no blow-up (peak %.3f)", peak(r.L));
    CHECK(w3 < w1 * 0.1, "repeats decay: %.1f -> %.1f -> %.1f dBFS", dbf(w1), dbf(w2), dbf(w3));
    float d1 = 0; { // measure first repeat time from impulse
      Engine e2; e2.init(SR); only(e2, 3); e2.masterDb = 0; e2.masterS.y = 1; e2.base[3][1] = 0; e2.holo.haloS.y = 0;
      std::vector<float> im((size_t)SR, 0.f); im[64] = 1.f;
      auto r2 = render(e2, im);
      size_t best = 0; float bv = 0; for (size_t i = 1000; i < r2.L.size(); i++) if (fabsf(r2.L[i]) > bv) { bv = fabsf(r2.L[i]); best = i; }
      d1 = (best - 64) * 1000.f / SR;
    }
    CHECK(fabsf(d1 - 375.f) < 1.5f, "first repeat at %.1f ms (dotted 8th @120 = 375 ms)", d1);
  }
  // 6. Reverb RT60
  {
    printf("6. Tesseract reverb RT60 (Schroeder T20, broadband)\n");
    for (float size : {0.f, 0.5f, 1.f}) {
      Engine e; e.init(SR); only(e, 4); e.masterDb = 0; e.masterS.y = 1; e.base[4][0] = size;
      std::vector<float> in((size_t)(12 * SR), 0.f); in[4800] = 0.5f;
      auto r = render(e, in);
      std::vector<double> edc(r.L.size()); double acc = 0;
      for (size_t i = r.L.size(); i-- > 4801;) { acc += (double)r.L[i] * r.L[i] + (double)r.R[i] * r.R[i]; edc[i] = acc; }
      double e0 = edc[4801]; size_t i5 = 0, i25 = 0;
      for (size_t i = 4801; i < r.L.size(); i++) { double d = 10 * log10(edc[i] / e0); if (!i5 && d <= -5) i5 = i; if (!i25 && d <= -25) { i25 = i; break; } }
      float rt = i25 ? 3.f * (i25 - i5) / SR : -1;
      float target = 0.5f + 7.f * size;
      CHECK(rt > 0 && fabsf(rt - target) / target < 0.35f, "size %.1f: RT60 %.2f s (target %.1f s)", size, rt, target);
    }
    Engine e; e.init(SR); only(e, 4); e.base[4][0] = 1; e.base[4][1] = 1;  // fold sweep stays lossless
    e.tess.foldS.y = 1;
    std::vector<float> in((size_t)(30 * SR), 0.f); for (size_t i = 0; i < (size_t)SR; i++) in[i] = gtr[(size_t)(0.2f * SR) + i];
    auto r = render(e, in);
    CHECK(e.nanResets == 0 && rms(r.L, 25 * SR, 30 * SR) < rms(r.L, 1 * SR, 3 * SR) * 0.01, "max size + max fold decays (%.1f -> %.1f dBFS)", dbf(rms(r.L, SR, 3 * SR)), dbf(rms(r.L, 25 * SR, 30 * SR)));
  }
  // 7. Clicks: bypass toggles and tempo jump
  {
    printf("7. Click checks on a 220 Hz sine (max sample step vs steady state)\n");
    std::vector<float> s((size_t)(6 * SR)); for (size_t i = 0; i < s.size(); i++) s[i] = 0.25f * sinf(2 * (float)M_PI * 220 * i / SR);
    Engine e; e.init(SR); e.byp[0] = true; e.wet[0] = 0; e.masterDb = 0; e.masterS.y = 1;
    auto steady = render(e, s);
    float base = maxStep(steady.L, 2 * SR, 3 * SR);
    Engine e2; e2.init(SR); e2.byp[0] = true; e2.wet[0] = 0; e2.masterDb = 0; e2.masterS.y = 1;
    auto r = render(e2, s, 128, [](Engine& x, int i) {
      if (i % 24000 < 128 && i > 48000) x.byp[1] = !x.byp[1];          // toggle amp every 0.5 s
      if (i == 4 * 48000) x.bpm = 70;                                    // tempo jump
    });
    float worst = maxStep(r.L, 1 * SR, 6 * SR);
    CHECK(worst < base * 3.f, "bypass toggles + tempo jump: max step %.4f vs steady %.4f", worst, base);
  }
  // 8. Modulation routing
  {
    printf("8. Modulation: LFO -> HOLOGRAM feedback (base 0.45, depth 0.22)\n");
    Engine e; e.init(SR); e.conn[3][0] = 0;
    float lo = 1, hi = 0;
    std::vector<float> sil((size_t)(8 * SR), 0.f);
    for (size_t i = 0; i < sil.size(); i += 128) { memcpy(e.inBuf, &sil[i], 128 * 4); e.process(128); lo = fminf(lo, e.eff[3][0]); hi = fmaxf(hi, e.eff[3][0]); }
    CHECK(fabsf(lo - 0.23f) < 0.01f && fabsf(hi - 0.67f) < 0.01f, "effective range %.3f..%.3f over one 8 s LFO cycle", lo, hi);
    Engine e3; e3.init(SR); e3.src.ctrl = 0.5f; e3.conn[2][1] = 5;
    for (int k = 0; k < 400; k++) { memset(e3.inBuf, 0, sizeof e3.inBuf); e3.process(128); }
    CHECK(fabsf(e3.eff[2][1] - (0.9f + 0.22f * 0.5f > 1 ? 1.f : 0.9f + 0.11f)) < 0.01f, "controller source reaches target (cab mix %.3f, clamped to 1)", e3.eff[2][1]);
  }
  // 9. Pick onsets
  {
    printf("9. Pick-ADSR onset detection on the riff\n");
    Engine e; e.init(SR);
    render(e, gtrN);
    int truth = countOnsetGroups(notes);
    CHECK(abs(e.src.onsets - truth) <= 2, "detected %d onsets, riff has %d pick events", e.src.onsets, truth);
  }
  // 10. Three-body stays bounded and moving
  {
    printf("10. Three-body source over 10 minutes\n");
    Engine e; e.init(SR); float lo = 1, hi = -1; int resets = 0; float last = 0;
    for (int k = 0; k < (int)(600 * SR / 128); k++) {
      memset(e.inBuf, 0, sizeof e.inBuf); e.process(128);
      float v = e.src.val[3]; lo = fminf(lo, v); hi = fmaxf(hi, v);
      if (fabsf(v - last) > 0.5f) resets++;
      last = v;
    }
    CHECK(std::isfinite(lo) && hi - lo > 1.0f, "range %.2f..%.2f, %d jumps from resets", lo, hi, resets);
  }
  printf("\n%s (%d failure%s)\n", fails ? "SOME TESTS FAILED" : "ALL TESTS PASSED", fails, fails == 1 ? "" : "s");
  return fails ? 1 : 0;
}

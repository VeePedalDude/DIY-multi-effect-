// test_synth.cpp - the synth filters in engine/modules/synth/: svf, comb, formant, ota, ladder, diode,
// sallen. Small-signal responses against their analog models, slopes, where each self-oscillates
// and how well that tracks CUTOFF, the built-in envelope, aliasing when driven, clicks, CPU.
#include "framework.h"
#include <chrono>
using namespace rigtest;

namespace {
template <class M> std::vector<float> defs() { M m; const ModuleDesc& d = m.desc(); std::vector<float> k(d.nparams); for (int i = 0; i < d.nparams; i++) k[i] = d.params[i].def; return k; }
template <class M> float at(int i, float real) {                 // knob position for a real value
  M m; const ParamDesc& p = m.desc().params[i];
  return p.curve == Curve::Log ? logf(real / p.min) / logf(p.max / p.min) : (real - p.min) / (p.max - p.min);
}
template <class M> float resp(const std::vector<float>& k, float f, float amp = 0.001f) {   // gain in dB of a steady sine
  M m; m.init(SR); float e[MAXPAR] = {0}; for (size_t i = 0; i < k.size(); i++) e[i] = k[i];
  float iL[128], oL[128], oR[128]; double si = 0, so = 0; const int N = (int)(0.6f * SR);
  for (int p = 0; p < N; p += 128) {
    for (int i = 0; i < 128; i++) iL[i] = amp * sinf(2 * (float)M_PI * f * (float)((p + i) % (int)SR) / SR);
    m.process(iL, iL, oL, oR, 128, e, Context{});
    if (p >= (int)(0.4f * SR)) for (int i = 0; i < 128; i++) { si += (double)iL[i] * iL[i]; so += (double)oL[i] * oL[i]; }
  }
  return 10.f * (float)log10(so / si);
}
// Kick the filter with 5 ms of noise, then silence: does it keep ringing after a second, at what pitch, how loud?
struct Ring { bool sustains; float hz, rms; };
template <class M> Ring ring(const std::vector<float>& k) {
  M m; m.init(SR); float e[MAXPAR] = {0}; for (size_t i = 0; i < k.size(); i++) e[i] = k[i];
  std::vector<float> y; float iL[128] = {0}, oL[128], oR[128];
  unsigned n = 5;
  for (int p = 0; p < (int)(1.5f * SR); p += 128) { for (int i = 0; i < 128; i++) { n = n * 1664525u + 1013904223u; iL[i] = p + i < 240 ? 0.25f * ((n >> 8) / 8388608.f - 1.f) : 0.f; } m.process(iL, iL, oL, oR, 128, e, Context{}); if (p >= (int)SR) y.insert(y.end(), oL, oL + 128); }
  double s = 0; int zc = 0, first = -1, last = -1;
  for (size_t i = 1; i < y.size(); i++) { s += (double)y[i] * y[i]; if (y[i - 1] < 0 && y[i] >= 0) { zc++; if (first < 0) first = (int)i; last = (int)i; } }
  const float rmsv = (float)sqrt(s / y.size());
  return {rmsv > 1e-3f, zc > 1 ? (zc - 1) * SR / (float)(last - first) : 0.f, rmsv};
}
float cents(float a, float b) { return 1200.f * log2f(a / b); }
std::complex<double> jw(float f, float fc) { return std::complex<double>(0, f / fc); }
template <class M> double cpuNs() {
  M m; m.init(SR); auto k = defs<M>(); float e[MAXPAR] = {0}; for (size_t i = 0; i < k.size(); i++) e[i] = k[i];
  const auto& g = riffSignal(); float oL[128], oR[128]; double best = 1e9;
  for (int rep = 0; rep < 3; rep++) {
    const auto t0 = std::chrono::steady_clock::now();
    for (size_t p = 0; p + 128 <= g.size(); p += 128) m.process(&g[p], &g[p], oL, oR, 128, e, Context{});
    best = std::min(best, std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / (double)g.size());
  }
  return best;
}
// Non-harmonic energy below the harmonics for a sine at f (dB; bigger is cleaner).
template <class M> float aliasDb(std::vector<float> k, float f, float amp) {
  M m; m.init(SR); float e[MAXPAR] = {0}; for (size_t i = 0; i < k.size(); i++) e[i] = k[i];
  const int N = 1 << 15; std::vector<float> y(N); float iL[128], oL[128], oR[128];
  for (int p = 0; p < N + 9600; p += 128) { for (int i = 0; i < 128; i++) iL[i] = amp * sinf(2 * (float)M_PI * f * (p + i) / SR); m.process(iL, iL, oL, oR, 128, e, Context{}); for (int i = 0; i < 128; i++) if (p + i >= 9600 && p + i - 9600 < N) y[p + i - 9600] = oL[i]; }
  double harm = 0, other = 0;
  for (int b = 4; b < N / 2; b += 4) {
    const double fb = b * SR / N; double re = 0, im = 0;
    for (int i = 0; i < N; i++) { const double w = 0.5 - 0.5 * cos(2 * M_PI * i / N), a = 2 * M_PI * b * i / N; re += w * y[i] * cos(a); im += w * y[i] * sin(a); }
    const double pw = re * re + im * im; (fabs(fb / f - round(fb / f)) * f < 60 ? harm : other) += pw;
  }
  return dbf(sqrt(harm / other));
}
} // namespace

TEST("110 svf: 12 dB/octave, the analog SVF's response, a deep notch; it never self-oscillates") {
  auto k = defs<SvfFilter>(); k[0] = at<SvfFilter>(0, 1000); k[1] = at<SvfFilter>(1, 0); // Q 0.5
  const float slope = resp<SvfFilter>(k, 8000) - resp<SvfFilter>(k, 4000);
  CHECK(fabsf(slope + 12.f) < 1.5f, "low-pass: %.1f dB from 4 to 8 kHz (12 dB/octave)", slope);
  k[1] = logf(4.f) / logf(50.f);                                          // Q 2
  float worst = 0; for (float f : {200.f, 700.f, 1000.f, 1400.f, 3000.f}) {
    const auto s = jw(f, 1000); const double a = dbf(std::abs(1.0 / (s * s + s / 2.0 + 1.0)));
    worst = fmaxf(worst, fabsf(resp<SvfFilter>(k, f) - (float)a)); }
  CHECK(worst < 0.15f, "Q 2: within %.3f dB of the analog low-pass, 200 Hz-3 kHz", worst);
  auto kb = k; kb[2] = at<SvfFilter>(2, 3); const float bp = resp<SvfFilter>(kb, 1000);
  auto kh = k; kh[2] = at<SvfFilter>(2, 2); const float hpSlope = resp<SvfFilter>(kh, 100) - resp<SvfFilter>(kh, 200);
  k[2] = at<SvfFilter>(2, 1); const float n2 = resp<SvfFilter>(k, 1000);
  CHECK(n2 < -40.f && fabsf(bp) < 0.2f && fabsf(hpSlope + 12.f) < 1.5f, "notch %.1f dB at the cutoff; band-pass %+.2f dB at its peak; high-pass %.1f dB from 200 to 100 Hz", n2, bp, hpSlope);
  auto kr = defs<SvfFilter>(); kr[1] = 1.f; const Ring r = ring<SvfFilter>(kr);
  CHECK(!r.sustains, "RESO 100%%: rings, then dies away (%.1e RMS after a second), like the SEM", r.rms);
}

TEST("111 comb: rings at FREQ and its harmonics; negative feedback an octave down, odd harmonics") {
  auto c = defs<Comb>(); c[4] = 1.f; c[2] = 0.f;                          // 220 Hz, +70%, no damping, fully wet
  const float p1 = resp<Comb>(c, 220), p2 = resp<Comb>(c, 440), t = resp<Comb>(c, 330);
  CHECK(p1 - t > 12.f && p2 - t > 12.f, "+70%%: 220 and 440 Hz stand %.1f and %.1f dB above 330 Hz", p1 - t, p2 - t);
  c[1] = at<Comb>(1, -70); const float o1 = resp<Comb>(c, 110), o3 = resp<Comb>(c, 330), ev = resp<Comb>(c, 220);
  CHECK(o1 - ev > 12.f && o3 - ev > 12.f, "-70%%: 110 and 330 Hz stand %.1f and %.1f dB above 220 Hz", o1 - ev, o3 - ev);
  c = defs<Comb>(); c[4] = 1.f; c[1] = 1.f; c[2] = 0.f; float pk = 0;
  { Comb m; m.init(SR); float e[MAXPAR]; for (int i = 0; i < 5; i++) e[i] = c[i]; const auto& g = riffSignal(); float oL[128], oR[128];
    for (size_t p = 0; p + 128 <= g.size(); p += 128) { m.process(&g[p], &g[p], oL, oR, 128, e, Context{}); pk = fmaxf(pk, peak(std::vector<float>(oL, oL + 128))); } }
  CHECK(pk < 1.f, "FEEDBACK +95%%, no damping, the riff: peak %.2f (wet scaled by 1 - feedback)", pk);
}

TEST("112 formant: the vowels' resonances where the table puts them; SHIFT moves them") {
  auto k = defs<Formant>(); k[2] = 1.f;                                   // sharp, to find the peaks
  auto peaks = [&](const std::vector<float>& kk) {
    std::vector<std::pair<float, float>> r; std::vector<float> f, g; for (float hz = 150; hz < 4000; hz *= 1.01f) { f.push_back(hz); g.push_back(resp<Formant>(kk, hz)); }
    for (size_t i = 1; i + 1 < g.size(); i++) if (g[i] > g[i - 1] && g[i] >= g[i + 1]) r.push_back({f[i], g[i]});
    return r;
  };
  for (int v : {0, 2, 4}) {
    auto kk = k; kk[0] = v / 4.f; auto pk = peaks(kk); float worst = 0; int found = 0;
    for (int b = 0; b < 3; b++) { float best = 1e9; for (auto& p : pk) best = fminf(best, fabsf(p.first / FORMANT_VOWELS[v].f[b] - 1)); worst = fmaxf(worst, best); found += best < 0.05f; }
    CHECK(found == 3, "vowel %c: all three formants within %.1f%% of %g, %g, %g Hz", "AEIOU"[v], worst * 100, FORMANT_VOWELS[v].f[0], FORMANT_VOWELS[v].f[1], FORMANT_VOWELS[v].f[2]);
  }
  auto ks = k; ks[1] = 1.f; auto pk = peaks(ks); float f1 = pk.empty() ? 0 : pk[0].first;
  CHECK(fabsf(f1 / 1200.f - 1) < 0.05f, "SHIFT +12: the first formant moves from 600 to %.0f Hz (an octave)", f1);
}

TEST("113 ota: POLES sets the slope; the passband holds as RESO rises; it sings at 100%") {
  auto k = defs<Ota>(); k[0] = at<Ota>(0, 500); k[1] = 0;
  for (int p = 1; p <= 4; p++) {
    k[2] = at<Ota>(2, (float)p); const float g = resp<Ota>(k, 4000), want = -10.f * p * log10f(1 + 64.f);
    CHECK(fabsf(g - want) < 1.5f, "%d pole%s: %.1f dB at 8x the cutoff (analog %.1f)", p, p > 1 ? "s" : "", g, want);
  }
  k = defs<Ota>(); k[0] = at<Ota>(0, 2000); k[1] = 0.9f; const float pass = resp<Ota>(k, 100);
  CHECK(fabsf(pass) < 0.5f, "RESO 90%%: the passband stays at %+.2f dB", pass);
  float worst = 0; for (float f : {250.f, 1000.f, 4000.f}) { auto kk = defs<Ota>(); kk[0] = at<Ota>(0, f); kk[1] = 1.f; const Ring r = ring<Ota>(kk); worst = fmaxf(worst, r.sustains ? fabsf(cents(r.hz, f)) : 9999.f); }
  CHECK(worst < 30.f, "RESO 100%%: sings on its own at 250 Hz, 1 kHz and 4 kHz, within %.1f cents of CUTOFF", worst);
}

TEST("114 ladder: the analog ladder's response, 24 dB/octave, the bass loss COMP restores, oscillation tracks CUTOFF") {
  auto k = defs<Ladder>(); k[0] = at<Ladder>(0, 500); k[1] = 0.5f;            // k = 2.05
  const float kk = LADDER_KMAX * 0.5f; float worst = 0;
  for (float f : {50.f, 200.f, 500.f, 1000.f, 2000.f}) { const auto s = jw(f, 500); const double a = dbf(std::abs(1.0 / (std::pow(1.0 + s, 4) + (double)kk))); worst = fmaxf(worst, fabsf(resp<Ladder>(k, f) - (float)a)); }
  CHECK(worst < 0.3f, "RESO 50%%: within %.3f dB of 1/((1+s)^4 + k), 50 Hz-2 kHz", worst);
  k[1] = 0; const float sl = resp<Ladder>(k, 4000, 0.1f) - resp<Ladder>(k, 2000, 0.1f);
  const double want = dbf(std::abs(1.0 / std::pow(1.0 + jw(4000, 500), 4))) - dbf(std::abs(1.0 / std::pow(1.0 + jw(2000, 500), 4)));
  CHECK(fabsf(sl - (float)want) < 1.f, "RESO 0: %.1f dB from 2 to 4 kHz (analog %.1f, heading for 24 dB/octave)", sl, want);
  k[1] = 0.9f; const float loss = resp<Ladder>(k, 50); k[4] = 1.f; const float back = resp<Ladder>(k, 50);
  CHECK(loss < -12.f && fabsf(back) < 0.6f, "RESO 90%%: the bass drops %.1f dB (like the Moog); COMP 100%% brings it to %+.2f", loss, back);
  float worst2 = 0, lo = 9, hi = 0;
  for (float f : {100.f, 400.f, 1000.f, 3000.f, 8000.f}) { auto kq = defs<Ladder>(); kq[0] = at<Ladder>(0, f); kq[1] = 1.f; const Ring r = ring<Ladder>(kq); worst2 = fmaxf(worst2, r.sustains ? fabsf(cents(r.hz, f)) : 9999.f); lo = fminf(lo, r.rms); hi = fmaxf(hi, r.rms); }
  CHECK(worst2 < 25.f, "RESO 100%%: self-oscillates at 100 Hz-8 kHz within %.1f cents of CUTOFF", worst2);
  CHECK(hi < 1.f && lo > 0.1f, "...at a steady level: %.2f-%.2f RMS (the saturation holds it)", lo, hi);
}

TEST("115 diode: the diode ladder's analog response; it oscillates only past k = 18.4, on CUTOFF") {
  auto k = defs<Diode>(); k[0] = at<Diode>(0, 500); k[1] = 0.5f; const double kk = DIODE_KMAX * 0.5; float worst = 0;
  for (float f : {50.f, 200.f, 500.f, 1000.f, 2000.f}) {
    const auto s = jw(f, 500) * (double)VA_DIODE_W;
    const double a = dbf(std::abs(sqrt(1 + kk) / (s * s * s * s + 7.0 * s * s * s + 15.0 * s * s + 10.0 * s + 1.0 + kk)));
    worst = fmaxf(worst, fabsf(resp<Diode>(k, f) - (float)a)); }
  CHECK(worst < 0.3f, "RESO 50%%: within %.3f dB of the linear diode ladder, 50 Hz-2 kHz", worst);
  auto kb = defs<Diode>(); kb[1] = 0.9f; const Ring below = ring<Diode>(kb);        // k = 17.9
  float worst2 = 0; for (float f : {200.f, 1000.f, 4000.f}) { auto kq = defs<Diode>(); kq[0] = at<Diode>(0, f); kq[1] = 1.f; const Ring r = ring<Diode>(kq); worst2 = fmaxf(worst2, r.sustains ? fabsf(cents(r.hz, f)) : 9999.f); }
  CHECK(!below.sustains && worst2 < 30.f, "RESO 90%% (k 17.9) dies away; 100%% (k 19.9) sings within %.1f cents of CUTOFF", worst2);
}

TEST("116 sallen: the Sallen-Key responses; LP PEAK past the edge screams, held by the clipper") {
  auto k = defs<Sallen>(); k[2] = at<Sallen>(2, 1000); k[3] = 0.5f; const double kl = SALLEN_KMAX * 0.5; float worst = 0;
  for (float f : {100.f, 500.f, 1000.f, 1500.f, 2000.f}) { const auto s = jw(f, 1000), h = jw(f, 20);   // and the open high-pass at 20 Hz (k 0) in series
    const double a = dbf(std::abs(1.0 / (s * s + (2.0 - kl) * s + 1.0) * (h * h / ((h + 1.0) * (h + 1.0))))); worst = fmaxf(worst, fabsf(resp<Sallen>(k, f) - (float)a)); }
  CHECK(worst < 0.3f, "LP PEAK 50%%: within %.3f dB of 1/(s^2 + (2-k)s + 1) (with the high-pass open at 20 Hz), 100 Hz-2 kHz", worst);
  auto kh = defs<Sallen>(); kh[0] = at<Sallen>(0, 500); kh[1] = 0.5f; kh[2] = 1.f; kh[3] = 0; worst = 0;
  for (float f : {100.f, 250.f, 500.f, 1000.f}) { const auto s = jw(f, 500); const double a = dbf(std::abs(s * s / (s * s + (2.0 - kl) * s + 1.0))); worst = fmaxf(worst, fabsf(resp<Sallen>(kh, f) - (float)a)); }
  CHECK(worst < 0.4f, "HP PEAK 50%%: within %.3f dB of s^2/(s^2 + (2-k)s + 1), 100 Hz-1 kHz (LP open)", worst);
  auto ks = defs<Sallen>(); ks[2] = at<Sallen>(2, 1000); ks[3] = 1.f; const Ring r = ring<Sallen>(ks);
  CHECK(r.sustains && r.rms < 1.5f, "LP PEAK 100%%: screams on its own at %.0f Hz (%.0f cents from CUTOFF, the clipping pulls it), %.2f RMS", r.hz, cents(r.hz, 1000), r.rms);
}

TEST("117 ENV: the filters open with your picking, up to 4 octaves") {
  auto run = [](auto m, std::vector<float> k, int envKnob) {
    m.init(SR); float e[MAXPAR] = {0}; for (size_t i = 0; i < k.size(); i++) e[i] = k[i]; e[envKnob] = 1.f;   // +100%
    const auto& g = riffSignal(); float oL[128], oR[128], mt[2], lo = 1e9, hi = 0;
    for (size_t p = 0; p + 128 <= g.size(); p += 128) { m.process(&g[p], &g[p], oL, oR, 128, e, Context{}); m.meters(mt); lo = fminf(lo, mt[0]); hi = fmaxf(hi, mt[0]); }
    return std::make_pair(lo, hi);
  };
  auto l = run(Ladder{}, defs<Ladder>(), 2); auto s = run(SvfFilter{}, defs<SvfFilter>(), 3);
  printf("      the riff (peaks -12 dBFS) with ENV +100%%: LADDER cutoff %.0f-%.0f Hz, STATE VARIABLE %.0f-%.0f Hz (from 1 kHz)\n", l.first, l.second, s.first, s.second);
  CHECK(log2f(l.second / 1000.f) > 2.3f && l.first < 1100.f, "LADDER: up %.1f octaves on the picks (4 x the level read over -48..0 dBFS: about 3 for -12 dBFS peaks), back to %.0f Hz between notes", log2f(l.second / 1000.f), l.first);
}

TEST("118 synth filters: aliasing when driven, knob sweeps without clicks, CPU") {
  auto lk = defs<Ladder>(); lk[0] = at<Ladder>(0, 6000); lk[1] = 0.5f; lk[3] = 1.f;    // DRIVE 24 dB
  auto dk = defs<Diode>(); dk[0] = at<Diode>(0, 6000); dk[1] = 0.5f; dk[3] = 1.f;
  auto sk = defs<Sallen>(); sk[2] = at<Sallen>(2, 6000); sk[3] = 0.8f; sk[5] = 1.f;
  auto vk = defs<SvfFilter>(); vk[0] = at<SvfFilter>(0, 6000); vk[1] = 0.5f; vk[4] = 1.f;
  auto ok = defs<Ota>(); ok[0] = at<Ota>(0, 6000); ok[1] = 0.5f; ok[4] = 1.f;
  const float al = aliasDb<Ladder>(lk, 2500, 0.25f), ad = aliasDb<Diode>(dk, 2500, 0.25f), as = aliasDb<Sallen>(sk, 2500, 0.25f), av = aliasDb<SvfFilter>(vk, 2500, 0.25f), ao = aliasDb<Ota>(ok, 2500, 0.25f);
  printf("      2.5 kHz at -12 dBFS, DRIVE 24 dB, cutoff 6 kHz: aliasing %.1f (svf), %.1f (ota), %.1f (ladder), %.1f (diode), %.1f (sallen) dB below the harmonics\n", av, ao, al, ad, as);
  const float worstA = fminf(fminf(av, ao), fminf(al, fminf(ad, as)));
  CHECK(worstA > 55.f, "worst %.1f dB down (the amp's shipped setting: 55.4)", worstA);
  // CUTOFF swept 200 Hz-4 kHz four times a second under a 440 Hz sine: no jump beyond a clean sine's steepest.
  auto sweep = [](auto m, std::vector<float> k, int cut) {
    m.init(SR); float e[MAXPAR] = {0}; for (size_t i = 0; i < k.size(); i++) e[i] = k[i];
    float iL[16], oL[16], oR[16], prev = 0, worst = 0;
    for (int p = 0; p < (int)(2 * SR); p += 16) {
      e[cut] = (logf(200.f / 20.f) + logf(20.f) * 0.5f * (1 + sinf(2 * (float)M_PI * 4 * p / SR))) / logf(1000.f);
      for (int i = 0; i < 16; i++) iL[i] = 0.3f * sinf(2 * (float)M_PI * 440 * (p + i) / SR);
      m.process(iL, iL, oL, oR, 16, e, Context{});
      for (int i = 0; i < 16; i++) { if (p > 4800) worst = fmaxf(worst, fabsf(oL[i] - prev)); prev = oL[i]; }
    }
    return worst / (0.3f * 2 * (float)M_PI * 440 / SR * 4.f);   // vs a 440 Hz sine with a 12 dB resonant peak
  };
  const float a = sweep(Ladder{}, defs<Ladder>(), 0), b = sweep(SvfFilter{}, defs<SvfFilter>(), 0), c = sweep(Diode{}, defs<Diode>(), 0), d = sweep(Sallen{}, defs<Sallen>(), 2);
  CHECK(fmaxf(fmaxf(a, b), fmaxf(c, d)) < 1.f, "CUTOFF swept at 4 Hz: largest steps %.2f (ladder) %.2f (svf) %.2f (diode) %.2f (sallen) of a clean sine's", a, b, c, d);
  printf("      CPU, ns per stereo frame (a 48 kHz frame = 20833 ns): svf %.1f, comb %.1f, formant %.1f, ota %.1f, ladder %.1f, diode %.1f, sallen %.1f\n",
         cpuNs<SvfFilter>(), cpuNs<Comb>(), cpuNs<Formant>(), cpuNs<Ota>(), cpuNs<Ladder>(), cpuNs<Diode>(), cpuNs<Sallen>());
}

TEST("119 synth filters in the chain: all seven added live; the riff through each stays finite") {
  int bad = 0;
  for (const char* t : {"svf", "comb", "formant", "ota", "ladder", "diode", "sallen"}) {
    Engine e; e.init(SR); const int s = e.addModule(t, nullptr, -1, false); only(e, s);
    const auto& g = riffSignal(); auto r = render(e, g); bool fin = true; for (float v : r.L) fin = fin && std::isfinite(v);
    const float lv = dbf(rms(r.L, 0, r.L.size()) / rms(g, 0, g.size()));
    printf("      %-8s %+.1f dB\n", t, lv); if (s < 0 || !fin || fabsf(lv) > 15.f) bad++;
  }
  CHECK(bad == 0, "added, playing and finite, within 15 dB of the input at their defaults");
}

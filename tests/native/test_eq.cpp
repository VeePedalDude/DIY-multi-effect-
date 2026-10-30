// test_eq.cpp - the equalisers in engine/modules/filter/, simple to sophisticated: tilt, bax, geq,
// peq, stack, prog, dyneq. Each is measured with sine tones and compared with the analog design it
// follows; flat settings must leave the signal alone.
#include "framework.h"
#include <chrono>
using namespace rigtest;

namespace {
// Gain (dB) of a module at f Hz with these knobs: a steady sine of amplitude amp, after 0.4 s to settle.
template <class M> float respDb(const std::vector<float>& knobs, float f, float amp = 0.01f) {
  M m; m.init(SR); float e[MAXPAR] = {0}; for (size_t k = 0; k < knobs.size(); k++) e[k] = knobs[k];
  float iL[128], iR[128], oL[128], oR[128]; double si = 0, so = 0; const int N = (int)(0.6f * SR);
  for (int p = 0; p < N; p += 128) {
    for (int i = 0; i < 128; i++) iL[i] = iR[i] = amp * sinf(2 * (float)M_PI * f * (float)((p + i) % (int)SR) / SR);
    m.process(iL, iR, oL, oR, 128, e, Context{});
    if (p >= (int)(0.4f * SR)) for (int i = 0; i < 128; i++) { si += (double)iL[i] * iL[i]; so += (double)oL[i] * oL[i]; }
  }
  return 10.f * (float)log10(so / si);
}
template <class M> std::vector<float> defaults() { M m; const ModuleDesc& d = m.desc(); std::vector<float> k(d.nparams); for (int i = 0; i < d.nparams; i++) k[i] = d.params[i].def; return k; }
// The knob position (0..1) for a real value of knob i.
template <class M> float pos(int i, float real) {
  M m; const ParamDesc& p = m.desc().params[i];
  return p.curve == Curve::Log ? logf(real / p.min) / logf(p.max / p.min) : (real - p.min) / (p.max - p.min);
}
// Largest difference between output and input over the riff (a flat EQ must leave it alone).
template <class M> float nullDiff(const std::vector<float>& knobs) {
  M m; m.init(SR); float e[MAXPAR] = {0}; for (size_t k = 0; k < knobs.size(); k++) e[k] = knobs[k];
  const auto& g = riffSignal(); float oL[128], oR[128], worst = 0;
  for (size_t p = 0; p + 128 <= g.size(); p += 128) { m.process(&g[p], &g[p], oL, oR, 128, e, Context{}); for (int i = 0; i < 128; i++) worst = fmaxf(worst, fabsf(oL[i] - g[p + i])); }
  return worst;
}
// Analog first-order split, as in Shelf1: gLo*lp + gHi*hp with the split at fc.
float split1Db(float f, float fc, float lo, float hi) { const std::complex<double> s(0, f / fc); return dbf(std::abs(((double)lo + (double)hi * s) / (1.0 + s))); }
float bellDb(float f, float f0, float q, float db) {                // analog bell (the Svf's prototype)
  const double A = pow(10.0, db / 40.0); const std::complex<double> s(0, f / f0);
  return dbf(std::abs((s * s + s * (A / q) + 1.0) / (s * s + s / (A * q) + 1.0)));
}
// ns per stereo sample frame for one instance at default knobs, on the riff.
template <class M> double cpuNs() {
  M m; m.init(SR); auto k = defaults<M>(); float e[MAXPAR] = {0}; for (size_t i = 0; i < k.size(); i++) e[i] = k[i];
  const auto& g = riffSignal(); float oL[128], oR[128]; double best = 1e9;
  for (int rep = 0; rep < 3; rep++) {
    const auto t0 = std::chrono::steady_clock::now();
    for (size_t p = 0; p + 128 <= g.size(); p += 128) m.process(&g[p], &g[p], oL, oR, 128, e, Context{});
    best = std::min(best, std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / (double)g.size());
  }
  return best;
}
} // namespace

TEST("100 tilt: flat leaves the riff alone; +6 dB tilts about the pivot, matching the analog shelf") {
  CHECK(nullDiff<Tilt>(defaults<Tilt>()) < 1e-6f, "default: largest change to the riff %.1e", nullDiff<Tilt>(defaults<Tilt>()));
  auto k = defaults<Tilt>(); k[0] = 1.f;                                 // +6 dB, pivot 800 Hz
  const float G = db2lin(6.f); float worst = 0; printf("      ");
  for (float f : {50.f, 200.f, 800.f, 3000.f, 8000.f}) { float d = respDb<Tilt>(k, f), a = split1Db(f, 800 * G, 1 / G, G); worst = fmaxf(worst, fabsf(d - a)); printf("%g Hz %+.2f dB  ", f, d); }
  printf("\n");
  CHECK(fabsf(respDb<Tilt>(k, 800)) < 0.1f && respDb<Tilt>(k, 50) < -5.f && respDb<Tilt>(k, 8000) > 5.f, "pivot stays at 0 dB, bottom down 5+ dB, top up 5+ dB");
  CHECK(worst < 0.1f, "within %.3f dB of the analog response, 50 Hz-8 kHz", worst);
}

TEST("101 bax: bass and treble shelves reach their gain, half of it at their frequency") {
  CHECK(nullDiff<Bax>(defaults<Bax>()) < 1e-6f, "default: largest change to the riff %.1e", nullDiff<Bax>(defaults<Bax>()));
  auto k = defaults<Bax>(); k[0] = 1.f; k[1] = 0.f;                       // bass +15 at 120 Hz, treble -15 at 3 kHz
  const float b20 = respDb<Bax>(k, 20), b120 = respDb<Bax>(k, 120), mid = respDb<Bax>(k, 600), t3k = respDb<Bax>(k, 3000), t16k = respDb<Bax>(k, 16000);
  printf("      20 Hz %+.1f, 120 Hz %+.1f, 600 Hz %+.1f, 3 kHz %+.1f, 16 kHz %+.1f dB\n", b20, b120, mid, t3k, t16k);
  CHECK(b20 > 13.f && fabsf(b120 - 7.5f) < 0.3f, "bass: %.1f dB at 20 Hz, %.2f at its 120 Hz (half of 15)", b20, b120);
  CHECK(fabsf(t3k + 7.5f) < 0.4f && t16k < -12.f, "treble: %.2f dB at its 3 kHz (half of -15), %.1f at 16 kHz", t3k, t16k);
  CHECK(fabsf(mid) < 2.5f, "the mids in between move less: %.1f dB at 600 Hz", mid);
}

TEST("102 geq: each band boosts its own octave; flat is transparent") {
  CHECK(nullDiff<Geq>(defaults<Geq>()) < 1e-5f, "default: largest change to the riff %.1e", nullDiff<Geq>(defaults<Geq>()));
  float worst = 0, leak = 0;
  for (int b = 0; b < 7; b++) {
    auto k = defaults<Geq>(); k[b] = pos<Geq>(b, 12.f);
    worst = fmaxf(worst, fabsf(respDb<Geq>(k, GEQ_BANDS[b]) - 12.f));
    if (b < 6) leak = fmaxf(leak, respDb<Geq>(k, GEQ_BANDS[b] * 4));    // two octaves away
  }
  CHECK(worst < 0.05f, "+12 dB on each band alone: within %.3f dB at its centre", worst);
  CHECK(leak < 1.5f, "two octaves away a +12 dB band adds at most %.2f dB", leak);
  auto k = defaults<Geq>(); for (int b = 0; b < 7; b++) k[b] = pos<Geq>(b, 6.f);
  float lo = 99, hi = -99; for (float f = 100; f <= 6400; f *= 1.189f) { float d = respDb<Geq>(k, f); lo = fminf(lo, d); hi = fmaxf(hi, d); }
  printf("      every band at +6 dB: %.1f to %.1f dB from 100 Hz to 6.4 kHz (bands add where they overlap)\n", lo, hi);
  CHECK(hi - lo < 3.f, "every band at +6: ripple %.2f dB across the bands", hi - lo);
}

TEST("103 peq: mids hit their gain at their frequency with their width; shelves reach theirs") {
  CHECK(nullDiff<Peq>(defaults<Peq>()) < 1e-5f, "default: largest change to the riff %.1e", nullDiff<Peq>(defaults<Peq>()));
  auto k = defaults<Peq>(); k[1] = pos<Peq>(1, 1000); k[2] = pos<Peq>(2, 9); k[3] = pos<Peq>(3, 2);   // MID 1: 1 kHz +9 dB Q 2
  float worst = 0; printf("      MID 1 at 1 kHz, +9 dB, Q 2:");
  for (float f : {250.f, 707.f, 1000.f, 1414.f, 4000.f}) { float d = respDb<Peq>(k, f); worst = fmaxf(worst, fabsf(d - bellDb(f, 1000, 2, 9))); printf(" %g Hz %+.2f", f, d); }
  printf("\n");
  CHECK(worst < 0.15f, "within %.3f dB of the analog bell, 250 Hz-4 kHz", worst);
  k = defaults<Peq>(); k[4] = pos<Peq>(4, 3000); k[5] = pos<Peq>(5, -12); k[6] = pos<Peq>(6, 8);
  const float notch = respDb<Peq>(k, 3000), side = respDb<Peq>(k, 2000);
  CHECK(fabsf(notch + 12.f) < 0.1f && side > -2.f, "MID 2 narrow cut: %.2f dB at 3 kHz, %.2f a fifth below", notch, side);
  k = defaults<Peq>(); k[0] = pos<Peq>(0, 12); k[7] = pos<Peq>(7, -12);
  const float l = respDb<Peq>(k, 25), h = respDb<Peq>(k, 18000), m = respDb<Peq>(k, 700);
  printf("      LOW +12, HIGH -12: 25 Hz %+.2f, 700 Hz %+.2f, 18 kHz %+.2f dB\n", l, m, h);
  CHECK(fabsf(l - 12) < 0.5f && h < -11.f && fabsf(m) < 1.f, "shelves reach their gain; the mids stay put");
}

TEST("104 stack: matches the circuit's analog response; the knobs interact like the amp's") {
  double worst = 0, top = 0;           // top: at 5 kHz, where the bilinear transform's warping shows
  for (int model = 0; model < 3; model++) for (float bass : {0.1f, 0.5f, 1.f}) for (float midk : {0.f, 0.5f, 1.f}) for (float tr : {0.f, 0.5f, 1.f}) {
    std::vector<float> k = {bass, midk, tr, model / 2.f, 0.5f};
    double B[4], A[4]; stackCoeffs(STACK_MODELS[model], stackBassTaper(bass), midk, tr, B, A);
    Stack s; s.init(SR); const double mk = s.makeup[model];
    for (float f : {60.f, 200.f, 700.f, 2000.f, 5000.f}) { const double d = fabs(respDb<Stack>(k, f) - (20 * log10(stackMag(B, A, f)) + mk)); if (f < 3000) worst = std::max(worst, d); else top = std::max(top, d); }
  }
  CHECK(worst < 0.1, "3 models x 27 knob settings x 60 Hz-2 kHz: within %.3f dB of the analog stack", worst);
  CHECK(top < 0.35, "...and within %.3f dB at 5 kHz (bilinear, as in Yeh & Smith; prewarping only trades it for error lower down)", top);
  auto k = defaults<Stack>();
  for (int model = 0; model < 3; model++) {
    k[3] = model / 2.f; double sum = 0; int n = 0;
    for (float f = 80; f <= 5000; f *= 1.2f) { sum += respDb<Stack>(k, f); n++; }
    printf("      %s: knobs at 5 average %+.2f dB over 80 Hz-5 kHz, 500 Hz %+.1f dB (the mid scoop)\n", model == 0 ? "Bassman" : model == 1 ? "JCM800 " : "Twin   ", sum / n, respDb<Stack>(k, 500));
  }
  k = defaults<Stack>(); k[0] = 0; const float b0 = respDb<Stack>(k, 80); k[0] = 1; const float b10 = respDb<Stack>(k, 80), mid10 = respDb<Stack>(k, 500);
  k[0] = 0; const float mid0 = respDb<Stack>(k, 500);
  CHECK(b10 - b0 > 10.f, "BASS 0 to 10: +%.1f dB at 80 Hz", b10 - b0);
  CHECK(fabsf(mid10 - mid0) > 0.5f, "...and it moves 500 Hz too (%.1f dB): the knobs interact", mid10 - mid0);
  k = defaults<Stack>(); k[1] = 0; const float m0 = respDb<Stack>(k, 500); k[1] = 1; const float m10 = respDb<Stack>(k, 500);
  CHECK(m10 - m0 > 6.f, "MID 0 to 10: +%.1f dB at 500 Hz", m10 - m0);
  k = defaults<Stack>(); k[3] = 0; const float a = respDb<Stack>(k, 300); k[3] = 0.25f; const float mid = respDb<Stack>(k, 300); k[3] = 0.5f; const float b = respDb<Stack>(k, 300);
  CHECK(mid > fminf(a, b) - 0.1f && mid < fmaxf(a, b) + 0.1f, "MODEL halfway between Bassman and JCM800 lands between them at 300 Hz (%.1f, %.1f, %.1f dB)", a, mid, b);
}

TEST("105 prog: the boost-and-cut trick, the high bell, and the tube's second harmonic") {
  auto k = defaults<Prog>(); k[7] = 0;                                    // drive off
  CHECK(fabsf(respDb<Prog>(k, 100)) < 0.01f && fabsf(respDb<Prog>(k, 5000)) < 0.01f, "flat with DRIVE 0: %.3f dB at 100 Hz", respDb<Prog>(k, 100));
  k[1] = 1.f; const float boost20 = respDb<Prog>(k, 20);
  k[2] = 1.f; const float both25 = respDb<Prog>(k, 25), both120 = respDb<Prog>(k, 120), both1k = respDb<Prog>(k, 1000);
  printf("      60 Hz, BOOST 10: %+.1f dB at 20 Hz; with ATTEN 10 too: %+.1f at 25 Hz, %+.1f at 120 Hz, %+.1f at 1 kHz\n", boost20, both25, both120, both1k);
  CHECK(boost20 > 13.f, "LOW BOOST 10 alone: %+.1f dB at 20 Hz", boost20);
  CHECK(both25 > 3.f && both120 < -5.f && fabsf(both1k) < 1.f, "both at 10: low end up, a dip above it, mids left alone");
  k = defaults<Prog>(); k[7] = 0; k[4] = 1.f;                             // HIGH BOOST 10 at 8 kHz
  const float hb = respDb<Prog>(k, 8000); k[5] = 0; const float sharp = respDb<Prog>(k, 5000); k[5] = 1; const float broad = respDb<Prog>(k, 5000);
  CHECK(fabsf(hb - 16.f) < 0.2f && broad > sharp + 3.f, "HIGH BOOST 10: %+.2f dB at 8 kHz; at 5 kHz %+.1f (sharp) to %+.1f (broad)", hb, sharp, broad);
  k = defaults<Prog>(); k[6] = 1.f; k[7] = 0;
  CHECK(respDb<Prog>(k, 16000) < -12.f, "HIGH ATTEN 10: %+.1f dB at 16 kHz", respDb<Prog>(k, 16000));
  // Harmonics of a 220 Hz sine at 0.5 through DRIVE 100% and 20% (the default).
  for (float d : {1.f, 0.2f}) {
    Prog m; m.init(SR); float e[MAXPAR] = {0}; auto kk = defaults<Prog>(); for (int i = 0; i < 8; i++) e[i] = kk[i]; e[7] = d;
    std::vector<float> y; float iL[128], oL[128], oR[128];
    for (int p = 0; p < (int)SR; p += 128) { for (int i = 0; i < 128; i++) iL[i] = 0.5f * sinf(2 * (float)M_PI * 220 * (p + i) / SR); m.process(iL, iL, oL, oR, 128, e, Context{}); if (p >= (int)(SR / 2)) y.insert(y.end(), oL, oL + 128); }
    auto h = [&](int n) { double re = 0, im = 0; for (size_t i = 0; i < y.size(); i++) { re += y[i] * cos(2 * M_PI * 220 * n * i / SR); im += y[i] * sin(2 * M_PI * 220 * n * i / SR); } return 2 * sqrt(re * re + im * im) / y.size(); };
    const double h1 = h(1), h2 = h(2), h3 = h(3);
    printf("      DRIVE %3.0f%%: 2nd harmonic %.1f dB, 3rd %.1f dB below the fundamental\n", d * 100, dbf(h1 / h2), dbf(h1 / h3));
    if (d == 1.f) CHECK(h2 > h3 && dbf(h1 / h2) < 30.f, "DRIVE 100%%: mostly second harmonic (%.1f dB down) over third (%.1f)", dbf(h1 / h2), dbf(h1 / h3));
    else CHECK(dbf(h1 / h2) > 30.f, "DRIVE 20%% stays clean-ish: second harmonic %.1f dB down", dbf(h1 / h2));
  }
}

TEST("106 dyneq: leaves quiet playing alone, cuts the band when it's loud, and only that band") {
  auto k = defaults<Dyneq>();                                             // 3 kHz, -24 dB, up to -6 dB, 4:1
  const float quiet = respDb<Dyneq>(k, 3000, 0.01f), loud = respDb<Dyneq>(k, 3000, 0.5f), other = respDb<Dyneq>(k, 300, 0.5f);
  printf("      3 kHz at -40 dBFS: %+.2f dB; at -6 dBFS: %+.2f dB; 300 Hz at -6 dBFS: %+.2f dB\n", quiet, loud, other);
  CHECK(fabsf(quiet) < 0.05f, "below the threshold: untouched (%+.3f dB)", quiet);
  CHECK(fabsf(loud + 6.f) < 0.5f, "well above: cut by RANGE (%+.2f dB, want -6)", loud);
  CHECK(fabsf(other) < 0.6f, "a loud tone elsewhere isn't cut (%+.2f dB at 300 Hz)", other);
  k[3] = pos<Dyneq>(3, 6.f); const float up = respDb<Dyneq>(k, 3000, 0.5f);
  CHECK(fabsf(up - 6.f) < 0.5f, "RANGE +6: lifts the band when loud (%+.2f dB)", up);
  // Attack: a 3 kHz burst from silence; how long until the cut is 90% there.
  Dyneq m; m.init(SR); float e[MAXPAR] = {0}; auto d = defaults<Dyneq>(); for (int i = 0; i < 7; i++) e[i] = d[i];
  float iL[16], oL[16], oR[16], mt[2]; int t90 = -1;
  for (int p = 0; p < (int)(0.2f * SR) && t90 < 0; p += 16) {
    for (int i = 0; i < 16; i++) iL[i] = 0.5f * sinf(2 * (float)M_PI * 3000 * (p + i) / SR);
    m.process(iL, iL, oL, oR, 16, e, Context{}); m.meters(mt); if (mt[0] < -5.4f) t90 = p;
  }
  CHECK(t90 > 0 && t90 < 0.03f * SR, "ATTACK 5 ms: 90%% of the cut in %.1f ms", t90 * 1000.f / SR);
}

TEST("107 eqs in the chain: all seven added live, the riff stays finite and near its level; CPU") {
  Engine e; e.init(SR); int slots[7], n = 0;
  for (const char* t : {"tilt", "bax", "geq", "peq", "stack", "prog", "dyneq"}) slots[n++] = e.addModule(t, nullptr, -1, false);
  bool ok = true; for (int s : slots) ok = ok && s >= 0;
  CHECK(ok, "added: tilt, bax, geq, peq, stack, prog, dyneq");
  for (int m = 0; m < MAXMOD; m++) { bool eq = false; for (int s : slots) eq = eq || s == m; e.byp[m] = !eq; e.wet[m] = eq ? 1.f : 0.f; }
  e.masterDb = 0; e.masterS.y = 1;
  const auto& g = riffSignal(); auto r = render(e, g);
  bool fin = true; for (float v : r.L) fin = fin && std::isfinite(v);
  const float lv = dbf(rms(r.L, 0, r.L.size()) / rms(g, 0, g.size()));
  CHECK(fin && fabsf(lv) < 6.f, "the riff through all seven at their defaults: %+.1f dB", lv);
  printf("      CPU, ns per stereo frame (48 kHz frame = 20833 ns): tilt %.1f, bax %.1f, geq %.1f, peq %.1f, stack %.1f, prog %.1f, dyneq %.1f\n",
         cpuNs<Tilt>(), cpuNs<Bax>(), cpuNs<Geq>(), cpuNs<Peq>(), cpuNs<Stack>(), cpuNs<Prog>(), cpuNs<Dyneq>());
}

TEST("108 eqs: knobs swept fast while a note rings don't click; PROGRAM EQ's aliasing at full drive") {
  // MID 1 at +12 dB swept 200 Hz-4 kHz four times a second (as a modulation source would) under a
  // 440 Hz sine. A click is a jump between samples bigger than the waveform's own steepest slope.
  auto run = [](auto& m, int knob, float lo, float hi, float* e) {
    float iL[16], oL[16], oR[16], prev = 0, worst = 0, slope = 0;
    for (int p = 0; p < (int)(2 * SR); p += 16) {
      e[knob] = lo + (hi - lo) * 0.5f * (1 + sinf(2 * (float)M_PI * 4 * p / SR));
      for (int i = 0; i < 16; i++) iL[i] = 0.3f * sinf(2 * (float)M_PI * 440 * (p + i) / SR);
      m.process(iL, iL, oL, oR, 16, e, Context{});
      for (int i = 0; i < 16; i++) { if (p > 4800) worst = fmaxf(worst, fabsf(oL[i] - prev)); prev = oL[i]; }
    }
    slope = 0.3f * 2 * (float)M_PI * 440 / SR * db2lin(15.f);         // the steepest a 440 Hz sine boosted 15 dB can be
    return worst / slope;
  };
  { Peq m; m.init(SR); float e[MAXPAR]; for (int i = 0; i < 8; i++) e[i] = PEQ_PARAMS[i].def; e[2] = pos<Peq>(2, 12); e[3] = pos<Peq>(3, 4);
    const float r = run(m, 1, pos<Peq>(1, 200), pos<Peq>(1, 4000), e);
    CHECK(r < 1.f, "PARAMETRIC MID 1 swept 200 Hz-4 kHz at 4 Hz: largest step %.2f of a clean sine's", r); }
  { Stack m; m.init(SR); float e[MAXPAR]; for (int i = 0; i < 5; i++) e[i] = STACK_PARAMS[i].def;
    const float r = run(m, 3, 0.f, 1.f, e);
    CHECK(r < 1.f, "TONE STACK MODEL swept Bassman-Twin at 4 Hz: largest step %.2f of a clean sine's", r); }
  { Geq m; m.init(SR); float e[MAXPAR]; for (int i = 0; i < 8; i++) e[i] = 0.5f;
    const float r = run(m, 2, 0.f, 1.f, e);
    CHECK(r < 1.f, "GRAPHIC 400 Hz slider swept -15 to +15 dB at 4 Hz: largest step %.2f of a clean sine's", r); }
  // Aliasing: a 2.5 kHz sine at 0.5 through DRIVE 100%; energy that isn't at a harmonic, below the harmonics'.
  Prog m; m.init(SR); float e[MAXPAR]; for (int i = 0; i < 8; i++) e[i] = PROG_PARAMS[i].def; e[7] = 1.f;
  const int N = 1 << 15; std::vector<float> y(N); float iL[128], oL[128], oR[128];
  for (int p = 0; p < N + 4800; p += 128) { for (int i = 0; i < 128; i++) iL[i] = 0.5f * sinf(2 * (float)M_PI * 2500 * (p + i) / SR); m.process(iL, iL, oL, oR, 128, e, Context{}); for (int i = 0; i < 128; i++) if (p + i >= 4800 && p + i - 4800 < N) y[p + i - 4800] = oL[i]; }
  double harm = 0, other = 0;
  for (int b = 1; b < N / 2; b++) {
    const double f = b * SR / N; double re = 0, im = 0;
    if (b % 4) continue;                                                  // every 4th bin is plenty, and quick
    for (int i = 0; i < N; i++) { const double w = 0.5 - 0.5 * cos(2 * M_PI * i / N), a = 2 * M_PI * b * i / N; re += w * y[i] * cos(a); im += w * y[i] * sin(a); }
    const double pw = re * re + im * im, nearest = fabs(f / 2500 - round(f / 2500)) * 2500;
    if (nearest < 60) harm += pw; else other += pw;
  }
  const float ratio = dbf(sqrt(harm / other));
  printf("      2.5 kHz at DRIVE 100%%: non-harmonic (aliased) energy %.1f dB below the harmonics (amp's shipped setting: 55.4)\n", ratio);
  CHECK(ratio > 40.f, "PROGRAM EQ DRIVE 100%%: aliasing %.1f dB down", ratio);
}

// test_tuner.cpp - the strobe tuner (engine/core/tuner.h): accuracy, speed, the strobe bands, mute.
#include "framework.h"
#include <chrono>
using namespace rigtest;

// A note with a few harmonics, like a sustained string, at `cents` from the note `midi`.
static std::vector<float> tone(float midi, float cents, float secs, float a4 = 440.f, float amp = 0.3f) {
  const double f = a4 * pow(2.0, (midi - 69 + cents / 100.0) / 12.0);
  std::vector<float> s((size_t)(secs * SR));
  for (size_t i = 0; i < s.size(); i++) { double t = i / (double)SR; s[i] = (float)(amp * (sin(2 * M_PI * f * t) + .5 * sin(4 * M_PI * f * t + .4) + .25 * sin(6 * M_PI * f * t + 1.1) + .15 * sin(8 * M_PI * f * t + 2.3))); }
  return s;
}
struct Reading { int note; float cents, freq; std::vector<float> ph[TUN_BANDS]; int firstBlock; };
static Reading run(Engine& e, const std::vector<float>& in) {
  Reading r{-1, 0, 0, {}, -1};
  for (size_t i = 0, b = 0; i + 128 <= in.size(); i += 128, b++) {
    memcpy(e.inBuf, &in[i], 512); e.process(128);
    const float* m = &e.meters[M_TUN];
    if (m[T_NOTE] >= 0 && r.firstBlock < 0) r.firstBlock = (int)b;
    r.note = (int)m[T_NOTE]; r.cents = m[T_CENTS]; r.freq = m[T_FREQ];
    for (int k = 0; k < TUN_BANDS; k++) r.ph[k].push_back(m[T_PH0 + k]);
  }
  return r;
}
static const char* NAMES[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

TEST("70 tuner: every note from low B (7-string) to C7, ±0.1 cent") {
  // every semitone, each at a different tuning error between -45 and +45 cents
  float worst = 0; int wrong = 0, count = 0;
  printf("      ");
  for (int midi = 35; midi <= 96; midi++) {
    const float c = -45.f + fmodf(midi * 37.f, 90.f);
    Engine x; x.init(SR); x.tuner.on = true;
    auto r = run(x, tone((float)midi, c, 1.f));
    count++; if (r.note != midi) wrong++; else worst = fmaxf(worst, fabsf(r.cents - c));
    if (midi % 7 == 0) printf("%s%d %+.2f (%+.2f)  ", NAMES[midi % 12], midi / 12 - 1, r.cents, c);
  }
  printf("\n");
  CHECK(wrong == 0 && worst < 0.1f, "%d notes B1..C7: %d read as the wrong note, worst cents error %.3f", count, wrong, worst);
}

TEST("71 tuner: speed and a plucked string") {
  Engine e; e.init(SR); e.tuner.on = true;
  auto r = run(e, tone(40, 0, 0.5f));
  float ms = (r.firstBlock + 1) * 128 / SR * 1000;
  CHECK(r.firstBlock >= 0 && ms < 70, "low E (82 Hz) read after %.0f ms", ms);
  std::vector<Note> nn = {{0.05f, 110.f, 0.8f, false}};                  // Karplus-Strong A string
  auto g = guitar(1.5f, nn);
  Engine p; p.init(SR); p.tuner.on = true;
  auto q = run(p, g);
  const float expect = 1200.f * log2f(SR / ((int)(SR / 110.f) - 0.5f) / 110.f);   // the model's own pitch: its loop is (whole samples - 0.5) long
  CHECK(q.note == 45 && fabsf(q.cents - expect) < 3.f, "plucked A: note %s%d, %+.1f cents (the model plays %+.1f)", q.note >= 0 ? NAMES[q.note % 12] : "-", q.note / 12 - 1, q.cents, expect);
  for (float f : {329.63f, 659.26f, 987.77f}) {                       // plucked E4, E5, B5 (the high detector's range)
    std::vector<Note> hn = {{0.05f, f, 0.8f, false}}; auto hg = guitar(1.f, hn);
    Engine h; h.init(SR); h.tuner.on = true; auto hr = run(h, hg);
    const int want = (int)lrintf(69 + 12 * log2f(f / 440.f));
    const float model = 1200.f * log2f(SR / ((int)(SR / f) - 0.5f) / f);
    CHECK(hr.note == want && fabsf(hr.cents - model) < 3.f, "plucked %s%d: %+.1f cents (the model plays %+.1f)", NAMES[want % 12], want / 12 - 1, hr.cents, model);
  }
  {                                                                     // a held E5 bent up 30 cents over a second, then held
    std::vector<float> b((size_t)(2.5f * SR)); double ph = 0;
    for (size_t i = 0; i < b.size(); i++) { const double t = i / (double)SR, c = t < 0.5 ? 0 : t < 1.5 ? 30 * (t - 0.5) : 30;
      ph += 2 * M_PI * 659.255 * pow(2.0, c / 1200) / SR; b[i] = (float)(0.3 * (sin(ph) + .5 * sin(2 * ph + .4))); }
    Engine h; h.init(SR); h.tuner.on = true; auto br = run(h, b);
    CHECK(br.note == 76 && fabsf(br.cents - 30.f) < 0.1f, "a held E5 bent 30 cents: reads %+.2f cents after the bend", br.cents);
  }
  Engine s; s.init(SR); s.tuner.on = true;
  auto z = run(s, std::vector<float>((size_t)SR, 0.f));
  CHECK(z.note == -1 && z.freq == 0, "silence: no note");
}

TEST("72 tuner: strobe bands turn at the tuning error, and stand still in tune") {
  for (float cents : {0.f, 2.f, -5.f}) {
    Engine e; e.init(SR); e.tuner.on = true;
    auto r = run(e, tone(45, cents, 3.f));                              // A2, 110 Hz
    printf("      %+.0f cents:", cents);
    bool ok = true;
    for (int b = 0; b < TUN_BANDS; b++) {
      // unwrap the band's phase over the last 2 s and fit its rate
      const auto& p = r.ph[b]; size_t a = p.size() - (size_t)(2 * SR / 128); double acc = 0, prev = p[a], first = 0; std::vector<double> u;
      for (size_t i = a; i < p.size(); i++) { double d = p[i] - prev; if (d > M_PI) d -= 2 * M_PI; if (d < -M_PI) d += 2 * M_PI; acc += d; prev = p[i]; u.push_back(acc); }
      (void)first;
      const double rate = (u.back() - u.front()) / ((u.size() - 1) * 128 / SR) / (2 * M_PI);   // turns per second
      const double want = TUN_HARM[b] * 110.0 * (pow(2.0, cents / 1200.0) - 1);
      printf("  x%d %.3f Hz (expect %.3f)", TUN_HARM[b], rate, want);
      ok = ok && fabs(rate - want) < 0.02 + 0.03 * fabs(want);
    }
    printf("\n");
    CHECK(ok, "%+.0f cents: every band turns at harmonic x tuning error", cents);
  }
}

TEST("73 tuner: mutes the output while on (if asked), and costs nothing when off") {
  auto s = tone(57, 0, 1.f);
  auto outRms = [&](bool on, bool mute) { Engine e; e.init(SR); e.tuner.on = on; e.tuner.mute = mute; e.masterDb = 0; e.masterS.y = 1; auto r = render(e, s); return rms(r.L, (size_t)(0.5f * SR), s.size()); };
  double off = outRms(false, true), muted = outRms(true, true), open = outRms(true, false);
  CHECK(muted < 1e-6 && fabs(open - off) < 1e-9, "output RMS: tuner off %.4f, on + mute %.1e, on without mute %.4f", off, muted, open);
  Engine a; a.init(SR); Engine b; b.init(SR); b.tuner.on = true; b.tuner.on = false;
  auto ra = render(a, riffSignal()), rb = render(b, riffSignal());
  CHECK(ra.L == rb.L, "switched off, the output is bit-identical to never having had a tuner");
  std::vector<float> riff60; while (riff60.size() < 30 * SR) riff60.insert(riff60.end(), riffSignal().begin(), riffSignal().end());
  auto cpu = [&](bool on) { Engine e; e.init(SR); e.tuner.on = on; auto t0 = std::chrono::steady_clock::now(); render(e, riff60); return 100 * std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / (riff60.size() / SR); };
  double c0 = cpu(false), c1 = cpu(true);
  printf("      whole engine: %.2f%% of one core with the tuner off, %.2f%% on\n", c0, c1);
  CHECK(c1 - c0 < 3.0, "the tuner adds %.2f%% of one core", c1 - c0);
  Engine r; r.init(SR); r.tuner.on = true; r.tuner.a4 = 432.f;
  auto q = run(r, tone(69, 0, 1.f, 432.f));
  CHECK(q.note == 69 && fabsf(q.cents) < 0.1f, "A4 = 432 Hz reference: a 432 Hz A reads %+.2f cents", q.cents);
}

TEST("74 tuner: re-picking a held note doesn't throw the reading off") {
  // the same note picked again every 0.7 s: each new pick makes the string's phase jump (90 degrees here)
  float worstAll = 0; printf("      ");
  for (int midi : {40, 45, 57, 64, 76}) {
    const float cents = -2.f; const double f = 440 * pow(2.0, (midi - 69 + cents / 100.0) / 12.0); double ph = 0;
    std::vector<float> s((size_t)(6 * SR));
    for (size_t i = 0; i < s.size(); i++) { if (i % 33600 == 0 && i) ph += M_PI / 2; ph += 2 * M_PI * f / SR; s[i] = (float)(0.3 * (sin(ph) + .5 * sin(2 * ph + .4) + .25 * sin(3 * ph + 1.1))); }
    Engine e; e.init(SR); e.tuner.on = true; float worst = 0;
    for (size_t i = 0, b = 0; i + 128 <= s.size(); i += 128, b++) { memcpy(e.inBuf, &s[i], 512); e.process(128); if (i > SR) worst = fmaxf(worst, fabsf(e.meters[M_TUN + T_CENTS] - cents)); }
    printf("%s%d %.2f  ", NAMES[midi % 12], midi / 12 - 1, worst); worstAll = fmaxf(worstAll, worst);
  }
  printf(" (worst cents error over 5 s with a new pick every 0.7 s)\n");
  CHECK(worstAll < 0.5f, "worst %.2f cents through every re-pick (was up to 5 cents before the hold)", worstAll);
}

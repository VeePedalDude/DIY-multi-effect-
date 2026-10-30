// test_polyv.cpp - POLY VOICE: finding the notes in chords, the voices it plays, and its knobs, with numbers.
#include "framework.h"
#include <chrono>
#include <memory>
using namespace rigtest;

namespace {
float nrm(int i, float real) { const ParamDesc& q = POLYV_PARAMS[i]; return q.curve == Curve::Log ? logf(real / q.min) / logf(q.max / q.min) : (real - q.min) / (q.max - q.min); }
struct PV {
  std::unique_ptr<PolyVoice> m{new PolyVoice}; float e[MAXPAR];
  PV() { m->init(SR); for (int i = 0; i < 8; i++) e[i] = POLYV_PARAMS[i].def; set(PV_DRY, 0); }
  PV& set(int i, float real) { e[i] = nrm(i, real); return *this; }
  // play `in`; return the wet+dry output (left), and call `each` after every block
  std::vector<float> play(const std::vector<float>& in, const std::function<void(size_t)>& each = nullptr) {
    std::vector<float> out(in.size()), r(128); Context c; c.sr = SR;
    for (size_t i = 0; i + 128 <= in.size(); i += 128) { m->process(&in[i], &in[i], &out[i], r.data(), 128, e, c); if (each) each(i); }
    return out;
  }
  std::vector<float> notesNow() const { std::vector<float> v; for (int k = 0; k < PolyAlloc::MAXV; k++) if (m->alloc.v[k].on) v.push_back(m->alloc.v[k].hz); std::sort(v.begin(), v.end()); return v; }
};
// notes as tones with six harmonics falling off as 1/h (sines alone have nothing to tell a note from a stray partial)
std::vector<float> sines(std::initializer_list<float> hz, float secs, float amp = 0.15f) {
  std::vector<float> v((size_t)(secs * SR), 0.f);
  for (float f : hz) for (int h = 1; h <= 6; h++) if (f * h < 4000.f) for (size_t i = 0; i < v.size(); i++) v[i] += amp * 0.6f / h * sinf(2 * (float)M_PI * f * h * i / SR + h);
  return v;
}
// a plucked string: Karplus-Strong started from the string's shape under the pick (a triangle peaking 12-22%
// along it, so the harmonics fall off smoothly as a real string's do), tuned with an allpass, a touch of noise.
// (framework.h's guitar() starts from pure noise: every harmonic gets a random level, the fundamental up to
// 20 dB under its neighbours, which a real string doesn't do.)
std::vector<float> pluck(float secs, const std::vector<std::pair<float, float>>& nts, unsigned seed = 1) {
  std::vector<float> out((size_t)(secs * SR), 0.f); unsigned r = seed * 7919u;
  auto rnd = [&] { r = r * 1664525u + 1013904223u; return (r >> 8) / 16777216.f; };
  for (auto& nt : nts) {
    const double P = SR / nt.second - 0.5; const int L = (int)P; const float fr = (float)(P - L), c = (1 - fr) / (1 + fr), pp = 0.12f + 0.1f * rnd();
    std::vector<float> line(L + 1); float m = 0;
    for (int i = 0; i <= L; i++) { const float x = (float)i / (L + 1); line[i] = (x < pp ? x / pp : (1 - x) / (1 - pp)) + 0.05f * (2 * rnd() - 1); m += line[i]; }
    for (auto& v : line) v -= m / line.size();
    float ap = 0; int idx = 0;
    for (size_t i = (size_t)(nt.first * SR); i < out.size(); i++) {
      const float a = line[idx], b = line[(idx + 1) % (L + 1)], y = 0.4985f * (a + b), o = c * y + ap; ap = y - c * o;
      line[idx] = o; idx = (idx + 1) % (L + 1); out[i] += 0.1f * a;
    }
  }
  return out;
}
float noteOf(float hz) { return 69.f + 12.f * log2f(hz / 440.f); }
const char* NAMES[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
std::string name(float hz) { const int n = (int)lrintf(noteOf(hz)); return std::string(NAMES[n % 12]) + std::to_string(n / 12 - 1); }
std::string names(const std::vector<float>& v) { std::string s; for (float f : v) s += (s.empty() ? "" : " ") + name(f); return s.empty() ? "-" : s; }
// strongest frequency in out[a..b) between lo and hi Hz (a plain DFT scan, 1 Hz steps, refined to 0.05 Hz)
float peakHz(const std::vector<float>& x, size_t a, size_t b, float lo, float hi) {
  auto pw = [&](float f) { double re = 0, im = 0; for (size_t i = a; i < b; i++) { const double w = 0.5 - 0.5 * cos(2 * M_PI * (i - a) / (b - a)); re += w * x[i] * cos(2 * M_PI * f * i / SR); im += w * x[i] * sin(2 * M_PI * f * i / SR); } return re * re + im * im; };
  float best = lo; double bp = -1; for (float f = lo; f <= hi; f += 1.f) { const double p = pw(f); if (p > bp) { bp = p; best = f; } }
  for (float f = best - 1; f <= best + 1; f += 0.05f) { const double p = pw(f); if (p > bp) { bp = p; best = f; } }
  return best;
}
}

TEST("160 polyv: chords of steady tones: every note found, to a few cents") {
  struct C { const char* what; std::vector<float> hz; } cs[] = {{"A3", {220.f}}, {"C major (C4 E4 G4)", {261.63f, 329.63f, 392.f}}, {"E minor 7 (E3 G3 B3 D4)", {164.81f, 196.f, 246.94f, 293.66f}},
    {"open fifth E2 B2", {82.41f, 123.47f}}, {"wide: A2 E4 C#5", {110.f, 329.63f, 554.37f}}};
  for (auto& c : cs) {
    PV p; p.set(PV_MODEL, 3); std::vector<float> in((size_t)(0.5f * SR), 0.f);
    for (float f : c.hz) { auto t = sines({f}, 0.5f, 0.12f); for (size_t i = 0; i < in.size(); i++) in[i] += t[i]; }
    p.play(in); auto got = p.notesNow(); float worst = 0; bool all = got.size() == c.hz.size();
    for (size_t i = 0; all && i < got.size(); i++) worst = fmaxf(worst, fabsf(1200.f * log2f(got[i] / c.hz[i])));
    CHECK(all && worst < 8.f, "%-24s -> %s (worst %.1f cents)", c.what, names(got).c_str(), worst);
  }
}

TEST("161 polyv: plucked strings and the test riff") {
  // single plucked notes across the neck, three plucks each (pick positions vary)
  int right = 0, total = 0; std::string wrong;
  for (float f : {82.41f, 110.f, 146.83f, 196.f, 246.94f, 329.63f, 440.f, 659.26f, 987.77f}) for (unsigned seed = 1; seed <= 3; seed++) {
    auto in = pluck(0.6f, {{0.05f, f}}, seed); PV p; p.set(PV_MODEL, 0); p.play(in); auto got = p.notesNow(); total++;
    if (got.size() == 1 && fabsf(noteOf(got[0]) - noteOf(f)) < 0.5f) right++; else wrong += " " + name(f) + "->" + names(got);
  }
  CHECK(right == total, "single plucks E2 to B5: %d of %d found as that one note%s", right, total, wrong.c_str());
  // chords, strummed low to high 12 ms apart, three strums each
  struct C { const char* what; std::vector<float> hz; } cs[] = {{"E major", {82.41f, 123.47f, 164.81f, 207.65f, 246.94f, 329.63f}}, {"A minor", {110.f, 164.81f, 220.f, 261.63f, 329.63f}},
    {"G major", {98.f, 123.47f, 146.83f, 196.f, 246.94f, 392.f}}, {"C major", {130.81f, 164.81f, 196.f, 261.63f, 329.63f}}, {"D major", {146.83f, 220.f, 293.66f, 369.99f}},
    {"E5 power chord", {82.41f, 123.47f, 164.81f}}, {"A7 (A2 E3 G3 C#4 E4)", {110.f, 164.81f, 196.f, 277.18f, 329.63f}}};
  int strums = 0, strumsRight = 0;
  for (auto& c : cs) {
    int good = 0; std::string got3;
    for (unsigned seed = 1; seed <= 3; seed++) {
      std::vector<std::pair<float, float>> nts; float t = 0.05f; for (float f : c.hz) { nts.push_back({t, f}); t += 0.012f; }
      PV p; p.set(PV_MODEL, 0); p.play(pluck(0.8f, nts, seed)); auto got = p.notesNow();
      // right: every note it plays is in the chord, and every chord note is played or an octave above one it plays
      std::vector<int> want, have; for (float f : c.hz) want.push_back((int)lrintf(noteOf(f))); for (float g : got) have.push_back((int)lrintf(noteOf(g)));
      bool ok = !have.empty(); for (int h : have) ok = ok && std::find(want.begin(), want.end(), h) != want.end();
      for (int w : want) { bool f = false; for (int h : have) f = f || h == w || (w > h && (w - h) % 12 == 0); ok = ok && f; }
      good += ok; strums++; strumsRight += ok; if (!ok || seed == 1) got3 = names(got);
    }
    CHECK(good >= 2, "%-22s %d of 3 strums right; plays %s", c.what, good, got3.c_str());
  }
  CHECK(strumsRight >= strums - 2, "chords: %d of %d strums right (a note can hide in a lower string's harmonics)", strumsRight, strums);
  // the noise-started test guitar (random harmonic levels): reported, the lowest note checked
  int low = 0; std::string lw;
  for (float f : {82.41f, 110.f, 146.83f, 196.f, 246.94f, 329.63f, 440.f, 659.26f}) {
    std::vector<Note> n = {{0.05f, f, 1.f, false}}; auto in = guitar(0.6f, n); PV p; p.set(PV_MODEL, 0); p.play(in); auto got = p.notesNow();
    low += !got.empty() && fabsf(noteOf(got[0]) - noteOf(f)) < 0.5f; lw += " " + name(f) + "->" + names(got);
  }
  CHECK(low >= 6, "noise-started plucks: the lowest voice is the note for %d of 8:%s", low, lw.c_str());
  // the riff: voices start within ms of each pick
  PV r; r.set(PV_MODEL, 3); const auto& riffIn = riffSignal(); auto nts = riff(); std::vector<double> starts; unsigned last = 0;
  r.play(riffIn, [&](size_t i) { if (r.m->alloc.seq != last) { last = r.m->alloc.seq; starts.push_back((i + 128) / (double)SR); } });
  double worst = 0, sum = 0; int counted = 0; float prev = -1;
  for (auto& nt : nts) { if (nt.t - prev < 0.05f) continue; prev = nt.t; double best = 1e9; for (double s : starts) if (s >= nt.t && s - nt.t < best) best = s - nt.t; if (best < 0.2) { worst = fmax(worst, best); sum += best; counted++; } }
  CHECK(counted >= 20 && sum / counted < 0.04 && worst < 0.1, "the riff: a voice starts or restarts for %d of its pick events, %.0f ms after the pick on average (worst %.0f ms: a palm mute over the same note still ringing)", counted, 1000 * sum / counted, 1000 * worst);
  // what share of the voices' sound is on notes the riff doesn't have (E, A, B, D and octaves)
  auto stray = [](const std::vector<float>& sig) { PV q; q.set(PV_MODEL, 3); double in = 0, out = 0;
    q.play(sig, [&](size_t) { for (int k = 0; k < 8; k++) { const auto& a = q.m->alloc.v[k]; if (!a.on) continue; const int pc = (int)lrintf(noteOf(a.hz)) % 12;
      (pc == 4 || pc == 11 || pc == 9 || pc == 2 ? in : out) += q.m->vs[k].env; } });
    return 100 * out / (in + out); };
  std::vector<std::pair<float, float>> rn; for (auto& x : riff()) rn.push_back({x.t, x.f});
  const double sp = stray(pluck(14.f, rn, 3)), sn = stray(riffSignal());
  CHECK(sp < 2 && sn < 15, "voice sound on notes not in the riff: %.1f%% played on plucked strings; %.1f%% on the noise-started test guitar (its random harmonic levels stick out as notes)", sp, sn);
}

TEST("162 polyv: the voices sound at the notes; OCTAVE; quiet input stays quiet") {
  PV p; p.set(PV_MODEL, 3).set(PV_ATTACK, 5); auto out = p.play(sines({220.f}, 1.0f));
  const float f0 = peakHz(out, (size_t)(0.5 * SR), (size_t)(0.9 * SR), 150, 300);
  PV u; u.set(PV_MODEL, 3).set(PV_OCTAVE, 1); auto o2 = u.play(sines({220.f}, 1.0f)); const float fu = peakHz(o2, (size_t)(0.5 * SR), (size_t)(0.9 * SR), 300, 600);
  PV d; d.set(PV_MODEL, 1).set(PV_OCTAVE, -1); auto o3 = d.play(sines({220.f}, 1.0f)); const float fd = peakHz(o3, (size_t)(0.5 * SR), (size_t)(0.9 * SR), 80, 150);
  CHECK(fabsf(1200 * log2f(f0 / 220)) < 3 && fabsf(1200 * log2f(fu / 440)) < 3 && fabsf(1200 * log2f(fd / 110)) < 10, "A3 in: SYNTH SAW plays %.2f Hz; +1 OCT %.2f Hz; TAPE FLUTE -1 OCT %.2f Hz", f0, fu, fd);
  PV q; std::vector<float> hiss((size_t)(2 * SR)); unsigned r = 1; for (auto& x : hiss) { r = r * 1664525u + 1013904223u; x = 0.0002f * ((r >> 8) / 8388608.f - 1.f); }
  int maxV = 0; q.play(hiss, [&](size_t) { maxV = std::max(maxV, (int)q.notesNow().size()); });
  PV z; auto zo = z.play(std::vector<float>((size_t)SR, 0.f)); float zp = peak(zo);
  CHECK(maxV == 0 && zp == 0.f, "-74 dBFS hiss: %d voices; silence in, silence out (peak %.0g)", maxV, zp);
}

TEST("163 polyv: tape voices: snapped to keys, wow and flutter, the tape runs out") {
  PV a; a.set(PV_MODEL, 0).set(PV_MOD, 0); a.play(sines({225.f}, 0.5f)); auto g = a.notesNow();
  CHECK(g.size() == 1 && fabsf(g[0] - 220.f) < 0.01f, "225 Hz in (39 cents sharp of A3): TAPE STRINGS plays the key, %.2f Hz", g.empty() ? 0.f : g[0]);
  auto wobble = [](float mod) { PV p; p.set(PV_MODEL, 1).set(PV_MOD, mod); auto o = p.play(sines({220.f}, 4.f)); float lo = 1e9, hi = 0;
    for (double t = 1.0; t + 0.2 <= 3.9; t += 0.1) { const float f = peakHz(o, (size_t)(t * SR), (size_t)((t + 0.2) * SR), 200, 245); lo = fminf(lo, f); hi = fmaxf(hi, f); }
    return 1200 * log2f(hi / lo); };
  const float w0 = wobble(0), w100 = wobble(100);
  CHECK(w0 < 16 && w100 > w0 + 15 && w100 < 80, "TAPE FLUTE pitch swing over 3 s: MOD 0 %.1f cents (its vibrato), MOD 100 %.1f cents (with wow and flutter)", w0, w100);
  PV h; h.set(PV_MODEL, 0).set(PV_RELEASE, 4000); auto o = h.play(sines({220.f}, 10.f));
  CHECK(rms(o, (size_t)(6 * SR), (size_t)(7 * SR)) > 0.01 && rms(o, (size_t)(9 * SR), (size_t)(10 * SR)) < 1e-4, "held 10 s: %.3f RMS at 6-7 s, %.5f at 9-10 s (the tape ran out at 8 s)", rms(o, (size_t)(6 * SR), (size_t)(7 * SR)), rms(o, (size_t)(9 * SR), (size_t)(10 * SR)));
}

TEST("164 polyv: ATTACK, RELEASE, TONE, MOD and the mix") {
  auto rise = [](float att) { PV p; p.set(PV_MODEL, 3).set(PV_ATTACK, att); std::vector<float> in((size_t)(1.5 * SR), 0.f); for (size_t i = (size_t)(0.2 * SR); i < in.size(); i++) in[i] = 0.15f * sinf(2 * (float)M_PI * 220 * i / SR);
    auto o = p.play(in); float pk = peak(o, (size_t)(1.2 * SR), (size_t)(1.5 * SR)); size_t at = (size_t)(0.2 * SR); while (at < o.size() && peak(o, at, at + 480) < 0.5f * pk) at += 480; return (at - 0.2 * SR) / SR * 1000; };
  const double a5 = rise(5), a500 = rise(500);
  CHECK(a5 < 50 && a500 > 250 && a500 < 700, "half way up after %.0f ms with ATTACK 5 ms, %.0f ms with 500 ms (detection adds ~25 ms)", a5, a500);
  auto fall = [](float rel) { PV p; p.set(PV_MODEL, 3).set(PV_RELEASE, rel); auto in = sines({220.f}, 0.8f); in.resize((size_t)(2.5 * SR), 0.f);
    auto o = p.play(in); float pk = peak(o, (size_t)(0.5 * SR), (size_t)(0.8 * SR)); size_t at = (size_t)(0.8 * SR); while (at < o.size() && peak(o, at, at + 480) > 0.1f * pk) at += 480; return (at - 0.8 * SR) / SR * 1000; };
  const double r50 = fall(50), r1000 = fall(1000);
  CHECK(r50 < 200 && r1000 > 1000, "down to 10%% after the note stops: %.0f ms with RELEASE 50 ms, %.0f ms with 1 s", r50, r1000);
  auto bright = [](float tone, float mod) { PV p; p.set(PV_MODEL, 3).set(PV_TONE, tone).set(PV_MOD, mod); auto o = p.play(sines({110.f}, 1.f));
    std::vector<float> hp(o.size()); for (size_t i = 1; i < o.size(); i++) hp[i] = o[i] - o[i - 1]; return rms(hp, (size_t)(0.6 * SR), o.size()) / rms(o, (size_t)(0.6 * SR), o.size()); };
  const double b500 = bright(500, 0), b8k = bright(8000, 0);
  CHECK(b8k > 2.5 * b500, "TONE 500 Hz -> 8 kHz: brightness x%.1f (high-passed / full RMS)", b8k / b500);
  auto early = [](float mod) { PV p; p.set(PV_MODEL, 3).set(PV_TONE, 400).set(PV_MOD, mod); auto o = p.play(sines({110.f}, 1.5f));
    std::vector<float> hp(o.size()); for (size_t i = 1; i < o.size(); i++) hp[i] = o[i] - o[i - 1];
    return (rms(hp, (size_t)(0.08 * SR), (size_t)(0.2 * SR)) / rms(o, (size_t)(0.08 * SR), (size_t)(0.2 * SR))) / (rms(hp, (size_t)(1.2 * SR), (size_t)(1.5 * SR)) / rms(o, (size_t)(1.2 * SR), (size_t)(1.5 * SR))); };
  const double s0 = early(0), s100 = early(100);
  CHECK(s0 < 1.3 && s100 > 2.0, "SYNTH SAW, MOD 100%%: the note starts %.1fx brighter than it ends (MOD 0: %.1fx), the filter sweeping down", s100, s0);
  PV m; m.set(PV_EFFECT, 0).set(PV_DRY, 100); auto in = sines({220.f, 330.f}, 0.5f); auto o = m.play(in); float diff = 0; for (size_t i = (size_t)(0.4 * SR); i + 128 < o.size(); i++) diff = fmaxf(diff, fabsf(o[i] - in[i]));
  CHECK(diff < 1e-6f, "EFFECT 0, DRY 100: the guitar alone (largest difference %.0g)", diff);
}

TEST("165 polyv: every model, a model change, CPU") {
  auto in = sines({164.81f, 196.f, 246.94f, 293.66f}, 2.f, 0.1f);
  double lo = 1e9, hi = 0;
  for (int m = 0; m < 6; m++) { PV p; p.set(PV_MODEL, (float)m); auto o = p.play(in); const double lv = rms(o, (size_t)SR, o.size()); lo = fmin(lo, lv); hi = fmax(hi, lv);
    bool fin = true; for (float x : o) fin = fin && std::isfinite(x) && fabsf(x) <= 1.5f;
    CHECK(fin && lv > 0.02 && p.notesNow().size() == 4, "%-13s E minor 7: 4 voices, %.1f dBFS, finite and below 1.5", POLYV_MODELS[m].name, 20 * log10(lv)); }
  CHECK(20 * log10(hi / lo) < 3, "the six models within %.1f dB of each other", 20 * log10(hi / lo));
  PV p; p.set(PV_MODEL, 0); std::vector<float> o, part; std::vector<float> all;
  { auto a = p.play(sines({220.f}, 1.f)); all.insert(all.end(), a.begin(), a.end()); p.set(PV_MODEL, 4); std::vector<float> s2((size_t)(0.3 * SR)); for (size_t i = 0; i < s2.size(); i++) s2[i] = 0.15f * sinf(2 * (float)M_PI * 220 * (i + SR) / SR);
    auto b = p.play(s2); all.insert(all.end(), b.begin(), b.end()); }
  const float before = maxStep(all, (size_t)(0.6 * SR), (size_t)SR), after = maxStep(all, (size_t)(1.1 * SR), (size_t)(1.3 * SR)), at = maxStep(all, (size_t)SR - 2, (size_t)(SR + 0.05 * SR));
  CHECK(at <= before + after, "TAPE STRINGS -> SYNTH PULSE mid-note: largest sample step %.3f in the 20 ms crossfade, no more than the two mixed (strings alone %.3f, pulse alone %.3f)", at, before, after);
  for (int m : {0, 2, 3, 5}) {
    PV q; q.set(PV_MODEL, (float)m); auto six = sines({82.41f, 123.47f, 164.81f, 207.65f, 246.94f, 329.63f}, 3.f, 0.06f); std::vector<float> out(six.size()), r(128); Context c; c.sr = SR;
    for (size_t i = 0; i + 128 <= SR; i += 128) q.m->process(&six[i], &six[i], &out[i], r.data(), 128, q.e, c);
    auto t0 = std::chrono::steady_clock::now(); size_t blocks = 0;
    for (size_t i = (size_t)SR; i + 128 <= six.size(); i += 128, blocks++) q.m->process(&six[i], &six[i], &out[i], r.data(), 128, q.e, c);
    const double us = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / blocks * 1e6;
    CHECK(us < 1000, "%-13s 6-string chord: %.1f us per block (%.1f%% of one core)", POLYV_MODELS[m].name, us, us / (128 / SR * 1e6) * 100);
  }
}

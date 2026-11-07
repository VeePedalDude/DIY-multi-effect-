// test_listen.cpp - the notes being played, for the Visualizer's keyboard (engine/core/listen.h): chords
// found, how steady the keyboard is, how fast notes light and go out, and that it costs nothing when off.
#include "framework.h"
#include <chrono>
#include <set>
#include <memory>
using namespace rigtest;

namespace {
const char* NN[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
float hzOf(int midi) { return 440.f * exp2f((midi - 69) / 12.f); }
// notes as tones with six harmonics falling off as 1/h
std::vector<float> tones(const std::vector<int>& midi, float secs, float amp = 0.12f) {
  std::vector<float> v((size_t)(secs * SR), 0.f);
  for (int m : midi) { const float f = hzOf(m); for (int h = 1; h <= 6; h++) if (f * h < 4000.f) for (size_t i = 0; i < v.size(); i++) v[i] += amp * 0.6f / h * sinf(2 * (float)M_PI * f * h * i / SR + h); }
  return v;
}
// plucked strings (Karplus-Strong from a triangle under the pick, as in test_polyv.cpp), strummed 15 ms apart
std::vector<float> strum(const std::vector<int>& midi, float secs, unsigned seed = 3) {
  std::vector<float> out((size_t)(secs * SR), 0.f); unsigned r = seed * 7919u;
  auto rnd = [&] { r = r * 1664525u + 1013904223u; return (r >> 8) / 16777216.f; };
  for (size_t k = 0; k < midi.size(); k++) {
    const double P = SR / hzOf(midi[k]) - 0.5; const int L = (int)P; const float fr = (float)(P - L), c = (1 - fr) / (1 + fr), pp = 0.12f + 0.1f * rnd();
    std::vector<float> line(L + 1); float m = 0;
    for (int i = 0; i <= L; i++) { const float x = (float)i / (L + 1); line[i] = (x < pp ? x / pp : (1 - x) / (1 - pp)) + 0.05f * (2 * rnd() - 1); m += line[i]; }
    for (auto& v : line) v -= m / line.size();
    float ap = 0; int idx = 0;
    for (size_t i = (size_t)(0.015 * k * SR); i < out.size(); i++) {
      const float a = line[idx], b = line[(idx + 1) % (L + 1)], y = 0.4985f * (a + b), o = c * y + ap; ap = y - c * o;
      line[idx] = o; idx = (idx + 1) % (L + 1); out[i] += 0.1f * a;
    }
  }
  return out;
}
std::set<int> classes(const std::vector<int>& midi) { std::set<int> s; for (int m : midi) s.insert(m % 12); return s; }
std::string names(const std::set<int>& s) { std::string o; for (int c : s) o += (o.empty() ? "" : " ") + std::string(NN[c]); return o.empty() ? "-" : o; }
// Play `in` with the listener on; for each block after `from` s, the pitch classes the meters show.
std::vector<std::set<int>> listen(const std::vector<float>& in, float from = 0.f) {
  std::unique_ptr<Engine> e(new Engine); e->init(SR); e->listen.on = true;
  std::vector<std::set<int>> seen;
  for (size_t i = 0; i + 128 <= in.size(); i += 128) {
    memcpy(e->inBuf, &in[i], 512); e->process(128);
    if (i < from * SR) continue;
    const float* m = &e->meters[M_NOTE]; std::set<int> s;
    for (int k = 0; k < (int)m[L_N]; k++) s.insert((int)lrintf(m[L_NOTE0 + k]) % 12);
    seen.push_back(s);
  }
  return seen;
}
}

TEST("175 listen: the notes of chords, steady tones and strummed strings") {
  struct Chord { const char* name; std::vector<int> notes; };
  const Chord chords[] = {
    {"C major (C3 E3 G3)", {48, 52, 55}}, {"A minor (A2 E3 A3 C4 E4)", {45, 52, 57, 60, 64}},
    {"E minor open (E2 B2 E3 G3 B3 E4)", {40, 47, 52, 55, 59, 64}}, {"G7 (G2 B2 D3 F3)", {43, 47, 50, 53}},
    {"D power chord (D3 A3)", {50, 57}}, {"Cmaj7 (C3 E3 G3 B3)", {48, 52, 55, 59}},
  };
  int ok = 0, total = 0;
  for (const auto& c : chords) for (int kind = 0; kind < 2; kind++) {
    const auto want = classes(c.notes);
    auto seen = listen(kind ? strum(c.notes, 1.2f) : tones(c.notes, 1.2f), 0.3f);
    int right = 0; std::set<int> last;
    for (const auto& s : seen) { if (s == want) right++; last = s; }
    const float share = (float)right / seen.size(); total++;
    if (share >= 0.9f) ok++;
    printf("      %-34s %-8s keyboard right %5.1f%% of 0.3-1.2 s (shows %s)\n", c.name, kind ? "strummed" : "steady", 100 * share, names(last).c_str());
  }
  CHECK(ok == total, "%d of %d chords show exactly their notes (as pitch classes) at least 90%% of the time after the first 0.3 s", ok, total);
}

TEST("176 listen: a note lights within 40 ms, goes out within 60 ms of silence, and doesn't flicker") {
  auto in = tones({57}, 1.0f); in.resize((size_t)(1.5f * SR), 0.f);
  std::unique_ptr<Engine> e(new Engine); e->init(SR); e->listen.on = true;
  int litAt = -1, outAt = -1, flips = 0; bool prev = false;
  for (size_t i = 0, b = 0; i + 128 <= in.size(); i += 128, b++) {
    memcpy(e->inBuf, &in[i], 512); e->process(128);
    const float* m = &e->meters[M_NOTE];
    const bool on = m[L_N] >= 1 && lrintf(m[L_NOTE0]) == 57;
    if (on && litAt < 0) litAt = (int)i;
    if (!on && prev && i >= SR) outAt = (int)i;
    if (on != prev && i > 0.1 * SR && i < 0.95 * SR) flips++;
    prev = on;
  }
  const float litMs = 1000.f * litAt / SR, outMs = 1000.f * (outAt - SR) / SR;
  CHECK(litAt >= 0 && litMs < 40.f, "A3 lights %.1f ms after it starts", litMs);
  CHECK(outAt >= 0 && outMs < 60.f, "and goes out %.1f ms after it stops", outMs);
  CHECK(flips == 0, "while it rings (0.1-0.95 s) it never goes out and back on (%d flips)", flips);
}

TEST("177 listen: switched off, the output is bit-identical and it costs nothing; on, a little") {
  Engine a; a.init(SR); Engine b; b.init(SR); b.listen.on = true;
  auto ra = render(a, riffSignal()), rb = render(b, riffSignal());
  CHECK(ra.L == rb.L, "listening doesn't touch the sound: output bit-identical on and off");
  Engine c; c.init(SR); auto rc = render(c, riffSignal());
  bool zero = c.meters[M_NOTE + L_ON] == 0.f && c.meters[M_NOTE + L_N] == 0.f;
  CHECK(zero, "off: its meters read nothing");
  std::vector<float> riff30; while (riff30.size() < 30 * SR) riff30.insert(riff30.end(), riffSignal().begin(), riffSignal().end());
  auto cpu = [&](bool on) { std::unique_ptr<Engine> e(new Engine); e->init(SR); e->listen.on = on; auto t0 = std::chrono::steady_clock::now(); render(*e, riff30); return 100 * std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / (riff30.size() / SR); };
  double c0 = cpu(false), c1 = cpu(true);
  printf("      whole engine: %.2f%% of one core with the keyboard off, %.2f%% on\n", c0, c1);
  CHECK(c1 - c0 < 3.0, "listening adds %.2f%% of one core", c1 - c0);
}

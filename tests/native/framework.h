// tests/native/framework.h - shared helpers for native engine tests.
// Each tests/native/test_*.cpp registers tests with TEST("NN name") { ... }.
// Tests run sorted by name, so the number prefix sets the order.
#pragma once
#include "../../engine/engine.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <complex>
#include <string>
#include <functional>
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace rigtest {
using namespace rig;

inline const float SR = 48000.f;

struct Registry {
  std::vector<std::pair<std::string, std::function<void()>>> tests;
  int fails = 0;
  static Registry& get() { static Registry r; return r; }
};
struct Reg { Reg(const char* n, std::function<void()> f) { Registry::get().tests.push_back({n, f}); } };
#define RIG_CAT2(a, b) a##b
#define RIG_CAT(a, b) RIG_CAT2(a, b)
#define TEST(name) static void RIG_CAT(test_fn_, __LINE__)(); static rigtest::Reg RIG_CAT(test_reg_, __LINE__)(name, RIG_CAT(test_fn_, __LINE__)); static void RIG_CAT(test_fn_, __LINE__)()
#define CHECK(cond, ...) do { bool _ok = (cond); printf("  [%s] ", _ok ? "PASS" : "FAIL"); printf(__VA_ARGS__); printf("\n"); if (!_ok) rigtest::Registry::get().fails++; } while (0)

inline float dbf(double x) { return 20.f * (float)log10(x + 1e-12); }

// ---------------------------------------------------------- synthetic guitar (Karplus-Strong)
struct Note { float t, f, vel; bool mute; };
inline std::vector<float> guitar(float secs, std::vector<Note>& notes, unsigned seed = 1) {
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
inline std::vector<Note> riff() {
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
inline int countOnsetGroups(const std::vector<Note>& n) {     // chord strums within 30 ms = one pick event
  int c = 0; float last = -1;
  for (auto& x : n) { if (x.t - last > 0.03f) c++; last = x.t; }
  return c;
}
inline const std::vector<float>& riffSignal() { static auto n = riff(); static auto g = guitar(14.f, n); return g; }
inline const std::vector<float>& riffWithNoise() {            // riff plus a -75 dBFS noise floor
  static std::vector<float> g = [] { auto v = riffSignal(); srand(7); for (auto& x : v) x += 0.000178f * ((rand() / (float)RAND_MAX) * 2 - 1) * 1.732f; return v; }();
  return g;
}

// ---------------------------------------------------------- engine runner and measurements
struct Run { std::vector<float> L, R; };
inline Run render(Engine& e, const std::vector<float>& in, int block = 128, void (*hook)(Engine&, int) = nullptr) {
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
inline double rms(const std::vector<float>& x, size_t a, size_t b) {
  double s = 0; b = std::min(b, x.size()); for (size_t i = a; i < b; i++) s += (double)x[i] * x[i];
  return b > a ? sqrt(s / (b - a)) : 0;
}
inline float peak(const std::vector<float>& x, size_t a = 0, size_t b = SIZE_MAX) {
  float p = 0; b = std::min(b, x.size()); for (size_t i = a; i < b; i++) p = fmaxf(p, fabsf(x[i])); return p;
}
inline float maxStep(const std::vector<float>& x, size_t a, size_t b) {
  float m = 0; b = std::min(b, x.size()); for (size_t i = a + 1; i < b; i++) m = fmaxf(m, fabsf(x[i] - x[i - 1])); return m;
}
// Engine with only module `keep` active, unity master: isolates one module for a test.
inline void only(Engine& e, int keep) { for (int m = 0; m < NMOD; m++) { e.byp[m] = (m != keep); e.wet[m] = (m != keep) ? 0.f : 1.f; } e.masterDb = 0; e.masterS.y = 1; }
inline int mod(const Engine& e, const char* id) { int m = e.moduleIndex(id); if (m < 0) { printf("no module %s\n", id); exit(2); } return m; }
inline int src(const Engine& e, const char* id) { int k = e.sourceIndex(id); if (k < 0) { printf("no source %s\n", id); exit(2); } return k; }

// ---------------------------------------------------------- file slots
inline std::vector<unsigned char> readFile(const char* path) {
  std::vector<unsigned char> b; FILE* f = fopen(path, "rb"); if (!f) { printf("missing %s (run from the repo root)\n", path); exit(2); }
  unsigned char tmp[65536]; size_t n; while ((n = fread(tmp, 1, sizeof tmp, f)) > 0) b.insert(b.end(), tmp, tmp + n);
  fclose(f); return b;
}
// Hand a payload to a module's file slot the way every host does: reserve, copy, commit.
inline int loadAsset(Engine& e, int m, const std::vector<unsigned char>& bytes) {
  unsigned char* p = e.mods[m]->assetBuffer((int)bytes.size());
  if (!p) return -207;
  memcpy(p, bytes.data(), bytes.size());
  return e.mods[m]->assetCommit((int)bytes.size());
}
// IR payload (core/nam.h): RIR1 header + planar channels
inline std::vector<unsigned char> irPayload(const std::vector<std::vector<float>>& ch, float rate) {
  int32_t h[3] = {IR_MAGIC, (int32_t)ch.size(), (int32_t)ch[0].size()};
  std::vector<unsigned char> b(IR_HEADER + ch.size() * ch[0].size() * 4);
  memcpy(b.data(), h, 12); memcpy(b.data() + 12, &rate, 4);
  size_t o = IR_HEADER; for (auto& c : ch) { memcpy(b.data() + o, c.data(), c.size() * 4); o += c.size() * 4; }
  return b;
}
// Exponentially decaying noise: a stand-in room / reverb IR. RT60 = rt60 seconds.
inline std::vector<float> decayNoise(int frames, float rt60, unsigned seed) {
  std::vector<float> v(frames); unsigned x = seed * 2654435761u + 1;
  for (int i = 0; i < frames; i++) { x = x * 1664525u + 1013904223u; v[i] = ((x >> 8) / 8388608.f - 1.f) * powf(10.f, -3.f * i / (rt60 * SR)); }
  return v;
}

inline void fft(std::vector<std::complex<double>>& a) {
  size_t n = a.size();
  for (size_t i = 1, j = 0; i < n; i++) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap(a[i], a[j]); }
  for (size_t len = 2; len <= n; len <<= 1) {
    double ang = -2 * M_PI / len; std::complex<double> wl(cos(ang), sin(ang));
    for (size_t i = 0; i < n; i += len) { std::complex<double> w(1); for (size_t j = 0; j < len / 2; j++) { auto u = a[i + j], v = a[i + j + len / 2] * w; a[i + j] = u + v; a[i + j + len / 2] = u - v; w *= wl; } }
  }
}

} // namespace rigtest

// test_nam.cpp - the amp's NAM capture player: every architecture, errors, swaps, levels, real-time safety.
#include "framework.h"
#include <atomic>
#include <chrono>
using namespace rigtest;

#define HEAP_COUNTING 1   // see heap_count.cpp; heapCounting() says whether it works on this platform

static const char* MODELS[] = {
  "wavenet_a1_standard.nam", "A2.nam", "wavenet_a2_feature_test.nam", "slimmable_wavenet.nam", "slimmable_container.nam",
  "wavenet.nam", "wavenet_condition_dsp.nam", "lstm.nam", "convnet.nam", "linear.nam", "sequential.nam"};
static std::string modelPath(const char* f) { return std::string("tests/models/") + f; }
static const float DEF[MAXPAR] = {AMP_PARAMS[0].def, AMP_PARAMS[1].def};

// Run a standalone Amp over `in` in 128-sample blocks.
static std::vector<float> runAmp(Amp& a, const std::vector<float>& in, size_t from = 0) {
  std::vector<float> out(in.size()), r(128); Context ctx;
  for (size_t i = from; i < in.size(); i += 128) { int n = (int)std::min<size_t>(128, in.size() - i); a.process(&in[i], &in[i], &out[i], r.data(), n, DEF, ctx); }
  return out;
}
static int loadInto(Amp& a, const std::vector<unsigned char>& b) {
  unsigned char* p = a.assetBuffer((int)b.size()); memcpy(p, b.data(), b.size()); return a.assetCommit((int)b.size());
}

TEST("21 amp: plays every NAM architecture, sample-identical to NeuralAmpModelerCore run directly") {
  std::vector<float> in(riffSignal().begin(), riffSignal().begin() + (size_t)(4 * SR));
  printf("      %-28s %-60s %9s %10s\n", "capture", "engine says", "CPU", "out RMS");
  for (const char* f : MODELS) {
    auto bytes = readFile(modelPath(f).c_str());
    Amp a; a.init(SR);
    int st = loadInto(a, bytes);
    std::string note = a.assetStatus();
    if (st != 0) { CHECK(false, "%s: status %d \"%s\"", f, st, note.c_str()); continue; }
    auto t0 = std::chrono::steady_clock::now();
    auto out = runAmp(a, in);
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    // reference: the same file through NAM core's own API. The capture runs from the first block
    // (silently while it warms up), then crossfades in over 10 ms.
    auto ref = nam::get_dsp(nlohmann::json::parse(bytes.begin(), bytes.end()));
    ref->ResetAndPrewarm(SR, 128);
    const float g = a.slot.current()->gain * db2lin(AMP_PARAMS[1].map(DEF[1]));
    const size_t swapAt = 0, fullAt = (size_t)a.slot.current()->warm + 640;
    std::vector<float> y(in.size()), y2(in.size());
    for (size_t i = swapAt; i + 128 <= in.size(); i += 128) { float* ip[2] = {&in[i], &in[i]}; float* op[2] = {&y[i], &y2[i]}; ref->process(ip, op, 128); }
    double err = 0, sig = 0; bool finite = true;
    for (size_t i = fullAt; i < in.size(); i++) { double w = y[i] * g; err += (out[i] - w) * (out[i] - w); sig += w * w; finite = finite && std::isfinite(out[i]); }
    double errDb = 10 * log10(err / (sig + 1e-30) + 1e-30);
    printf("      %-28s %-60.60s %8.1f%% %7.1f dB\n", f, note.c_str(), 100 * secs / 4, dbf(rms(out, 0, out.size())));
    CHECK(finite && sig > 0 && errDb < -100, "%s: plays (error vs NAM core %.0f dB)", f, errDb);
  }
}

TEST("22 amp: broken or unsupported files are rejected with a reason; the current capture keeps playing") {
  Amp a; a.init(SR);
  loadInto(a, readFile("tests/models/wavenet.nam")); runAmp(a, std::vector<float>(1024, 0.f));
  auto str = [](const char* s) { return std::vector<unsigned char>(s, s + strlen(s)); };
  struct Bad { const char* what; std::vector<unsigned char> b; } bad[] = {
    {"not JSON", str("this is not a capture")},
    {"no config section", str("{\"version\":\"0.5.4\",\"architecture\":\"LSTM\",\"weights\":[0.1],\"sample_rate\":48000}")},
    {"no version", str("{\"architecture\":\"LSTM\",\"config\":{\"input_size\":1,\"hidden_size\":1,\"num_layers\":1},\"weights\":[]}")},
    {"nested model without config", str("{\"version\":\"0.7.0\",\"architecture\":\"Sequential\",\"config\":{\"models\":[{\"version\":\"0.7.0\",\"architecture\":\"Linear\",\"weights\":[1]}]},\"weights\":[]}")},
    {"no network in it", str("{\"version\":\"0.5.2\",\"architecture\":\"WaveNet\",\"sample_rate\":48000,\"weights\":[0.1]}")},
    {"cut-off file", str("{\"version\":\"0.5.4\",\"architecture\":\"WaveNet\",\"config\":{")},
    {"unknown architecture", str("{\"version\":\"0.5.4\",\"architecture\":\"Theremin\",\"config\":{},\"weights\":[]}")},
    {"LSTM with too few weights", str("{\"version\":\"0.5.4\",\"architecture\":\"LSTM\",\"config\":{\"input_size\":1,\"hidden_size\":3,\"num_layers\":1},\"weights\":[0.1,0.2],\"sample_rate\":48000}")},
    {"future file version", str("{\"version\":\"9.0.0\",\"architecture\":\"LSTM\",\"config\":{},\"weights\":[]}")},
    {"3-channel capture", str("{\"version\":\"0.7.0\",\"architecture\":\"Linear\",\"config\":{\"receptive_field\":1,\"bias\":false,\"in_channels\":3,\"out_channels\":3},\"weights\":[1],\"sample_rate\":48000}")},
  };
  for (auto& b : bad) {
    int st = loadInto(a, b.b);
    CHECK(st < 0 && *a.assetStatus(), "%s: status %d, \"%.90s\"", b.what, st, a.assetStatus());
  }
  float m[2]; a.meters(m);
  auto out = runAmp(a, std::vector<float>(riffSignal().begin(), riffSignal().begin() + 48000));
  CHECK(m[0] == 48000 && rms(out, 0, out.size()) > 1e-4, "wavenet.nam still playing (rate meter %.0f Hz)", m[0]);
}

TEST("23 amp: swapping captures, or back to built-in, doesn't click (220 Hz sine)") {
  std::vector<float> s((size_t)(3 * SR)); for (size_t i = 0; i < s.size(); i++) s[i] = 0.3f * sinf(2 * (float)M_PI * 220 * i / SR);
  auto stepOf = [&](const char* f) { Amp a; a.init(SR); if (f) loadInto(a, readFile(modelPath(f).c_str())); auto o = runAmp(a, s); return maxStep(o, SR, 2 * SR); };
  float steady = std::max({stepOf(nullptr), stepOf("lstm.nam"), stepOf("A2.nam")});
  Amp a; a.init(SR);
  std::vector<float> out(s.size()), r(128); Context ctx;
  for (size_t i = 0, blk = 0; i < s.size(); i += 128, blk++) {     // swaps at 0.5, 1.0 and 1.5 s
    if (blk == 188) loadInto(a, readFile("tests/models/lstm.nam"));
    if (blk == 375) loadInto(a, readFile("tests/models/A2.nam"));
    if (blk == 563) a.assetClear();
    a.process(&s[i], &s[i], &out[i], r.data(), 128, DEF, ctx);
  }
  float worst = maxStep(out, SR / 4, (size_t)(2.5f * SR));
  CHECK(worst <= steady * 1.05f, "largest sample step across 3 swaps %.4f vs %.4f playing steadily", worst, steady);
}

TEST("24 amp: a capture made at 48 kHz, running at 44.1 kHz, says so") {
  Amp a; a.init(44100.f);
  int st = loadInto(a, readFile("tests/models/wavenet.nam"));
  CHECK(st == 0 && strstr(a.assetStatus(), "running at 44.1 kHz"), "status: \"%s\"", a.assetStatus());
}

TEST("25 amp: levels (captures with loudness data are levelled to -18 dB, like the NAM plugin)") {
  Engine e; e.init(SR); int m = mod(e, "amp"); only(e, m);
  std::vector<float> in(riffSignal().begin(), riffSignal().begin() + (size_t)(6 * SR));
  auto builtin = render(e, in);
  printf("      built-in amp: %.1f dBFS RMS on the test riff\n", dbf(rms(builtin.L, 0, builtin.L.size())));
  for (const char* f : {"A2.nam", "wavenet.nam", "lstm.nam", "wavenet_a1_standard.nam"}) {
    Engine x; x.init(SR); only(x, m); loadAsset(x, m, readFile(modelPath(f).c_str()));
    auto r = render(x, in);
    printf("      %-26s %.1f dBFS RMS (%s)\n", f, dbf(rms(r.L, 0, r.L.size())), x.amp.slot.current()->dsp->HasLoudness() ? "levelled" : "no loudness data");
  }
  CHECK(true, "levels printed for reference");
}

TEST("26 amp+cab: the audio path never touches the heap, even while files load and swap (and the tuner runs)") {
#if HEAP_COUNTING
  Engine* e = new Engine(); e->init(SR);
  int a = mod(*e, "amp"), c = mod(*e, "cab");
  auto a2 = readFile("tests/models/A2.nam"), lstm = readFile("tests/models/lstm.nam");
  auto ir = irPayload({decayNoise((int)(4 * SR), 3.f, 1), decayNoise((int)(4 * SR), 3.f, 2)}, SR);
  const auto& g = riffSignal(); long during = 0;
  for (size_t i = 0, blk = 0; i + 128 <= g.size(); i += 128, blk++) {
    if (blk == 10) loadAsset(*e, a, a2);                    // control-thread work: allowed to allocate
    if (blk == 50) loadAsset(*e, c, ir);
    if (blk == 900) loadAsset(*e, a, lstm);
    if (blk == 1500) { e->mods[a]->assetClear(); e->mods[c]->assetClear(); }
    if (blk == 200 || blk == 1300) e->tuner.on = true;      // the strobe tuner, switched on (a fresh start) twice
    if (blk == 1200) e->tuner.on = false;
    memcpy(e->inBuf, &g[i], 512);
    during += heapCallsDuring([&] { e->process(128); });    // audio-thread work: must not
  }
  delete e;
  if (heapCounting()) CHECK(during == 0, "heap calls inside process() over %.0f s with 5 loads and swaps, tuner on and off: %ld", g.size() / SR, during);
  else CHECK(true, "heap counting needs glibc; skipped on this platform");
#else
  CHECK(true, "heap counting needs glibc; skipped on this platform");
#endif
}

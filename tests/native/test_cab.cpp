// test_cab.cpp - impulse responses: built-in cab, the file slot, long and stereo IRs.
#include "framework.h"
#include <chrono>
using namespace rigtest;

static void cabFullWet(Engine& e, int c) { only(e, c); e.base[c][0] = 0; e.base[c][1] = 1; e.cab.mixS.y = 1; }

TEST("30 cab: built-in IR response (dB rel 1 kHz)") {
  Engine e; e.init(SR); int c = mod(e, "cab"); cabFullWet(e, c);
  std::vector<float> imp(8192, 0.f); imp[100] = 0.5f;
  auto r = render(e, imp);
  auto mag = [&](float f) { std::complex<double> a = 0; for (int i = 0; i < 8192; i++) a += (double)r.L[i] * std::polar(1.0, -2 * M_PI * f * i / SR); return std::abs(a); };
  double ref = mag(1000);
  printf("      ");
  for (float f : {80.f, 120.f, 400.f, 1000.f, 2600.f, 5000.f, 8000.f, 12000.f}) printf("%gHz %+.1f  ", f, dbf(mag(f) / ref));
  printf("\n");
  CHECK(dbf(mag(12000) / ref) < -20, "high end rolled off like a guitar speaker (12 kHz %.1f dB)", dbf(mag(12000) / ref));
  CHECK(r.L[99] == 0.f && r.L[100] != 0.f, "no latency: output starts on the impulse's sample");
}

TEST("31 cab: loads an IR through the generic file-slot API, and clears back to built-in") {
  Engine e; e.init(SR); int c = mod(e, "cab"); cabFullWet(e, c);
  const AssetSlot& slot = e.mods[c]->desc().asset;
  CHECK(slot.kind && !strcmp(slot.kind, "ir") && slot.ready && slot.maxBytes >= irMaxBytes(), "cab declares a ready 'ir' slot taking up to %.1f MB (20 s stereo at 192 kHz)", slot.maxBytes / 1048576.0);
  int st = loadAsset(e, c, irPayload({{1.f}}, SR));
  std::vector<float> s((size_t)SR); for (size_t i = 0; i < s.size(); i++) s[i] = 0.25f * sinf(2 * (float)M_PI * 1000 * i / SR);
  auto r = render(e, s);
  double ratio = rms(r.L, SR / 2, SR) / rms(s, SR / 2, SR);
  float met[2]; e.mods[c]->meters(met);
  CHECK(st == 0 && met[0] == 1 && met[1] == 1 && fabs(ratio - 0.21) < 0.005, "1-tap IR: status %d, %d tap(s), %d channel(s), gain %.4f (energy-normalised to 0.21)", st, (int)met[0], (int)met[1], ratio);
  e.mods[c]->assetClear(); render(e, std::vector<float>(2048, 0.f)); e.mods[c]->meters(met);
  CHECK(met[0] == (int)(0.021f * SR), "clear restores the built-in IR (%d taps)", (int)met[0]);
  struct Bad { const char* what; std::vector<unsigned char> b; };
  auto good = irPayload({{1.f, 0.5f}}, SR);
  std::vector<Bad> bad = {{"empty", {}}, {"no header", {1, 2, 3, 4}}, {"wrong magic", good}, {"size mismatch", good}, {"3 channels", irPayload({{1.f}, {1.f}, {1.f}}, SR)},
                          {"longer than 20 s", irPayload({std::vector<float>((size_t)(20.5f * 8000), 0.1f)}, 8000)}, {"silent", irPayload({{0.f, 0.f}}, SR)}};
  bad[2].b[0] ^= 1; bad[3].b.pop_back();
  for (auto& b : bad) {
    int r2 = b.b.empty() ? e.mods[c]->assetCommit(0) : loadAsset(e, c, b.b);
    CHECK(r2 < 0, "%s rejected (status %d): \"%s\"", b.what, r2, e.mods[c]->assetStatus());
  }
  e.mods[c]->meters(met);
  CHECK(met[0] == (int)(0.021f * SR), "rejected files leave the current IR playing (%d taps)", (int)met[0]);
}

// Convolution check: an impulse through the cab must come out as lo-cut(gain x IR), from sample 0.
static double irError(Engine& e, const std::vector<std::vector<float>>& ir, size_t check) {
  const size_t lead = 1000, total = lead + check;
  std::vector<float> imp(total, 0.f); imp[lead] = 1.f;
  render(e, std::vector<float>(2048, 0.f));               // let the 10 ms swap fades finish
  auto r = render(e, imp);
  const float g = e.cab.slot.current()->gain;
  double err = 0, sig = 0;
  for (size_t ch = 0; ch < 2; ch++) {
    const auto& h = ir[ch % ir.size()]; const auto& out = ch ? r.R : r.L;
    Biquad lc; lc.hp(SR, e.cab.map(0, 0.f), 0.707f); DCBlock dc;     // the cab's lo cut, then the engine's output DC blocker
    for (size_t i = 0; i < check; i++) {
      double want = dc.tick(lc.tick(i < h.size() ? h[i] * g : 0.f)), got = out[lead + i];
      err += (got - want) * (got - want); sig += want * want;
    }
    for (size_t i = 0; i < lead; i++) err += (double)out[i] * out[i];        // nothing before the impulse
  }
  return 10 * log10(err / sig + 1e-30);
}

// The convolver alone, exactly as the cab builds it (core/nam.h makeIr), against the exact sum.
static double convolverError(const std::vector<std::vector<float>>& ir, size_t check) {
  std::string note; const int ch = (int)ir.size(), frames = (int)ir[0].size();
  std::vector<float> planar; for (auto& c : ir) planar.insert(planar.end(), c.begin(), c.end());
  auto lm = makeIr(planar.data(), ch, frames, SR, SR, 128, 0.21f, note);
  std::vector<float> in(((check + 127) / 128) * 128, 0.f), oL(in.size()), oR(in.size()); in[0] = 1.f;
  for (size_t i = 0; i < in.size(); i += 128) {
    float* ip[1] = {&in[i]}; float* l[1] = {&oL[i]}; float* r[1] = {&oR[i]};
    lm->dsp->process(ip, l, 128); if (lm->dsp2) lm->dsp2->process(ip, r, 128);
  }
  double err = 0, sig = 0;
  for (int c = 0; c < ch; c++) for (size_t i = 0; i < check; i++) { double want = i < ir[c].size() ? ir[c][i] : 0, d = (c ? oR : oL)[i] - want; err += d * d; sig += want * want; }
  return 10 * log10(err / sig + 1e-30);
}

TEST("33 cab: long IRs, convolution-reverb style (exact, zero latency, CPU)") {
  std::vector<float> riff30; const auto& g = riffSignal(); while (riff30.size() < 30 * SR) riff30.insert(riff30.end(), g.begin(), g.end()); riff30.resize((size_t)(30 * SR));
  printf("      %-14s %8s %12s %12s %10s %18s\n", "IR", "taps", "convolver", "whole cab", "CPU", "99.9% of blocks <");
  struct L { float secs; int ch; } lens[] = {{0.021f, 1}, {0.1f, 1}, {0.5f, 1}, {2.f, 1}, {5.f, 2}, {10.f, 2}, {20.f, 2}};
  for (auto& l : lens) {
    Engine* e = new Engine(); e->init(SR); int c = mod(*e, "cab"); cabFullWet(*e, c);
    int frames = (int)(l.secs * SR);
    std::vector<std::vector<float>> ir;
    for (int k = 0; k < l.ch; k++) ir.push_back(decayNoise(frames, std::max(0.05f, l.secs * 0.9f), 11 + k));
    int st = loadAsset(*e, c, irPayload(ir, SR));
    const size_t check = std::min<size_t>(frames + 200, (size_t)(3 * SR));
    double convDb = convolverError(ir, check), cabDb = irError(*e, ir, check);
    std::vector<double> t; auto t0 = std::chrono::steady_clock::now();
    for (size_t i = 0; i < riff30.size(); i += 128) {
      auto b0 = std::chrono::steady_clock::now();
      memcpy(e->inBuf, &riff30[i], 512); e->process(128);
      t.push_back(std::chrono::duration<double>(std::chrono::steady_clock::now() - b0).count());
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::sort(t.begin(), t.end());
    char name[32]; snprintf(name, sizeof name, "%.3g s %s", l.secs, l.ch == 2 ? "stereo" : "mono");
    printf("      %-14s %8d %9.1f dB %9.1f dB %9.2f%% %11.0f%% of budget\n", name, frames, convDb, cabDb, 100 * secs / 30, 100 * t[(size_t)(t.size() * 0.999)] / (128 / SR));
    CHECK(st == 0 && convDb < -120 && cabDb < -60, "%s: loads (status %d), convolver exact to %.0f dB, whole cab (float lo cut) %.0f dB", name, st, convDb, cabDb);
    delete e;
  }
}

TEST("34 cab: stereo IRs give a stereo output; mono IRs stay centred") {
  Engine e; e.init(SR); int c = mod(e, "cab"); cabFullWet(e, c);
  int n = (int)(1.5f * SR);
  loadAsset(e, c, irPayload({decayNoise(n, 1.2f, 3), decayNoise(n, 1.2f, 4)}, SR));
  auto r = render(e, riffSignal());
  double lr = 0, ll = 0, rr = 0; for (size_t i = 0; i < r.L.size(); i++) { lr += (double)r.L[i] * r.R[i]; ll += (double)r.L[i] * r.L[i]; rr += (double)r.R[i] * r.R[i]; }
  double corr = lr / sqrt(ll * rr);
  CHECK(fabs(corr) < 0.5, "stereo IR: left/right correlation %.2f (decorrelated, wide)", corr);
  loadAsset(e, c, irPayload({decayNoise(n, 1.2f, 3)}, SR));
  auto m = render(e, riffSignal());
  float d = 0; for (size_t i = SR; i < m.L.size(); i++) d = fmaxf(d, fabsf(m.L[i] - m.R[i]));
  CHECK(d == 0.f, "mono IR: left and right identical (max diff %g)", d);
}

TEST("35 cab: IRs at another sample rate are resampled to the engine's") {
  Engine e; e.init(SR); int c = mod(e, "cab"); cabFullWet(e, c);
  std::vector<float> ir96 = decayNoise(96000, 0.8f, 9);
  int st = loadAsset(e, c, irPayload({ir96}, 96000.f));
  float met[2]; render(e, std::vector<float>(2048, 0.f)); e.mods[c]->meters(met);
  CHECK(st == 0 && met[0] == 48000, "1 s at 96 kHz loads as %d taps at 48 kHz (status %d)", (int)met[0], st);
  CHECK(strstr(e.mods[c]->assetStatus(), "resampled from 96 kHz") != nullptr, "status says so: \"%s\"", e.mods[c]->assetStatus());
}

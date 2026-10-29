#include "framework.h"
using namespace rigtest;

TEST("30 cab: built-in IR response (dB rel 1 kHz)") {
  Engine e; e.init(SR); int c = mod(e, "cab"); only(e, c); e.base[c][0] = 0; e.cab.mixS.y = 1; e.base[c][1] = 1;
  std::vector<float> imp(8192, 0.f); imp[100] = 0.5f;
  auto r = render(e, imp);
  auto mag = [&](float f) { std::complex<double> a = 0; for (int i = 0; i < 8192; i++) a += (double)r.L[i] * std::polar(1.0, -2 * M_PI * f * i / SR); return std::abs(a); };
  double ref = mag(1000);
  printf("      ");
  for (float f : {80.f, 120.f, 400.f, 1000.f, 2600.f, 5000.f, 8000.f, 12000.f}) printf("%gHz %+.1f  ", f, dbf(mag(f) / ref));
  printf("\n");
  CHECK(dbf(mag(12000) / ref) < -20, "high end rolled off like a guitar speaker (12 kHz %.1f dB)", dbf(mag(12000) / ref));
}

TEST("31 cab: loads an IR through the generic file-slot API, and clears back to built-in") {
  Engine e; e.init(SR); int c = mod(e, "cab"); only(e, c); e.base[c][0] = 0; e.cab.mixS.y = 1; e.base[c][1] = 1;
  const AssetSlot& slot = e.mods[c]->desc().asset;
  CHECK(slot.kind && !strcmp(slot.kind, "ir") && slot.ready && slot.maxBytes == 2048 * 4, "cab declares a ready 'ir' slot of %d bytes", slot.maxBytes);
  float one = 1.f; memcpy(e.mods[c]->assetBuffer(), &one, 4);
  int st = e.mods[c]->assetCommit(4);
  std::vector<float> s((size_t)SR); for (size_t i = 0; i < s.size(); i++) s[i] = 0.25f * sinf(2 * (float)M_PI * 1000 * i / SR);
  auto r = render(e, s);
  double ratio = rms(r.L, SR / 2, SR) / rms(s, SR / 2, SR);
  float taps[2]; e.mods[c]->meters(taps);
  CHECK(st == 0 && taps[0] == 1 && fabs(ratio - 0.21) < 0.005, "1-tap IR: status %d, %d tap(s), gain %.4f (energy-normalised to 0.21)", st, (int)taps[0], ratio);
  e.mods[c]->assetClear(); e.mods[c]->meters(taps);
  CHECK(taps[0] == (int)(0.021f * SR), "clear restores the built-in IR (%d taps)", (int)taps[0]);
  CHECK(e.mods[c]->assetCommit(0) < 0, "empty file rejected (status %d)", e.mods[c]->assetCommit(0));
}

TEST("32 slots: amp declares a NAM slot that is not loadable yet") {
  Engine e; e.init(SR); const AssetSlot& a = e.mods[mod(e, "amp")]->desc().asset;
  CHECK(a.kind && !strcmp(a.kind, "nam") && !a.ready && strstr(e.descJson, "\"kind\":\"nam\""), "amp slot kind=%s ready=%d, listed in describe()", a.kind ? a.kind : "-", a.ready);
}

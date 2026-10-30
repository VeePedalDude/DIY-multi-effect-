// tests/parity/dump.cpp - render one scripted scenario through the C API.
// Writes build/in.f32 (input), build/out_native.f32 (reference output) and
// build/riff48k.wav (fake microphone for the browser tests).
#include "../native/framework.h"
#include "../../engine/api/rig_api.cpp"
using namespace rigtest;
int main() {
  const auto& g = riffSignal();
  FILE* f = fopen("build/in.f32", "wb"); fwrite(g.data(), 4, g.size(), f); fclose(f);
  {
    FILE* w = fopen("build/riff48k.wav", "wb");
    unsigned n = (unsigned)g.size() * 2, v32; unsigned short v16;
    fwrite("RIFF", 1, 4, w); v32 = 36 + n; fwrite(&v32, 4, 1, w); fwrite("WAVEfmt ", 1, 8, w);
    v32 = 16; fwrite(&v32, 4, 1, w); v16 = 1; fwrite(&v16, 2, 1, w); fwrite(&v16, 2, 1, w);
    v32 = 48000; fwrite(&v32, 4, 1, w); v32 = 96000; fwrite(&v32, 4, 1, w); v16 = 2; fwrite(&v16, 2, 1, w); v16 = 16; fwrite(&v16, 2, 1, w);
    fwrite("data", 1, 4, w); fwrite(&n, 4, 1, w);
    for (float x : g) { short s = (short)lrintf(fmaxf(-1.f, fminf(1.f, x)) * 32767.f); fwrite(&s, 2, 1, w); }
    fclose(w);
  }
  // Scenario (mirrored exactly in parity.mjs): instances 0 gate 1 amp 2 cab 3 holo 4 tess; sources 0 lfo 1 env 2 adsr 3 orbit 4 heart 5 ctrl
  rig_init(48000); rig_mod(1, 0, 1, 0.22f); rig_mod(3, 0, 0, 0.22f); rig_mod(3, 1, 3, 0.22f); rig_mod(4, 1, 2, 0.22f);
  std::vector<float> out(g.size() * 2);
  for (size_t i = 0; i < g.size(); i += 128) {
    if (i == 48000 * 5) { rig_param(3, 0, 0.8f); rig_bypass(1, 1); rig_tempo(90); }
    if (i == 48000 * 8) { rig_bypass(1, 0); rig_ext(5, 0.7f); rig_mod(2, 1, 5, 0.3f); }
    if (i == 48000 * 10) { rig_src_param(0, 2, 0.875f); rig_src_param(0, 4, 0.25f); rig_mod_tap(3, 0, 90.f, 1.5f); }   // LFO: SMOOTH RANDOM, 8 STEPS; HOLOGRAM's patch at 90 degrees, 3/2
    memcpy(rig_in(), &g[i], 512); rig_process(128);
    for (int k = 0; k < 128; k++) { out[2 * (i + k)] = rig_out_l()[k]; out[2 * (i + k) + 1] = rig_out_r()[k]; }
  }
  f = fopen("build/out_native.f32", "wb"); fwrite(out.data(), 4, out.size(), f); fclose(f);
  printf("native dump: %zu samples\n", g.size());

  // Scenario 2 (mirrored in parity.mjs): files. Re-init, load A2.nam into the amp and a 3 s stereo
  // IR into the cab (35% mix); at 5 s swap the amp to lstm.nam; at 8 s put the cab back to built-in.
  const int n = 3 * 48000;
  auto ir = irPayload({decayNoise(n, 2.5f, 21), decayNoise(n, 2.5f, 22)}, 48000.f);
  f = fopen("build/ir_test.rir", "wb"); fwrite(ir.data(), 1, ir.size(), f); fclose(f);
  auto a2 = readFile("tests/models/A2.nam"), lstm = readFile("tests/models/lstm.nam");
  auto load = [](int m, const std::vector<unsigned char>& b) { memcpy(rig_asset_buf(m, (int)b.size()), b.data(), b.size()); return rig_asset_commit(m, (int)b.size()); };
  rig_init(48000); rig_param(2, 1, 0.35f);
  int s1 = load(1, a2), s2 = load(2, ir);
  for (size_t i = 0; i < g.size(); i += 128) {
    if (i == 48000 * 5) load(1, lstm);
    if (i == 48000 * 8) rig_asset_clear(2);
    memcpy(rig_in(), &g[i], 512); rig_process(128);
    for (int k = 0; k < 128; k++) { out[2 * (i + k)] = rig_out_l()[k]; out[2 * (i + k) + 1] = rig_out_r()[k]; }
  }
  f = fopen("build/out_native2.f32", "wb"); fwrite(out.data(), 4, out.size(), f); fclose(f);
  printf("native dump 2 (A2 capture, 3 s stereo IR): loads %d %d · %s · %s\n", s1, s2, rig_asset_status(1), rig_asset_status(2));
  return s1 || s2;
}

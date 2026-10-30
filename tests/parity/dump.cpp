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
    if (i == 48000 * 11) { rig_src_param(3, 1, 4.f / 11.f); rig_src_param(3, 4, 1.f); rig_src_param(3, 2, 1.f / 3.f); }   // THREE-BODY: MOTH I, synced, BODY X
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

  // Scenario 3 (mirrored in parity.mjs): GEO PULSE, HYPERBOLIC, random direction and probability, patched to
  // HOLOGRAM feedback; settings change at 4 s and 9 s. Every block: its value and both meters (step, fired).
  rig_init(48000); strcpy(rig_text(), "geo"); const int gk = rig_src_add();
  auto cfg = [&](int i, float real) { const rig::ParamDesc& q = rig::GEO_SETTINGS[i]; rig_src_cfg(gk, i, (real - q.min) / (q.max - q.min)); };
  cfg(rig::GS_SHAPE, 3); cfg(rig::GS_VERTS, 9); cfg(rig::GS_DIR, 3); cfg(rig::GS_SEED, 11); cfg(rig::GS_OUT, 2);
  rig_src_param(gk, rig::GK_PROB, 0.7f); rig_src_param(gk, rig::GK_WARP, 0.5f); rig_src_param(gk, rig::GK_CURVE, 0.8f); rig_src_param(gk, rig::GK_TIMING, 1.f);
  rig_src_cell(gk, 4, rig::GC_ON, 0.f); rig_src_cell(gk, 2, rig::GC_ACCENT, 1.f); rig_mod(3, 0, gk, 0.3f);
  std::vector<float> trace; trace.reserve(g.size() / 128 * 3 + 8);
  for (size_t i = 0; i < g.size(); i += 128) {
    if (i == 48000 * 4) { cfg(rig::GS_SHAPE, 7); cfg(rig::GS_DIR, 2); rig_src_param(gk, rig::GK_DEPTH, 0.6f); }
    if (i == 48000 * 9) { cfg(rig::GS_SHAPE, 1); cfg(rig::GS_VERTS, 12); cfg(rig::GS_SKIP, 4); cfg(rig::GS_CYCLES, 1); cfg(rig::GS_SWING, 30); rig_ext(gk, 2); }
    memcpy(rig_in(), &g[i], 512); rig_process(128);
    trace.push_back(rig_meters()[rig::M_SRC + gk]); trace.push_back(rig_meters()[rig::M_SRCM + gk * rig::MAXSRCMETERS]); trace.push_back(rig_meters()[rig::M_SRCM + gk * rig::MAXSRCMETERS + 1]);
  }
  f = fopen("build/geo_native.f32", "wb"); fwrite(trace.data(), 4, trace.size(), f); fclose(f);
  printf("native dump 3 (GEO PULSE): %zu blocks, %d events fired\n", trace.size() / 3, (int)(trace.back() / 32));
  return s1 || s2;
}

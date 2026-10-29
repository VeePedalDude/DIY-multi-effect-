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
    memcpy(rig_in(), &g[i], 512); rig_process(128);
    for (int k = 0; k < 128; k++) { out[2 * (i + k)] = rig_out_l()[k]; out[2 * (i + k) + 1] = rig_out_r()[k]; }
  }
  f = fopen("build/out_native.f32", "wb"); fwrite(out.data(), 4, out.size(), f); fclose(f);
  printf("native dump: %zu samples\n", g.size());
}

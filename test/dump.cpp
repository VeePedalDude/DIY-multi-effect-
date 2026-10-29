// dump.cpp - render the full-chain scenario natively through the C API; writes input and output for the wasm parity check
#define main harness_main
#include "harness.cpp"
#undef main
#include "../dsp/rig_api.cpp"
int main() {
  auto notes = riff(); auto g = guitar(14.f, notes);
  FILE* f = fopen("build/in.f32", "wb"); fwrite(g.data(), 4, g.size(), f); fclose(f);
  {  // same riff as a 16-bit WAV: the fake microphone for test/browser.cjs
    FILE* w = fopen("build/riff48k.wav", "wb");
    unsigned n = (unsigned)g.size() * 2, v32; unsigned short v16;
    fwrite("RIFF", 1, 4, w); v32 = 36 + n; fwrite(&v32, 4, 1, w); fwrite("WAVEfmt ", 1, 8, w);
    v32 = 16; fwrite(&v32, 4, 1, w); v16 = 1; fwrite(&v16, 2, 1, w); fwrite(&v16, 2, 1, w);
    v32 = 48000; fwrite(&v32, 4, 1, w); v32 = 96000; fwrite(&v32, 4, 1, w); v16 = 2; fwrite(&v16, 2, 1, w); v16 = 16; fwrite(&v16, 2, 1, w);
    fwrite("data", 1, 4, w); fwrite(&n, 4, 1, w);
    for (float x : g) { short s = (short)lrintf(fmaxf(-1.f, fminf(1.f, x)) * 32767.f); fwrite(&s, 2, 1, w); }
    fclose(w);
  }
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

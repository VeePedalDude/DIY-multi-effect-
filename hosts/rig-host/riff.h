// hosts/rig-host/riff.h - the built-in test riff (the same synthesized guitar the native tests and the
// browser use: plucked strings by Karplus-Strong, peaks at -12 dBFS like a 2i2 on INST), so the host
// can be tried with no guitar plugged in. Made once at start-up (it allocates), then played in a loop.
#pragma once
#include <vector>
#include <cmath>
#include <cstdlib>

inline std::vector<float> makeRiff(float sr) {
  struct Note { float t, f, vel; bool mute; };
  std::vector<Note> notes; const float E2 = 82.41f, A2 = 110.f, B2 = 123.47f, D3 = 146.83f; float t = 0.2f;
  for (int bar = 0; bar < 4; bar++) {
    const float root = bar % 2 ? A2 : E2;
    notes.push_back({t, root, 1.f, false}); notes.push_back({t + 0.008f, root * 1.4983f, .8f, false}); notes.push_back({t + 0.016f, root * 2, .6f, false});
    t += 0.75f;
    for (int k = 0; k < 4; k++) { notes.push_back({t, root, .7f, true}); t += 0.125f; }
    notes.push_back({t, bar % 2 ? D3 : B2, .9f, false}); t += 0.75f;
  }
  const int N = (int)(8.f * sr); std::vector<float> out(N, 0.f); unsigned seed = 1;
  for (const Note& nt : notes) {
    const int start = (int)(nt.t * sr), period = (int)(sr / nt.f), dur = (int)((nt.mute ? 0.18f : 2.2f) * sr);
    std::vector<float> line(period);
    for (float& v : line) { seed = seed * 1103515245u + 12345u; v = nt.vel * (((seed >> 8) & 0xffff) / 32767.5f - 1.f); }
    const float damp = nt.mute ? 0.93f : 0.996f; int idx = 0;
    for (int i = 0; i < dur && start + i < N; i++) {
      const float a = line[idx], b = line[(idx + 1) % period];
      line[idx] = 0.5f * (a + b) * damp; idx = (idx + 1) % period;
      out[start + i] += a * (i > dur - 480 ? (float)(dur - i) / 480.f : 1.f);
    }
  }
  float pk = 0; for (float v : out) pk = std::fmax(pk, std::fabs(v));
  for (float& v : out) v *= 0.25f / pk;
  return out;
}

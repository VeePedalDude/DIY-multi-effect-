// engine/core/listen.h - the notes being played, for the Visualizer's keyboard: Poly Voice's tracker
// (core/polytrack.h) on the dry input, whatever is in the chain. It only runs while switched on
// (`listen 1`, sent while the Visualizer is up), so it costs nothing otherwise.
//
// The tracker gives the notes it finds every hop (about 5 ms). Shown on a keyboard, those flicker: a note
// can drop out of a hop or two while it rings. Each semitone keeps a count: up by one in a hop that has
// it (to 8), down by one in a hop that doesn't. It lights after three hops (16 ms) and goes out when the
// count reaches nothing, so a ringing note holds steady. The tracker's window is 170 ms long, so a note
// stays in it that long after it stops; the input's own level says sooner (as Poly Voice's voices do): when
// it falls under -54 dBFS (the tracker looks at nothing below -56), every key goes out.
//
// Meters (L_*): on, how many notes, then up to six (MIDI note number with its fraction, from the tuner's
// A4; and level), loudest first, -1 for an empty slot.
#pragma once
#include "polytrack.h"
#include <math.h>
#include <string.h>

namespace rig {

constexpr int LISTEN_NOTES = PolyTracker::MAXNOTES;
enum ListenMeter { L_ON, L_N, L_NOTE0, L_AMP0 = L_NOTE0 + LISTEN_NOTES, L_LEN = L_AMP0 + LISTEN_NOTES };

struct NoteListener {
  bool on = false, was = false;
  PolyTracker trk;
  unsigned char cnt[128] = {0}; bool lit[128] = {false};
  float level[128] = {0}, cents[128] = {0}, env = 0.f, envFall = 0.f;
  int n = 0, note[LISTEN_NOTES]; float amp[LISTEN_NOTES], frac[LISTEN_NOTES];

  void init(float sr) { trk.init(sr); envFall = expf(-1.f / (0.005f * sr)); reset(); }
  void reset() {
    trk.reset(); env = 0.f; memset(cnt, 0, sizeof cnt); memset(lit, 0, sizeof lit); memset(level, 0, sizeof level); memset(cents, 0, sizeof cents);
    n = 0; for (int k = 0; k < LISTEN_NOTES; k++) { note[k] = -1; amp[k] = 0.f; frac[k] = 0.f; }
  }
  void process(const float* in, int frames, float a4) {
    if (!on) { if (was) reset(); was = false; return; }
    was = true;
    for (int i = 0; i < frames; i++) {
      const float a = fabsf(in[i]); env = a > env ? a : env * envFall;      // peak level, falling with 5 ms
      if (trk.push(in[i])) hop(a4);
    }
  }
  void hop(float a4) {
    bool seen[128] = {false};
    for (int k = 0; k < trk.nnotes; k++) {
      const float m = 69.f + 12.f * log2f(trk.notes[k].hz / a4); const int r = (int)lrintf(m);
      if (r < 0 || r > 127) continue;
      seen[r] = true; level[r] = trk.notes[k].amp; cents[r] = m - (float)r;
    }
    const bool quiet = env < 0.002f;                             // the input stopped: nothing is sounding
    for (int r = 0; r < 128; r++) {
      if (quiet) { cnt[r] = 0; lit[r] = false; continue; }
      if (seen[r]) { if (cnt[r] < 8) cnt[r]++; } else if (cnt[r]) cnt[r]--;
      if (!lit[r] && cnt[r] >= 3) lit[r] = true;
      if (lit[r] && cnt[r] == 0) lit[r] = false;
    }
    // the loudest six that are lit
    n = 0; for (int k = 0; k < LISTEN_NOTES; k++) { note[k] = -1; amp[k] = 0.f; frac[k] = 0.f; }
    for (int r = 0; r < 128; r++) {
      if (!lit[r]) continue;
      int at = n < LISTEN_NOTES ? n : -1;
      if (at < 0) { int lo = 0; for (int k = 1; k < LISTEN_NOTES; k++) if (amp[k] < amp[lo]) lo = k; if (level[r] <= amp[lo]) continue; at = lo; } else n++;
      note[at] = r; amp[at] = level[r]; frac[at] = cents[r];
    }
    for (int a = 1; a < n; a++) for (int b = a; b > 0 && amp[b] > amp[b - 1]; b--) {     // loudest first
      int t = note[b]; note[b] = note[b - 1]; note[b - 1] = t;
      float u = amp[b]; amp[b] = amp[b - 1]; amp[b - 1] = u; u = frac[b]; frac[b] = frac[b - 1]; frac[b - 1] = u;
    }
  }
  void meters(float* m) const {
    m[L_ON] = on ? 1.f : 0.f; m[L_N] = (float)n;
    for (int k = 0; k < LISTEN_NOTES; k++) { m[L_NOTE0 + k] = note[k] < 0 ? -1.f : (float)note[k] + frac[k]; m[L_AMP0 + k] = amp[k]; }
  }
};

} // namespace rig

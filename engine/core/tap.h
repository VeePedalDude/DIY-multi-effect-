// engine/core/tap.h - per-destination PHASE and DIV for a patch from a tapping source (an LFO).
//
// Every patch (module knob or source knob) holds a Tap. At its default (0 degrees, 1x) nothing is
// worked out and the knob moves by the source's own value, exactly as before. Otherwise the patch
// reads the source's shape at its own phase:  frac(cycles x DIV + PHASE / 360), so 1x, 2x, 1.5x...
// stay locked to the source's cycle (DIV is one of TAP_DIVS, whose denominators all divide 12).
//
// Turning PHASE or DIV never jumps the output: the difference between where the patch was heading
// and where the new setting puts it is kept as an offset that glides back to 0 (100 ms), so the
// patch slides onto the new phase and stays on the beat grid. Control thread writes `deg` and `div`;
// the audio thread reads them once per block (plain floats: a torn write is one block late at worst).
#pragma once
#include "source.h"

namespace rig {

inline constexpr float TAP_DIVS[] = {0.25f, 1.f / 3.f, 0.5f, 2.f / 3.f, 1.f, 1.5f, 2.f, 3.f, 4.f, 8.f};
inline constexpr const char* TAP_DIV_NAMES[] = {"1/4", "1/3", "1/2", "2/3", "1", "3/2", "2", "3", "4", "8"};
constexpr int NTAPDIVS = 10, TAP_DIV_DEFAULT = 4;
inline int tapDivIndex(float d) { int b = 0; for (int i = 1; i < NTAPDIVS; i++) if (fabsf(TAP_DIVS[i] - d) < fabsf(TAP_DIVS[b] - d)) b = i; return b; }

struct Tap {
  float deg = 0.f, div = 1.f;                         // settings: PHASE 0..360, DIV (a TAP_DIVS value)
  float glide = 0.f, lastOut = 0.f, lastSrc = 0.f, lastDeg = 0.f, lastDiv = 1.f; bool primed = false;
  void reset() { deg = 0.f; div = 1.f; glide = 0.f; primed = false; }
  void set(float d, float v) { float w = fmodf(d, 360.f); if (w < 0) w += 360.f; deg = w; div = TAP_DIVS[tapDivIndex(v)]; }
  bool plain() const { return deg == 0.f && div == 1.f && glide == 0.f; }
  void follow() { primed = false; }                   // the patch used the source's own value this block
  // This block's value of source `s` for this patch. dt = block length in seconds.
  float eval(const Source& s, float dt) {
    unsigned n; float ph; s.cycle(n, ph);
    const float d = div, off = deg / 360.f;
    const float w = ((float)(n % 12u) + ph) * d + off, whole = floorf(w);
    float raw = w - whole;                                    // phase on the grid, 0..1
    unsigned idx = (n / 12u) * (unsigned)lrintf(12.f * d) + (unsigned)whole;
    if (!primed) { lastOut = lastSrc = ph; lastDeg = 0.f; lastDiv = 1.f; primed = true; }   // it was following the source itself
    if ((d != lastDiv || deg != lastDeg)) {         // setting moved: keep going from where it was
      float adv = ph - lastSrc; if (adv < 0) adv += 1.f;
      float g = lastOut + adv * d - raw + glide;              // includes the glide still pending
      glide = g - floorf(g + 0.5f);                           // -0.5..0.5
    }
    glide *= expf(-dt / 0.1f); if (fabsf(glide) < 1e-5f) glide = 0.f;
    float p = raw + glide;
    if (p < 0.f) { p += 1.f; idx--; } else if (p >= 1.f) { p -= 1.f; idx++; }
    lastOut = p; lastSrc = ph; lastDeg = deg; lastDiv = d; primed = true;
    const float v = s.at(p, idx);
    return v == v ? clampf(v, -1.f, 1.f) : 0.f;
  }
};

} // namespace rig

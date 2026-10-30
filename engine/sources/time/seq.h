// engine/sources/time/seq.h - GATE SEQ: a step sequencer for gates and pulses, locked to the tempo.
// Up to 32 steps, each a square you switch on or off, with its own velocity, chance, ratchet (1-4 hits
// in the step), gate length and a value (-100..+100%, for stepped modulation).
// Knobs (cross-modulatable):
//  RATE    each step's length (note values, 1/32 to 4 bars; default 1/16).
//  GATE    gate length, % of the step (default 50%; a step's own GATE multiplies it, over 100% ties).
//  PROB    chance any step fires (default 100%; x the step's own PROB).
//  SWING   every second step starts late, by this share of a step (0-75%).
//  FILL    EUCLID pattern: how many hits, spread as evenly as they go over the steps (default 4).
//  SHIFT   rotates the pattern: step n plays square n + SHIFT (0-31).
//  SLEW    VALUE glides to each new step's value (0-500 ms).
// Settings: STEPS 1-32 (16), PATTERN (GRID: your squares; EUCLID: FILL hits, also only where a square
// is on), DIRECTION (forward, reverse, ping-pong, random), RESYNC (back to step 1 every 1-16 bars, so a
// 5-step pattern against 4/4 lines up again), SEED, OFFSET (ms), OUT (gate, trigger, envelope,
// velocity, value, step), POLARITY, RETRIGGER, ATTACK, DECAY.
// Transport: `ext <id> 0` stops, `1` plays on, `2` back to step 1 (it fires at once).
// Real time: steps are found by sample and only from time passing (this block's knobs at both ends of
// the window), so a knob that jumps moves the pattern but never fires or skips extra steps. Chance and
// random order come from a hash of (seed, resync period, step number, ratchet hit): the same seed
// plays the same pattern at any block size. Meters: "step" (square + 32 x square + 1024 x position in
// the step, 0..1023) and "fired" (32 x hits fired + the square), which the panel's squares follow.
#pragma once
#include "../../core/source.h"
#include "../../core/notes.h"
#include <stdint.h>

namespace rig {

constexpr int SEQ_MAX = 32;
inline constexpr ParamDesc SEQ_PARAMS[] = {
  {"rate",  "RATE",  0.125f, 16.f,  1.f / 7.f,  "beats", Curve::Log, 2, false, STEP_STOPS, NSTEP_STOPS},   // 1/16
  {"gate",  "GATE",  1.f,    100.f, 49.f / 99.f, "%",    Curve::Lin, 0, false},                           // 50%
  {"prob",  "PROB",  0.f,    100.f, 1.f,        "%",     Curve::Lin, 0, false},
  {"swing", "SWING", 0.f,    75.f,  0.f,        "%",     Curve::Lin, 0, false},
  {"fill",  "FILL",  0.f,    32.f,  0.125f,     "",      Curve::Lin, 0, false},                           // 4 hits
  {"shift", "SHIFT", 0.f,    31.f,  0.f,        "",      Curve::Lin, 0, false},
  {"slew",  "SLEW",  0.f,    500.f, 0.f,        "ms",    Curve::Lin, 0, false},
};
enum { SK_RATE, SK_GATE, SK_PROB, SK_SWING, SK_FILL, SK_SHIFT, SK_SLEW };

inline constexpr ParamStop SEQ_PATTERNS[] = {{0, "GRID"}, {1, "EUCLID"}};
inline constexpr ParamStop SEQ_DIRS[] = {{0, "FORWARD"}, {1, "REVERSE"}, {2, "PING-PONG"}, {3, "RANDOM"}};
inline constexpr ParamStop SEQ_RESYNCS[] = {{0, "OFF"}, {1, "1 BAR"}, {2, "2 BARS"}, {4, "4 BARS"}, {8, "8 BARS"}, {16, "16 BARS"}};
inline constexpr ParamStop SEQ_OUTS[] = {{0, "GATE"}, {1, "TRIGGER"}, {2, "ENVELOPE"}, {3, "VELOCITY"}, {4, "VALUE"}, {5, "STEP"}};
inline constexpr ParamStop SEQ_POLARS[] = {{0, "-1 TO +1"}, {1, "0 TO +1"}, {2, "0 TO -1"}};
inline constexpr ParamStop SEQ_RETRIGS[] = {{0, "RESTART"}, {1, "FROM LEVEL"}};
inline constexpr ParamDesc SEQ_SETTINGS[] = {
  {"steps",   "STEPS",     1.f,   32.f,   15.f / 31.f, "",  Curve::Lin, 0, false},             // 16
  {"pattern", "PATTERN",   0.f,   1.f,    0.f,        "",   Curve::Lin, 0, false, SEQ_PATTERNS, 2},
  {"dir",     "DIRECTION", 0.f,   3.f,    0.f,        "",   Curve::Lin, 0, false, SEQ_DIRS, 4},
  {"resync",  "RESYNC",    0.f,   16.f,   0.f,        "",   Curve::Lin, 0, false, SEQ_RESYNCS, 6},
  {"seed",    "SEED",      0.f,   999.f,  1.f / 999.f, "",  Curve::Lin, 0, false},             // 1
  {"offset",  "OFFSET",   -50.f,  50.f,   0.5f,       "ms", Curve::Lin, 1, true},
  {"out",     "OUT",       0.f,   5.f,    0.f,        "",   Curve::Lin, 0, false, SEQ_OUTS, 6},
  {"polar",   "POLARITY",  0.f,   2.f,    0.f,        "",   Curve::Lin, 0, false, SEQ_POLARS, 3},
  {"retrig",  "RETRIGGER", 0.f,   1.f,    0.f,        "",   Curve::Lin, 0, false, SEQ_RETRIGS, 2},
  {"attack",  "ATTACK",    0.5f,  1000.f, 0.18237f,   "ms", Curve::Log, 1, false},             // 2 ms
  {"decay",   "DECAY",     10.f,  5000.f, 0.48204f,   "ms", Curve::Log, 0, false},             // 200 ms
};
enum { SS_STEPS, SS_PATTERN, SS_DIR, SS_RESYNC, SS_SEED, SS_OFFSET, SS_OUT, SS_POLAR, SS_RETRIG, SS_ATT, SS_DEC, SS_COUNT };
inline constexpr ParamStop SEQ_ONOFF[] = {{0, "OFF"}, {1, "ON"}};
inline constexpr ParamStop SEQ_RATCHETS[] = {{1, "1"}, {2, "2"}, {3, "3"}, {4, "4"}};
inline constexpr ParamDesc SEQ_COLS[] = {
  {"on",      "ON",      0.f,    1.f,   1.f,  "",  Curve::Lin, 0, false, SEQ_ONOFF, 2},
  {"vel",     "VEL",     0.f,    100.f, 0.8f, "%", Curve::Lin, 0, false},
  {"prob",    "PROB",    0.f,    100.f, 1.f,  "%", Curve::Lin, 0, false},
  {"ratchet", "RATCHET", 1.f,    4.f,   0.f,  "",  Curve::Lin, 0, false, SEQ_RATCHETS, 4},
  {"gate",    "GATE",    0.f,    200.f, 0.5f, "%", Curve::Lin, 0, false},                      // x the GATE knob
  {"value",   "VALUE",  -100.f,  100.f, 0.5f, "%", Curve::Lin, 0, true},
};
enum { SC_ON, SC_VEL, SC_PROB, SC_RATCHET, SC_GATE, SC_VALUE };
inline constexpr SourceDesc SEQ_DESC = {"seq", "GATE SEQ", "step sequencer · gates and pulses", "transport: 0 stop, 1 play, 2 back to step 1", 2, {"step", "fired"},
  7, SEQ_PARAMS, false, SS_COUNT, SEQ_SETTINGS, SEQ_MAX, 6, SEQ_COLS};

inline uint32_t seqHash(uint32_t seed, uint32_t a, uint32_t b, uint32_t c) {
  uint32_t x = seed * 0x9E3779B1u ^ a * 0x85EBCA77u ^ b * 0xC2B2AE3Du ^ c * 0x27D4EB2Fu;
  x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16; return x;
}
// Euclidean rhythm: `hits` spread as evenly as they go over `steps` (3 of 8: x..x..x.).
inline bool seqEuclid(int idx, int hits, int steps) { return hits > 0 && steps > 0 && (idx * hits) % steps < hits; }

struct GateSeq : Source {
  double beats = 0, now = 0;
  bool playing = true, inclusive = true; int resetReq = 0, resetSeen = 0, playReq = -1;
  double gateEnd = -1, trigEnd = -1, envStart = -1e18; bool gateHit = false, trigHit = false;
  float env = 0, envFrom = 0, envPeak = 0, vel = 0, value = 0, slewed = 0, stepFrac = 0;
  int cur = 0, lastFired = 0; unsigned fired = 0; bool started = false;
  int out = 0, prevOut = 0, polar = 0, prevPolar = 0; float oxf = 1, pxf = 1, rawOut[6] = {0};
  struct Ev { double sample; int square, hit; long long step; bool fired; float level; };   // for tests
  Ev log[256]; unsigned nlog = 0;
  const SourceDesc& desc() const override { return SEQ_DESC; }
  void ext(float v) override { const int c = (int)lrintf(v); if (c == 2) resetReq++; else if (c == 0 || c == 1) playReq = c; }

  // The square step n plays (n counts steps from the start of the resync period).
  int squareOf(const SourceContext& c, long long n, int steps, int dir, uint32_t seed, uint32_t period) const {
    const int len = dir == 2 ? (steps > 1 ? 2 * steps - 2 : 1) : steps;
    const long long cyc = n / len; const int j = (int)(n - cyc * len);
    int p = dir == 0 ? j : dir == 1 ? steps - 1 - j : dir == 2 ? (j < steps ? j : 2 * steps - 2 - j)
          : (int)(seqHash(seed, period, (uint32_t)cyc, (uint32_t)j) % (uint32_t)steps);
    return (p + (int)lrintf(clampf(knob(c, SK_SHIFT), 0, 31))) % steps;
  }
  bool active(const SourceContext& c, int sq, int steps) const {
    if (cell(c, sq, SC_ON) < 0.5f) return false;
    return setting(c, SS_PATTERN) < 0.5f || seqEuclid(sq, (int)lrintf(clampf(knob(c, SK_FILL), 0, (float)steps)), steps);
  }
  float update(const SourceContext& c) override {
    if (resetReq != resetSeen) { resetSeen = resetReq; beats = 0; inclusive = true; gateEnd = trigEnd = -1; }
    if (playReq >= 0) { playing = playReq == 1; playReq = -1; }
    const int steps = (int)lrintf(clampf(setting(c, SS_STEPS), 1, 32)), dir = (int)lrintf(clampf(setting(c, SS_DIR), 0, 3));
    const uint32_t seed = (uint32_t)lrintf(clampf(setting(c, SS_SEED), 0, 999));
    const double rate = onStop(knob(c, SK_RATE), STEP_STOPS, NSTEP_STOPS), sw = knob(c, SK_SWING) / 100.0, spb = 60.0 / c.bpm;      // beats per step, swing, seconds per beat
    const double offSteps = setting(c, SS_OFFSET) / 1000.0 / spb / rate;                           // OFFSET, in steps
    const double P = setting(c, SS_RESYNC) >= 0.5f ? (double)lrintf(setting(c, SS_RESYNC)) * 4.0 : 0.0;   // resync period, beats
    const double db = playing ? (double)c.n / c.sr / spb : 0.0, B0 = beats, B1 = beats + db;
    gateHit = trigHit = false;
    // one window per resync period the block touches: (u0, u1] in beats from that period's start
    auto window = [&](double pstart, double u0, double u1, bool incl, uint32_t period, bool inclEnd = true) {
      const double s0 = u0 / rate - offSteps, s1 = u1 / rate - offSteps;     // in steps, this block's knobs at both ends
      for (long long n = (long long)floor(s0) - 1; n <= (long long)floor(s1); n++) {
        if (n < 0) continue;
        const double start = n + ((n & 1) ? sw : 0.0), next = n + 1 + (((n + 1) & 1) ? sw : 0.0), span = next - start;
        const int sq = squareOf(c, n, steps, dir, seed, period);
        const int r = (int)lrintf(clampf(cell(c, sq, SC_RATCHET), 1, 4));
        for (int k = 0; k < r; k++) {
          const double t = start + span * k / r;
          if (!((t > s0 || (incl && t >= s0)) && (t < s1 || (inclEnd && t == s1)))) continue;
          const double at = pstart + (t + offSteps) * rate, off = db > 0 ? (at - B0) / db * c.n : 0.0;   // its sample in the block
          hit(c, sq, k, n, period, seed, off, span / r * rate * spb);
        }
      }
    };
    if (B1 > B0 || inclusive) {
      if (P > 0) {
        const double p0 = floor(B0 / P), p1 = floor(B1 / P);
        if (p1 > p0) { window(p0 * P, B0 - p0 * P, P, inclusive, (uint32_t)p0, false); window(p1 * P, 0.0, B1 - p1 * P, true, (uint32_t)p1); }   // a new period: step 1 again
        else window(p0 * P, B0 - p0 * P, B1 - p0 * P, inclusive, (uint32_t)p0);
      } else window(0.0, B0, B1, inclusive, 0);
    }
    inclusive = false; beats = B1;
    // where the playhead is now
    { const double pst = P > 0 ? floor(B1 / P) * P : 0.0, s = (B1 - pst) / rate - offSteps; const long long n = (long long)floor(s);
      if (n >= 0) { const double start = n + ((n & 1) ? sw : 0.0), next = n + 1 + (((n + 1) & 1) ? sw : 0.0);
        if (s < start) stepFrac = 0.f; else stepFrac = clampf((float)((s - start) / (next - start)), 0.f, 0.999f); } }
    // outputs at the end of the block
    const double end = now + c.n, t = (end - envStart) / c.sr, A = setting(c, SS_ATT) / 1000.0, D = setting(c, SS_DEC) / 1000.0 / 4.6;
    if (t >= 0) { if (t < A) env = envFrom + (envPeak - envFrom) * (float)(t / A); else env = envPeak * (float)exp(-(t - A) / D); }
    if (env < 1e-6f) env = 0.f;
    const float sl = knob(c, SK_SLEW) / 1000.f;
    slewed = sl > 0 ? slewed + (value - slewed) * (1.f - expf(-c.dt / sl)) : value;
    rawOut[0] = (gateHit || gateEnd >= end) ? 1.f : 0.f;     // a gate or trigger shorter than a block still shows for one block
    rawOut[1] = (trigHit || trigEnd >= end) ? 1.f : 0.f;
    rawOut[2] = env; rawOut[3] = vel; rawOut[4] = 0.5f * (slewed + 1.f); rawOut[5] = steps > 1 ? (float)cur / (steps - 1) : 0.f;
    now = end;
    const int o = (int)lrintf(clampf(setting(c, SS_OUT), 0, 5)), pl = (int)lrintf(clampf(setting(c, SS_POLAR), 0, 2));
    if (!started) { out = prevOut = o; polar = prevPolar = pl; started = true; }
    if (o != out && oxf >= 1.f) { prevOut = out; out = o; oxf = 0.f; }
    if (pl != polar && pxf >= 1.f) { prevPolar = polar; polar = pl; pxf = 0.f; }
    auto map = [](float x, int p) { return p == 0 ? 2.f * x - 1.f : p == 1 ? x : -x; };
    auto val = [&](int oo) { float v = map(rawOut[oo], polar); if (pxf < 1.f) v = map(rawOut[oo], prevPolar) + (v - map(rawOut[oo], prevPolar)) * pxf; return v; };
    float y = val(out); if (oxf < 1.f) y = val(prevOut) + (y - val(prevOut)) * oxf;
    const float fade = c.dt / 0.010f;
    if (oxf < 1.f) oxf = fminf(1.f, oxf + fade);
    if (pxf < 1.f) pxf = fminf(1.f, pxf + fade);
    return clampf(y, -1.f, 1.f);
  }
  // One hit of square `sq` (ratchet hit k of step n), `off` samples into this block, `secs` long.
  void hit(const SourceContext& c, int sq, int k, long long n, uint32_t period, uint32_t seed, double off, double secs) {
    const int steps = (int)lrintf(clampf(setting(c, SS_STEPS), 1, 32));
    if (k == 0) { cur = sq; value = cell(c, sq, SC_VALUE) / 100.f; }           // every step reached moves VALUE and STEP
    const float p = knob(c, SK_PROB) / 100.f * cell(c, sq, SC_PROB) / 100.f;
    const bool fire = active(c, sq, steps) && (float)(seqHash(seed, period, (uint32_t)n, (uint32_t)(k + 1) * 977u) >> 8) * (1.f / 16777216.f) < p;
    const float level = cell(c, sq, SC_VEL) / 100.f;
    log[nlog++ & 255] = {now + off, sq, k, n, fire, level};
    if (!fire) return;
    gateEnd = fmax(gateEnd, now + off + secs * knob(c, SK_GATE) / 100.f * cell(c, sq, SC_GATE) / 100.f * c.sr); gateHit = true;
    trigEnd = now + off + 0.005 * c.sr; trigHit = true;
    envFrom = setting(c, SS_RETRIG) >= 0.5f ? env : 0.f; envPeak = level; envStart = now + off;
    vel = level; fired++; lastFired = sq;
  }
  void meters(float* m) const override {
    m[0] = (float)(cur + 32 * cur + 1024 * (int)(stepFrac * 1024.f));
    m[1] = (float)(32u * (fired & 0x7FFFFu) + (unsigned)lastFired);
  }
  // For the panel: STEPS, then whether each square can play with this PATTERN and FILL (1/0).
  int view(const float* knobs, const float* cfg, float* o, int max) const override {
    auto S = [&](int i) { return SEQ_SETTINGS[i].map(cfg[i]); };
    const int steps = (int)lrintf(clampf(S(SS_STEPS), 1, 32)); const bool euclid = S(SS_PATTERN) >= 0.5f;
    const int hits = (int)lrintf(clampf(SEQ_PARAMS[SK_FILL].map(knobs[SK_FILL]), 0, (float)steps));
    int w = 0; if (w < max) o[w++] = (float)steps;
    for (int i = 0; i < steps && w < max; i++) o[w++] = (!euclid || seqEuclid(i, hits, steps)) ? 1.f : 0.f;
    return w;
  }
};

} // namespace rig

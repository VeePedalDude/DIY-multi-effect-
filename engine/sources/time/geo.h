// engine/sources/time/geo.h - GEO PULSE (the geometric pulse generator): a shape becomes a rhythm. A point travels round the
// vertices of a polygon, star, warped polygon, hyperbolic polygon or polyhedron, locked to the tempo;
// each vertex it reaches can fire an event (gate, trigger, envelope). How long each step lasts can
// follow the geometry: the angle turned, or the distance travelled (a true hyperbolic distance on
// the HYPERBOLIC shape), always scaled so the whole cycle keeps its length.
//
// Knobs (cross-modulatable):
//  RATE    beats per step (stops on note values; default 1/16). The cycle is steps x RATE, or LENGTH.
//  GATE    gate length, % of each step (default 50%).          PROB    chance each step fires (100%).
//  CURVE   HYPERBOLIC shape: how far the polygon reaches into the hyperbolic plane (0 = Euclidean).
//  WARP    vertex displacement (a seeded jitter; on WARPED, a smooth bunching of the spacing).
//  TIMING  UNIFORM -> ANGULAR -> GEODESIC, a continuous blend: equal steps, steps as long as the angle
//          turned round the centre, or as long as the edge travelled.
//  PHASE   where the cycle starts against the master clock, 0-360 degrees of the cycle.
//  DEPTH   3D: lifts a flat shape into a helix (the long way back becomes a long step); stretches a
//          polyhedron along its axis.
// Settings (not modulated): SHAPE, VERTICES 3-32, SKIP (star polygons; when SKIP and VERTICES share a
// factor the star splits into separate cycles: CYCLES plays ONE of them or ALL in turn), ROTATION (turns
// a solid before DEPTH stretches it, so it changes a solid's rhythm; a flat shape just turns),
// DIRECTION (forward, reverse, ping-pong, random), LENGTH (AUTO or a cycle length in beats: 7 steps
// in 1 bar is 7 against 4), SWING, OFFSET (ms), SEED, OUT (gate, trigger, envelope, velocity, phase,
// vertex), POLARITY, SMOOTH, RETRIGGER, ATTACK, DECAY, ACCENT, and TILT / TRAIL (the 3D view only).
// Table, one row per vertex: ON, PROB, VEL, ACCENT, GATE.
// Transport: `ext <id> 0` stops, `1` plays on, `2` goes back to the start (the first step fires
// at once). It plays from the moment it's added.
//
// Real-time: events are found by sample (each one's exact offset in the block) and only from time
// passing: the window (start, end] of the block is worked out with this block's knobs at both ends,
// so a knob that jumps moves the pattern but never fires or skips extra steps. Random choices
// (probability, random direction) come from a counter-based hash of (seed, cycle, step): the same
// seed plays the same pattern whatever the block size or knob history. The geometry is rebuilt only
// when what it depends on changes (measured in test 147).
// Meters: "step" (from vertex + 32 x to vertex + 1024 x position in the step, 0..1023) and "fired"
// (32 x events fired + the vertex): the 3D view draws from these, never from a clock of its own.
#pragma once
#include "../../core/source.h"
#include "../../core/notes.h"
#include <stdint.h>

namespace rig {

constexpr int GEO_MAXV = 32, GEO_MAXSTEPS = 2 * GEO_MAXV;

inline constexpr ParamStop GEO_TIMING_STOPS[] = {{0, "UNIFORM"}, {1, "ANGULAR"}, {2, "GEODESIC"}};
inline constexpr ParamDesc GEO_PARAMS[] = {
  {"rate",   "RATE",   0.125f, 16.f,  1.f / 7.f, "beats", Curve::Log, 2, false, STEP_STOPS, NSTEP_STOPS},   // 1/16
  {"gate",   "GATE",   1.f,    100.f, 49.f / 99.f, "%",   Curve::Lin, 0, false},                     // 50%
  {"prob",   "PROB",   0.f,    100.f, 1.f,       "%",     Curve::Lin, 0, false},
  {"curve",  "CURVE",  0.f,    100.f, 0.f,       "%",     Curve::Lin, 0, false},
  {"warp",   "WARP",   0.f,    100.f, 0.f,       "%",     Curve::Lin, 0, false},
  {"timing", "TIMING", 0.f,    2.f,   0.f,       "",      Curve::Lin, 2, false, GEO_TIMING_STOPS, 3},
  {"phase",  "PHASE",  0.f,    360.f, 0.f,       "deg",   Curve::Lin, 0, false},
  {"depth",  "DEPTH",  0.f,    100.f, 0.f,       "%",     Curve::Lin, 0, false},
};
enum { GK_RATE, GK_GATE, GK_PROB, GK_CURVE, GK_WARP, GK_TIMING, GK_PHASE, GK_DEPTH };

enum GeoShape { GEO_POLYGON, GEO_STAR, GEO_WARPED, GEO_HYPERBOLIC, GEO_TETRA, GEO_CUBE, GEO_OCTA, GEO_DODECA, GEO_ICOSA };
inline constexpr ParamStop GEO_SHAPES[] = {{0, "POLYGON"}, {1, "STAR"}, {2, "WARPED"}, {3, "HYPERBOLIC"}, {4, "TETRAHEDRON"},
  {5, "CUBE"}, {6, "OCTAHEDRON"}, {7, "DODECAHEDRON"}, {8, "ICOSAHEDRON"}};
inline constexpr ParamStop GEO_CYCLES[] = {{0, "ONE"}, {1, "ALL"}};
inline constexpr ParamStop GEO_DIRS[] = {{0, "FORWARD"}, {1, "REVERSE"}, {2, "PING-PONG"}, {3, "RANDOM"}};
inline constexpr ParamStop GEO_LENGTHS[] = {{0, "AUTO"}, {1, "1 beat"}, {2, "2 beats"}, {3, "3 beats"}, {4, "1 bar"}, {6, "6 beats"},
  {8, "2 bars"}, {12, "3 bars"}, {16, "4 bars"}, {32, "8 bars"}, {64, "16 bars"}};
inline constexpr ParamStop GEO_OUTS[] = {{0, "GATE"}, {1, "TRIGGER"}, {2, "ENVELOPE"}, {3, "VELOCITY"}, {4, "PHASE"}, {5, "VERTEX"}};
inline constexpr ParamStop GEO_POLARS[] = {{0, "-1 TO +1"}, {1, "0 TO +1"}, {2, "0 TO -1"}};
inline constexpr ParamStop GEO_RETRIGS[] = {{0, "RESTART"}, {1, "FROM LEVEL"}};
inline constexpr ParamDesc GEO_SETTINGS[] = {
  {"shape",   "SHAPE",     0.f,   8.f,    0.f,        "",   Curve::Lin, 0, false, GEO_SHAPES, 9},
  {"verts",   "VERTICES",  3.f,   32.f,   4.f / 29.f, "",   Curve::Lin, 0, false},             // 7
  {"skip",    "SKIP",      1.f,   16.f,   0.f,        "",   Curve::Lin, 0, false},             // 1
  {"rot",     "ROTATION",  0.f,   360.f,  0.f,        "deg", Curve::Lin, 0, false},
  {"cycles",  "CYCLES",    0.f,   1.f,    0.f,        "",   Curve::Lin, 0, false, GEO_CYCLES, 2},
  {"dir",     "DIRECTION", 0.f,   3.f,    0.f,        "",   Curve::Lin, 0, false, GEO_DIRS, 4},
  {"length",  "LENGTH",    0.f,   64.f,   0.f,        "beats", Curve::Lin, 0, false, GEO_LENGTHS, 11},
  {"swing",   "SWING",     0.f,   75.f,   0.f,        "%",  Curve::Lin, 0, false},
  {"offset",  "OFFSET",   -50.f,  50.f,   0.5f,       "ms", Curve::Lin, 1, true},
  {"seed",    "SEED",      0.f,   999.f,  1.f / 999.f, "",  Curve::Lin, 0, false},             // 1
  {"out",     "OUT",       0.f,   5.f,    0.f,        "",   Curve::Lin, 0, false, GEO_OUTS, 6},
  {"polar",   "POLARITY",  0.f,   2.f,    0.f,        "",   Curve::Lin, 0, false, GEO_POLARS, 3},
  {"smooth",  "SMOOTH",    0.f,   100.f,  0.f,        "ms", Curve::Lin, 0, false},
  {"retrig",  "RETRIGGER", 0.f,   1.f,    0.f,        "",   Curve::Lin, 0, false, GEO_RETRIGS, 2},
  {"attack",  "ATTACK",    0.5f,  1000.f, 0.18237f,   "ms", Curve::Log, 1, false},             // 2 ms
  {"decay",   "DECAY",     10.f,  5000.f, 0.48204f,   "ms", Curve::Log, 0, false},             // 200 ms
  {"accent",  "ACCENT",    0.f,   100.f,  0.2f,       "%",  Curve::Lin, 0, false},
  {"tilt",    "VIEW TILT", 0.f,   90.f,   35.f / 90.f, "deg", Curve::Lin, 0, false},           // the 3D view only
  {"trail",   "TRAIL",     0.f,   100.f,  0.5f,       "%",  Curve::Lin, 0, false},             // the 3D view only
};
enum { GS_SHAPE, GS_VERTS, GS_SKIP, GS_ROT, GS_CYCLES, GS_DIR, GS_LEN, GS_SWING, GS_OFFSET, GS_SEED, GS_OUT, GS_POLAR, GS_SMOOTH,
       GS_RETRIG, GS_ATT, GS_DEC, GS_ACCENT, GS_TILT, GS_TRAIL, GS_COUNT };
inline constexpr ParamStop GEO_ONOFF[] = {{0, "OFF"}, {1, "ON"}};
inline constexpr ParamDesc GEO_COLS[] = {
  {"on",     "ON",     0.f, 1.f,   1.f,  "",  Curve::Lin, 0, false, GEO_ONOFF, 2},
  {"prob",   "PROB",   0.f, 100.f, 1.f,  "%", Curve::Lin, 0, false},
  {"vel",    "VEL",    0.f, 100.f, 0.8f, "%", Curve::Lin, 0, false},
  {"accent", "ACCENT", 0.f, 1.f,   0.f,  "",  Curve::Lin, 0, false, GEO_ONOFF, 2},
  {"gate",   "GATE",   0.f, 200.f, 0.5f, "%", Curve::Lin, 0, false},                             // x the GATE knob
};
enum { GC_ON, GC_PROB, GC_VEL, GC_ACCENT, GC_GATE };
inline constexpr SourceDesc GEO_DESC = {"geo", "GEO PULSE", "shapes -> rhythm", "transport: 0 stop, 1 play, 2 back to the start", 2, {"step", "fired"},
  8, GEO_PARAMS, false, GS_COUNT, GEO_SETTINGS, GEO_MAXV, 5, GEO_COLS};

// Platonic solids, unit circumradius, vertices in the order of a Hamiltonian cycle along their edges
// (found by search; consecutive vertices share an edge, and so do the last and the first).
inline constexpr float GEO_POLY[][3] = {
  // TETRAHEDRON: 4 vertices in the order of a Hamiltonian cycle along its edges
  {0.577350269f, 0.577350269f, 0.577350269f}, {0.577350269f, -0.577350269f, -0.577350269f}, {-0.577350269f, 0.577350269f, -0.577350269f}, {-0.577350269f, -0.577350269f, 0.577350269f},
  // CUBE: 8 vertices in the order of a Hamiltonian cycle along its edges
  {-0.577350269f, -0.577350269f, -0.577350269f}, {-0.577350269f, -0.577350269f, 0.577350269f}, {-0.577350269f, 0.577350269f, 0.577350269f}, {-0.577350269f, 0.577350269f, -0.577350269f}, {0.577350269f, 0.577350269f, -0.577350269f}, {0.577350269f, 0.577350269f, 0.577350269f}, {0.577350269f, -0.577350269f, 0.577350269f}, {0.577350269f, -0.577350269f, -0.577350269f},
  // OCTAHEDRON: 6 vertices in the order of a Hamiltonian cycle along its edges
  {1.000000000f, 0.000000000f, 0.000000000f}, {0.000000000f, 1.000000000f, 0.000000000f}, {-1.000000000f, 0.000000000f, 0.000000000f}, {0.000000000f, 0.000000000f, 1.000000000f}, {0.000000000f, -1.000000000f, 0.000000000f}, {0.000000000f, 0.000000000f, -1.000000000f},
  // DODECAHEDRON: 20 vertices in the order of a Hamiltonian cycle along its edges
  {-0.577350269f, -0.577350269f, -0.577350269f}, {0.000000000f, -0.356822090f, -0.934172359f}, {0.577350269f, -0.577350269f, -0.577350269f}, {0.356822090f, -0.934172359f, 0.000000000f}, {0.577350269f, -0.577350269f, 0.577350269f}, {0.000000000f, -0.356822090f, 0.934172359f}, {0.000000000f, 0.356822090f, 0.934172359f}, {-0.577350269f, 0.577350269f, 0.577350269f}, {-0.356822090f, 0.934172359f, 0.000000000f}, {0.356822090f, 0.934172359f, 0.000000000f}, {0.577350269f, 0.577350269f, 0.577350269f}, {0.934172359f, 0.000000000f, 0.356822090f}, {0.934172359f, 0.000000000f, -0.356822090f}, {0.577350269f, 0.577350269f, -0.577350269f}, {0.000000000f, 0.356822090f, -0.934172359f}, {-0.577350269f, 0.577350269f, -0.577350269f}, {-0.934172359f, 0.000000000f, -0.356822090f}, {-0.934172359f, 0.000000000f, 0.356822090f}, {-0.577350269f, -0.577350269f, 0.577350269f}, {-0.356822090f, -0.934172359f, 0.000000000f},
  // ICOSAHEDRON: 12 vertices in the order of a Hamiltonian cycle along its edges
  {0.000000000f, -0.525731112f, -0.850650808f}, {-0.525731112f, -0.850650808f, 0.000000000f}, {-0.850650808f, 0.000000000f, -0.525731112f}, {-0.525731112f, 0.850650808f, 0.000000000f}, {-0.850650808f, 0.000000000f, 0.525731112f}, {0.000000000f, -0.525731112f, 0.850650808f}, {0.525731112f, -0.850650808f, 0.000000000f}, {0.850650808f, 0.000000000f, -0.525731112f}, {0.850650808f, 0.000000000f, 0.525731112f}, {0.000000000f, 0.525731112f, 0.850650808f}, {0.525731112f, 0.850650808f, 0.000000000f}, {0.000000000f, 0.525731112f, -0.850650808f},
};
inline constexpr int GEO_POLY_N[5] = {4, 8, 6, 20, 12}, GEO_POLY_AT[5] = {0, 4, 12, 18, 38};

// Counter-based random numbers: the same (seed, a, b, c) always gives the same value.
inline uint32_t geoHash(uint32_t seed, uint32_t a, uint32_t b, uint32_t c) {
  uint32_t x = seed * 0x9E3779B1u ^ a * 0x85EBCA77u ^ b * 0xC2B2AE3Du ^ c * 0x27D4EB2Fu;
  x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16; return x;
}
inline float geoRand01(uint32_t seed, uint32_t a, uint32_t b, uint32_t c) { return (float)(geoHash(seed, a, b, c) >> 8) * (1.f / 16777216.f); }
inline int geoGcd(int a, int b) { while (b) { int t = a % b; a = b; b = t; } return a; }

// What the geometry depends on (knobs quantised to 1/1024, so tiny modulation doesn't rebuild it).
struct GeoIn {
  int shape = 0, n = 7, skip = 1, cycles = 0, dir = 0; uint32_t seed = 1; int rot = 0, swing = 0, warp = 0, curve = 0, depth = 0, timing = 0;
  bool operator==(const GeoIn& o) const { return shape == o.shape && n == o.n && skip == o.skip && cycles == o.cycles && dir == o.dir && seed == o.seed
    && rot == o.rot && swing == o.swing && warp == o.warp && curve == o.curve && depth == o.depth && timing == o.timing; }
};

// A built geometry: vertex positions, the order the steps visit them, and when each step starts.
struct GeoShapeData {
  int n = 0, base = 0, steps = 0; bool hyper = false;   // vertices, steps in one pass of the order, steps in a cycle
  int skipK = 1, nCycles = 1, per = 1;                  // star: the step, how many separate cycles, vertices in each
  float x[GEO_MAXV], y[GEO_MAXV], z[GEO_MAXV];          // display / Euclidean positions (Poincare ball for HYPERBOLIC)
  float tx[GEO_MAXV], ty[GEO_MAXV], tz[GEO_MAXV];       // directions from the centre (for ANGULAR)
  int order[GEO_MAXV];                                  // one pass (star cycles included)
  int seq[GEO_MAXSTEPS];                                // this cycle's vertices, step by step
  double tau[GEO_MAXSTEPS + 1];                         // where each step starts in the cycle, 0..1 (tau[steps] = 1)
  unsigned forCycle = 0; bool ready = false;            // the cycle seq/tau were made for (it matters for RANDOM only)

  static GeoIn read(const float* k, const float* cfg) {   // knobs and settings, normalised -> what the geometry needs
    GeoIn g; auto S = [&](int i) { return GEO_SETTINGS[i].map(cfg ? cfg[i] : GEO_SETTINGS[i].def); };
    g.shape = (int)lrintf(clampf(S(GS_SHAPE), 0, 8)); g.n = (int)lrintf(clampf(S(GS_VERTS), 3, 32)); g.skip = (int)lrintf(clampf(S(GS_SKIP), 1, 16));
    g.cycles = (int)lrintf(clampf(S(GS_CYCLES), 0, 1)); g.dir = (int)lrintf(clampf(S(GS_DIR), 0, 3)); g.seed = (uint32_t)lrintf(clampf(S(GS_SEED), 0, 999));
    g.rot = (int)lrintf(clampf(S(GS_ROT), 0, 360)); g.swing = (int)lrintf(clampf(S(GS_SWING), 0, 75));
    auto q = [&](int i) { return (int)lrintf(clampf(k[i], 0.f, 1.f) * 1024.f); };
    g.warp = q(GK_WARP); g.curve = q(GK_CURVE); g.depth = q(GK_DEPTH); g.timing = q(GK_TIMING);
    return g;
  }
  // Vertex positions and the base order (control-rate work: runs only when GeoIn changes).
  void build(const GeoIn& g) {
    const bool solid = g.shape >= GEO_TETRA;
    n = solid ? GEO_POLY_N[g.shape - GEO_TETRA] : g.n; hyper = g.shape == GEO_HYPERBOLIC;
    const float warp = g.warp / 1024.f, depth = g.depth / 1024.f, rot = g.rot * (PI_F / 180.f);
    for (int i = 0; i < n; i++) {
      float vx, vy, vz;
      if (!solid) {
        const float u = (float)i / n; float th = rot + 2 * PI_F * u, r = 1.f;
        if (g.shape == GEO_WARPED) { th = rot + 2 * PI_F * u + 0.9f * warp * sinf(2 * PI_F * u); r = 1.f + 0.35f * warp * cosf(2 * PI_F * u); }
        else if (warp > 0) { th += warp * 0.45f * (2 * PI_F / n) * (2 * geoRand01(g.seed, i, 0, 7) - 1); r *= 1.f + warp * 0.6f * (2 * geoRand01(g.seed, i, 1, 7) - 1); }
        vx = r * cosf(th); vy = r * sinf(th); vz = n > 1 ? depth * 0.9f * (2.f * i / (n - 1) - 1.f) : 0.f;   // DEPTH: a helix
      } else {
        const float* p = GEO_POLY[GEO_POLY_AT[g.shape - GEO_TETRA] + i];
        const float c = cosf(rot), s = sinf(rot);                                  // ROTATION about the vertical (y) axis
        vx = c * p[0] + s * p[2]; vy = p[1]; vz = -s * p[0] + c * p[2];
        if (warp > 0) { const float m = 1.f + warp * 0.5f * (2 * geoRand01(g.seed, i, 1, 7) - 1);
          vx = vx * m + warp * 0.3f * (2 * geoRand01(g.seed, i, 2, 7) - 1); vy = vy * m + warp * 0.3f * (2 * geoRand01(g.seed, i, 3, 7) - 1); vz = vz * m + warp * 0.3f * (2 * geoRand01(g.seed, i, 4, 7) - 1); }
        vz *= 1.f + 2.f * depth;                                                   // DEPTH: stretched along its axis
      }
      const float len = sqrtf(vx * vx + vy * vy + vz * vz) + 1e-12f;
      tx[i] = vx / len; ty[i] = vy / len; tz[i] = vz / len;
      if (hyper) {                                    // CURVE: hyperbolic circumradius rho; the point at hyperbolic
        const float rho = 0.02f + 3.98f * g.curve / 1024.f, R = tanhf(0.5f * rho * len);   // distance rho*len from the centre
        x[i] = R * tx[i]; y[i] = R * ty[i]; z[i] = R * tz[i];
      } else { x[i] = vx; y[i] = vy; z[i] = vz; }
    }
    // the order of one pass: star polygons step by SKIP; with a common factor there are g separate cycles
    const int k = (solid ? 1 : (g.shape == GEO_STAR || g.shape == GEO_HYPERBOLIC || g.shape == GEO_WARPED ? g.skip : 1)) % n, kk = k ? k : 1;
    const int gcd = geoGcd(n, kk); per = n / gcd; skipK = kk; nCycles = gcd;
    base = g.cycles ? n : per;
    for (int j = 0; j < base; j++) order[j] = ((j / per) + (j % per) * kk) % n;
    steps = g.dir == 2 ? (base > 1 ? 2 * base - 2 : 1) : base;
    ready = false;
  }
  double dist(int a, int b) const {               // length of the edge a -> b
    const double dx = x[a] - x[b], dy = y[a] - y[b], dz = z[a] - z[b], e2 = dx * dx + dy * dy + dz * dz;
    if (!hyper) return sqrt(e2);
    const double na = 1.0 - ((double)x[a] * x[a] + (double)y[a] * y[a] + (double)z[a] * z[a]), nb = 1.0 - ((double)x[b] * x[b] + (double)y[b] * y[b] + (double)z[b] * z[b]);
    return 2.0 * asinh(sqrt(e2) / sqrt(fmax(na * nb, 1e-30)));   // Poincare ball metric: d = 2 asinh(|a-b| / sqrt((1-|a|^2)(1-|b|^2)))
  }
  double angle(int a, int b) const { return acos(fmax(-1.0, fmin(1.0, (double)tx[a] * tx[b] + (double)ty[a] * ty[b] + (double)tz[a] * tz[b]))); }
  // This cycle's steps (DIRECTION) and their start times (TIMING, SWING): cheap, per cycle for RANDOM.
  void sequence(const GeoIn& g, unsigned cyc) {
    if (ready && (g.dir != 3 || forCycle == cyc)) return;
    for (int j = 0; j < steps; j++) {
      if (g.dir == 0) seq[j] = order[j];
      else if (g.dir == 1) seq[j] = order[base - 1 - j];
      else if (g.dir == 2) seq[j] = order[j < base ? j : 2 * base - 2 - j];
      else seq[j] = order[geoHash(g.seed, cyc, j, 3) % (unsigned)base];
    }
    const float t = g.timing / 1024.f * 2.f;
    double w[GEO_MAXSTEPS], wa[GEO_MAXSTEPS], wg[GEO_MAXSTEPS], sa = 0, sg = 0;
    for (int j = 0; j < steps; j++) { const int a = seq[j], b = seq[(j + 1) % steps]; wa[j] = angle(a, b); wg[j] = dist(a, b); sa += wa[j]; sg += wg[j]; }
    double sum = 0;
    for (int j = 0; j < steps; j++) {
      const double u = 1.0 / steps, an = sa > 0 ? wa[j] / sa : u, ge = sg > 0 ? wg[j] / sg : u;
      w[j] = t <= 1.f ? u + (an - u) * t : an + (ge - an) * (t - 1.f);
      w[j] = fmax(w[j], 0.05 / steps);              // a step never vanishes (the same vertex twice in RANDOM)
      sum += w[j];
    }
    double acc = 0; for (int j = 0; j < steps; j++) { tau[j] = acc / sum; acc += w[j]; }
    tau[steps] = 1.0;
    const float sw = g.swing / 100.f;                 // SWING: every second step starts late, by a share of the one before
    for (int j = 1; j < steps; j += 2) tau[j] += sw * (tau[j] - tau[j - 1]);   // (late by SWING x the step before)
    forCycle = cyc; ready = true;
  }
};

struct GeoPulse : Source {
  GeoShapeData geo; GeoIn have; bool built = false;
  double beats = 0, now = 0;                              // beats since the start; samples since init (block start)
  bool playing = true, inclusive = true; int resetReq = 0, resetSeen = 0, playReq = -1;
  double gateEnd = -1, trigEnd = -1, envStart = -1e18; bool gateHit = false, trigHit = false;
  float env = 0, envFrom = 0, envPeak = 0, vel = 0, phaseU = 0, stepFrac = 0;
  int active = 0, next = 0, lastFired = 0; unsigned fired = 0;
  int out = 0, prevOut = 0, polar = 0, prevPolar = 0; float oxf = 1, pxf = 1, smoothY = 0; bool smoothInit = false;
  float rawOut[6] = {0};
  // a small log of the latest steps, for tests: absolute sample, vertex, step, cycle, fired, level
  struct Ev { double sample; int vertex, step; unsigned cycle; bool fired; float level; };
  Ev log[256]; unsigned nlog = 0;
  const SourceDesc& desc() const override { return GEO_DESC; }

  void ext(float v) override {                            // transport (control thread): 0 stop, 1 play, 2 back to the start
    const int c = (int)lrintf(v);
    if (c == 2) resetReq++; else if (c == 0 || c == 1) playReq = c;
  }
  double cycleBeats(const SourceContext& c) const {
    const float len = setting(c, GS_LEN); const int L = (int)lrintf(len);
    return L >= 1 ? (double)L : (double)geo.steps * onStop(knob(c, GK_RATE), STEP_STOPS, NSTEP_STOPS);
  }
  void event(const SourceContext& c, unsigned k, int j, double off, double C) {
    const int v = geo.seq[j];
    const bool on = cell(c, v, GC_ON) >= 0.5f;
    const float p = knob(c, GK_PROB) / 100.f * cell(c, v, GC_PROB) / 100.f;
    const bool fire = on && geoRand01((uint32_t)lrintf(setting(c, GS_SEED)), k, (uint32_t)j, 1) < p;
    float level = cell(c, v, GC_VEL) / 100.f;
    if (cell(c, v, GC_ACCENT) >= 0.5f) level = fminf(1.f, level + setting(c, GS_ACCENT) / 100.f);
    log[nlog++ & 255] = {now + off, v, j, k, fire, level};
    if (!fire) return;
    const double stepSec = (geo.tau[j + 1] - geo.tau[j]) * C * 60.0 / c.bpm;
    const double gate = stepSec * knob(c, GK_GATE) / 100.f * cell(c, v, GC_GATE) / 100.f;
    gateEnd = fmax(gateEnd, now + off + gate * c.sr); gateHit = true;
    trigEnd = now + off + 0.005 * c.sr; trigHit = true;
    envFrom = setting(c, GS_RETRIG) >= 0.5f ? env : 0.f; envPeak = level; envStart = now + off;
    vel = level; fired++; lastFired = v;
  }
  float update(const SourceContext& c) override {
    const GeoIn in = GeoShapeData::read(c.p, c.cfg);
    if (!built || !(in == have)) { geo.build(in); have = in; built = true; }
    if (resetReq != resetSeen) { resetSeen = resetReq; beats = 0; inclusive = true; gateEnd = trigEnd = -1; }
    if (playReq >= 0) { playing = playReq == 1; playReq = -1; }
    const double C = cycleBeats(c);
    const double db = playing ? (double)c.n * c.bpm / 60.0 / c.sr : 0.0;
    const double phi = knob(c, GK_PHASE) / 360.0 - setting(c, GS_OFFSET) / 1000.0 * c.bpm / 60.0 / C;
    const double x0 = beats / C + phi, x1 = (beats + db) / C + phi;   // this block's window, with this block's knobs
    gateHit = trigHit = false;
    if (x1 > x0 || inclusive) {
      const double k0 = floor(x0), k1 = floor(x1);
      for (double kd = fmax(k0, k1 - 2); kd <= k1; kd += 1.0) {       // at most 3 cycles in one block
        const unsigned k = (unsigned)(long long)kd;
        geo.sequence(in, k);
        for (int j = 0; j < geo.steps; j++) {
          const double t = kd + geo.tau[j];
          if ((t > x0 || (inclusive && t >= x0)) && t <= x1) event(c, k, j, x1 > x0 ? (t - x0) / (x1 - x0) * c.n : 0.0, C);
        }
      }
    }
    inclusive = false;
    beats += db;
    // where the travelling point is now: this cycle, its step, and how far into it
    const double kd1 = floor(x1); const unsigned k1 = (unsigned)(long long)kd1;
    geo.sequence(in, k1);
    const double u1 = x1 - kd1; phaseU = (float)u1; int j = 0; while (j + 1 < geo.steps && geo.tau[j + 1] <= u1) j++;
    active = geo.seq[j];
    if (j + 1 < geo.steps) next = geo.seq[j + 1];
    else next = in.dir == 3 ? geo.order[geoHash(in.seed, k1 + 1, 0, 3) % (unsigned)geo.base] : geo.seq[0];
    stepFrac = clampf((float)((u1 - geo.tau[j]) / fmax(geo.tau[j + 1] - geo.tau[j], 1e-12)), 0.f, 0.999f);
    // envelope at the end of the block, timed from the event's own sample
    const double end = now + c.n, t = (end - envStart) / c.sr, A = setting(c, GS_ATT) / 1000.0, D = setting(c, GS_DEC) / 1000.0 / 4.6;
    if (t < 0) {} else if (t < A) env = envFrom + (envPeak - envFrom) * (float)(t / A); else env = envPeak * (float)exp(-(t - A) / D);
    if (env < 1e-6f) env = 0.f;
    rawOut[0] = (gateHit || gateEnd >= end) ? 1.f : 0.f;     // a gate or trigger shorter than a block still shows for one block
    rawOut[1] = (trigHit || trigEnd >= end) ? 1.f : 0.f;
    rawOut[2] = env; rawOut[3] = vel; rawOut[4] = phaseU; rawOut[5] = geo.n > 1 ? (float)active / (geo.n - 1) : 0.f;
    now = end;
    // OUT and POLARITY crossfade over 10 ms when changed; then SMOOTH
    const float fade = c.dt / 0.010f;
    const int o = (int)lrintf(clampf(setting(c, GS_OUT), 0, 5)), pl = (int)lrintf(clampf(setting(c, GS_POLAR), 0, 2));
    if (!smoothInit) { out = prevOut = o; polar = prevPolar = pl; }              // the first block starts as set: nothing to fade from
    if (o != out && oxf >= 1.f) { prevOut = out; out = o; oxf = 0.f; }
    if (pl != polar && pxf >= 1.f) { prevPolar = polar; polar = pl; pxf = 0.f; }
    auto map = [](float x, int p) { return p == 0 ? 2.f * x - 1.f : p == 1 ? x : -x; };
    auto val = [&](int oo) { float v = map(rawOut[oo], polar); if (pxf < 1.f) v = map(rawOut[oo], prevPolar) + (v - map(rawOut[oo], prevPolar)) * pxf; return v; };
    float y = val(out); if (oxf < 1.f) y = val(prevOut) + (y - val(prevOut)) * oxf;
    if (oxf < 1.f) oxf = fminf(1.f, oxf + fade);
    if (pxf < 1.f) pxf = fminf(1.f, pxf + fade);
    const float sm = setting(c, GS_SMOOTH) / 1000.f;
    if (!smoothInit) { smoothY = y; smoothInit = true; }
    smoothY = sm > 0 ? smoothY + (y - smoothY) * (1.f - expf(-c.dt / sm)) : y;
    return clampf(smoothY, -1.f, 1.f);
  }
  void meters(float* m) const override {
    m[0] = (float)(active + 32 * next + 1024 * (int)(stepFrac * 1024.f));
    m[1] = (float)(32u * (fired & 0x7FFFFu) + (unsigned)lastFired);
  }
  // The 3D view: vertices (display positions) and the edges to draw, for these knobs and settings.
  // out: n, hyperbolic (0/1), n x (x, y, z), edge count, edge pairs. Pure: reads nothing but its inputs.
  int view(const float* knobs, const float* cfg, float* o, int max) const override {
    GeoShapeData g; const GeoIn in = GeoShapeData::read(knobs, cfg); g.build(in);
    int w = 0; auto put = [&](float v) { if (w < max) o[w] = v; w++; };
    put((float)g.n); put(g.hyper ? 1.f : 0.f);
    for (int i = 0; i < g.n; i++) { put(g.x[i]); put(g.y[i]); put(g.z[i]); }
    const int at = w; put(0); int ne = 0;
    if (in.shape >= GEO_TETRA) {                       // a solid: its real edges (pairs at the edge length, before warping)
      const float* P = GEO_POLY[GEO_POLY_AT[in.shape - GEO_TETRA]]; float e2 = 1e9f;
      for (int a = 1; a < g.n; a++) { const float dx = P[0] - P[3 * a], dy = P[1] - P[3 * a + 1], dz = P[2] - P[3 * a + 2]; e2 = fminf(e2, dx * dx + dy * dy + dz * dz); }
      for (int a = 0; a < g.n; a++) for (int b = a + 1; b < g.n; b++) {
        const float dx = P[3 * a] - P[3 * b], dy = P[3 * a + 1] - P[3 * b + 1], dz = P[3 * a + 2] - P[3 * b + 2];
        if (fabsf(dx * dx + dy * dy + dz * dz - e2) < 1e-3f) { put((float)a); put((float)b); ne++; }
      }
    } else for (int cy = 0; cy < (in.cycles ? g.nCycles : 1); cy++) for (int m = 0; m < g.per; m++) {   // a flat shape: its lines,
      put((float)((cy + m * g.skipK) % g.n)); put((float)((cy + (m + 1) * g.skipK) % g.n)); ne++;            // each star cycle closed
    }
    if (at < max) o[at] = (float)ne;
    return w <= max ? w : max;
  }
};

} // namespace rig

// engine/sources/chaos/orbit.h - THREE-BODY: three gravitating bodies, read out as one value -1..1.
//  SPEED  how fast simulated time runs when SYNC is FREE (default 1x).
//  ORBIT  CLASSIC (default: a figure-eight nudged off its path, so it wanders chaotically, as before)
//         or one of eleven known periodic solutions, each with its own masses and starting conditions:
//         FIGURE-8 (Moore 1993), BUTTERFLY I, BUTTERFLY III, MOTH I, GOGGLES, YIN-YANG, DRAGONFLY,
//         BUMBLEBEE, YARN (Suvakov & Dmitrasinovic 2013), HEAVY THIRD (third body 2x) and LIGHT THIRD
//         (third body 0.5x) (Li, Jing & Liao 2018). Each repeats exactly: the path is a loop.
//  OUT    what is read: DISTANCE between bodies 1 and 2 (default), BODY X (body 1 left-right), SPEED
//         (body 1), CLOSENESS (rises to +1 when any two bodies nearly collide).
//  RATE   beats per orbit when SYNC is on (stops on note values; default 8 beats).
//  SYNC   FREE (SPEED) or SYNC (one orbit = RATE): the physics scales, so any orbit fits the tempo.
//  CHAOS  0% repeats the orbit exactly; more nudges each new cycle off the exact start, so cycles
//         vary (most on the unstable orbits); 100% stops re-starting it, and it runs free into chaos.
// Changing ORBIT or OUT, and the start of a nudged cycle, crossfade over 30 ms: no jumps.
// Defaults (CLASSIC, DISTANCE, FREE) reproduce the source before these knobs existed, bit for bit.
//
// The catalogued orbits are integrated in double with a 4th-order (Yoshida) logarithmic-Hamiltonian
// leapfrog (Mikkola & Tanikawa 1999): fixed steps in a regularised time, so near-collisions get tiny
// physical steps on their own. Each orbit's starting velocities and step were refined (Newton
// shooting, tools/orbits/refine_orbits.cpp) so that exactly ORBIT_STEPS steps close the loop to
// ~1e-12; the published values (5-10 digits) alone drift off unstable orbits within a cycle or two.
// Meters: "period" (seconds per orbit now), "cycles".
#pragma once
#include "../../core/source.h"
#include "../../core/notes.h"

namespace rig {

// A periodic orbit. Bodies 1 and 2 (mass 1) start at (-1, 0) and (1, 0) moving at (p1, p2); body 3
// (mass m3) at the origin moving at -2 (p1, p2) / m3. G = 1. `ds` is the regularised step that closes
// it in ORBIT_STEPS steps; T its period; lo/hi the range of each OUT quantity (distance 1-2, body 1's x,
// sqrt of body 1's speed, -log of the closest pair's distance): 1st to 99th percentile of the time spent,
// so the brief spikes of a near-collision clip at +1 rather than squash everything else.
struct OrbitDef { const char* name; double m3, p1, p2, ds, T; float lo[4], hi[4]; };
constexpr int ORBIT_STEPS = 6000, NORBITS = 11;
inline constexpr OrbitDef ORBITS[NORBITS] = {
  {"FIGURE-8", 1.0, 0.34711688811946956, 0.53272494538790671, 0.0027141165160600262, 6.3259139829, {0.690857f, -1.057451f, 0.683629f, 0.005085f}, {1.999690f, 1.057442f, 1.127661f, 0.370240f}},
  {"BUTTERFLY I", 1.0, 0.30689339298600221, 0.1255065287995753, 0.0045101503803457402, 6.2346745039, {1.331994f, -1.042062f, 0.285132f, 0.014354f}, {1.999705f, -0.479689f, 1.947490f, 3.150705f}},
  {"BUTTERFLY III", 1.0, 0.40591556685896335, 0.23016303852971842, 0.0085364736867460551, 13.8671221734, {1.548560f, -1.080170f, 0.257699f, -0.030059f}, {1.998390f, -0.478749f, 1.706006f, 2.552058f}},
  {"MOTH I", 1.0, 0.46444517611180752, 0.39606001405263408, 0.0068627072177795375, 14.8943053072, {1.456880f, -1.113341f, 0.413338f, -0.367915f}, {1.998282f, 0.276267f, 1.525268f, 2.126251f}},
  {"GOGGLES", 1.0, 0.083300192196996761, 0.12788899212168578, 0.0084769336961282478, 10.4648487432, {0.773852f, -1.048720f, 0.362018f, 0.006739f}, {2.091947f, -0.214086f, 1.986920f, 2.951257f}},
  {"YIN-YANG", 1.0, 0.51393865433313324, 0.30473592350768036, 0.0082543577980191719, 17.3288407678, {1.490473f, -1.273180f, 0.271480f, -0.352325f}, {2.708646f, -0.074656f, 1.465979f, 1.921748f}},
  {"DRAGONFLY", 1.0, 0.080585260084010873, 0.58883600486195986, 0.010213096254653421, 21.2723420963, {0.102072f, -1.026023f, 0.724184f, -0.256182f}, {2.021199f, 0.164193f, 1.774644f, 2.282320f}},
  {"BUMBLEBEE", 1.0, 0.18427318076498228, 0.58719600803334493, 0.028881989636932691, 63.5358872513, {0.141578f, -1.032239f, 0.675891f, -0.328173f}, {2.158893f, 0.768830f, 1.630013f, 1.955028f}},
  {"YARN", 1.0, 0.5590583925715874, 0.3492177772752047, 0.022137233256691674, 55.5048811102, {1.140219f, -1.160686f, 0.328151f, -0.541652f}, {2.763675f, 1.441565f, 1.451652f, 1.679979f}},
  {"HEAVY THIRD", 2.0, 0.3057224376828499, 0.521512470576452, 0.011085861765375194, 8.8237071690, {0.794239f, -1.021305f, 0.181347f, -0.035981f}, {1.998764f, -0.142211f, 2.203215f, 2.397272f}},
  {"LIGHT THIRD", 0.5, 0.20096562233544515, 0.24310763230117291, 0.0063540143089525882, 19.0134163279, {0.397050f, -1.028117f, 0.337127f, -0.482527f}, {1.998218f, 1.030046f, 1.271560f, 1.890557f}},
};

// Yoshida's 4th-order composition: drift weights C, kick weights D (w1 = 1/(2 - 2^(1/3)), w0 = -2^(1/3) w1).
inline constexpr double ORBIT_C[4] = {0.67560359597982889, -0.17560359597982886, -0.17560359597982886, 0.67560359597982889};
inline constexpr double ORBIT_D[3] = {1.3512071919596578, -1.7024143839193153, 1.3512071919596578};

// One catalogued orbit in flight (double precision, no softening).
struct OrbitSim {
  double x[3], y[3], vx[3], vy[3], m[3], bind = 1; int def = 0, n = 0;
  double x0[3], y0[3], vx0[3], vy0[3], lastDt = 0, ahead = 0;   // the step before, and how far the last step overshot real time
  void seat(int d, double dp1 = 0, double dp2 = 0) {          // the orbit's start, optionally nudged
    const OrbitDef& o = ORBITS[d]; def = d; n = 0;
    const double p1 = o.p1 + dp1, p2 = o.p2 + dp2;
    m[0] = m[1] = 1; m[2] = o.m3;
    x[0] = -1; x[1] = 1; x[2] = 0; y[0] = y[1] = y[2] = 0;
    vx[0] = vx[1] = p1; vy[0] = vy[1] = p2; vx[2] = -2 * p1 / o.m3; vy[2] = -2 * p2 / o.m3;
    bind = pot() - kin();                                       // -energy, constant along the orbit
    keep(); lastDt = 0; ahead = 0;
  }
  void keep() { for (int a = 0; a < 3; a++) { x0[a] = x[a]; y0[a] = y[a]; vx0[a] = vx[a]; vy0[a] = vy[a]; } }
  double kin() const { double k = 0; for (int a = 0; a < 3; a++) k += 0.5 * m[a] * (vx[a] * vx[a] + vy[a] * vy[a]); return k; }
  double pot() const { double u = 0; for (int a = 0; a < 3; a++) for (int b = a + 1; b < 3; b++) u += m[a] * m[b] / sqrt((x[b] - x[a]) * (x[b] - x[a]) + (y[b] - y[a]) * (y[b] - y[a])); return u; }
  // One regularised step; returns the physical time it covered.
  double step(double h) {
    keep(); double t = 0;
    for (int k = 0; k < 4; k++) {
      const double dt = ORBIT_C[k] * h / (kin() + bind);
      for (int a = 0; a < 3; a++) { x[a] += dt * vx[a]; y[a] += dt * vy[a]; }
      t += dt;
      if (k == 3) break;
      double ax[3] = {0, 0, 0}, ay[3] = {0, 0, 0}, u = 0;
      for (int a = 0; a < 3; a++) for (int b = a + 1; b < 3; b++) {
        const double dx = x[b] - x[a], dy = y[b] - y[a], r2 = dx * dx + dy * dy, r = sqrt(r2), i3 = 1.0 / (r2 * r);
        u += m[a] * m[b] / r; ax[a] += m[b] * dx * i3; ay[a] += m[b] * dy * i3; ax[b] -= m[a] * dx * i3; ay[b] -= m[a] * dy * i3;
      }
      const double dk = ORBIT_D[k] * h / u;
      for (int a = 0; a < 3; a++) { vx[a] += dk * ax[a]; vy[a] += dk * ay[a]; }
    }
    n++; lastDt = t;
    return t;
  }
  // OUT quantity q (0 distance 1-2, 1 body 1's x, 2 sqrt speed of body 1, 3 -log closest pair), raw.
  static double quantity(int q, const double* x, const double* y, const double* vx, const double* vy) {
    if (q == 0) return sqrt((x[0] - x[1]) * (x[0] - x[1]) + (y[0] - y[1]) * (y[0] - y[1]));
    if (q == 1) return x[0];
    if (q == 2) return sqrt(sqrt(vx[0] * vx[0] + vy[0] * vy[0]));
    double c = 1e9;
    for (int a = 0; a < 3; a++) for (int b = a + 1; b < 3; b++) c = fmin(c, (x[b] - x[a]) * (x[b] - x[a]) + (y[b] - y[a]) * (y[b] - y[a]));
    return -0.5 * log(c + 1e-30);
  }
  double quantity(int q) const { return quantity(q, x, y, vx, vy); }
  // -1..1 over the orbit's own range, at the exact time asked for: between the last two steps.
  float out(int q) const {
    const OrbitDef& o = ORBITS[def];
    double v = quantity(q);
    if (lastDt > 0 && ahead > 0) { const double w = fmax(0.0, 1.0 - ahead / lastDt); const double v0 = quantity(q, x0, y0, vx0, vy0); v = v0 + (v - v0) * w; }
    return clampf((float)(2.0 * (v - o.lo[q]) / (o.hi[q] - o.lo[q]) - 1.0), -1.f, 1.f);
  }
  bool escaped() const {                                        // flung apart (only possible when nudged)
    const double cx = (m[0] * x[0] + m[1] * x[1] + m[2] * x[2]) / (m[0] + m[1] + m[2]), cy = (m[0] * y[0] + m[1] * y[1] + m[2] * y[2]) / (m[0] + m[1] + m[2]);
    for (int a = 0; a < 3; a++) if (fabs(x[a] - cx) > 8 || fabs(y[a] - cy) > 8 || !(x[a] == x[a])) return true;
    return false;
  }
};

inline constexpr ParamStop ORBIT_NAMES[] = {
  {0, "CLASSIC"}, {1, "FIGURE-8"}, {2, "BUTTERFLY I"}, {3, "BUTTERFLY III"}, {4, "MOTH I"}, {5, "GOGGLES"}, {6, "YIN-YANG"},
  {7, "DRAGONFLY"}, {8, "BUMBLEBEE"}, {9, "YARN"}, {10, "HEAVY THIRD"}, {11, "LIGHT THIRD"}};
inline constexpr ParamStop ORBIT_OUTS[] = {{0, "DISTANCE"}, {1, "BODY X"}, {2, "SPEED"}, {3, "CLOSENESS"}};

inline constexpr ParamDesc ORBIT_PARAMS[] = {
  {"speed", "SPEED", 0.1f, 10.f,  0.5f, "x",     Curve::Log, 2, false},   // 1x
  {"orbit", "ORBIT", 0.f,  11.f,  0.f,  "",      Curve::Lin, 0, false, ORBIT_NAMES, 12},
  {"out",   "OUT",   0.f,  3.f,   0.f,  "",      Curve::Lin, 0, false, ORBIT_OUTS, 4},
  {"rate",  "RATE",  0.5f, 128.f, 0.5f, "beats", Curve::Log, 1, false, NOTE_STOPS, NNOTE_STOPS},   // 8 beats per orbit
  {"sync",  "SYNC",  0.f,  1.f,   0.f,  "",      Curve::Lin, 0, false, SYNC_STOPS, 2},
  {"chaos", "CHAOS", 0.f,  100.f, 0.f,  "%",     Curve::Lin, 0, false},
};
inline constexpr SourceDesc ORBIT_DESC = {"orbit", "THREE-BODY", "chaotic or periodic orbits", nullptr, 2, {"period", "cycles"}, 6, ORBIT_PARAMS};

struct Orbit : Source {
  // CLASSIC: the original simulation, unchanged (float, softened, 2 ms steps)
  float px[3], py[3], vx[3], vy[3];
  // catalogued orbits: the one playing, and the one fading out (after a change, or a nudged restart)
  OrbitSim cur, old; float xf = 1.f;                             // xf: 0..1 crossfade from `old` (or CLASSIC) to `cur`
  int sel = 0, prevSel = 0, outQ = 0, prevOut = 0; float oxf = 1.f;
  double owe = 0, oldOwe = 0; unsigned cycles = 0, seed = 0x51ED270Bu; float period = 0;
  const SourceDesc& desc() const override { return ORBIT_DESC; }
  void init(float s) override { sr = s; resetBodies(); cur.seat(0); old.seat(0); }
  void resetBodies() {
    const float x0 = 0.97000436f, y0 = -0.24308753f, vx3 = -0.93240737f, vy3 = -0.86473146f;
    px[0] = -x0 + 0.02f; py[0] = -y0; px[1] = x0; py[1] = y0; px[2] = 0; py[2] = 0;
    vx[2] = vx3; vy[2] = vy3; vx[0] = vx[1] = -vx3 / 2; vy[0] = vy[1] = -vy3 / 2;
  }
  void step(float dt) {
    float ax[3] = {0}, ay[3] = {0};
    for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) if (a != b) {
      float dx = px[b] - px[a], dy = py[b] - py[a], r2 = dx * dx + dy * dy + 0.0025f;
      float inv = 1.f / (r2 * sqrtf(r2));
      ax[a] += dx * inv; ay[a] += dy * inv;
    }
    for (int a = 0; a < 3; a++) { vx[a] += ax[a] * dt; vy[a] += ay[a] * dt; }
    for (int a = 0; a < 3; a++) { px[a] += vx[a] * dt; py[a] += vy[a] * dt; }
    float cx = (px[0] + px[1] + px[2]) / 3, cy = (py[0] + py[1] + py[2]) / 3;
    for (int a = 0; a < 3; a++) if (fabsf(px[a] - cx) > 4 || fabsf(py[a] - cy) > 4) { resetBodies(); break; }
  }
  float classic(int q) const {                                   // CLASSIC read out (DISTANCE is the original mapping)
    if (q == 0) { float dx = px[0] - px[1], dy = py[0] - py[1]; return tanhf((sqrtf(dx * dx + dy * dy) - 1.2f) * 1.5f); }
    if (q == 1) return tanhf(px[0]);
    if (q == 2) return tanhf((sqrtf(sqrtf(vx[0] * vx[0] + vy[0] * vy[0])) - 1.f) * 2.f);
    float c = 1e9f; for (int a = 0; a < 3; a++) for (int b = a + 1; b < 3; b++) c = fminf(c, (px[b] - px[a]) * (px[b] - px[a]) + (py[b] - py[a]) * (py[b] - py[a]));
    return tanhf(-0.5f * logf(c + 1e-12f) - 0.5f);
  }
  float rnd() { seed = seed * 1664525u + 1013904223u; return (float)(seed >> 8) * (2.f / 16777215.f) - 1.f; }
  // Run a catalogued orbit `s` for `t` simulated seconds (carrying the remainder in `owe`). At the end of
  // each cycle it starts again: exactly (CHAOS 0), nudged with a crossfade, or not at all (100%).
  void run(OrbitSim& s, double& debt, double t, float chaos, bool main) {
    debt += t;
    for (int guard = 0; debt > 0 && guard < 4096; guard++) {
      debt -= s.step(ORBITS[s.def].ds);
      if (s.n >= ORBIT_STEPS) {
        s.n = 0; if (main) cycles++;
        if (!main || chaos >= 1.f || xf < 1.f) continue;        // free-running (or mid-fade): the bodies carry on
        if (chaos <= 0.f) { s.seat(s.def); continue; }          // closes to ~1e-12: nothing to hear
        restart(s.def, chaos);
        return;                                                 // the new cycle starts next block
      }
    }
    s.ahead = debt < 0 ? -debt : 0;
    if (main && xf >= 1.f && s.escaped()) restart(s.def, 0.f);
  }
  void restart(int d, float chaos) {                              // fade from what's playing to a fresh start
    old = cur; oldOwe = owe; prevSel = sel; xf = 0.f;
    const double a = (double)chaos * chaos * 0.02;               // up to 0.02 off the start velocities
    cur.seat(d, a * rnd(), a * rnd()); owe = 0;
  }

  float update(const SourceContext& c) override {
    const int s = (int)lrintf(clampf(knob(c, 1), 0.f, 11.f)), q = (int)lrintf(clampf(knob(c, 2), 0.f, 3.f));
    const bool sync = c.p[4] >= 0.5f;
    const float chaos = knob(c, 5) / 100.f, fade = c.dt / 0.030f;
    if (q != outQ && oxf >= 1.f) { prevOut = outQ; outQ = q; oxf = 0.f; }
    if (s != sel && xf >= 1.f) {                                 // a new orbit: fade over from the old one
      old = cur; oldOwe = owe; prevSel = sel; sel = s; xf = 0.f;
      if (s > 0) { cur.seat(s - 1); owe = 0; }
    }
    // CLASSIC runs while it's playing or fading out
    if (sel == 0 || (prevSel == 0 && xf < 1.f)) {
      if (!sync) { const float span = c.dt * knob(c, 0);         // SPEED scales simulated time
        for (float acc = 0; acc < span; acc += 0.002f) step(0.002f); }
      else { const float span = c.dt * (float)ORBITS[0].T / (knob(c, 3) * 60.f / c.bpm);
        for (float acc = 0; acc < span; acc += 0.002f) step(0.002f); }
    }
    // the catalogued orbit(s): simulated time per real second, from SPEED or from the tempo
    auto scale = [&](int d) { return sync ? ORBITS[d].T / (knob(c, 3) * 60.0 / c.bpm) : (double)knob(c, 0); };
    if (sel > 0) run(cur, owe, c.dt * scale(cur.def), chaos, true);
    if (xf < 1.f && prevSel > 0) run(old, oldOwe, c.dt * scale(old.def), 1.f, false);
    period = sel > 0 ? (float)(ORBITS[cur.def].T / scale(cur.def)) : (float)(ORBITS[0].T / (sync ? scale(0) : (double)knob(c, 0)));
    // read out, with the OUT and ORBIT crossfades
    auto now = [&](int qq) { return sel == 0 ? classic(qq) : cur.out(qq); };
    auto was = [&](int qq) { return prevSel == 0 ? classic(qq) : old.out(qq); };
    float v = now(outQ);
    if (oxf < 1.f) v = now(prevOut) + (v - now(prevOut)) * oxf;
    if (xf < 1.f) { float w = was(outQ); if (oxf < 1.f) w = was(prevOut) + (w - was(prevOut)) * oxf; v = w + (v - w) * xf; }
    if (xf < 1.f) xf = fminf(1.f, xf + fade);
    if (oxf < 1.f) oxf = fminf(1.f, oxf + fade);
    return v;
  }
  void meters(float* out) const override { out[0] = period; out[1] = (float)cycles; }
};

} // namespace rig

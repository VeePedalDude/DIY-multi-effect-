// tools/orbits/refine_orbits.cpp - re-derives THREE-BODY's orbit table (engine/sources/chaos/orbit.h)
// with the source's own integrator, so the stored numbers always close the loop in the code that plays them.
//
// For each orbit: start from its published starting velocities and period, find the regularised step
// `ds` that covers the period in ORBIT_STEPS steps, then Gauss-Newton shooting on (p1, p2, ds) until
// the state after ORBIT_STEPS steps equals the start (~1e-12). Prints the refined table rows, how far each
// was from closing before and after, its energy error, and how much a 1e-9 nudge grows over one cycle
// (above ~100: unstable). Build and run from the repo root:
//   g++ -O2 -std=c++20 -I. -o build/refine_orbits tools/orbits/refine_orbits.cpp && ./build/refine_orbits
// Published starting points: Suvakov & Dmitrasinovic, PRL 110, 114301 (2013); Moore's figure-eight
// (Suvakov & Dmitrasinovic, arXiv:1312.7002); Li, Jing & Liao, PASJ 70, 64 (2018).
#include "engine/sources/chaos/orbit.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>
using namespace rig;

struct Start { double p1, p2, T; };
static const Start PUBLISHED[NORBITS] = {
  {0.3471128135672417, 0.532726851767674, 6.3250}, {0.30689, 0.12551, 6.2356}, {0.40592, 0.23016, 13.8658},
  {0.46444, 0.39606, 14.8939}, {0.08330, 0.12789, 10.4668}, {0.51394, 0.30474, 17.3284}, {0.08058, 0.58884, 21.2710},
  {0.18428, 0.58719, 63.5345}, {0.55906, 0.34919, 55.5018}, {0.3057224330, 0.5215124257, 8.8237067653},
  {0.2009656237, 0.2431076328, 19.0134164290}};

static OrbitDef def;
static double lastT;
static OrbitSim start(double p1, double p2, double ds) {
  OrbitDef d = def; d.p1 = p1; d.p2 = p2; d.ds = ds;
  OrbitSim s; s.def = 0; s.m[0] = s.m[1] = 1; s.m[2] = d.m3;
  s.x[0] = -1; s.x[1] = 1; s.x[2] = 0; s.y[0] = s.y[1] = s.y[2] = 0;
  s.vx[0] = s.vx[1] = p1; s.vy[0] = s.vy[1] = p2; s.vx[2] = -2 * p1 / d.m3; s.vy[2] = -2 * p2 / d.m3;
  s.bind = s.pot() - s.kin(); return s;
}
static void residual(double p1, double p2, double ds, double* r) {
  OrbitSim s = start(p1, p2, ds), s0 = s; double t = 0;
  for (int i = 0; i < ORBIT_STEPS; i++) t += s.step(ds);
  lastT = t;
  for (int a = 0; a < 3; a++) { r[4 * a] = s.x[a] - s0.x[a]; r[4 * a + 1] = s.y[a] - s0.y[a]; r[4 * a + 2] = s.vx[a] - s0.vx[a]; r[4 * a + 3] = s.vy[a] - s0.vy[a]; }
}
static double norm(const double* r) { double q = 0; for (int i = 0; i < 12; i++) q += r[i] * r[i]; return sqrt(q); }
static void solve3(double A[3][3], const double* b, double* x) {
  auto det = [](double M[3][3]) { return M[0][0] * (M[1][1] * M[2][2] - M[1][2] * M[2][1]) - M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0]) + M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0]); };
  const double d = det(A);
  for (int c = 0; c < 3; c++) { double B[3][3]; memcpy(B, A, sizeof B); for (int k = 0; k < 3; k++) B[k][c] = b[k]; x[c] = det(B) / d; }
}

int main() {
  for (int o = 0; o < NORBITS; o++) {
    def = ORBITS[o];
    double p[3] = {PUBLISHED[o].p1, PUBLISHED[o].p2, 0.01}, r[12];
    for (int k = 0; k < 6; k++) { residual(p[0], p[1], p[2], r); p[2] *= PUBLISHED[o].T / lastT; }
    residual(p[0], p[1], p[2], r); const double before = norm(r);
    for (int it = 0; it < 40 && norm(r) > 1e-13; it++) {
      double J[12][3], eps[3] = {1e-8, 1e-8, 1e-6 * p[2]}, A[3][3] = {{0}}, b[3] = {0}, dx[3], rr[12], q[3];
      for (int j = 0; j < 3; j++) { double s[3] = {p[0], p[1], p[2]}; s[j] += eps[j]; residual(s[0], s[1], s[2], rr); for (int i = 0; i < 12; i++) J[i][j] = (rr[i] - r[i]) / eps[j]; }
      for (int i = 0; i < 12; i++) for (int j = 0; j < 3; j++) { b[j] -= J[i][j] * r[i]; for (int k = 0; k < 3; k++) A[j][k] += J[i][j] * J[i][k]; }
      solve3(A, b, dx);
      double lam = 1; bool better = false;
      for (int t = 0; t < 30 && !better; t++, lam *= 0.5) { for (int j = 0; j < 3; j++) q[j] = p[j] + lam * dx[j]; residual(q[0], q[1], q[2], rr); better = norm(rr) < norm(r); }
      if (!better) break;
      memcpy(p, q, sizeof p); memcpy(r, rr, sizeof r);
    }
    residual(p[0], p[1], p[2], r); const double T = lastT, after = norm(r);
    OrbitSim s = start(p[0], p[1], p[2]); const double e0 = s.kin() - s.pot(); double de = 0;
    // each OUT quantity's 1st..99th percentile, weighted by the time spent there
    std::vector<std::pair<double, double>> qs[4]; double lo[4], hi[4];
    for (int i = 0; i < ORBIT_STEPS; i++) {
      double v[4]; for (int k = 0; k < 4; k++) v[k] = s.quantity(k);
      const double dt = s.step(p[2]); for (int k = 0; k < 4; k++) qs[k].push_back({0.5 * (v[k] + s.quantity(k)), dt});
      de = fmax(de, fabs(s.kin() - s.pot() - e0) / fabs(e0));
    }
    for (int k = 0; k < 4; k++) {
      std::sort(qs[k].begin(), qs[k].end()); double acc = 0; lo[k] = qs[k].front().first; hi[k] = qs[k].back().first; bool l = false;
      for (auto& e : qs[k]) { acc += e.second / T; if (!l && acc >= 0.01) { lo[k] = e.first; l = true; } if (acc <= 0.99) hi[k] = e.first; }
    }
    double r1[12]; residual(p[0] + 1e-9, p[1], p[2], r1); double grow = 0; for (int i = 0; i < 12; i++) grow += (r1[i] - r[i]) * (r1[i] - r[i]);
    printf("  {\"%s\", %.1f, %.17g, %.17g, %.17g, %.10f, {%.6ff, %.6ff, %.6ff, %.6ff}, {%.6ff, %.6ff, %.6ff, %.6ff}},\n",
           def.name, def.m3, p[0], p[1], p[2], T, lo[0], lo[1], lo[2], lo[3], hi[0], hi[1], hi[2], hi[3]);
    fprintf(stderr, "%-14s closes to %.1e (published start: %.1e), T %.6f (published %.4f), energy error %.1e, a 1e-9 nudge grows x%.0f per cycle\n",
            def.name, after, before, T, PUBLISHED[o].T, de, sqrt(grow) / 1e-9);
  }
}

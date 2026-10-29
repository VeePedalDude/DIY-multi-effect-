// rig.h - test-rig DSP engine (header only, no allocation after init)
// One source builds natively (test harness) and to WebAssembly (browser rig).
// Later each module becomes its own DPF/LV2 plugin for mod-host.
#pragma once
#include <math.h>
#include <string.h>

namespace rig {

constexpr int   MAXBLOCK = 128;
constexpr int   NMOD = 5, NPAR = 2, NSRC = 6;
constexpr float PI_F = 3.14159265358979f;

inline float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
inline float db2lin(float d) { return powf(10.f, d * 0.05f); }
inline float undenorm(float x) { return fabsf(x) < 1e-15f ? 0.f : x; }

// ------------------------------------------------------------------ utilities
struct OnePole {
  float y = 0, a = 1;
  void setMs(float sr, float ms) { a = 1.f - expf(-1.f / (0.001f * ms * sr)); }
  float next(float t) { y += a * (t - y); return y; }
};

struct Biquad {                         // RBJ cookbook, transposed DF-II
  float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
  void lp(float sr, float f, float q) { pass(sr, f, q, false); }
  void hp(float sr, float f, float q) { pass(sr, f, q, true); }
  void pass(float sr, float f, float q, bool high) {
    float w = 2 * PI_F * clampf(f, 5.f, 0.49f * sr) / sr, al = sinf(w) / (2 * q), c = cosf(w), a0 = 1 + al;
    if (!high) { b0 = (1 - c) / 2 / a0; b1 = (1 - c) / a0; }
    else       { b0 = (1 + c) / 2 / a0; b1 = -(1 + c) / a0; }
    b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
  }
  void peak(float sr, float f, float q, float db) {
    float A = powf(10.f, db / 40.f), w = 2 * PI_F * f / sr, al = sinf(w) / (2 * q), c = cosf(w);
    float a0 = 1 + al / A;
    b0 = (1 + al * A) / a0; b1 = -2 * c / a0; b2 = (1 - al * A) / a0; a1 = b1; a2 = (1 - al / A) / a0;
  }
  float tick(float x) {
    float y = b0 * x + z1;
    z1 = undenorm(b1 * x - a1 * y + z2);
    z2 = undenorm(b2 * x - a2 * y);
    return y;
  }
  void reset() { z1 = z2 = 0; }
};

struct DCBlock {
  float x1 = 0, y1 = 0, r = 0.995f;
  float tick(float x) { float y = x - x1 + r * y1; x1 = x; y1 = undenorm(y); return y; }
  void reset() { x1 = y1 = 0; }
};

template <int N> struct DelayLine {     // power-of-two ring, linear-interp fractional read
  static_assert((N & (N - 1)) == 0, "N must be a power of two");
  float b[N]; int w = 0;
  void clear() { memset(b, 0, sizeof b); w = 0; }
  void push(float x) { b[w] = x; w = (w + 1) & (N - 1); }
  float tap(float d) const {             // value written d samples ago (d >= 1)
    float rp = (float)w - d;
    while (rp < 0) rp += (float)N;
    int i0 = (int)rp; float fr = rp - (float)i0;
    float a = b[i0 & (N - 1)], c = b[(i0 + 1) & (N - 1)];
    return a + fr * (c - a);
  }
};

struct Allpass {
  float buf[4096]; int len = 1, i = 0; float g = 0.6f;
  void init(int l, float gain) { len = l < 1 ? 1 : (l > 4095 ? 4095 : l); g = gain; clear(); }
  void clear() { memset(buf, 0, sizeof buf); i = 0; }
  float tick(float x) {
    float d = buf[i], v = x + g * d;
    buf[i] = undenorm(v); if (++i >= len) i = 0;
    return d - g * v;
  }
};

// ------------------------------------------------------------------ modules
struct Module {
  float sr = 48000;
  virtual ~Module() {}
  virtual void init(float s) = 0;
  virtual void reset() = 0;
  // e[] = effective normalised params (0..1) for this block
  virtual void process(const float* inL, const float* inR, float* oL, float* oR, int n, const float* e) = 0;
};

// 0 NOISE GATE   THRESH v -> -80..-20 dBFS    RELEASE v -> 20..500 ms
struct Gate : Module {
  float env = 0, gain = 0, hold = 0; bool open = false;
  OnePole thS;
  void init(float s) override { sr = s; thS.setMs(sr, 30); thS.y = 0.35f; reset(); }
  void reset() override { env = 0; gain = 0; hold = 0; open = false; }
  void process(const float* inL, const float* inR, float* oL, float* oR, int n, const float* e) override {
    float aAtt = 1.f - expf(-1.f / (0.0005f * sr)), aRel = 1.f - expf(-1.f / (0.050f * sr));
    float relMs = 20.f + 480.f * e[1];
    float gUp = 1.f - expf(-1.f / (0.001f * sr)), gDn = 1.f - expf(-1.f / (0.001f * relMs * sr));
    for (int i = 0; i < n; i++) {
      float th = db2lin(-80.f + 60.f * thS.next(e[0]));
      float x = inL[i], ax = fabsf(x);
      env += (ax > env ? aAtt : aRel) * (ax - env);
      if (env > th) { open = true; hold = 0.010f * sr; }
      else if (env < th * 0.5f) { if (hold > 0) hold -= 1; else open = false; }   // 6 dB hysteresis
      float tgt = open ? 1.f : 0.f;
      gain += (tgt > gain ? gUp : gDn) * (tgt - gain);
      oL[i] = x * gain; oR[i] = inR[i] * gain;
    }
  }
};

// 1 AMP (stand-in for the NAM A2 slot)   INPUT v -> -12..+24 dB   OUTPUT v -> -18..+6 dB
// Shaper is oversampled (os x) and optionally uses 1st-order antiderivative anti-aliasing (ADAA).
struct Amp : Module {
  Biquad hp, tight, scoop, post, up[3], dn[3];
  DCBlock dc;
  OnePole inS, outS;
  int os = 2; bool adaa = true;
  double xPrev = 0, fPrev = 0;
  static constexpr float BIAS = 0.15f;
  void init(float s) override {
    sr = s;
    hp.hp(sr, 90, 0.707f); tight.peak(sr, 900, 0.7f, 6.f);
    scoop.peak(sr, 420, 0.8f, -4.f); post.lp(sr, 7000, 0.707f);
    setOS(os);
    inS.setMs(sr, 20); outS.setMs(sr, 20); inS.y = 0.6f; outS.y = 0.5f;
    reset();
  }
  void setOS(int o) {
    os = o < 1 ? 1 : o;
    const float q[3] = {0.5176f, 0.7071f, 1.9319f};       // 6th-order Butterworth at os x rate
    for (int k = 0; k < 3; k++) { up[k].lp(os * sr, 19000, q[k]); dn[k].lp(os * sr, 19000, q[k]); }
  }
  void reset() override {
    hp.reset(); tight.reset(); scoop.reset(); post.reset(); dc.reset();
    for (int k = 0; k < 3; k++) { up[k].reset(); dn[k].reset(); }
    xPrev = 0; fPrev = F(0);
  }
  static float shape(float x) { return tanhf(x + BIAS) - tanhf(BIAS); }
  static double logcosh(double x) { double a = fabs(x); return a + log1p(exp(-2.0 * a)) - 0.69314718055994531; }
  static double F(double x) { return logcosh(x + BIAS) - x * tanh((double)BIAS); }   // antiderivative of shape
  float shapeAA(float xf) {
    double x = xf, dx = x - xPrev, Fx = F(x), y;
    if (fabs(dx) > 1e-5) y = (Fx - fPrev) / dx;
    else y = shape((float)(0.5 * (x + xPrev)));
    xPrev = x; fPrev = Fx;
    return (float)y;
  }
  void process(const float* inL, const float*, float* oL, float* oR, int n, const float* e) override {
    for (int i = 0; i < n; i++) {
      float drive = db2lin(6.f + 36.f * inS.next(e[0]));
      float outG  = db2lin(-18.f + 24.f * outS.next(e[1]));
      float x = tight.tick(hp.tick(inL[i])) * drive;
      float y = 0;
      for (int ph = 0; ph < os; ph++) {                    // zero-stuff, filter, shape, filter, decimate
        float u = ph == 0 ? (float)os * x : 0.f;
        if (os > 1) for (int k = 0; k < 3; k++) u = up[k].tick(u);
        u = adaa ? shapeAA(u) : shape(u);
        if (os > 1) for (int k = 0; k < 3; k++) u = dn[k].tick(u);
        if (ph == 0) y = u;
      }
      y = post.tick(scoop.tick(dc.tick(y))) * AMP_MAKEUP * outG;
      oL[i] = y; oR[i] = y;
    }
  }
  static constexpr float AMP_MAKEUP = 1.0f;
};

// 2 CAB IR (direct FIR, <= 2048 taps)   LO CUT v -> 20..400 Hz   MIX v -> 0..100 %
struct Cab : Module {
  static constexpr int MAXIR = 2048;
  float ir[MAXIR], load[MAXIR], hist[2 * MAXIR];
  int len = 1, p = 0;
  Biquad lc; float lcHz = -1;
  OnePole mixS;
  void init(float s) override { sr = s; mixS.setMs(sr, 20); mixS.y = 0.9f; makeDefault(); }
  void reset() override { memset(hist, 0, sizeof hist); p = 0; lc.reset(); }
  void makeDefault() {                                     // synthetic closed-back 4x12-ish response
    Biquad f[6];
    f[0].hp(sr, 75, 0.707f); f[1].peak(sr, 115, 1.0f, 5.f); f[2].peak(sr, 900, 1.0f, -4.f);
    f[3].peak(sr, 2600, 1.3f, 4.f); f[4].lp(sr, 5200, 0.6f); f[5].lp(sr, 6500, 1.1f);
    int n = (int)(0.021f * sr); if (n > MAXIR) n = MAXIR;
    int r1 = (int)(0.0006f * sr), r2 = (int)(0.0013f * sr);
    for (int i = 0; i < n; i++) {
      float x = (i == 0) ? 1.f : 0.f;
      if (i == r1) x += 0.25f;
      if (i == r2) x -= 0.15f;
      for (auto& b : f) x = b.tick(x);
      float fade = i > n - 64 ? (float)(n - i) / 64.f : 1.f;
      load[i] = x * fade;
    }
    commit(n);
  }
  void commit(int n) {                                     // energy-normalise so IRs swap at similar loudness
    if (n < 1) return;
    if (n > MAXIR) n = MAXIR;
    double e = 0; for (int i = 0; i < n; i++) e += (double)load[i] * load[i];
    float g = e > 1e-12 ? (float)(0.21 / sqrt(e)) : 0.f;
    for (int i = 0; i < n; i++) ir[i] = load[i] * g;
    len = n; reset();
  }
  void process(const float* inL, const float*, float* oL, float* oR, int n, const float* e) override {
    float hz = 20.f * powf(10.f, 1.3f * e[0]);
    if (fabsf(hz - lcHz) > 0.5f) { lc.hp(sr, hz, 0.707f); lcHz = hz; }
    for (int i = 0; i < n; i++) {
      float x = inL[i];
      p = (p == 0) ? len - 1 : p - 1;                      // window hist[p..p+len) = newest..oldest
      hist[p] = x; hist[p + len] = x;
      const float* h = hist + p;
      float a0 = 0, a1 = 0, a2 = 0, a3 = 0;
      int k = 0;
      for (; k + 3 < len; k += 4) {
        a0 += ir[k] * h[k]; a1 += ir[k + 1] * h[k + 1];
        a2 += ir[k + 2] * h[k + 2]; a3 += ir[k + 3] * h[k + 3];
      }
      for (; k < len; k++) a0 += ir[k] * h[k];
      float wet = (a0 + a1) + (a2 + a3);
      float m = mixS.next(e[1]);
      float y = lc.tick(wet * m + x * 0.3f * (1.f - m));
      oL[i] = y; oR[i] = y;
    }
  }
};

// 3 HOLOGRAM delay (tempo-synced dotted 8th)   FEEDBACK v -> 0..95 %   HALO v -> 0..1
struct Holo : Module {
  DelayLine<262144> line;
  Allpass ap1, ap2;
  Biquad lp, hp;
  OnePole timeS, fbS, haloS;
  float bpm = 120, ph1 = 0, ph2 = 0, curMs = 375;
  void init(float s) override {
    sr = s; lp.lp(sr, 4500, 0.6f); hp.hp(sr, 120, 0.6f);
    timeS.setMs(sr, 150); fbS.setMs(sr, 30); haloS.setMs(sr, 30);
    timeS.y = 0.375f * sr; fbS.y = 0.45f; haloS.y = 0.6f;
    ap1.init((int)(0.0113f * sr), 0.6f); ap2.init((int)(0.0171f * sr), 0.55f);
    reset();
  }
  void reset() override { line.clear(); ap1.clear(); ap2.clear(); lp.reset(); hp.reset(); }
  void process(const float* inL, const float* inR, float* oL, float* oR, int n, const float* e) override {
    float secs = clampf(0.75f * 60.f / bpm, 0.05f, 2.4f);
    curMs = secs * 1000.f;
    float target = secs * sr, i1 = 0.31f / sr, i2 = 0.19f / sr;
    for (int i = 0; i < n; i++) {
      float d = timeS.next(target), fb = 0.95f * fbS.next(e[0]), h = haloS.next(e[1]);
      ph1 += i1; if (ph1 >= 1) ph1 -= 1;
      ph2 += i2; if (ph2 >= 1) ph2 -= 1;
      float s1 = sinf(2 * PI_F * ph1), s2 = sinf(2 * PI_F * ph2);
      float y = line.tap(d + 1.f + h * 0.0006f * sr * (1.f + s1));
      float a = line.tap(d * 1.5f + 1.f + h * 0.003f * sr * (1.f + s2));
      float b = line.tap(d * 0.5f + 1.f + h * 0.003f * sr * (1.f - s1));
      float diff = ap2.tick(ap1.tick(0.5f * (a + b)));
      float loop = hp.tick(lp.tick((1.f - h) * y + h * diff));
      float xin = 0.5f * (inL[i] + inR[i]);
      line.push(undenorm(xin + fb * tanhf(loop)));
      float wl = y + 0.6f * h * a, wr = y + 0.6f * h * b;
      oL[i] = inL[i] + 0.4f * wl;
      oR[i] = inR[i] + 0.4f * wr;
    }
  }
};

// 4 TESSERACT reverb (8-line FDN, 4D-rotated Hadamard)   SIZE v -> RT60 0.5..7.5 s   4D FOLD v -> 0..1
struct Tess : Module {
  static constexpr int L = 8;
  DelayLine<8192> line[L];
  DelayLine<8192> pre;
  Allpass dif1, dif2;
  float len[L], g[L], lpz[L], lpa = 0.5f, t = 0, lastRt = -1;
  OnePole foldS;
  void init(float s) override {
    sr = s;
    const float ms[L] = {29.7f, 37.1f, 41.1f, 43.7f, 53.3f, 59.9f, 67.1f, 79.3f};
    for (int k = 0; k < L; k++) len[k] = ms[k] * 0.001f * sr;
    lpa = 1.f - expf(-2.f * PI_F * 7000.f / sr);
    dif1.init((int)(0.0047f * sr), 0.65f); dif2.init((int)(0.0083f * sr), 0.6f);
    foldS.setMs(sr, 50); foldS.y = 0.3f;
    reset();
  }
  void reset() override {
    for (auto& l : line) l.clear();
    pre.clear(); dif1.clear(); dif2.clear();
    memset(lpz, 0, sizeof lpz);
  }
  void setRt(float rt) {
    if (fabsf(rt - lastRt) < 1e-3f) return;
    for (int k = 0; k < L; k++) g[k] = powf(10.f, -3.f * (len[k] / sr) / rt);
    lastRt = rt;
  }
  static void wht8(float* v) {                         // fast Walsh-Hadamard, orthonormal
    for (int hlen = 1; hlen < 8; hlen <<= 1)
      for (int i = 0; i < 8; i += hlen << 1)
        for (int j = i; j < i + hlen; j++) { float a = v[j], b = v[j + hlen]; v[j] = a + b; v[j + hlen] = a - b; }
    const float s = 0.35355339f;
    for (int i = 0; i < 8; i++) v[i] *= s;
  }
  void process(const float* inL, const float* inR, float* oL, float* oR, int n, const float* e) override {
    setRt(0.5f + 7.f * e[0]);
    float fold = foldS.y;
    for (int i = 0; i < n; i++) foldS.next(e[1]);
    t += (float)n / sr;
    float th = fold * (PI_F * 0.25f + 0.3f * sinf(2 * PI_F * 0.05f * t));
    float c = cosf(th), sn = sinf(th);
    float m0 = 0.0003f * sr * sinf(2 * PI_F * 0.7f * t), m5 = 0.0003f * sr * sinf(2 * PI_F * 0.53f * t + 1.f);
    const float sign[L] = {1, -1, 1, -1, 1, -1, 1, -1};
    for (int i = 0; i < n; i++) {
      float x = 0.5f * (inL[i] + inR[i]);
      pre.push(x);
      float d = dif2.tick(dif1.tick(pre.tap(0.012f * sr)));
      float v[L];
      for (int k = 0; k < L; k++) {
        float dl = len[k] + (k == 0 ? m0 : (k == 5 ? m5 : 0.f));
        float r = line[k].tap(dl);
        lpz[k] += lpa * (r - lpz[k]); lpz[k] = undenorm(lpz[k]);
        v[k] = lpz[k] * g[k];
      }
      float wl = 0.5f * (v[0] - v[2] + v[4] - v[6]);
      float wr = 0.5f * (v[1] - v[3] + v[5] - v[7]);
      for (int k = 0; k < 4; k++) {                      // rotate the two 4-D halves into each other
        float a = v[k], b = v[k + 4];
        v[k] = c * a - sn * b; v[k + 4] = sn * a + c * b;
      }
      wht8(v);
      for (int k = 0; k < L; k++) line[k].push(undenorm(v[k] + 0.35f * sign[k] * d));
      oL[i] = inL[i] + 0.3f * wl;
      oR[i] = inR[i] + 0.3f * wr;
    }
  }
};

// ------------------------------------------------------------------ modulation sources
// 0 LFO  1 ENV FOLLOW  2 PICK ADSR  3 THREE-BODY  4 HEART  5 CONTROLLER   (all output -1..1)
struct Sources {
  float sr = 48000, val[NSRC] = {0};
  float lfoPh = 0;
  float env = 0, fast = 0, slow = 0, refr = 0, adsr = 0; int stage = 0; int onsets = 0;
  float hfFast = 0, hfSlow = 0; Biquad hf1, hf2;
  float px[3], py[3], vx[3], vy[3];
  float heartPh = 0, extBpm = 0, extAge = 99, heartBpm = 72;
  float ctrl = 0, ctrlS = 0;
  float ONSET_FLOOR = 0.001f, ONSET_RATIO = 2.f;
  void init(float s) {
    sr = s; resetBodies();
    hf1.hp(sr, 1800, 0.5412f); hf2.hp(sr, 1800, 1.3066f);   // 4th-order: pick transients live up here
  }
  void resetBodies() {
    const float x0 = 0.97000436f, y0 = -0.24308753f, vx3 = -0.93240737f, vy3 = -0.86473146f;
    px[0] = -x0 + 0.02f; py[0] = -y0; px[1] = x0; py[1] = y0; px[2] = 0; py[2] = 0;
    vx[2] = vx3; vy[2] = vy3; vx[0] = vx[1] = -vx3 / 2; vy[0] = vy[1] = -vy3 / 2;
  }
  void stepBodies(float dt) {
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
  static float heartShape(float u) {
    if (u > .27f && u < .30f) return -.45f;
    if (u >= .30f && u < .33f) return 1.f;
    if (u >= .33f && u < .36f) return -.85f;
    if (u > .50f && u < .62f) return -.15f + .3f * sinf(PI_F * (u - .5f) / .12f);
    return -.15f;
  }
  void update(const float* in, int n, float bpm) {
    float dt = (float)n / sr;
    // per-sample detectors
    float eA = 1.f - expf(-1.f / (0.005f * sr)), eR = 1.f - expf(-1.f / (0.150f * sr));
    float fA = 1.f - expf(-1.f / (0.001f * sr)), fR = 1.f - expf(-1.f / (0.030f * sr));
    float sA = 1.f - expf(-1.f / (0.030f * sr)), sR = 1.f - expf(-1.f / (0.400f * sr));
    float hA = 1.f - expf(-1.f / (0.0005f * sr)), hR = 1.f - expf(-1.f / (0.010f * sr));
    float hsA = 1.f - expf(-1.f / (0.025f * sr)), hsR = 1.f - expf(-1.f / (0.150f * sr));
    bool trig = false;
    for (int i = 0; i < n; i++) {
      float ax = fabsf(in[i]);
      env += (ax > env ? eA : eR) * (ax - env);
      fast += (ax > fast ? fA : fR) * (ax - fast);
      slow += (ax > slow ? sA : sR) * (ax - slow);
      float hx = fabsf(hf2.tick(hf1.tick(in[i])));
      hfFast += (hx > hfFast ? hA : hR) * (hx - hfFast);
      hfSlow += (hx > hfSlow ? hsA : hsR) * (hx - hfSlow);
      if (refr > 0) refr -= 1.f;
      else if (hfFast > ONSET_FLOOR && hfFast > ONSET_RATIO * hfSlow) { trig = true; refr = 0.060f * sr; onsets++; }
    }
    hfFast = undenorm(hfFast); hfSlow = undenorm(hfSlow);
    env = undenorm(env); fast = undenorm(fast); slow = undenorm(slow);
    // 0 LFO: one cycle per 8 beats (0.25 Hz at 120 bpm)
    lfoPh += dt * bpm / 60.f / 8.f; lfoPh -= floorf(lfoPh);
    val[0] = sinf(2 * PI_F * lfoPh);
    // 1 envelope follower: -60 dBFS -> -1, 0 dBFS -> +1
    float edb = 20.f * log10f(env + 1e-9f);
    val[1] = clampf((edb + 60.f) / 30.f - 1.f, -1.f, 1.f);
    // 2 pick-triggered ADSR (A 40 ms, D 300 ms to S 0.4, R 600 ms below -45 dBFS)
    if (trig) stage = 1;
    if (stage == 1) { adsr += dt / 0.040f; if (adsr >= 1) { adsr = 1; stage = 2; } }
    else if (stage == 2) { adsr -= dt / 0.300f * 0.6f; if (adsr <= 0.4f) { adsr = 0.4f; stage = 3; } }
    if (stage == 3 && fast < 0.0056f) stage = 4;
    if (stage == 4) { adsr -= dt / 0.600f; if (adsr <= 0) { adsr = 0; stage = 0; } }
    val[2] = 2.f * adsr - 1.f;
    // 3 three-body orbit: distance between bodies 0 and 1
    for (float acc = 0; acc < dt; acc += 0.002f) stepBodies(0.002f);
    float dx = px[0] - px[1], dy = py[0] - py[1];
    val[3] = tanhf((sqrtf(dx * dx + dy * dy) - 1.2f) * 1.5f);
    // 4 heart: external strap bpm if fresh, else simulated 72 bpm
    extAge += dt;
    heartBpm = (extAge < 5.f && extBpm > 30.f) ? extBpm : 72.f;
    heartPh += dt * heartBpm / 60.f; heartPh -= floorf(heartPh);
    val[4] = heartShape(heartPh);
    // 5 controller (gamepad stick / mouse), smoothed
    ctrlS += (1.f - expf(-dt / 0.03f)) * (ctrl - ctrlS);
    val[5] = clampf(ctrlS, -1.f, 1.f);
  }
};

// ------------------------------------------------------------------ engine
struct Engine {
  Gate gate; Amp amp; Cab cab; Holo holo; Tess tess;
  Module* mods[NMOD] = {&gate, &amp, &cab, &holo, &tess};
  Sources src;
  float sr = 48000, bpm = 120;
  float base[NMOD][NPAR] = {{.35f, .4f}, {.6f, .5f}, {.25f, .9f}, {.45f, .6f}, {.7f, .3f}};
  int   conn[NMOD][NPAR];
  float depth[NMOD][NPAR];
  float eff[NMOD][NPAR];
  bool  byp[NMOD] = {false, false, false, false, false};
  float wet[NMOD] = {1, 1, 1, 1, 1};
  float inBuf[MAXBLOCK], outL[MAXBLOCK], outR[MAXBLOCK];
  float aL[MAXBLOCK], aR[MAXBLOCK], bL[MAXBLOCK], bR[MAXBLOCK];
  float masterDb = 0.f; OnePole masterS;
  DCBlock dcL, dcR;
  int nanResets = 0;
  float meters[32];

  void init(float s) {
    sr = s;
    for (auto* m : mods) m->init(sr);
    src.init(sr);
    for (int m = 0; m < NMOD; m++) for (int p = 0; p < NPAR; p++) { conn[m][p] = -1; depth[m][p] = 0.22f; eff[m][p] = base[m][p]; }
    masterS.setMs(sr, 30); masterS.y = db2lin(masterDb);
    memset(meters, 0, sizeof meters);
  }
  static float limit(float x) {                       // soft knee from 0.8, ceiling 0.98
    float a = fabsf(x);
    if (a <= 0.8f) return x;
    float y = 0.8f + 0.18f * tanhf((a - 0.8f) / 0.18f);
    return x < 0 ? -y : y;
  }
  void process(int n) {
    if (n > MAXBLOCK) n = MAXBLOCK;
    src.update(inBuf, n, bpm);
    holo.bpm = bpm;
    for (int m = 0; m < NMOD; m++) for (int p = 0; p < NPAR; p++) {
      float v = base[m][p];
      if (conn[m][p] >= 0) v += depth[m][p] * src.val[conn[m][p]];
      eff[m][p] = clampf(v, 0.f, 1.f);
    }
    memcpy(aL, inBuf, n * sizeof(float)); memcpy(aR, inBuf, n * sizeof(float));
    float step = (float)n / (0.010f * sr);              // 10 ms bypass crossfade
    for (int m = 0; m < NMOD; m++) {
      float w0 = wet[m], w1 = byp[m] ? fmaxf(0.f, w0 - step) : fminf(1.f, w0 + step);
      wet[m] = w1;
      if (w0 == 0.f && w1 == 0.f) continue;           // fully bypassed: dry passes untouched
      mods[m]->process(aL, aR, bL, bR, n, eff[m]);
      for (int i = 0; i < n; i++) {
        float w = w0 + (w1 - w0) * (float)(i + 1) / (float)n;
        aL[i] += w * (bL[i] - aL[i]);
        aR[i] += w * (bR[i] - aR[i]);
      }
    }
    bool bad = false;
    float g = db2lin(masterDb);
    for (int i = 0; i < n; i++) {
      float mg = masterS.next(g);
      float l = limit(dcL.tick(aL[i]) * mg), r = limit(dcR.tick(aR[i]) * mg);
      if (!(fabsf(l) <= 1.f) || !(fabsf(r) <= 1.f)) bad = true;   // catches NaN/Inf
      outL[i] = l; outR[i] = r;
    }
    if (bad) {
      for (auto* m : mods) m->reset();
      dcL.reset(); dcR.reset();
      memset(outL, 0, n * sizeof(float)); memset(outR, 0, n * sizeof(float));
      nanResets++;
    }
    meters[0] = gate.open ? 1.f : 0.f;
    for (int k = 0; k < NSRC; k++) meters[1 + k] = src.val[k];
    for (int m = 0; m < NMOD; m++) for (int p = 0; p < NPAR; p++) meters[7 + m * 2 + p] = eff[m][p];
    meters[17] = (float)nanResets;
    meters[18] = (float)src.onsets;
    meters[19] = src.heartBpm;
    meters[20] = holo.curMs;
  }
};

} // namespace rig

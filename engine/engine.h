// engine/engine.h - the chain: runs registered modules in order, applies modulation,
// bypass crossfades, master gain, limiter and NaN protection, and publishes meters
// plus a JSON self-description that the protocol and UI are built from.
//
// Nothing in here names a specific effect or source: both lists come from the
// registries, so adding one never touches this file.
#pragma once
#include "modules/registry.h"
#include "sources/registry.h"
#include "core/tuner.h"

namespace rig {

#define RIG_COUNT(T, m) +1
constexpr int NMOD = 0 RIG_MODULES(RIG_COUNT);
constexpr int NSRC = 0 RIG_SOURCES(RIG_COUNT);
#undef RIG_COUNT

// Meter block layout (offsets are also published in describe()).
constexpr int M_NAN  = 0;                                   // NaN/Inf resets so far
constexpr int M_SRC  = 1;                                   // NSRC source values, -1..1
constexpr int M_EFF  = M_SRC + NSRC;                        // NMOD x MAXPAR effective params, 0..1
constexpr int M_MOD  = M_EFF + NMOD * MAXPAR;               // NMOD x MAXMODMETERS module display values
constexpr int M_SRCM = M_MOD + NMOD * MAXMODMETERS;         // NSRC x MAXSRCMETERS source display values
constexpr int M_TUN  = M_SRCM + NSRC * MAXSRCMETERS;        // the tuner (core/tuner.h TunerMeter)
constexpr int M_LEN  = M_TUN + T_LEN;

struct Sources {
#define RIG_DECL(T, m) T m;
  RIG_SOURCES(RIG_DECL)
#undef RIG_DECL
#define RIG_PTR(T, m) &m,
  Source* list[NSRC] = { RIG_SOURCES(RIG_PTR) };
#undef RIG_PTR
  float val[NSRC] = {0};
  float base[NSRC][MAXPAR];                                 // each source's knobs, normalised
  void init(float sr) {
    for (auto* s : list) s->init(sr);
    memset(val, 0, sizeof val);
    for (int k = 0; k < NSRC; k++) for (int p = 0; p < MAXPAR; p++) base[k][p] = p < list[k]->desc().nparams ? list[k]->desc().params[p].def : 0.f;
  }
  void update(const float* in, int n, float sr, float bpm) {
    for (int k = 0; k < NSRC; k++) {
      SourceContext c{in, n, sr, (float)n / sr, bpm, base[k]};
      val[k] = clampf(list[k]->update(c), -1.f, 1.f);
    }
  }
};

// Small allocation-free JSON writer (no printf, so the WebAssembly build needs no imports).
struct JsonOut {
  char* buf; int cap, len = 0;
  JsonOut(char* b, int c) : buf(b), cap(c) { buf[0] = 0; }
  void ch(char c) { if (len < cap - 1) { buf[len++] = c; buf[len] = 0; } }
  void raw(const char* s) { while (*s) ch(*s++); }
  void str(const char* s) {
    if (!s) { raw("null"); return; }
    ch('"'); for (; *s; s++) { if (*s == '"' || *s == '\\') ch('\\'); ch(*s); } ch('"');
  }
  void num(double v) {
    if (!(v == v)) { raw("null"); return; }
    if (v < 0) { ch('-'); v = -v; }
    long long scaled = (long long)(v * 1000000.0 + 0.5), ip = scaled / 1000000, fp = scaled % 1000000;
    char t[24]; int n = 0; do { t[n++] = (char)('0' + ip % 10); ip /= 10; } while (ip);
    while (n) ch(t[--n]);
    if (fp) { ch('.'); int d = 100000; while (fp && d) { ch((char)('0' + fp / d)); fp %= d; d /= 10; } }
  }
  void key(const char* k) { str(k); ch(':'); }
};

struct Engine {
#define RIG_DECL(T, m) T m;
  RIG_MODULES(RIG_DECL)
#undef RIG_DECL
#define RIG_PTR(T, m) &m,
  Module* mods[NMOD] = { RIG_MODULES(RIG_PTR) };
#undef RIG_PTR
  Sources src;
  Context ctx;
  float sr = 48000, bpm = 120;
  float base[NMOD][MAXPAR];
  int   conn[NMOD][MAXPAR];
  float depth[NMOD][MAXPAR];
  float eff[NMOD][MAXPAR];
  bool  byp[NMOD];
  float wet[NMOD];
  float inBuf[MAXBLOCK], outL[MAXBLOCK], outR[MAXBLOCK];
  float aL[MAXBLOCK], aR[MAXBLOCK], bL[MAXBLOCK], bR[MAXBLOCK];
  float masterDb = 0.f; OnePole masterS;
  Tuner tuner; float muteG = 1.f, muteStep = 0.f;          // tuner on the dry input; output fades out while tuning with mute
  DCBlock dcL, dcR;
  int nanResets = 0;
  float meters[M_LEN];
  char descJson[16384];

  int nparams(int m) const { return mods[m]->desc().nparams; }

  void init(float s) {
    sr = s; ctx.sr = s;
    for (auto* m : mods) m->init(sr);
    src.init(sr); tuner.init(sr); muteG = 1.f; muteStep = 1.f / (0.010f * sr);
    for (int m = 0; m < NMOD; m++) {
      byp[m] = false; wet[m] = 1.f;
      for (int p = 0; p < MAXPAR; p++) {
        base[m][p] = p < nparams(m) ? mods[m]->desc().params[p].def : 0.f;
        conn[m][p] = -1; depth[m][p] = 0.22f; eff[m][p] = base[m][p];
      }
    }
    masterS.setMs(sr, 30); masterS.y = db2lin(masterDb);
    memset(meters, 0, sizeof meters);
    describe();
  }
  static float limit(float x) {                       // soft knee from 0.8, ceiling 0.98
    float a = fabsf(x);
    if (a <= 0.8f) return x;
    float y = 0.8f + 0.18f * tanhf((a - 0.8f) / 0.18f);
    return x < 0 ? -y : y;
  }
  void process(int n) {
    if (n > MAXBLOCK) n = MAXBLOCK;
    ctx.bpm = bpm;
    src.update(inBuf, n, sr, bpm);
    for (int m = 0; m < NMOD; m++) for (int p = 0; p < nparams(m); p++) {
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
      mods[m]->process(aL, aR, bL, bR, n, eff[m], ctx);
      for (int i = 0; i < n; i++) {
        float w = w0 + (w1 - w0) * (float)(i + 1) / (float)n;
        aL[i] += w * (bL[i] - aL[i]);
        aR[i] += w * (bR[i] - aR[i]);
      }
    }
    bool bad = false;
    tuner.process(inBuf, n);
    const float muteTarget = tuner.on && tuner.mute ? 0.f : 1.f;
    float g = db2lin(masterDb);
    for (int i = 0; i < n; i++) {
      muteG += muteG < muteTarget ? fminf(muteStep, muteTarget - muteG) : -fminf(muteStep, muteG - muteTarget);
      float mg = masterS.next(g) * muteG;
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
    publish();
  }
  void publish() {
    tuner.meters(&meters[M_TUN]);
    meters[M_NAN] = (float)nanResets;
    for (int k = 0; k < NSRC; k++) meters[M_SRC + k] = src.val[k];
    for (int m = 0; m < NMOD; m++) for (int p = 0; p < MAXPAR; p++) meters[M_EFF + m * MAXPAR + p] = eff[m][p];
    for (int m = 0; m < NMOD; m++) mods[m]->meters(&meters[M_MOD + m * MAXMODMETERS]);
    for (int k = 0; k < NSRC; k++) src.list[k]->meters(&meters[M_SRCM + k * MAXSRCMETERS]);
  }
  // Index lookups used by the text protocol.
  int moduleIndex(const char* id) const { for (int m = 0; m < NMOD; m++) if (!strcmp(mods[m]->desc().id, id)) return m; return -1; }
  int paramIndex(int m, const char* sym) const {
    const ModuleDesc& d = mods[m]->desc();
    for (int p = 0; p < d.nparams; p++) if (!strcmp(d.params[p].symbol, sym)) return p;
    return -1;
  }
  int sourceIndex(const char* id) const { for (int k = 0; k < NSRC; k++) if (!strcmp(src.list[k]->desc().id, id)) return k; return -1; }

  static void params(JsonOut& j, int n, const ParamDesc* ps) {
    j.ch('[');
    for (int p = 0; p < n; p++) {
      const ParamDesc& q = ps[p];
      if (p) j.ch(',');
      j.ch('{'); j.key("symbol"); j.str(q.symbol); j.ch(','); j.key("label"); j.str(q.label); j.ch(',');
      j.key("min"); j.num(q.min); j.ch(','); j.key("max"); j.num(q.max); j.ch(','); j.key("def"); j.num(q.def); j.ch(',');
      j.key("unit"); j.str(q.unit); j.ch(','); j.key("curve"); j.str(q.curve == Curve::Log ? "log" : "lin"); j.ch(',');
      j.key("decimals"); j.num(q.decimals); j.ch(','); j.key("sign"); j.raw(q.sign ? "true" : "false"); j.ch('}');
    }
    j.ch(']');
  }
  int srcParamIndex(int k, const char* sym) const {
    const SourceDesc& d = src.list[k]->desc();
    for (int p = 0; p < d.nparams; p++) if (!strcmp(d.params[p].symbol, sym)) return p;
    return -1;
  }
  void describe() {
    JsonOut j(descJson, sizeof descJson);
    j.ch('{');
    j.key("sr"); j.num(sr); j.ch(',');
    j.key("maxpar"); j.num(MAXPAR); j.ch(',');
    j.key("meters"); j.ch('{');
    j.key("nan"); j.num(M_NAN); j.ch(','); j.key("src"); j.num(M_SRC); j.ch(','); j.key("eff"); j.num(M_EFF); j.ch(',');
    j.key("mod"); j.num(M_MOD); j.ch(','); j.key("srcm"); j.num(M_SRCM); j.ch(','); j.key("len"); j.num(M_LEN); j.ch(',');
    j.key("perMod"); j.num(MAXMODMETERS); j.ch(','); j.key("perSrc"); j.num(MAXSRCMETERS); j.ch(',');
    j.key("tun"); j.num(M_TUN); j.ch(','); j.key("tunLen"); j.num(T_LEN); j.ch(','); j.key("tunBands"); j.num(TUN_BANDS);
    j.raw("},");
    j.key("modules"); j.ch('[');
    for (int m = 0; m < NMOD; m++) {
      const ModuleDesc& d = mods[m]->desc();
      if (m) j.ch(',');
      j.ch('{'); j.key("id"); j.str(d.id); j.ch(','); j.key("name"); j.str(d.name); j.ch(','); j.key("kind"); j.str(d.kind); j.ch(',');
      j.key("params"); params(j, d.nparams, d.params);
      j.ch(','); j.key("meters"); j.ch('[');
      for (int k = 0; k < d.nmeters; k++) { if (k) j.ch(','); j.str(d.meters[k]); }
      j.raw("],"); j.key("asset");
      if (!d.asset.kind) j.raw("null");
      else {
        j.ch('{'); j.key("kind"); j.str(d.asset.kind); j.ch(','); j.key("label"); j.str(d.asset.label); j.ch(',');
        j.key("exts"); j.str(d.asset.exts); j.ch(','); j.key("ready"); j.raw(d.asset.ready ? "true" : "false"); j.ch(',');
        j.key("maxBytes"); j.num(d.asset.maxBytes); j.ch('}');
      }
      j.ch('}');
    }
    j.raw("],");
    j.key("sources"); j.ch('[');
    for (int k = 0; k < NSRC; k++) {
      const SourceDesc& d = src.list[k]->desc();
      if (k) j.ch(',');
      j.ch('{'); j.key("id"); j.str(d.id); j.ch(','); j.key("name"); j.str(d.name); j.ch(','); j.key("sub"); j.str(d.sub); j.ch(',');
      j.key("ext"); j.str(d.ext); j.ch(','); j.key("params"); params(j, d.nparams, d.params); j.ch(','); j.key("meters"); j.ch('[');
      for (int q = 0; q < d.nmeters; q++) { if (q) j.ch(','); j.str(d.meters[q]); }
      j.raw("]}");
    }
    j.raw("]}");
  }
};

} // namespace rig

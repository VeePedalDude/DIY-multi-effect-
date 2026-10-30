// engine/engine.h - the chain: runs its module instances in order, applies modulation,
// bypass crossfades, master gain, limiter and NaN protection, and publishes meters
// plus a JSON self-description that the protocol and UI are built from.
//
// The chain is dynamic: modules are added, removed and moved while it plays (fx_chain.h).
//  - A module that's added starts fully dry and fades in over 10 ms (the bypass crossfade).
//  - One that's removed fades out over 10 ms, leaves the order, and is freed on a later control
//    call once the audio thread has finished a block without it (collect()).
//  - Moving one changes what you hear, so the signal dips for 10 ms either side of the swap: the
//    output, and the input of every module, so no delay or reverb records the jump to play it back later.
// Control-thread calls (add, remove, move, files) may allocate; process() never does.
//
// Nothing in here names a specific effect or source: both lists come from the
// registries, so adding one never touches this file.
#pragma once
#include "fx_chain.h"
#include "mod_rack.h"
#include "core/tuner.h"

namespace rig {

// Meter block layout (offsets are also published in describe()).
constexpr int M_NAN  = 0;                                   // NaN/Inf resets so far
constexpr int M_SRC  = 1;                                   // MAXSRC rack slots' values, -1..1 (0 when empty)
constexpr int M_EFF  = M_SRC + MAXSRC;                      // MAXMOD chain slots x MAXPAR effective params, 0..1
constexpr int M_MOD  = M_EFF + MAXMOD * MAXPAR;             // MAXMOD x MAXMODMETERS module display values
constexpr int M_SRCM = M_MOD + MAXMOD * MAXMODMETERS;       // MAXSRC x MAXSRCMETERS source display values
constexpr int M_SEFF = M_SRCM + MAXSRC * MAXSRCMETERS;      // MAXSRC x MAXPAR source knobs after cross-modulation, 0..1
constexpr int M_TUN  = M_SEFF + MAXSRC * MAXPAR;            // the tuner (core/tuner.h TunerMeter)
constexpr int M_LEN  = M_TUN + T_LEN;

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
  // The chain: module instances by slot, and the order they run in.
  Module* mods[MAXMOD] = {nullptr};                         // nullptr = free slot
  int mtype[MAXMOD];                                        // registry index of each slot's type
  char mid[MAXMOD][MOD_ID_LEN];                             // instance ids ("holo", "holo2"); empty while dying
  int dying[MAXMOD];                                        // 0 alive; 1 fading out; 2 out of the order, waiting to be freed
  unsigned long dyingAt[MAXMOD];
  Order order;                                              // the chain as the control thread (and the UI) sees it
  std::atomic<uint64_t> wantOrder{0};                       // control -> audio: the order to run (includes modules still fading out)
  std::atomic<uint64_t> curOrder{0};                        // audio -> control: the order it runs now
  std::atomic<unsigned long> blocks{0};
  uint64_t runWord = 0;                                     // audio thread's copy
  float dipG = 1.f, dipTarget = 1.f, dipStep = 0.f;         // a 10 ms dip either side of a reorder you'd hear
  float dipRamp[MAXBLOCK];
  ModRack rack;                                             // modulation sources (mod_rack.h)
  Context ctx;
  float sr = 48000, bpm = 120;
  float base[MAXMOD][MAXPAR];
  int   conn[MAXMOD][MAXPAR];
  float depth[MAXMOD][MAXPAR];
  Tap   tap[MAXMOD][MAXPAR];                                // each patch's PHASE and DIV (core/tap.h)
  float eff[MAXMOD][MAXPAR];
  bool  byp[MAXMOD];
  float wet[MAXMOD];
  float inBuf[MAXBLOCK], outL[MAXBLOCK], outR[MAXBLOCK];
  float aL[MAXBLOCK], aR[MAXBLOCK], bL[MAXBLOCK], bR[MAXBLOCK];
  float masterDb = 0.f; OnePole masterS;
  Tuner tuner; float muteG = 1.f, muteStep = 0.f;          // tuner on the dry input; output fades out while tuning with mute
  DCBlock dcL, dcR;
  int nanResets = 0;
  float meters[M_LEN];
  char descJson[24576];
  char rackJson[4096];
  char chainJson[2048];

  Engine() { for (int k = 0; k < MAXMOD; k++) { mtype[k] = -1; mid[k][0] = 0; dying[k] = 0; dyingAt[k] = 0; byp[k] = false; wet[k] = 0.f; } }
  ~Engine() { for (auto*& m : mods) { delete m; m = nullptr; } }
  Engine(const Engine&) = delete; Engine& operator=(const Engine&) = delete;

  int nparams(int m) const { return mods[m] ? mods[m]->desc().nparams : 0; }
  bool used(int m) const { return m >= 0 && m < MAXMOD && mods[m] && !dying[m]; }   // a live instance (not fading out)

  // A fresh engine: the default chain (one of each type, registry order) and the default rack.
  void init(float s) {
    sr = s; ctx.sr = s;
    for (int k = 0; k < MAXMOD; k++) { delete mods[k]; mods[k] = nullptr; mtype[k] = -1; mid[k][0] = 0; dying[k] = 0; }
    order = Order();
    int nd = 0; const char* const* ids = defaultChain(nd);    // engine/modules/default_chain.txt
    for (int i = 0; i < nd; i++) addModule(ids[i], nullptr, -1, false);
    runWord = wantOrder.load(); curOrder.store(runWord); dipG = dipTarget = 1.f; dipStep = 1.f / (0.010f * sr);
    rack.init(sr); tuner.init(sr); muteG = 1.f; muteStep = 1.f / (0.010f * sr);
    masterS.setMs(sr, 30); masterS.y = db2lin(masterDb);
    memset(meters, 0, sizeof meters);
    describe(); writeRack(); writeChain();
  }

  // ------------------------------------------------------------------ the chain (control thread)
  int moduleIndex(const char* id) const { for (int m = 0; m < MAXMOD; m++) if (used(m) && !strcmp(mid[m], id)) return m; return -1; }   // slot of an instance
  int chainPos(int m) const { return order.indexOf(m); }
  // The next free id for a type: "holo", then "holo2"... (never a source's id)
  void autoModuleId(int t, char* out) const {
    const char* base0 = moduleTypeDesc(t).id;
    for (int n = 1; n < 100; n++) {
      int len = 0; while (base0[len] && len < MOD_ID_LEN - 3) { out[len] = base0[len]; len++; }
      if (n >= 10) out[len++] = (char)('0' + n / 10);
      if (n >= 2) out[len++] = (char)('0' + n % 10);
      out[len] = 0;
      if (moduleIndex(out) < 0 && rack.find(out) < 0) return;
    }
  }
  // Publish the order the audio thread should run: the chain, plus modules still fading out where they were.
  void publishOrder() {
    Order w = order; const Order old = Order::unpack(wantOrder.load(std::memory_order_acquire));
    for (int i = 0; i < old.n; i++) if (mods[old.s[i]] && dying[old.s[i]] == 1 && w.indexOf(old.s[i]) < 0) w.insert(i < w.n ? i : w.n, old.s[i]);
    wantOrder.store(w.pack(), std::memory_order_release);
  }
  // Add an instance of `type` at chain position `pos` (-1 = the end); id nullptr or "" = automatic.
  // It starts fully dry and fades in (fadeIn = false: straight in, for a fresh engine). Returns its slot or a ChainError.
  int addModule(const char* type, const char* id, int pos = -1, bool fadeIn = true) {
    collect(false);
    const int t = moduleTypeIndex(type); if (t < 0) return CHAIN_BAD_TYPE;
    char want[MOD_ID_LEN];
    if (id && id[0]) { if (!validInstanceId(id) || moduleIndex(id) >= 0 || rack.find(id) >= 0) return CHAIN_BAD_ID; memcpy(want, id, strlen(id) + 1); }
    else autoModuleId(t, want);
    int k = 0; while (k < MAXMOD && mods[k]) k++;
    if (k == MAXMOD || order.n >= MAXMOD) return CHAIN_FULL;
    Module* m = moduleTypes()[t].make(); m->init(sr);
    const ModuleDesc& d = m->desc();
    for (int p = 0; p < MAXPAR; p++) { base[k][p] = eff[k][p] = p < d.nparams ? d.params[p].def : 0.f; conn[k][p] = -1; depth[k][p] = 0.22f; tap[k][p].reset(); }
    byp[k] = false; wet[k] = fadeIn ? 0.f : 1.f; dying[k] = 0; mtype[k] = t; memcpy(mid[k], want, MOD_ID_LEN);
    mods[k] = m;
    order.insert(pos, k); publishOrder(); writeChain();
    return k;
  }
  // Take an instance out: it fades out where it is, then leaves the order and is freed (collect()).
  int removeModule(int m) {
    if (!used(m)) return CHAIN_NO_SUCH;
    byp[m] = true; dying[m] = 1; dyingAt[m] = blocks.load(); mid[m][0] = 0;
    for (int p = 0; p < MAXPAR; p++) { conn[m][p] = -1; tap[m][p].reset(); }
    order.erase(m); writeChain();                           // gone from the chain you see; still in the audio order until faded
    collect(false);
    return 0;
  }
  // Move an instance to chain position `pos`.
  int moveModule(int m, int pos) {
    if (!used(m)) return CHAIN_NO_SUCH;
    collect(false);
    order.erase(m); order.insert(pos, m); publishOrder(); writeChain();
    return 0;
  }
  // Housekeeping (control thread): modules that have faded out leave the order; ones the audio
  // thread has finished with are freed. `force` frees everything that's fading (no audio running).
  void collect(bool force) {
    bool changed = false;
    for (int m = 0; m < MAXMOD; m++) {
      if (!mods[m] || !dying[m]) continue;
      if (dying[m] == 1 && (force || wet[m] <= 0.f)) { dying[m] = 2; dyingAt[m] = blocks.load(); changed = true; }
    }
    if (changed) {                                          // publish an order without the faded ones (inaudible, so no dip)
      Order w = Order::unpack(wantOrder.load(std::memory_order_acquire));
      for (int m = 0; m < MAXMOD; m++) if (mods[m] && dying[m] == 2) w.erase(m);
      wantOrder.store(w.pack(), std::memory_order_release);
    }
    const Order cur = Order::unpack(curOrder.load(std::memory_order_acquire));
    for (int m = 0; m < MAXMOD; m++) {
      if (!mods[m] || dying[m] != 2) continue;
      if (force || (cur.indexOf(m) < 0 && blocks.load() > dyingAt[m] + 1)) { delete mods[m]; mods[m] = nullptr; mtype[m] = -1; dying[m] = 0; }
    }
  }
  static void chainList(JsonOut& j, const Engine& e) {
    j.ch('[');
    for (int i = 0; i < e.order.n; i++) {
      const int m = e.order.s[i];
      if (i) { j.ch(','); }
      j.ch('{'); j.key("id"); j.str(e.mid[m]); j.ch(','); j.key("type"); j.str(moduleTypeDesc(e.mtype[m]).id); j.ch(','); j.key("slot"); j.num(m); j.ch('}');
    }
    j.ch(']');
  }
  // {"max":12,"modules":[{"id":"gate","type":"gate","slot":0},...]}: the chain, in order (rig_chain()).
  void writeChain() { JsonOut j(chainJson, sizeof chainJson); j.ch('{'); j.key("max"); j.num(MAXMOD); j.ch(','); j.key("modules"); chainList(j, *this); j.ch('}'); }

  static float limit(float x) {                       // soft knee from 0.8, ceiling 0.98
    float a = fabsf(x);
    if (a <= 0.8f) return x;
    float y = 0.8f + 0.18f * tanhf((a - 0.8f) / 0.18f);
    return x < 0 ? -y : y;
  }
  // Would going from order a to order b change what you hear? Only modules you can hear count.
  bool sameAudible(uint64_t a, uint64_t b) const {
    const Order x = Order::unpack(a), y = Order::unpack(b); int i = 0, j = 0;
    while (true) {
      while (i < x.n && !(wet[x.s[i]] > 0.f)) i++;
      while (j < y.n && !(wet[y.s[j]] > 0.f)) j++;
      if (i == x.n || j == y.n) return i == x.n && j == y.n;
      if (x.s[i++] != y.s[j++]) return false;
    }
  }
  void process(int n) {
    if (n > MAXBLOCK) n = MAXBLOCK;
    ctx.bpm = bpm;
    const uint64_t want = wantOrder.load(std::memory_order_acquire);
    if (want != runWord) {
      if (dipG <= 0.f || sameAudible(runWord, want)) runWord = want;   // instant when nothing you hear moves
      else dipTarget = 0.f;                                             // else dip, then swap
    }
    if (want == runWord) dipTarget = 1.f;
    curOrder.store(runWord, std::memory_order_release);
    const Order ord = Order::unpack(runWord);
    rack.update(inBuf, n, bpm);
    for (int i = 0; i < ord.n; i++) for (int m = ord.s[i], p = 0; p < nparams(m); p++) {
      float v = base[m][p];
      if (conn[m][p] >= 0) v += depth[m][p] * rack.patchValue(conn[m][p], tap[m][p], (float)n / sr);
      eff[m][p] = clampf(v, 0.f, 1.f);
    }
    memcpy(aL, inBuf, n * sizeof(float)); memcpy(aR, inBuf, n * sizeof(float));
    const bool dipping = dipG != 1.f || dipTarget != 1.f;
    if (dipping) for (int i = 0; i < n; i++) { if (dipG != dipTarget) dipG = dipTarget > dipG ? fminf(dipTarget, dipG + dipStep) : fmaxf(dipTarget, dipG - dipStep); dipRamp[i] = dipG; }
    float step = (float)n / (0.010f * sr);              // 10 ms bypass crossfade
    for (int i = 0; i < ord.n; i++) {
      const int m = ord.s[i];
      float w0 = wet[m], w1 = byp[m] ? fmaxf(0.f, w0 - step) : fminf(1.f, w0 + step);
      wet[m] = w1;
      if (w0 == 0.f && w1 == 0.f) continue;           // fully bypassed: dry passes untouched
      if (dipping) for (int i = 0; i < n; i++) { aL[i] *= dipRamp[i]; aR[i] *= dipRamp[i]; }
      mods[m]->process(aL, aR, bL, bR, n, eff[m], ctx);
      for (int i = 0; i < n; i++) {
        float w = w0 + (w1 - w0) * (float)(i + 1) / (float)n;
        aL[i] += w * (bL[i] - aL[i]);
        aR[i] += w * (bR[i] - aR[i]);
      }
    }
    if (dipping) for (int i = 0; i < n; i++) { aL[i] *= dipRamp[i]; aR[i] *= dipRamp[i]; }
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
      for (int i = 0; i < ord.n; i++) mods[ord.s[i]]->reset();
      dcL.reset(); dcR.reset();
      memset(outL, 0, n * sizeof(float)); memset(outR, 0, n * sizeof(float));
      nanResets++;
    }
    publish();
    blocks.fetch_add(1, std::memory_order_acq_rel);
  }
  void publish() {
    tuner.meters(&meters[M_TUN]);
    meters[M_NAN] = (float)nanResets;
    for (int k = 0; k < MAXSRC; k++) meters[M_SRC + k] = rack.slot[k].val;
    for (int m = 0; m < MAXMOD; m++) {
      const bool live = mods[m] && Order::unpack(runWord).indexOf(m) >= 0;
      for (int p = 0; p < MAXPAR; p++) meters[M_EFF + m * MAXPAR + p] = live ? eff[m][p] : 0.f;
      float* mm = &meters[M_MOD + m * MAXMODMETERS]; for (int q = 0; q < MAXMODMETERS; q++) mm[q] = 0.f;
      if (live) mods[m]->meters(mm);
    }
    for (int k = 0; k < MAXSRC; k++) {
      float* sm = &meters[M_SRCM + k * MAXSRCMETERS]; for (int q = 0; q < MAXSRCMETERS; q++) sm[q] = 0.f;
      rack.meters(k, sm);
      for (int p = 0; p < MAXPAR; p++) meters[M_SEFF + k * MAXPAR + p] = rack.slot[k].eff[p];
    }
  }
  // Index lookups used by the text protocol.
  int paramIndex(int m, const char* sym) const {
    if (!mods[m]) return -1;
    const ModuleDesc& d = mods[m]->desc();
    for (int p = 0; p < d.nparams; p++) if (!strcmp(d.params[p].symbol, sym)) return p;
    return -1;
  }
  int sourceIndex(const char* id) const { return rack.find(id); }   // rack slot of a source instance

  // The rack, from the control thread. Instance ids may not clash with module ids.
  int addSource(const char* type, const char* id) {
    if (id && id[0] && moduleIndex(id) >= 0) return RACK_BAD_ID;
    int k = rack.add(sourceTypeIndex(type), id);
    if (k >= 0 && moduleIndex(rack.slot[k].id) >= 0) { rack.remove(k); return RACK_BAD_ID; }
    if (k >= 0) writeRack();
    return k;
  }
  int removeSource(int k) {
    if (!rack.used(k)) return RACK_NO_SUCH;
    for (int m = 0; m < MAXMOD; m++) for (int p = 0; p < MAXPAR; p++) if (conn[m][p] == k) { conn[m][p] = -1; tap[m][p].reset(); }
    rack.remove(k); writeRack();
    return 0;
  }
  static void rackList(JsonOut& j, const ModRack& r) {
    j.ch('['); bool first = true;
    for (int k = 0; k < MAXSRC; k++) if (r.used(k)) {
      if (!first) { j.ch(','); }
      first = false;
      j.ch('{'); j.key("id"); j.str(r.slot[k].id); j.ch(','); j.key("type"); j.str(sourceTypeDesc(r.slot[k].type).id); j.ch(','); j.key("slot"); j.num(k); j.ch('}');
    }
    j.ch(']');
  }
  // {"max":16,"sources":[{"id":"lfo","type":"lfo","slot":0},...]}: what's in the rack now (rig_rack()).
  void writeRack() { JsonOut j(rackJson, sizeof rackJson); j.ch('{'); j.key("max"); j.num(MAXSRC); j.ch(','); j.key("sources"); rackList(j, rack); j.ch('}'); }

  static void params(JsonOut& j, int n, const ParamDesc* ps) {
    j.ch('[');
    for (int p = 0; p < n; p++) {
      const ParamDesc& q = ps[p];
      if (p) j.ch(',');
      j.ch('{'); j.key("symbol"); j.str(q.symbol); j.ch(','); j.key("label"); j.str(q.label); j.ch(',');
      j.key("min"); j.num(q.min); j.ch(','); j.key("max"); j.num(q.max); j.ch(','); j.key("def"); j.num(q.def); j.ch(',');
      j.key("unit"); j.str(q.unit); j.ch(','); j.key("curve"); j.str(q.curve == Curve::Log ? "log" : "lin"); j.ch(',');
      j.key("decimals"); j.num(q.decimals); j.ch(','); j.key("sign"); j.raw(q.sign ? "true" : "false");
      if (q.nstops) {                                         // named stops: [[real value, "name"], ...]
        j.ch(','); j.key("stops"); j.ch('[');
        for (int i = 0; i < q.nstops; i++) { if (i) j.ch(','); j.ch('['); j.num(q.stops[i].v); j.ch(','); j.str(q.stops[i].name); j.ch(']'); }
        j.ch(']');
      }
      j.ch('}');
    }
    j.ch(']');
  }
  int srcParamIndex(int k, const char* sym) const {
    if (!rack.used(k)) return -1;
    const SourceDesc& d = sourceTypeDesc(rack.slot[k].type);
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
    j.key("perMod"); j.num(MAXMODMETERS); j.ch(','); j.key("perSrc"); j.num(MAXSRCMETERS); j.ch(','); j.key("seff"); j.num(M_SEFF); j.ch(',');
    j.key("tun"); j.num(M_TUN); j.ch(','); j.key("tunLen"); j.num(T_LEN); j.ch(','); j.key("tunBands"); j.num(TUN_BANDS);
    j.raw("},");
    j.key("maxModules"); j.num(MAXMOD); j.ch(',');
    j.key("moduleCategories"); j.ch('[');                   // the tabs of the add panel, in order
    for (int c = 0; c < NMODCATS; c++) { if (c) j.ch(','); j.ch('{'); j.key("id"); j.str(moduleCategories()[c].id); j.ch(','); j.key("label"); j.str(moduleCategories()[c].label); j.ch(','); j.key("blurb"); j.str(moduleCategories()[c].blurb); j.ch('}'); }
    j.raw("],");
    j.key("moduleTypes"); j.ch('[');                         // every kind of effect the chain can hold
    for (int t = 0; t < NMODTYPES; t++) {
      const ModuleDesc& d = moduleTypeDesc(t);
      if (t) j.ch(',');
      j.ch('{'); j.key("type"); j.str(d.id); j.ch(','); j.key("category"); j.str(moduleTypes()[t].category); j.ch(','); j.key("name"); j.str(d.name); j.ch(','); j.key("kind"); j.str(d.kind); j.ch(',');
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
    j.key("modules"); chainList(j, *this); j.ch(',');      // the chain as it starts: one of each type
    j.key("maxSources"); j.num(MAXSRC); j.ch(',');
    j.key("tapDivs"); j.ch('[');                             // per-destination DIV choices (core/tap.h)
    for (int i = 0; i < NTAPDIVS; i++) { if (i) j.ch(','); j.ch('['); j.num(TAP_DIVS[i]); j.ch(','); j.str(TAP_DIV_NAMES[i]); j.ch(']'); }
    j.raw("],");
    j.key("sourceCategories"); j.ch('[');
    for (int c = 0; c < NSRCCATS; c++) { if (c) j.ch(','); j.ch('{'); j.key("id"); j.str(sourceCategories()[c].id); j.ch(','); j.key("label"); j.str(sourceCategories()[c].label); j.ch(','); j.key("blurb"); j.str(sourceCategories()[c].blurb); j.ch('}'); }
    j.raw("],");
    j.key("sourceTypes"); j.ch('[');                         // every kind of source the rack can hold
    for (int t = 0; t < NSRCTYPES; t++) {
      const SourceDesc& d = sourceTypeDesc(t);
      if (t) j.ch(',');
      j.ch('{'); j.key("type"); j.str(d.id); j.ch(','); j.key("category"); j.str(sourceTypes()[t].category); j.ch(','); j.key("name"); j.str(d.name); j.ch(','); j.key("sub"); j.str(d.sub); j.ch(',');
      j.key("ext"); j.str(d.ext); j.ch(','); j.key("taps"); j.raw(d.taps ? "true" : "false"); j.ch(',');
      j.key("params"); params(j, d.nparams, d.params); j.ch(','); j.key("meters"); j.ch('[');
      for (int q = 0; q < d.nmeters; q++) { if (q) j.ch(','); j.str(d.meters[q]); }
      j.raw("]}");
    }
    j.raw("],");
    j.key("sources"); rackList(j, rack);                     // the rack as it starts: one of each type
    j.ch('}');
  }
};

} // namespace rig

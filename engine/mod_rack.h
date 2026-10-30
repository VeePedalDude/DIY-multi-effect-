// engine/mod_rack.h - the modulation rack: source instances you can add and remove while it plays,
// and cross-modulation (one source moving another source's knob).
//
//  - Source *types* are the registry (engine/sources/registry.h): one file each. The rack holds up
//    to MAXSRC *instances* of them in slots. Several instances of one type are fine (two LFOs).
//  - Each instance has an id: the type id for the first ("lfo"), then "lfo2", "lfo3"... or one the
//    host chooses ([a-z][a-z0-9_]*, up to 15 characters).
//  - A fresh rack holds the types in engine/sources/default_rack.txt, ids = type ids, so the default
//    rig (and every older preset) is exactly what it was before the rack existed.
//  - Cross-modulation: any instance's knob can take one source, with a depth, like a module's knob.
//    All knobs are worked out from the sources' values of the previous block (2.7 ms ago), so any
//    patch, including loops and a source moving its own knob, is well defined and stable.
//
// Threads: add() and remove() run on the control thread and allocate there (new/delete). The audio
// thread only reads a slot's instance pointer (atomic). A removed instance is freed on a later
// control call, once the audio thread has finished a block without it. update() never allocates.
// Nothing in here names a specific source.
#pragma once
#include <atomic>
#include <cstring>
#include "sources/registry.h"
#include "core/tap.h"

namespace rig {

#define RIG_COUNT(T, m, c) +1
constexpr int NSRCTYPES = 0 RIG_SOURCES(RIG_COUNT);
constexpr int NSRCCATS = 0 RIG_SOURCE_CATEGORIES(RIG_COUNT);
#undef RIG_COUNT
constexpr int MAXSRC = 16;                 // instances in the rack
constexpr int SRC_ID_LEN = 16;             // id, including the terminating zero

// The registry as a table: each type's description and a way to make one.
struct SourceType { const SourceDesc& (*desc)(); Source* (*make)(); const char* category; };
#define RIG_TYPE(T, m, c) SourceType{ []() -> const SourceDesc& { static T proto; return proto.desc(); }, []() -> Source* { return new T(); }, #c },
inline const SourceType* sourceTypes() { static const SourceType t[NSRCTYPES] = { RIG_SOURCES(RIG_TYPE) }; return t; }
#undef RIG_TYPE
inline const SourceDesc& sourceTypeDesc(int t) { return sourceTypes()[t].desc(); }
// The categories (folders, and the tabs of the add panel), in order, and what a fresh rack holds.
struct SourceCategory { const char* id; const char* label; const char* blurb; };
#define RIG_SCAT(c, label, blurb) SourceCategory{ #c, label, blurb },
inline const SourceCategory* sourceCategories() { static const SourceCategory t[NSRCCATS] = { RIG_SOURCE_CATEGORIES(RIG_SCAT) }; return t; }
#undef RIG_SCAT
inline const char* const* defaultRack(int& n) { static const char* const ids[] = { RIG_DEFAULT_RACK }; n = (int)(sizeof ids / sizeof ids[0]); return ids; }
inline int sourceTypeIndex(const char* type) { for (int t = 0; t < NSRCTYPES; t++) if (!strcmp(sourceTypeDesc(t).id, type)) return t; return -1; }

enum RackError { RACK_BAD_TYPE = -1, RACK_FULL = -2, RACK_BAD_ID = -3, RACK_NO_SUCH = -4 };

struct SrcSlot {
  std::atomic<Source*> s{nullptr};
  int type = -1;
  char id[SRC_ID_LEN] = {0};
  float base[MAXPAR] = {0};                // knobs as set, normalised
  float eff[MAXPAR] = {0};                 // knobs after cross-modulation (what the source runs with)
  int conn[MAXPAR];                        // slot of the source moving each knob, or -1
  float depth[MAXPAR];                     // its swing, +/- in normalised units
  Tap tap[MAXPAR];                         // its PHASE and DIV, for a tapping source (core/tap.h)
  float val = 0;                           // output this block, -1..1
  int nparams() const { return type >= 0 ? sourceTypeDesc(type).nparams : 0; }
};

struct ModRack {
  SrcSlot slot[MAXSRC];
  float sr = 48000;
  std::atomic<unsigned long> blocks{0};    // audio blocks run, for freeing removed instances safely
  Source* retired[MAXSRC * 2] = {nullptr}; unsigned long retiredAt[MAXSRC * 2] = {0}; int nRetired = 0;

  ModRack() { for (auto& s : slot) for (int p = 0; p < MAXPAR; p++) { s.conn[p] = -1; s.depth[p] = 0.22f; } }
  ~ModRack() { clearAll(); collect(true); }
  ModRack(const ModRack&) = delete; ModRack& operator=(const ModRack&) = delete;

  // A fresh rack: one of every type, ids = type ids (control thread; not while audio runs).
  void init(float s) {
    sr = s; clearAll(); collect(true);
    int n = 0; const char* const* ids = defaultRack(n);      // engine/sources/default_rack.txt
    for (int i = 0; i < n; i++) add(sourceTypeIndex(ids[i]), nullptr);
  }
  bool used(int k) const { return k >= 0 && k < MAXSRC && slot[k].type >= 0; }
  int find(const char* id) const { for (int k = 0; k < MAXSRC; k++) if (used(k) && !strcmp(slot[k].id, id)) return k; return -1; }
  int count() const { int n = 0; for (int k = 0; k < MAXSRC; k++) n += used(k); return n; }
  static bool validId(const char* id) {
    if (!id || !(id[0] >= 'a' && id[0] <= 'z')) return false;
    int n = 0; for (; id[n]; n++) { const char c = id[n]; if (n >= SRC_ID_LEN - 1 || !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false; }
    return true;
  }
  // The next free id for a type: "lfo", then "lfo2", "lfo3"... (no printf: the WebAssembly build has no imports)
  void autoId(int t, char* out) const {
    const char* base = sourceTypeDesc(t).id;
    for (int n = 1; n < 100; n++) {
      int len = 0; while (base[len] && len < SRC_ID_LEN - 3) { out[len] = base[len]; len++; }
      if (n >= 10) out[len++] = (char)('0' + n / 10);
      if (n >= 2) out[len++] = (char)('0' + n % 10);
      out[len] = 0;
      if (find(out) < 0) return;
    }
  }
  // Add an instance of type t (id nullptr or "" = automatic). Returns its slot, or a RackError.
  int add(int t, const char* id) {
    collect(false);
    if (t < 0 || t >= NSRCTYPES) return RACK_BAD_TYPE;
    char want[SRC_ID_LEN];
    if (id && id[0]) { if (!validId(id) || find(id) >= 0) return RACK_BAD_ID; memcpy(want, id, strlen(id) + 1); }
    else autoId(t, want);
    int k = 0; while (k < MAXSRC && used(k)) k++;
    if (k == MAXSRC) return RACK_FULL;
    Source* src = sourceTypes()[t].make(); src->init(sr);
    SrcSlot& S = slot[k];
    const SourceDesc& d = sourceTypeDesc(t);
    for (int p = 0; p < MAXPAR; p++) { S.base[p] = S.eff[p] = p < d.nparams ? d.params[p].def : 0.f; S.conn[p] = -1; S.depth[p] = 0.22f; S.tap[p].reset(); }
    memcpy(S.id, want, SRC_ID_LEN); S.val = 0; S.type = t;
    S.s.store(src, std::memory_order_release);       // the audio thread sees it from its next block
    return k;
  }
  // Remove the instance in slot k, and every cross-mod patch from it. (The engine also unpatches
  // module knobs; see Engine::removeSource.) Returns 0 or RACK_NO_SUCH.
  int remove(int k) {
    collect(false);
    if (!used(k)) return RACK_NO_SUCH;
    Source* old = slot[k].s.exchange(nullptr, std::memory_order_acq_rel);
    for (int j = 0; j < MAXSRC; j++) for (int p = 0; p < MAXPAR; p++) if (slot[j].conn[p] == k) { slot[j].conn[p] = -1; slot[j].tap[p].reset(); }
    slot[k].type = -1; slot[k].id[0] = 0; slot[k].val = 0;
    for (int p = 0; p < MAXPAR; p++) { slot[k].conn[p] = -1; slot[k].base[p] = slot[k].eff[p] = 0.f; }
    if (old) { if (nRetired == MAXSRC * 2) collect(true); retired[nRetired] = old; retiredAt[nRetired++] = blocks.load(std::memory_order_acquire); }
    return 0;
  }
  void clearAll() { for (int k = 0; k < MAXSRC; k++) if (used(k)) remove(k); }
  // Free removed instances the audio thread can no longer be using (`force`: no audio is running).
  void collect(bool force) {
    const unsigned long now = blocks.load(std::memory_order_acquire);
    int w = 0;
    for (int i = 0; i < nRetired; i++) {
      if (force || now > retiredAt[i]) delete retired[i];
      else { retired[w] = retired[i]; retiredAt[w++] = retiredAt[i]; }
    }
    nRetired = w;
  }
  // Patch knob p of slot k from slot `from` (-1 unpatches). Returns 0 or a RackError.
  int patch(int k, int p, int from, float depth) {
    if (!used(k) || p < 0 || p >= slot[k].nparams()) return RACK_NO_SUCH;
    if (from >= 0 && !used(from)) return RACK_NO_SUCH;
    if (from != slot[k].conn[p]) slot[k].tap[p].reset();       // a new patch starts at 0 degrees, 1x
    slot[k].depth[p] = clampf(depth, 0.f, 1.f); slot[k].conn[p] = from;
    return 0;
  }
  // PHASE (degrees) and DIV (a TAP_DIVS value; the nearest is taken) of the patch on knob p of slot k.
  int tapSet(int k, int p, float deg, float div) {
    if (!used(k) || p < 0 || p >= slot[k].nparams()) return RACK_NO_SUCH;
    slot[k].tap[p].set(deg, div);
    return 0;
  }
  // What a patch from slot c gives this block: the source's value, or its value at the patch's own
  // PHASE and DIV when the source taps and the patch is set (audio thread).
  float patchValue(int c, Tap& t, float dt) {
    if (t.plain()) { t.follow(); return slot[c].val; }
    Source* s = slot[c].s.load(std::memory_order_acquire);
    if (!s || !s->desc().taps) { t.follow(); return slot[c].val; }
    return t.eval(*s, dt);
  }

  // Audio thread: every instance's knobs from last block's values, then this block's values.
  void update(const float* in, int n, float bpm) {
    Source* live[MAXSRC];
    for (int k = 0; k < MAXSRC; k++) {
      SrcSlot& S = slot[k];
      live[k] = S.s.load(std::memory_order_acquire);
      if (!live[k]) continue;
      for (int p = 0; p < MAXPAR; p++) {
        float v = S.base[p];
        const int c = S.conn[p];
        if (c >= 0 && c < MAXSRC) v += S.depth[p] * patchValue(c, S.tap[p], (float)n / sr);
        S.eff[p] = clampf(v, 0.f, 1.f);
      }
    }
    float next[MAXSRC];
    for (int k = 0; k < MAXSRC; k++) {
      if (!live[k]) { next[k] = 0.f; continue; }
      SourceContext c{in, n, sr, (float)n / sr, bpm, slot[k].eff};
      next[k] = clampf(live[k]->update(c), -1.f, 1.f);
      if (!(next[k] == next[k])) next[k] = 0.f;                   // a NaN never reaches a knob
    }
    for (int k = 0; k < MAXSRC; k++) slot[k].val = next[k];
    blocks.fetch_add(1, std::memory_order_acq_rel);
  }
  void meters(int k, float* out) const { Source* s = slot[k].s.load(std::memory_order_acquire); if (s) s->meters(out); }
  void ext(int k, float v) { Source* s = slot[k].s.load(std::memory_order_acquire); if (s) s->ext(v); }
};

} // namespace rig

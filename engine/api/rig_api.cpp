// engine/api/rig_api.cpp - flat C API. The browser (WebAssembly) and future hosts
// (the Linux JACK host) talk to the engine only through these functions.
#include "../engine.h"
#include <new>

#if defined(__wasm__)
#define EXPORT(name) extern "C" __attribute__((export_name(#name)))
#else
#define EXPORT(name) extern "C"
#endif

// Placement-new into zeroed static storage keeps the big delay buffers out of the binary
// (zero memory is free in wasm). Loaded captures and IRs live on the heap (see core/model_slot.h).
alignas(16) static unsigned char g_storage[sizeof(rig::Engine)];
static rig::Engine* g_engine = nullptr;
static rig::Engine& E() { return *g_engine; }

EXPORT(rig_init)     void   rig_init(float sr) { if (!g_engine) g_engine = new (g_storage) rig::Engine(); E().init(sr); }
EXPORT(rig_describe) const char* rig_describe() { return E().descJson; }
EXPORT(rig_in)       float* rig_in() { return E().inBuf; }
EXPORT(rig_out_l)    float* rig_out_l() { return E().outL; }
EXPORT(rig_out_r)    float* rig_out_r() { return E().outR; }
EXPORT(rig_meters)   float* rig_meters() { return E().meters; }
EXPORT(rig_meters_len) int  rig_meters_len() { return rig::M_LEN; }
EXPORT(rig_process)  void   rig_process(int n) { E().process(n); }

EXPORT(rig_param) void rig_param(int m, int p, float v) {
  if (m < 0 || m >= rig::NMOD || p < 0 || p >= E().nparams(m)) return;
  E().base[m][p] = rig::clampf(v, 0.f, 1.f);
}
EXPORT(rig_bypass) void rig_bypass(int m, int on) { if (m >= 0 && m < rig::NMOD) E().byp[m] = on != 0; }
// src = -1 disconnects; depth is the +/- swing in normalised units around the knob
EXPORT(rig_mod) void rig_mod(int m, int p, int src, float depth) {
  if (m < 0 || m >= rig::NMOD || p < 0 || p >= E().nparams(m)) return;
  E().conn[m][p] = E().rack.used(src) ? src : -1;
  E().depth[m][p] = rig::clampf(depth, 0.f, 1.f);
}
// Sources are addressed by rack slot (rig_rack() lists them; a fresh rack has one of each type in
// registry order, slots 0..). A source's knob (LFO rate, ADSR attack...), normalised 0..1.
EXPORT(rig_src_param) void rig_src_param(int src, int p, float v) {
  if (!E().rack.used(src) || p < 0 || p >= E().rack.slot[src].nparams()) return;
  E().rack.slot[src].base[p] = rig::clampf(v, 0.f, 1.f);
}
// The rack (control thread; allocates). Text in and out goes through a small shared buffer:
// write "<type> [id]" to rig_text(), then rig_src_add() returns the new slot, or a negative error
// (-1 unknown type, -2 rack full, -3 bad or taken id).
static char g_text[64];
EXPORT(rig_text) char* rig_text() { return g_text; }
EXPORT(rig_src_add) int rig_src_add() {
  g_text[sizeof g_text - 1] = 0;
  char type[32] = {0}, id[rig::SRC_ID_LEN + 8] = {0}; int i = 0, n = 0;
  while (g_text[i] == ' ') i++;
  while (g_text[i] && g_text[i] != ' ' && n < (int)sizeof type - 1) type[n++] = g_text[i++];
  while (g_text[i] == ' ') i++;
  n = 0; while (g_text[i] && g_text[i] != ' ' && n < (int)sizeof id - 1) id[n++] = g_text[i++];
  return E().addSource(type, id);
}
EXPORT(rig_src_remove) int rig_src_remove(int src) { return E().removeSource(src); }
// Cross-modulation: knob p of source `src` moved by source `from` (-1 unpatches), +/- depth.
EXPORT(rig_src_mod) int rig_src_mod(int src, int p, int from, float depth) { return E().rack.patch(src, p, from, depth); }
EXPORT(rig_rack) const char* rig_rack() { return E().rackJson; }
// Tuner on the dry input: on/off, and whether the output is muted while it's on.
EXPORT(rig_tuner) void rig_tuner(int on, int mute) { E().tuner.on = on != 0; E().tuner.mute = mute != 0; }
EXPORT(rig_tuner_ref) void rig_tuner_ref(float a4) { E().tuner.a4 = rig::clampf(a4, 400.f, 480.f); }
EXPORT(rig_ext)    void rig_ext(int src, float v) { if (E().rack.used(src)) E().rack.ext(src, v); }
EXPORT(rig_tempo)  void rig_tempo(float bpm) { E().bpm = rig::clampf(bpm, 30.f, 300.f); }
EXPORT(rig_master) void rig_master(float db) { E().masterDb = rig::clampf(db, -60.f, 6.f); }
// File slots (IRs, NAM captures...): p = rig_asset_buf(m, bytes) reserves room, the host writes the
// payload there, then rig_asset_commit(m, bytes). rig_asset_status(m) says what loaded, or why not.
// Loading allocates and parses: call these between rig_process calls (the browser does), or from a
// control thread (the Linux host); the swap itself happens inside rig_process without allocating.
EXPORT(rig_asset_max) int rig_asset_max(int m) { return (m >= 0 && m < rig::NMOD && E().mods[m]->desc().asset.ready) ? E().mods[m]->desc().asset.maxBytes : 0; }
EXPORT(rig_asset_buf) unsigned char* rig_asset_buf(int m, int bytes) {
  if (m < 0 || m >= rig::NMOD || bytes < 1 || bytes > rig_asset_max(m)) return nullptr;
  return E().mods[m]->assetBuffer(bytes);
}
EXPORT(rig_asset_commit) int rig_asset_commit(int m, int bytes) {
  if (m < 0 || m >= rig::NMOD || bytes < 0 || bytes > rig_asset_max(m)) return -207;
  return E().mods[m]->assetCommit(bytes);
}
EXPORT(rig_asset_clear) void rig_asset_clear(int m) { if (m >= 0 && m < rig::NMOD) E().mods[m]->assetClear(); }
EXPORT(rig_asset_status) const char* rig_asset_status(int m) { return (m >= 0 && m < rig::NMOD) ? E().mods[m]->assetStatus() : ""; }

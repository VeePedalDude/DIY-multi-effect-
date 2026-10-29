// engine/api/rig_api.cpp - flat C API. The browser (WebAssembly) and future hosts
// (the Linux JACK host) talk to the engine only through these functions.
#include "../engine.h"
#include <new>

#if defined(__wasm__)
#define EXPORT(name) extern "C" __attribute__((export_name(#name)))
#else
#define EXPORT(name) extern "C"
#endif

// Placement-new into zeroed static storage: keeps the big delay buffers out of the
// binary (zero memory is free in wasm) and needs no allocator or libc++ runtime.
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
  E().conn[m][p] = (src >= 0 && src < rig::NSRC) ? src : -1;
  E().depth[m][p] = rig::clampf(depth, 0.f, 1.f);
}
EXPORT(rig_ext)    void rig_ext(int src, float v) { if (src >= 0 && src < rig::NSRC) E().src.list[src]->ext(v); }
EXPORT(rig_tempo)  void rig_tempo(float bpm) { E().bpm = rig::clampf(bpm, 30.f, 300.f); }
EXPORT(rig_master) void rig_master(float db) { E().masterDb = rig::clampf(db, -60.f, 6.f); }
EXPORT(rig_ir_buf) float* rig_ir_buf() { return E().cab.load; }
EXPORT(rig_ir_max) int rig_ir_max() { return rig::Cab::MAXIR; }
EXPORT(rig_ir_commit) void rig_ir_commit(int n) { E().cab.commit(n); }
EXPORT(rig_ir_default) void rig_ir_default() { E().cab.makeDefault(); }

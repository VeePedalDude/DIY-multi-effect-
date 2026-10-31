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
// (zero memory is free in wasm). Module instances, captures and IRs live on the heap.
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
  if (!E().used(m) || p < 0 || p >= E().nparams(m)) return;
  E().base[m][p] = rig::clampf(v, 0.f, 1.f);
}
EXPORT(rig_bypass) void rig_bypass(int m, int on) { if (E().used(m)) E().byp[m] = on != 0; }
// src = -1 disconnects; depth is the +/- swing in normalised units around the knob
EXPORT(rig_mod) void rig_mod(int m, int p, int src, float depth) {
  if (!E().used(m) || p < 0 || p >= E().nparams(m)) return;
  const int c = E().rack.used(src) ? src : -1;
  if (c != E().conn[m][p]) E().tap[m][p].reset();               // a new patch starts at 0 degrees, 1x
  E().conn[m][p] = c;
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
// Per-destination PHASE (degrees) and DIV (multiple of the source's cycle: 1/4, 1/3, 1/2, 2/3, 1, 3/2,
// 2, 3, 4, 8; the nearest is taken) of a patch from a tapping source (an LFO). Other sources ignore it.
EXPORT(rig_mod_tap) void rig_mod_tap(int m, int p, float deg, float div) {
  if (!E().used(m) || p < 0 || p >= E().nparams(m)) return;
  E().tap[m][p].set(deg, div);
}
EXPORT(rig_src_tap) int rig_src_tap(int src, int p, float deg, float div) { return E().rack.tapSet(src, p, deg, div); }
// Settings (not patch targets) and table cells of a source, normalised 0..1 (SourceDesc settings / cols).
EXPORT(rig_src_cfg) int rig_src_cfg(int src, int i, float v) { return E().rack.cfgSet(src, i, v); }
EXPORT(rig_src_cell) int rig_src_cell(int src, int row, int col, float v) { return E().rack.cellSet(src, row, col, v); }
// For the UI's drawing only (its main-thread copy of the engine, not the audio one): what a source type
// draws for the knobs and settings written to rig_view_buf() (knobs at 0, settings at MAXPAR, both
// normalised); the result is written from rig_view_buf() + 64. Returns how many numbers. Allocates once per type.
static float g_view[64 + 512];
static_assert(rig::MAXPAR + rig::MAXCFG <= 64, "rig_view_buf: knobs and settings must fit before the output");
static rig::Source* g_viewSrc[64] = {nullptr};
EXPORT(rig_view_buf) float* rig_view_buf() { return g_view; }
EXPORT(rig_view) int rig_view(int type) {
  if (type < 0 || type >= rig::NSRCTYPES || type >= 64) return 0;
  if (!g_viewSrc[type]) { g_viewSrc[type] = rig::sourceTypes()[type].make(); g_viewSrc[type]->init(48000.f); }
  return g_viewSrc[type]->view(g_view, g_view + rig::MAXPAR, g_view + 64, 512);
}
EXPORT(rig_rack) const char* rig_rack() { return E().rackJson; }
// The effect chain (control thread; allocates). Modules are addressed by slot (rig_chain() lists the
// chain in order with each instance's slot; a fresh chain is one of each type, slots 0.. in order).
// Write "<type> [id] [position]" to rig_text(), then rig_fx_add() returns the new slot, or -1 unknown
// type, -2 chain full, -3 bad or taken id. It fades in over 10 ms; a removed one fades out, then is
// freed by a later rig_collect() (or any other chain call) once the audio thread is done with it.
EXPORT(rig_fx_add) int rig_fx_add() {
  g_text[sizeof g_text - 1] = 0;
  char tok[3][32] = {{0}}; int i = 0;
  for (int t = 0; t < 3; t++) { while (g_text[i] == ' ') i++; int n = 0; while (g_text[i] && g_text[i] != ' ' && n < 31) tok[t][n++] = g_text[i++]; }
  int pos = -1; if (tok[2][0]) { pos = 0; for (int k = 0; tok[2][k]; k++) pos = pos * 10 + (tok[2][k] - '0'); }
  return E().addModule(tok[0], (tok[1][0] == '-' && !tok[1][1]) ? nullptr : tok[1], pos);
}
EXPORT(rig_fx_remove) int rig_fx_remove(int m) { return E().removeModule(m); }
EXPORT(rig_fx_move) int rig_fx_move(int m, int pos) { return E().moveModule(m, pos); }
EXPORT(rig_chain) const char* rig_chain() { return E().chainJson; }
EXPORT(rig_collect) void rig_collect() { E().collect(false); }
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
EXPORT(rig_asset_max) int rig_asset_max(int m) { return (E().used(m) && E().mods[m]->desc().asset.ready) ? E().mods[m]->desc().asset.maxBytes : 0; }
EXPORT(rig_asset_buf) unsigned char* rig_asset_buf(int m, int bytes) {
  if (!E().used(m) || bytes < 1 || bytes > rig_asset_max(m)) return nullptr;
  return E().mods[m]->assetBuffer(bytes);
}
EXPORT(rig_asset_commit) int rig_asset_commit(int m, int bytes) {
  if (!E().used(m) || bytes < 0 || bytes > rig_asset_max(m)) return -207;
  return E().mods[m]->assetCommit(bytes);
}
EXPORT(rig_asset_clear) void rig_asset_clear(int m) { if (E().used(m)) E().mods[m]->assetClear(); }
EXPORT(rig_asset_status) const char* rig_asset_status(int m) { return E().used(m) ? E().mods[m]->assetStatus() : ""; }

// ------------------------------------------------------------------ the text protocol (docs/PROTOCOL.md)
// One implementation for every host: the browser's worklet and rig-host (hosts/rig-host) both write a
// command line to rig_cmd_buf() and call rig_exec(), which returns 0 or a negative error, and sets
// rig_exec_changed() (1: the rack changed, re-read rig_rack(); 2: the chain changed, re-read rig_chain()).
// Call it from the control thread (the engine's add/remove/patch calls are made for that); in the
// browser that is the audio thread itself, which is fine too.
//   param_set <inst> <symbol> <0..1>          bypass <inst> <0|1>
//   mod_connect <inst> <symbol> <source> <depth> [phase] [div]     mod_disconnect <inst> <symbol>
//   set_bpm <bpm>   master <dB>   ext <source> <value>   input_channel <0|1>
//   tuner <0|1> [mute 0|1]   tuner_ref <A4 Hz, 400..480>
//   src_add <type> [id]   src_remove <source>   src_set <source> <symbol> <0..1>
//   src_mod <source> <symbol> <from source> <depth> [phase] [div]   src_unmod <source> <symbol>
//   src_cfg <source> <symbol> <0..1>   src_cell <source> <row> <symbol> <0..1>
//   fx_add <type> [id] [position]   fx_remove <inst>   fx_move <inst> <position>
// <inst>: a module id ("holo") or its position in the chain (0 = first). Errors follow mod-host's numbers.
#include <cstdlib>
namespace {
enum { ERR_INSTANCE = -1, ERR_ID_TAKEN = -2, ERR_FULL = -3, ERR_SYMBOL = -103, ERR_RANGE = -207, ERR_OPERATION = -902 };
char g_cmd[1024];
int g_changed = 0, g_inCh = 0;
// A whole token as a number (like JavaScript's Number(): NaN if absent or not all number).
double num(const char* t) { if (!t || !*t) return NAN; char* e; const double v = strtod(t, &e); return *e ? NAN : v; }
bool isInt(const char* t) { if (!t || !*t) return false; for (const char* p = t; *p; p++) if (*p < '0' || *p > '9') return false; return true; }
int inst(const char* t) {                       // module slot by id, or by position in the chain
  if (!t) return -1;
  if (isInt(t)) { const long i = strtol(t, nullptr, 10); return i < E().order.n ? E().order.s[i] : -1; }
  return E().moduleIndex(t);
}
int knobOf(const rig::ParamDesc* ps, int n, const char* sym) { if (!sym) return -1; for (int i = 0; i < n; i++) if (!strcmp(ps[i].symbol, sym)) return i; return -1; }
const rig::SourceDesc* srcType(int k) { return E().rack.used(k) ? &rig::sourceTypeDesc(E().rack.slot[k].type) : nullptr; }
bool tapArgs(const char* a, const char* b, float& deg, float& div) {   // [phase] [div]: degrees, and 3/2 or 1.5
  deg = a ? (float)num(a) : 0.f; div = 1.f;
  if (b) { const char* s = strchr(b, '/'); if (s) { char x[32] = {0}; strncpy(x, b, (size_t)std::min<long>(31, s - b)); div = (float)(num(x) / num(s + 1)); } else div = (float)num(b); }
  return std::isfinite(deg) && std::isfinite(div) && div > 0;
}
int exec(char* line) {
  const char* t[8] = {nullptr}; int n = 0;
  for (char* p = strtok(line, " \t\r\n"); p && n < 8; p = strtok(nullptr, " \t\r\n")) t[n++] = p;
  if (!n) return ERR_OPERATION;
  const char* c = t[0];
  if (!strcmp(c, "param_set") || !strcmp(c, "mod_connect") || !strcmp(c, "mod_disconnect")) {
    const int m = inst(t[1]); if (m < 0 || !E().used(m)) return ERR_INSTANCE;
    const rig::ModuleDesc& d = E().mods[m]->desc(); const int p = knobOf(d.params, d.nparams, t[2]); if (p < 0) return ERR_SYMBOL;
    if (!strcmp(c, "param_set")) { const double v = num(t[3]); if (!(v >= 0 && v <= 1)) return ERR_RANGE; rig_param(m, p, (float)v); return 0; }
    if (!strcmp(c, "mod_disconnect")) { rig_mod(m, p, -1, 0); return 0; }
    const int s = E().sourceIndex(t[3] ? t[3] : ""); const double dep = num(t[4]); float deg, div;
    if (s < 0 || !(dep >= 0 && dep <= 1) || !tapArgs(t[5], t[6], deg, div)) return ERR_RANGE;
    rig_mod(m, p, s, (float)dep); rig_mod_tap(m, p, deg, div); return 0;
  }
  if (!strcmp(c, "tuner")) {
    if (!t[1] || (strcmp(t[1], "0") && strcmp(t[1], "1"))) return ERR_RANGE;
    rig_tuner(t[1][0] == '1', !(t[2] && !strcmp(t[2], "0"))); return 0;
  }
  if (!strcmp(c, "tuner_ref")) { const double v = num(t[1]); if (!(v >= 400 && v <= 480)) return ERR_RANGE; rig_tuner_ref((float)v); return 0; }
  if (!strcmp(c, "fx_add")) {
    if (!t[1] || rig::moduleTypeIndex(t[1]) < 0) return ERR_INSTANCE;
    if (t[3] && !isInt(t[3])) return ERR_RANGE;
    const int k = E().addModule(t[1], (!t[2] || !strcmp(t[2], "-")) ? nullptr : t[2], t[3] ? (int)strtol(t[3], nullptr, 10) : -1);
    if (k < 0) return k == -2 ? ERR_FULL : k == -3 ? ERR_ID_TAKEN : ERR_INSTANCE;
    g_changed |= 2; return 0;
  }
  if (!strcmp(c, "fx_remove")) { const int m = inst(t[1]); if (m < 0 || !E().used(m)) return ERR_INSTANCE; E().removeModule(m); g_changed |= 2; return 0; }
  if (!strcmp(c, "fx_move")) {
    const int m = inst(t[1]); if (m < 0 || !E().used(m)) return ERR_INSTANCE;
    if (!isInt(t[2])) return ERR_RANGE;
    E().moveModule(m, (int)strtol(t[2], nullptr, 10)); g_changed |= 2; return 0;
  }
  if (!strcmp(c, "src_add")) {
    bool known = false; for (int k = 0; k < rig::NSRCTYPES && t[1]; k++) known = known || !strcmp(rig::sourceTypeDesc(k).id, t[1]);
    if (!known) return ERR_INSTANCE;
    const int k = E().addSource(t[1], t[2] ? t[2] : ""); if (k < 0) return k == -2 ? ERR_FULL : k == -3 ? ERR_ID_TAKEN : ERR_INSTANCE;
    g_changed |= 1; return 0;
  }
  if (!strcmp(c, "src_remove")) { const int k = E().sourceIndex(t[1] ? t[1] : ""); if (k < 0) return ERR_INSTANCE; E().removeSource(k); g_changed |= 1; return 0; }
  if (!strcmp(c, "src_mod") || !strcmp(c, "src_unmod") || !strcmp(c, "src_set") || !strcmp(c, "src_cfg") || !strcmp(c, "src_cell")) {
    const int k = E().sourceIndex(t[1] ? t[1] : ""); if (k < 0) return ERR_INSTANCE;
    const rig::SourceDesc* d = srcType(k); if (!d) return ERR_INSTANCE;
    if (!strcmp(c, "src_cfg")) {
      const int i = knobOf(d->settings, d->nsettings, t[2]); if (i < 0) return ERR_SYMBOL;
      const double v = num(t[3]); if (!(v >= 0 && v <= 1)) return ERR_RANGE; rig_src_cfg(k, i, (float)v); return 0;
    }
    if (!strcmp(c, "src_cell")) {
      const double row = num(t[2]); if (!(row >= 0 && row < d->rows && row == (int)row)) return ERR_RANGE;
      const int j = knobOf(d->cols, d->ncols, t[3]); if (j < 0) return ERR_SYMBOL;
      const double v = num(t[4]); if (!(v >= 0 && v <= 1)) return ERR_RANGE; rig_src_cell(k, (int)row, j, (float)v); return 0;
    }
    const int p = knobOf(d->params, d->nparams, t[2]); if (p < 0) return ERR_SYMBOL;
    if (!strcmp(c, "src_set")) { const double v = num(t[3]); if (!(v >= 0 && v <= 1)) return ERR_RANGE; rig_src_param(k, p, (float)v); return 0; }
    if (!strcmp(c, "src_unmod")) { rig_src_mod(k, p, -1, 0); return 0; }
    const int f = E().sourceIndex(t[3] ? t[3] : ""); const double dep = num(t[4]); float deg, div;
    if (f < 0 || !(dep >= 0 && dep <= 1) || !tapArgs(t[5], t[6], deg, div)) return ERR_RANGE;
    if (rig_src_mod(k, p, f, (float)dep) != 0) return ERR_INSTANCE;
    rig_src_tap(k, p, deg, div); return 0;
  }
  if (!strcmp(c, "bypass")) { const int m = inst(t[1]); if (m < 0 || !E().used(m)) return ERR_INSTANCE; rig_bypass(m, t[2] && !strcmp(t[2], "1")); return 0; }
  if (!strcmp(c, "set_bpm")) { const double v = num(t[1]); if (!(v >= 30 && v <= 300)) return ERR_RANGE; rig_tempo((float)v); return 0; }
  if (!strcmp(c, "master")) { const double v = num(t[1]); if (!(v >= -60 && v <= 6)) return ERR_RANGE; rig_master((float)v); return 0; }
  if (!strcmp(c, "ext")) { const int s = E().sourceIndex(t[1] ? t[1] : ""); const double v = num(t[2]); if (s < 0 || !std::isfinite(v)) return ERR_RANGE; rig_ext(s, (float)v); return 0; }
  if (!strcmp(c, "input_channel")) { g_inCh = (t[1] && !strcmp(t[1], "1")) ? 1 : 0; return 0; }
  return ERR_OPERATION;
}
} // namespace
EXPORT(rig_cmd_buf) char* rig_cmd_buf() { return g_cmd; }
EXPORT(rig_cmd_len) int rig_cmd_len() { return (int)sizeof g_cmd; }
EXPORT(rig_exec) int rig_exec() { g_cmd[sizeof g_cmd - 1] = 0; g_changed = 0; return exec(g_cmd); }
EXPORT(rig_exec_changed) int rig_exec_changed() { return g_changed; }
EXPORT(rig_input_channel) int rig_input_channel() { return g_inCh; }
// The slot of the module named in rig_cmd_buf() (an id, or a position in the chain), or -1: for hosts
// that load files into a module by name (asset messages).
EXPORT(rig_find_module) int rig_find_module() { g_cmd[sizeof g_cmd - 1] = 0; const int m = inst(g_cmd); return E().used(m) ? m : -1; }

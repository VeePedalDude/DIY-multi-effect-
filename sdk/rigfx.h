// sdk/rigfx.h - turns a RIG/01 effect (a rig::Module) or modulation source (a rig::Source) into a CLAP
// plugin, so it can be added to the rig as a file (a .rigfx bundle) instead of compiled into it.
//
// The effect or source is written exactly as a built-in one (docs/ADDING_A_MODULE.md,
// docs/ADDING_A_SOURCE.md); tools/build_plugin.py generates the few lines that use this header:
//
//   #include "rigfx.h"
//   #include "trem.h"
//   RIGFX_EFFECT(rig::Trem, "modulation", "[\"tremolo\",\"volume\"]", "RIG/01", "1.0.0")
//
// and builds it three times: a native .clap for Windows and for Linux (the rig-host runs those at full
// speed; any CLAP host can too) and a .wclap (WebAssembly, the browser runs it in its audio worklet).
//
// What the plugin is to CLAP:
//   - one stereo input and one stereo output (a source passes its input through);
//   - its knobs as CLAP params, ids 0..n-1, values normalised 0..1 (RIG's own convention: the
//     engine's modulation works in 0..1); the readout shows real units (value_to_text);
//   - clap.state: the knobs, as "symbol=value" lines;
//   - rig01.describe/1 (sdk/rig_clap_ext.h): the full description (curves, units, stops, category,
//     tags), the meters and, for a source, its output.
// Knob changes arrive as CLAP param events and apply from the start of the block, as the engine's
// built-ins see them. Nothing in process() allocates (the module's own rule), and blocks longer than
// the engine's 128 samples are processed in 128-sample pieces.
#pragma once
#include <clap/clap.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "rig_clap_ext.h"
#include "module.h"
#include "source.h"

namespace rigfx {

inline void jstr(std::string& o, const char* s) {
  o += '"';
  for (; s && *s; s++) {
    const unsigned char c = (unsigned char)*s;
    if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
    else if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
    else o += (char)c;
  }
  o += '"';
}
inline void jnum(std::string& o, double v) { char b[32]; snprintf(b, sizeof b, "%.9g", v); o += b; }
inline void jparams(std::string& o, const rig::ParamDesc* p, int n) {
  o += '[';
  for (int i = 0; i < n; i++) {
    if (i) o += ',';
    o += "{\"symbol\":"; jstr(o, p[i].symbol); o += ",\"label\":"; jstr(o, p[i].label);
    o += ",\"min\":"; jnum(o, p[i].min); o += ",\"max\":"; jnum(o, p[i].max); o += ",\"def\":"; jnum(o, p[i].def);
    o += ",\"unit\":"; jstr(o, p[i].unit); o += ",\"curve\":"; jstr(o, p[i].curve == rig::Curve::Log ? "log" : "lin");
    o += ",\"decimals\":"; jnum(o, p[i].decimals); o += ",\"sign\":"; o += p[i].sign ? "true" : "false";
    o += ",\"stops\":[";
    for (int k = 0; k < p[i].nstops; k++) { if (k) o += ','; o += '['; jnum(o, p[i].stops[k].v); o += ','; jstr(o, p[i].stops[k].name); o += ']'; }
    o += "]}";
  }
  o += ']';
}
// Real value <-> normalised, as the engine maps them (rig::ParamDesc::map and its inverse).
inline double unmap(const rig::ParamDesc& p, double real) {
  if (p.curve == rig::Curve::Log) return log(real / p.min) / log((double)p.max / p.min);
  return (real - p.min) / ((double)p.max - p.min);
}

template <class T, bool SOURCE>
struct Plugin {
  clap_plugin_t clap;
  const clap_host_t* host;
  T fx;
  float sr = 48000.f, bpm = 120.f, out = 0.f;
  float norm[rig::MAXPAR] = {0};
  float meterv[4] = {0};
  std::string json;

  static const rig::ParamDesc* params(const T& t) { return t.desc().params; }
  static int nparams(const T& t) { return t.desc().nparams; }
  static int nmeters(const T& t) { return t.desc().nmeters; }
  static const char* meterName(const T& t, int i) { return t.desc().meters[i]; }

  static Plugin* self(const clap_plugin_t* p) { return (Plugin*)p->plugin_data; }

  // ---- clap_plugin
  static bool init(const clap_plugin_t* p) {
    Plugin* s = self(p);
    for (int i = 0; i < nparams(s->fx); i++) s->norm[i] = params(s->fx)[i].def;
    s->fx.init(s->sr);
    return true;
  }
  static void destroy(const clap_plugin_t* p) { delete self(p); }
  static bool activate(const clap_plugin_t* p, double rate, uint32_t, uint32_t) {
    Plugin* s = self(p); s->sr = (float)rate; s->fx.init(s->sr); return true;
  }
  static void deactivate(const clap_plugin_t*) {}
  static bool start_processing(const clap_plugin_t*) { return true; }
  static void stop_processing(const clap_plugin_t*) {}
  static void reset(const clap_plugin_t* p) {
    Plugin* s = self(p);
    if constexpr (SOURCE) s->fx.init(s->sr); else s->fx.reset();
  }
  void events(const clap_input_events_t* in) {
    if (!in) return;
    const uint32_t n = in->size(in);
    for (uint32_t i = 0; i < n; i++) {
      const clap_event_header_t* h = in->get(in, i);
      if (h->space_id != CLAP_CORE_EVENT_SPACE_ID || h->type != CLAP_EVENT_PARAM_VALUE) continue;
      const clap_event_param_value_t* e = (const clap_event_param_value_t*)h;
      if (e->param_id < (clap_id)nparams(fx)) norm[e->param_id] = (float)(e->value < 0 ? 0 : e->value > 1 ? 1 : e->value);
    }
  }
  static clap_process_status process(const clap_plugin_t* p, const clap_process_t* pr) {
    Plugin* s = self(p);
    s->events(pr->in_events);
    if (pr->transport && (pr->transport->flags & CLAP_TRANSPORT_HAS_TEMPO)) s->bpm = (float)pr->transport->tempo;
    const uint32_t frames = pr->frames_count;
    const float* inL = nullptr; const float* inR = nullptr; float* oL = nullptr; float* oR = nullptr;
    if (pr->audio_inputs_count > 0 && pr->audio_inputs[0].data32) {
      inL = pr->audio_inputs[0].data32[0];
      inR = pr->audio_inputs[0].channel_count > 1 ? pr->audio_inputs[0].data32[1] : inL;
    }
    if (pr->audio_outputs_count > 0 && pr->audio_outputs[0].data32) {
      oL = pr->audio_outputs[0].data32[0];
      oR = pr->audio_outputs[0].channel_count > 1 ? pr->audio_outputs[0].data32[1] : nullptr;
    }
    static const float silence[rig::MAXBLOCK] = {0};
    float scratchR[rig::MAXBLOCK];
    for (uint32_t done = 0; done < frames;) {
      const int n = (int)((frames - done) < (uint32_t)rig::MAXBLOCK ? frames - done : (uint32_t)rig::MAXBLOCK);
      const float* a = inL ? inL + done : silence; const float* b = inR ? inR + done : a;
      float* x = oL ? oL + done : nullptr; float* y = oR ? oR + done : scratchR;
      s->run(a, b, x, y, n);
      done += (uint32_t)n;
    }
    return CLAP_PROCESS_CONTINUE;
  }
  void run(const float* a, const float* b, float* x, float* y, int n) {
    if constexpr (SOURCE) {
      rig::SourceContext c{a, n, sr, n / sr, bpm, norm};
      out = fx.update(c);
      if (x && x != a) memcpy(x, a, (size_t)n * sizeof(float));
      if (y != b) memcpy(y, b, (size_t)n * sizeof(float));
    } else {
      float tmp[rig::MAXBLOCK];
      rig::Context ctx; ctx.sr = sr; ctx.bpm = bpm;
      fx.process(a, b, x ? x : tmp, y, n, norm, ctx);
    }
    fx.meters(meterv);
  }
  static const void* get_extension(const clap_plugin_t*, const char* id);
  static void on_main_thread(const clap_plugin_t*) {}

  // ---- clap.params
  static uint32_t pcount(const clap_plugin_t* p) { return (uint32_t)nparams(self(p)->fx); }
  static bool pinfo(const clap_plugin_t* p, uint32_t i, clap_param_info_t* info) {
    Plugin* s = self(p); if (i >= pcount(p)) return false;
    const rig::ParamDesc& q = params(s->fx)[i];
    memset(info, 0, sizeof *info);
    info->id = i; info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE; info->cookie = nullptr;
    snprintf(info->name, sizeof info->name, "%s", q.label); info->module[0] = 0;
    info->min_value = 0; info->max_value = 1; info->default_value = q.def;
    return true;
  }
  static bool pvalue(const clap_plugin_t* p, clap_id id, double* v) { Plugin* s = self(p); if (id >= pcount(p)) return false; *v = s->norm[id]; return true; }
  static bool ptext(const clap_plugin_t* p, clap_id id, double v, char* out, uint32_t cap) {
    Plugin* s = self(p); if (id >= pcount(p)) return false;
    const rig::ParamDesc& q = params(s->fx)[id];
    const float real = q.map((float)v);
    if (q.nstops) { snprintf(out, cap, "%s", q.stops[q.nearestStop(real)].name); return true; }
    snprintf(out, cap, "%s%.*f%s%s", q.sign && real > 0 ? "+" : "", q.decimals, real, q.unit[0] ? " " : "", q.unit);
    return true;
  }
  static bool pparse(const clap_plugin_t* p, clap_id id, const char* text, double* v) {
    Plugin* s = self(p); if (id >= pcount(p)) return false;
    const rig::ParamDesc& q = params(s->fx)[id];
    for (int k = 0; k < q.nstops; k++) if (!strcmp(text, q.stops[k].name)) { *v = unmap(q, q.stops[k].v); return true; }
    char* e; const double real = strtod(text, &e); if (e == text) return false;
    const double u = unmap(q, real); *v = u < 0 ? 0 : u > 1 ? 1 : u; return true;
  }
  static void pflush(const clap_plugin_t* p, const clap_input_events_t* in, const clap_output_events_t*) { self(p)->events(in); }

  // ---- clap.audio-ports: one stereo in, one stereo out
  static uint32_t acount(const clap_plugin_t*, bool) { return 1; }
  static bool aget(const clap_plugin_t*, uint32_t i, bool is_input, clap_audio_port_info_t* info) {
    if (i) return false;
    memset(info, 0, sizeof *info);
    info->id = 0; snprintf(info->name, sizeof info->name, "%s", is_input ? "in" : "out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN; info->channel_count = 2; info->port_type = CLAP_PORT_STEREO; info->in_place_pair = CLAP_INVALID_ID;
    return true;
  }

  // ---- clap.state: "symbol=value" lines (normalised)
  static bool ssave(const clap_plugin_t* p, const clap_ostream_t* st) {
    Plugin* s = self(p); std::string t;
    for (int i = 0; i < nparams(s->fx); i++) { t += params(s->fx)[i].symbol; t += '='; jnum(t, s->norm[i]); t += '\n'; }
    const char* d = t.data(); size_t left = t.size();
    while (left) { const int64_t w = st->write(st, d, left); if (w <= 0) return false; d += w; left -= (size_t)w; }
    return true;
  }
  static bool sload(const clap_plugin_t* p, const clap_istream_t* st) {
    Plugin* s = self(p); std::string t; char buf[256];
    for (;;) { const int64_t r = st->read(st, buf, sizeof buf); if (r < 0) return false; if (r == 0) break; t.append(buf, (size_t)r); }
    size_t at = 0;
    while (at < t.size()) {
      size_t nl = t.find('\n', at); if (nl == std::string::npos) nl = t.size();
      const std::string line = t.substr(at, nl - at); at = nl + 1;
      const size_t eq = line.find('='); if (eq == std::string::npos) continue;
      for (int i = 0; i < nparams(s->fx); i++) if (line.compare(0, eq, params(s->fx)[i].symbol) == 0 && strlen(params(s->fx)[i].symbol) == eq) s->norm[i] = (float)atof(line.c_str() + eq + 1);
    }
    return true;
  }

  // ---- rig01.describe/1
  static const char* rjson(const clap_plugin_t* p) { return self(p)->json.c_str(); }
  static uint32_t rmeters(const clap_plugin_t* p, float* out, uint32_t max) {
    Plugin* s = self(p); const uint32_t n = (uint32_t)nmeters(s->fx) < max ? (uint32_t)nmeters(s->fx) : max;
    for (uint32_t i = 0; i < n; i++) out[i] = s->meterv[i];
    return n;
  }
  static float routput(const clap_plugin_t* p) { return self(p)->out; }
};

template <class T, bool SOURCE>
const void* Plugin<T, SOURCE>::get_extension(const clap_plugin_t*, const char* id) {
  static const clap_plugin_params_t params = {pcount, pinfo, pvalue, ptext, pparse, pflush};
  static const clap_plugin_audio_ports_t ports = {acount, aget};
  static const clap_plugin_state_t state = {ssave, sload};
  static const rig_plugin_describe_t rig = {rjson, rmeters, routput};
  if (!strcmp(id, CLAP_EXT_PARAMS)) return &params;
  if (!strcmp(id, CLAP_EXT_AUDIO_PORTS)) return &ports;
  if (!strcmp(id, CLAP_EXT_STATE)) return &state;
  if (!strcmp(id, RIG_EXT_DESCRIBE)) return &rig;
  return nullptr;
}

// The one plugin this library holds, and its description.
template <class T, bool SOURCE>
struct Library {
  static inline std::string clapId, json;
  static inline const char* features[4] = {nullptr, nullptr, nullptr, nullptr};
  static inline clap_plugin_descriptor_t descriptor;
  static void describe(const char* category, const char* tagsJson, const char* author, const char* version) {
    static T proto;
    const auto& d = proto.desc();
    clapId = std::string("com.rig01.") + d.id;
    std::string& o = json;
    o = "{\"rig\":1,\"kind\":"; jstr(o, SOURCE ? "source" : "effect");
    o += ",\"id\":"; jstr(o, d.id); o += ",\"name\":"; jstr(o, d.name);
    if constexpr (SOURCE) { o += ",\"sub\":"; jstr(o, d.sub); } else { o += ",\"sub\":"; jstr(o, d.kind); }
    o += ",\"category\":"; jstr(o, category); o += ",\"tags\":"; o += tagsJson && *tagsJson ? tagsJson : "[]";
    o += ",\"author\":"; jstr(o, author); o += ",\"version\":"; jstr(o, version);
    o += ",\"meters\":["; for (int i = 0; i < d.nmeters; i++) { if (i) o += ','; jstr(o, d.meters[i]); } o += ']';
    o += ",\"params\":"; jparams(o, d.params, d.nparams);
    o += '}';
    features[0] = SOURCE ? RIG_FEATURE_MOD_SOURCE : CLAP_PLUGIN_FEATURE_AUDIO_EFFECT;
    features[1] = SOURCE ? CLAP_PLUGIN_FEATURE_UTILITY : nullptr;
    const char* blurb; if constexpr (SOURCE) blurb = d.sub; else blurb = d.kind;
    descriptor = {CLAP_VERSION_INIT, clapId.c_str(), d.name, author, "", "", "", version, blurb, features};
  }
  static const clap_plugin_t* create(const clap_host_t* host) {
    auto* s = new Plugin<T, SOURCE>();
    s->host = host; s->json = json;
    s->clap = {&descriptor, s, Plugin<T, SOURCE>::init, Plugin<T, SOURCE>::destroy, Plugin<T, SOURCE>::activate, Plugin<T, SOURCE>::deactivate,
               Plugin<T, SOURCE>::start_processing, Plugin<T, SOURCE>::stop_processing, Plugin<T, SOURCE>::reset, Plugin<T, SOURCE>::process,
               Plugin<T, SOURCE>::get_extension, Plugin<T, SOURCE>::on_main_thread};
    return &s->clap;
  }
  static uint32_t fcount(const clap_plugin_factory_t*) { return 1; }
  static const clap_plugin_descriptor_t* fdesc(const clap_plugin_factory_t*, uint32_t i) { return i ? nullptr : &descriptor; }
  static const clap_plugin_t* fcreate(const clap_plugin_factory_t*, const clap_host_t* host, const char* id) {
    return id && clapId == id ? create(host) : nullptr;
  }
  static const void* factory(const char* id) {
    static const clap_plugin_factory_t f = {fcount, fdesc, fcreate};
    return !strcmp(id, CLAP_PLUGIN_FACTORY_ID) ? &f : nullptr;
  }
};

}  // namespace rigfx

#if defined(_WIN32)
#define RIGFX_EXPORT extern "C" __declspec(dllexport)
#elif defined(__wasm__)
#define RIGFX_EXPORT extern "C" __attribute__((used, visibility("default")))
#else
#define RIGFX_EXPORT extern "C" __attribute__((visibility("default")))
#endif

#define RIGFX_ENTRY(T, SOURCE, CATEGORY, TAGS, AUTHOR, VERSION)                                                   \
  static bool rigfx_init(const char*) { rigfx::Library<T, SOURCE>::describe(CATEGORY, TAGS, AUTHOR, VERSION); return true; } \
  static void rigfx_deinit() {}                                                                                   \
  static const void* rigfx_factory(const char* id) { return rigfx::Library<T, SOURCE>::factory(id); }             \
  RIGFX_EXPORT const clap_plugin_entry_t clap_entry = {CLAP_VERSION_INIT, rigfx_init, rigfx_deinit, rigfx_factory};
#define RIGFX_EFFECT(T, CATEGORY, TAGS, AUTHOR, VERSION) RIGFX_ENTRY(T, false, CATEGORY, TAGS, AUTHOR, VERSION)
#define RIGFX_SOURCE(T, CATEGORY, TAGS, AUTHOR, VERSION) RIGFX_ENTRY(T, true, CATEGORY, TAGS, AUTHOR, VERSION)

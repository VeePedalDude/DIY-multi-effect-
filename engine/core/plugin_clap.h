// engine/core/plugin_clap.h - the native plugin backend: opens a .clap (a shared library exporting
// clap_entry) with dlopen / LoadLibrary and runs its first plugin as a RIG effect or source.
// Used by the native tests and the rig-host; the browser runs the same plugins' .wclap instead.
//
// RIG plugins (sdk/rigfx.h) describe themselves through rig01.describe/1. Any other CLAP plugin with an
// audio input and output is taken as an effect: its first MAXPAR automatable parameters become its
// knobs (linear, in the plugin's own range; a stepped one with up to 12 values gets named stops from
// the plugin's own readout), the others stay where the plugin starts them.
//
// Per block, only knobs that moved are sent, as CLAP param-value events at the block's start.
// Nothing in process() allocates: the event list and buffers are sized when the instance is made.
#pragma once
#include <clap/clap.h>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <string>
#include "plugin.h"
#include "../../sdk/rig_clap_ext.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace rig {

struct ClapLibrary : PluginBackend {
  void* lib = nullptr;
  const clap_plugin_entry_t* entry = nullptr;
  const clap_plugin_factory_t* factory = nullptr;
  std::string pluginId;
  bool rigPlugin = false;
  int inCh = 2, outCh = 2;
  // For plain CLAP plugins: which CLAP param each knob is, and its range.
  struct Map { clap_id id; double min, max; };
  std::vector<Map> map;

  struct Inst {
    const clap_plugin_t* p = nullptr;
    const clap_plugin_params_t* params = nullptr;
    const rig_plugin_describe_t* rig = nullptr;
    float last[MAXPAR];
    clap_event_param_value_t ev[MAXPAR];
    uint32_t nev = 0;
    clap_input_events_t in;
    clap_output_events_t out;
    float silence[MAXBLOCK] = {0}, sink[2][MAXBLOCK] = {{0}};
  };

  static const clap_host_t* host() {
    static const clap_host_t h = {
        CLAP_VERSION_INIT, nullptr, "RIG/01", "RIG/01", "", "1",
        [](const clap_host_t*, const char*) -> const void* { return nullptr; },
        [](const clap_host_t*) {}, [](const clap_host_t*) {}, [](const clap_host_t*) {}};
    return &h;
  }

  ~ClapLibrary() override {
    if (entry) entry->deinit();
#ifdef _WIN32
    if (lib) FreeLibrary((HMODULE)lib);
#else
    if (lib) dlclose(lib);
#endif
  }

  // Opens `path` and fills `info` (the description) for its first plugin. false, with `err`, if it can't.
  bool open(const std::string& path, PluginInfo& info, std::string& err) {
#ifdef _WIN32
    lib = (void*)LoadLibraryA(path.c_str());
    if (!lib) { err = "the plugin file wouldn't load (error " + std::to_string((int)GetLastError()) + ")"; return false; }
    entry = (const clap_plugin_entry_t*)GetProcAddress((HMODULE)lib, "clap_entry");
#else
    lib = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!lib) { const char* e = dlerror(); err = std::string("the plugin file wouldn't load: ") + (e ? e : "?"); return false; }
    entry = (const clap_plugin_entry_t*)dlsym(lib, "clap_entry");
#endif
    if (!entry) { err = "not a CLAP plugin (no clap_entry)"; return false; }
    if (!clap_version_is_compatible(entry->clap_version)) { err = "a CLAP version this host doesn't know"; entry = nullptr; return false; }
    if (!entry->init(path.c_str())) { err = "the plugin wouldn't start"; entry = nullptr; return false; }
    factory = (const clap_plugin_factory_t*)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
    if (!factory || factory->get_plugin_count(factory) < 1) { err = "the file holds no plugin"; return false; }
    const clap_plugin_descriptor_t* d = factory->get_plugin_descriptor(factory, 0);
    pluginId = d->id;
    info.clapId = d->id;
    // A probe instance answers what the plugin is.
    const clap_plugin_t* p = factory->create_plugin(factory, host(), d->id);
    if (!p || !p->init(p)) { err = "the plugin wouldn't start"; if (p) p->destroy(p); return false; }
    auto* rig = (const rig_plugin_describe_t*)p->get_extension(p, RIG_EXT_DESCRIBE);
    auto* params = (const clap_plugin_params_t*)p->get_extension(p, CLAP_EXT_PARAMS);
    auto* ports = (const clap_plugin_audio_ports_t*)p->get_extension(p, CLAP_EXT_AUDIO_PORTS);
    bool ok = true;
    if (ports) {
      clap_audio_port_info_t a;
      if (ports->count(p, true) < 1 || ports->count(p, false) < 1) { err = "the plugin has no audio input or output (instruments aren't effects)"; ok = false; }
      else {
        if (ports->get(p, 0, true, &a)) inCh = (int)a.channel_count;
        if (ports->get(p, 0, false, &a)) outCh = (int)a.channel_count;
        if (inCh < 1 || inCh > 2 || outCh < 1 || outCh > 2) { err = "only mono or stereo plugins"; ok = false; }
      }
    }
    if (ok && rig) {
      rigPlugin = true;
      ok = parsePluginInfo(rig->json(p), info, err);
    } else if (ok) {
      ok = describePlain(p, d, params, info, err);
    }
    p->destroy(p);
    if (ok) info.backend = this;
    return ok;
  }

  // A plain CLAP effect, described from its descriptor and parameters.
  bool describePlain(const clap_plugin_t* p, const clap_plugin_descriptor_t* d, const clap_plugin_params_t* params, PluginInfo& info, std::string& err) {
    std::string id; std::string name = d->name ? d->name : "plugin";
    for (char c : name) { if (id.size() >= 15) break; const char l = (char)tolower((unsigned char)c); if ((l >= 'a' && l <= 'z') || (l >= '0' && l <= '9')) id += l; else if (!id.empty() && id.back() != '_' && id.size() < 14) id += '_'; }
    while (!id.empty() && id.back() == '_') id.pop_back();
    if (id.empty() || !(id[0] >= 'a' && id[0] <= 'z')) id = "clap" + id;
    std::string json = "{\"rig\":0,\"kind\":\"effect\",\"id\":" + q(id.substr(0, 15)) + ",\"name\":" + q(upper(name).substr(0, 24)) +
                       ",\"sub\":" + q(std::string(d->vendor ? d->vendor : "") + " · CLAP") + ",\"category\":" + q(categoryOf(d->features)) +
                       ",\"author\":" + q(d->vendor ? d->vendor : "") + ",\"version\":" + q(d->version ? d->version : "") + ",\"meters\":[],\"params\":[";
    int n = 0; std::vector<std::string> used;
    for (uint32_t i = 0; params && i < params->count(p) && n < MAXPAR; i++) {
      clap_param_info_t pi;
      if (!params->get_info(p, i, &pi) || !(pi.flags & CLAP_PARAM_IS_AUTOMATABLE) || (pi.flags & (CLAP_PARAM_IS_HIDDEN | CLAP_PARAM_IS_READONLY))) continue;
      if (!(pi.max_value > pi.min_value)) continue;
      std::string sym; for (char c : std::string(pi.name)) { const char l = (char)tolower((unsigned char)c); if ((l >= 'a' && l <= 'z') || (l >= '0' && l <= '9')) sym += l; if (sym.size() >= 12) break; }
      if (sym.empty() || !(sym[0] >= 'a' && sym[0] <= 'z')) sym = "p" + sym;
      for (int k = 2; std::find(used.begin(), used.end(), sym) != used.end(); k++) sym = sym.substr(0, 10) + std::to_string(k);
      used.push_back(sym);
      const double def = (pi.default_value - pi.min_value) / (pi.max_value - pi.min_value);
      std::string stops;
      if ((pi.flags & CLAP_PARAM_IS_STEPPED) && pi.max_value - pi.min_value <= 11) {
        for (double v = pi.min_value; v <= pi.max_value + 1e-9; v += 1) {
          char t[64] = {0}; if (!params->value_to_text(p, pi.id, v, t, sizeof t)) snprintf(t, sizeof t, "%g", v);
          stops += (stops.empty() ? "[" : ",[") + num(v) + "," + q(t) + "]";
        }
      }
      json += std::string(n ? "," : "") + "{\"symbol\":" + q(sym) + ",\"label\":" + q(upper(pi.name).substr(0, 12)) + ",\"min\":" + num(pi.min_value) + ",\"max\":" + num(pi.max_value) +
              ",\"def\":" + num(def < 0 ? 0 : def > 1 ? 1 : def) + ",\"unit\":\"\",\"curve\":\"lin\",\"decimals\":" + ((pi.flags & CLAP_PARAM_IS_STEPPED) ? "0" : "2") + ",\"sign\":false,\"stops\":[" + stops + "]}";
      map.push_back({pi.id, pi.min_value, pi.max_value});
      n++;
    }
    json += "]}";
    return parsePluginInfo(json.c_str(), info, err);
  }
  static std::string q(const std::string& s) { std::string o = "\""; for (char c : s) { if (c == '"' || c == '\\') o += '\\'; if ((unsigned char)c >= 0x20) o += c; } return o + "\""; }
  static std::string num(double v) { char b[32]; snprintf(b, sizeof b, "%.9g", v); return b; }
  static std::string upper(std::string s) { for (char& c : s) c = (char)toupper((unsigned char)c); return s; }
  // The add-panel tab for a CLAP plugin, from its features.
  static const char* categoryOf(const char* const* f) {
    struct { const char* feature; const char* cat; } T[] = {{"reverb", "reverb"}, {"delay", "delay"}, {"distortion", "drive"}, {"filter", "filter"},
        {"equalizer", "filter"}, {"compressor", "dynamics"}, {"limiter", "dynamics"}, {"gate", "dynamics"}, {"chorus", "modulation"}, {"flanger", "modulation"},
        {"phaser", "modulation"}, {"tremolo", "modulation"}, {"pitch-shifter", "pitch"}, {"utility", "utility"}};
    for (; f && *f; f++) for (auto& t : T) if (!strcmp(*f, t.feature)) return t.cat;
    return "experimental";
  }

  void* create(float sr) override {
    Inst* s = new Inst();
    s->p = factory->create_plugin(factory, host(), pluginId.c_str());
    if (!s->p || !s->p->init(s->p)) { if (s->p) s->p->destroy(s->p); delete s; return nullptr; }
    s->params = (const clap_plugin_params_t*)s->p->get_extension(s->p, CLAP_EXT_PARAMS);
    s->rig = (const rig_plugin_describe_t*)s->p->get_extension(s->p, RIG_EXT_DESCRIBE);
    if (!s->p->activate(s->p, sr, 1, MAXBLOCK) || !s->p->start_processing(s->p)) { s->p->destroy(s->p); delete s; return nullptr; }
    for (float& v : s->last) v = -1.f;                  // everything is sent on the first block
    s->in.ctx = s;
    s->in.size = [](const clap_input_events_t* l) -> uint32_t { return ((Inst*)l->ctx)->nev; };
    s->in.get = [](const clap_input_events_t* l, uint32_t i) -> const clap_event_header_t* { return &((Inst*)l->ctx)->ev[i].header; };
    s->out.ctx = s;
    s->out.try_push = [](const clap_output_events_t*, const clap_event_header_t*) -> bool { return true; };
    return s;
  }
  void destroy(void* inst) override {
    Inst* s = (Inst*)inst; if (!s) return;
    s->p->stop_processing(s->p); s->p->deactivate(s->p); s->p->destroy(s->p); delete s;
  }
  void process(void* inst, const float* inL, const float* inR, float* oL, float* oR, int n, const float* norm, int np, float bpm) override {
    Inst* s = (Inst*)inst;
    s->nev = 0;
    for (int k = 0; k < np && k < MAXPAR; k++) {
      if (norm[k] == s->last[k]) continue;
      s->last[k] = norm[k];
      clap_event_param_value_t& e = s->ev[s->nev++];
      e.header = {sizeof e, 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
      e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1; e.cookie = nullptr;
      if (rigPlugin) { e.param_id = (clap_id)k; e.value = norm[k]; }
      else { e.param_id = map[(size_t)k].id; e.value = map[(size_t)k].min + norm[k] * (map[(size_t)k].max - map[(size_t)k].min); }
    }
    float* ins[2] = {(float*)inL, (float*)(inCh > 1 ? inR : inL)};
    float* outs[2] = {oL, oR};
    if (inCh == 1) { for (int i = 0; i < n; i++) s->sink[0][i] = 0.5f * (inL[i] + inR[i]); ins[0] = s->sink[0]; }
    clap_audio_buffer_t ai{}, ao{};
    ai.data32 = ins; ai.channel_count = (uint32_t)inCh;
    ao.data32 = outs; ao.channel_count = (uint32_t)outCh;
    clap_event_transport_t tr{};
    tr.header = {sizeof tr, 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_TRANSPORT, 0};
    tr.flags = CLAP_TRANSPORT_HAS_TEMPO; tr.tempo = bpm;
    clap_process_t pr{};
    pr.steady_time = -1; pr.frames_count = (uint32_t)n; pr.transport = &tr;
    pr.audio_inputs = &ai; pr.audio_inputs_count = 1; pr.audio_outputs = &ao; pr.audio_outputs_count = 1;
    pr.in_events = &s->in; pr.out_events = &s->out;
    s->p->process(s->p, &pr);
    if (outCh == 1) memcpy(oR, oL, (size_t)n * sizeof(float));
  }
  float output(void* inst) override { Inst* s = (Inst*)inst; return s->rig ? s->rig->output(s->p) : 0.f; }
  int meters(void* inst, float* out, int max) override { Inst* s = (Inst*)inst; return s->rig ? (int)s->rig->meters(s->p, out, (uint32_t)max) : 0; }
};

}  // namespace rig

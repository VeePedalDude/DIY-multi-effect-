// engine/core/plugin.h - effects and modulation sources that arrive as files while the rig runs:
// CLAP plugins (a .rigfx bundle from tools/build_plugin.py, or, in the rig-host, any CLAP effect).
//
// A loaded plugin is a *type*, next to the built-in ones (fx_chain.h / mod_rack.h keep both in one
// table): it shows in the add panels, the protocol and presets like any other, and the chain or rack
// holds instances of it. The engine doesn't talk CLAP itself; a backend does:
//   - natively (tests, rig-host): plugin_clap.h opens the .clap with dlopen / LoadLibrary;
//   - in the browser: the worklet instantiates the bundle's .wclap and the engine calls it through
//     imports (plugin_js.h, ui/worklet/wclap-host.js).
// Either way the plugin describes itself with the JSON of sdk/rig_clap_ext.h (RIG plugins), or the
// backend builds that JSON from clap.params (any other CLAP effect), and parsePluginInfo() below turns
// it into the ModuleDesc / SourceDesc the rest of the engine reads.
#pragma once
#include <deque>
#include <string>
#include <vector>
#include "json.hpp"                                    // third_party/nlohmann (as NAM core uses it)
#include "module.h"
#include "source.h"

namespace rig {

// One loaded plugin, as the backend runs it. Instances are opaque handles.
struct PluginBackend {
  virtual ~PluginBackend() {}
  virtual void* create(float sr) = 0;                    // a new, activated instance (control thread)
  virtual void destroy(void* inst) = 0;
  // One block: knobs normalised 0..1 (the backend sends the plugin only those that changed).
  virtual void process(void* inst, const float* inL, const float* inR, float* oL, float* oR, int n, const float* norm, int nparams, float bpm) = 0;
  virtual float output(void* inst) = 0;                  // a source's value after process(), -1..1
  virtual int meters(void* inst, float* out, int max) = 0;
};

// The description, owned: the strings and arrays a ModuleDesc / SourceDesc points into.
struct PluginInfo {
  bool source = false;
  std::string id, name, sub, category, author, version, clapId, tags = "[]", file;
  std::deque<std::string> strs;                          // stable storage for every const char* below
  std::vector<ParamDesc> params;
  std::deque<std::vector<ParamStop>> stops;
  ModuleDesc mdesc{};
  SourceDesc sdesc{};
  PluginBackend* backend = nullptr;
  std::string origin;                                    // the .clap it was opened from (rig-host names its copies by content)
  bool retired = false;                                  // replaced by a newer file, or removed: hidden, never added again
  const char* keep(const std::string& s) { strs.push_back(s); return strs.back().c_str(); }
};

inline bool validTypeId(const std::string& id) {
  if (id.empty() || id.size() >= 16 || !(id[0] >= 'a' && id[0] <= 'z')) return false;
  for (char c : id) if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
  return true;
}

// Fills `p` from a description (sdk/rig_clap_ext.h). false, with `err`, if it isn't usable.
inline bool parsePluginInfo(const char* json, PluginInfo& p, std::string& err) {
  const nlohmann::json j = nlohmann::json::parse(json, nullptr, false);
  if (j.is_discarded() || !j.is_object()) { err = "the plugin's description isn't JSON"; return false; }
  auto str = [&](const char* k, const char* d = "") { return j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : std::string(d); };
  p.source = str("kind", "effect") == "source";
  p.id = str("id"); p.name = str("name"); p.sub = str("sub"); p.category = str("category", "experimental");
  p.author = str("author"); p.version = str("version");
  if (j.contains("tags") && j["tags"].is_array()) p.tags = j["tags"].dump();
  if (!validTypeId(p.id)) { err = "the plugin's id \"" + p.id + "\" isn't usable (a-z, 0-9, _; under 16)"; return false; }
  if (p.name.empty()) p.name = p.id;
  const nlohmann::json ps = j.contains("params") && j["params"].is_array() ? j["params"] : nlohmann::json::array();
  if ((int)ps.size() > MAXPAR) { err = "more than " + std::to_string(MAXPAR) + " knobs"; return false; }
  p.params.clear();
  for (const auto& q : ps) {
    if (!q.is_object()) { err = "a knob isn't described"; return false; }
    auto s = [&](const char* k, const char* d = "") { return q.contains(k) && q[k].is_string() ? q[k].get<std::string>() : std::string(d); };
    auto f = [&](const char* k, double d) { return q.contains(k) && q[k].is_number() ? q[k].get<double>() : d; };
    ParamDesc d{};
    d.symbol = p.keep(s("symbol")); d.label = p.keep(s("label", s("symbol").c_str()));
    d.min = (float)f("min", 0); d.max = (float)f("max", 1); d.def = (float)f("def", 0);
    d.unit = p.keep(s("unit")); d.curve = s("curve") == "log" ? Curve::Log : Curve::Lin;
    d.decimals = (int)f("decimals", 2); d.sign = q.contains("sign") && q["sign"].is_boolean() && q["sign"].get<bool>();
    if (!(d.max > d.min) || (d.curve == Curve::Log && d.min <= 0) || !(d.def >= 0 && d.def <= 1) || !*d.symbol) { err = std::string("knob \"") + d.symbol + "\": bad range or default"; return false; }
    if (q.contains("stops") && q["stops"].is_array() && !q["stops"].empty()) {
      p.stops.emplace_back();
      for (const auto& st : q["stops"]) if (st.is_array() && st.size() == 2 && st[0].is_number() && st[1].is_string()) p.stops.back().push_back({st[0].get<float>(), p.keep(st[1].get<std::string>())});
      d.stops = p.stops.back().data(); d.nstops = (int)p.stops.back().size();
    }
    p.params.push_back(d);
  }
  const nlohmann::json ms = j.contains("meters") && j["meters"].is_array() ? j["meters"] : nlohmann::json::array();
  const char* meters[2] = {nullptr, nullptr}; int nm = 0;
  for (const auto& m : ms) if (m.is_string() && nm < 2) meters[nm++] = p.keep(m.get<std::string>());
  const char* id = p.keep(p.id); const char* name = p.keep(p.name); const char* sub = p.keep(p.sub);
  p.mdesc = ModuleDesc{id, name, sub, (int)p.params.size(), p.params.data(), nm, {meters[0], meters[1]}};
  p.sdesc = SourceDesc{id, name, sub, nullptr, nm, {meters[0], meters[1]}, (int)p.params.size(), p.params.data()};
  return true;
}

// An instance in the chain.
struct PluginModule : Module {
  PluginInfo* info; void* inst = nullptr;
  explicit PluginModule(PluginInfo* i) : info(i) {}
  ~PluginModule() override { if (inst) info->backend->destroy(inst); }
  const ModuleDesc& desc() const override { return info->mdesc; }
  void init(float s) override { sr = s; if (inst) info->backend->destroy(inst); inst = info->backend->create(sr); }
  void reset() override { init(sr); }
  void process(const float* inL, const float* inR, float* oL, float* oR, int n, const float* e, const Context& ctx) override {
    if (!inst) { for (int i = 0; i < n; i++) { oL[i] = inL[i]; oR[i] = inR[i]; } return; }
    info->backend->process(inst, inL, inR, oL, oR, n, e, (int)info->params.size(), ctx.bpm);
    for (int i = 0; i < n; i++) { if (!std::isfinite(oL[i])) oL[i] = 0; if (!std::isfinite(oR[i])) oR[i] = 0; }   // a plugin's NaN stays in the plugin
  }
  void meters(float* out) const override { if (inst) info->backend->meters(inst, out, MAXMODMETERS); }
};

// An instance in the rack. Its input is the dry guitar (as every source gets); its audio output is unused.
struct PluginSource : Source {
  PluginInfo* info; void* inst = nullptr; float scratch[2][MAXBLOCK] = {{0}}; float last = 0;
  explicit PluginSource(PluginInfo* i) : info(i) {}
  ~PluginSource() override { if (inst) info->backend->destroy(inst); }
  const SourceDesc& desc() const override { return info->sdesc; }
  void init(float s) override { sr = s; if (inst) info->backend->destroy(inst); inst = info->backend->create(sr); }
  float update(const SourceContext& c) override {
    if (!inst) return 0.f;
    info->backend->process(inst, c.in, c.in, scratch[0], scratch[1], c.n, c.p, (int)info->params.size(), c.bpm);
    const float v = info->backend->output(inst);
    last = std::isfinite(v) ? (v < -1.f ? -1.f : v > 1.f ? 1.f : v) : 0.f;
    return last;
  }
  void meters(float* out) const override { if (inst) info->backend->meters(inst, out, MAXSRCMETERS); }
};

}  // namespace rig

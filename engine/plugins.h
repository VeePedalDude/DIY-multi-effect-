// engine/plugins.h - plugin types: loading a plugin (core/plugin.h) and adding it to the type tables
// (fx_chain.h, mod_rack.h). Types are global and outlive engines, like the built-ins; after adding one,
// re-describe the engine (Engine::describe) so the page sees it. Call from the engine's own thread
// (between blocks): the tables are read by fx_add / src_add and describe().
#pragma once
#include <deque>
#include <string>
#include "engine.h"
#if defined(__wasm__)
#include "core/plugin_js.h"
#else
#include "core/plugin_clap.h"
#endif

namespace rig {

inline std::deque<PluginInfo>& pluginInfos() { static std::deque<PluginInfo> d; return d; }   // never moved: types point into it
inline std::string& pluginStatus() { static std::string s; return s; }

// Adds a described plugin as a type; returns its index in the module or source table, or an error.
inline int commitPlugin(PluginInfo& p) {
  std::string& st = pluginStatus();
  for (int t = 0; t < moduleTypeCount(); t++) if (moduleTypes()[t].plugin == &p) return t;     // already added
  for (int t = 0; t < sourceTypeCount(); t++) if (sourceTypes()[t].plugin == &p) return t;
  if (moduleTypeIndex(p.id.c_str()) >= 0 || sourceTypeIndex(p.id.c_str()) >= 0) { st = "a type called \"" + p.id + "\" is already loaded"; return -2; }
  bool known = false;
  if (p.source) { for (int c = 0; c < NSRCCATS; c++) known = known || p.category == sourceCategories()[c].id; if (!known) p.category = "chaos"; }
  else { for (int c = 0; c < NMODCATS; c++) known = known || p.category == moduleCategories()[c].id; if (!known) p.category = "experimental"; }
  int t;
  if (p.source) {
    SourceTypeTable& tt = sourceTypeTable();
    if (tt.n >= NSRCTYPES + MAXPLUGINSOURCES) { st = "no room for more plugin sources"; return -3; }
    tt.t[tt.n] = SourceType{nullptr, nullptr, p.keep(p.category), &p}; t = tt.n++;
  } else {
    ModuleTypeTable& tt = moduleTypeTable();
    if (tt.n >= NMODTYPES + MAXPLUGINTYPES) { st = "no room for more plugin effects"; return -3; }
    tt.t[tt.n] = ModuleType{nullptr, nullptr, p.keep(p.category), &p}; t = tt.n++;
  }
  st = std::string(p.source ? "source " : "effect ") + p.id + " (" + p.name + ") added";
  return t;
}

#if defined(__wasm__)
inline int registerJsPlugin(int lib, const char* json, const char* file) {
  auto& d = pluginInfos(); d.emplace_back(); PluginInfo& p = d.back();
  std::string err;
  if (!json || !*json || !parsePluginInfo(json, p, err)) { pluginStatus() = err.empty() ? "no description" : err; d.pop_back(); return -1; }
  p.file = file ? file : ""; p.backend = new JsPlugin(lib);
  const int r = commitPlugin(p);
  if (r < 0) { delete p.backend; d.pop_back(); }
  return r;
}
#else
// Opens a .clap and reads what it is (off the engine's thread if it may be slow). Returns a handle for
// commitPluginHandle, or -1 (pluginStatus() says why).
inline int openClapPlugin(const char* path, const char* file) {
  auto& d = pluginInfos(); d.emplace_back(); PluginInfo& p = d.back();
  auto* lib = new ClapLibrary(); std::string err;
  if (!lib->open(path, p, err)) { pluginStatus() = err; delete lib; d.pop_back(); return -1; }
  p.file = file ? file : path;
  pluginStatus() = "opened " + p.id;
  return (int)d.size() - 1;
}
inline int commitPluginHandle(int h) {
  auto& d = pluginInfos();
  if (h < 0 || h >= (int)d.size() || !d[(size_t)h].backend) { pluginStatus() = "no such plugin"; return -1; }
  return commitPlugin(d[(size_t)h]);
}
inline int loadClapPlugin(const char* path) { const int h = openClapPlugin(path, nullptr); return h < 0 ? h : commitPluginHandle(h); }
#endif

}  // namespace rig

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

// The plugin type called `id` (an effect or a source), or nullptr: built-ins and retired types aren't.
inline PluginInfo* livePlugin(const char* id) {
  const int m = moduleTypeIndex(id); if (m >= 0) return moduleTypes()[m].plugin;
  const int s = sourceTypeIndex(id); return s >= 0 ? sourceTypes()[s].plugin : nullptr;
}
// Retires a plugin type (its file was removed): 0, or -1 if no plugin type has that id.
inline int retirePlugin(const char* id) {
  PluginInfo* p = livePlugin(id); if (!p) { pluginStatus() = std::string("no plugin type called \"") + id + "\""; return -1; }
  p->retired = true; pluginStatus() = std::string(p->source ? "source " : "effect ") + p->id + " (" + p->name + ") removed"; return 0;
}
// Adds a described plugin as a type; returns its index in the module or source table, or an error.
// A plugin type with the same id is replaced (retired: running instances keep its code); a built-in isn't.
inline int commitPlugin(PluginInfo& p) {
  std::string& st = pluginStatus();
  for (int t = 0; t < moduleTypeCount(); t++) if (moduleTypes()[t].plugin == &p) return t;     // already added
  for (int t = 0; t < sourceTypeCount(); t++) if (sourceTypes()[t].plugin == &p) return t;
  PluginInfo* old = livePlugin(p.id.c_str());
  if (old && !p.origin.empty() && old->origin == p.origin) {     // the very same file again (a page reloaded): keep it
    st = std::string(p.source ? "source " : "effect ") + p.id + " (" + p.name + ") already loaded";
    return p.source ? sourceTypeIndex(p.id.c_str()) : moduleTypeIndex(p.id.c_str());
  }
  if (!old && (moduleTypeIndex(p.id.c_str()) >= 0 || sourceTypeIndex(p.id.c_str()) >= 0)) { st = "a built-in type is called \"" + p.id + "\""; return -2; }
  if (p.source ? sourceTypeTable().n >= NSRCTYPES + MAXPLUGINSOURCES : moduleTypeTable().n >= NMODTYPES + MAXPLUGINTYPES) {
    st = std::string("no room for more plugin ") + (p.source ? "sources" : "effects") + " (replaced ones count until the rig restarts)"; return -3;
  }
  bool known = false;
  if (p.source) { for (int c = 0; c < NSRCCATS; c++) known = known || p.category == sourceCategories()[c].id; if (!known) p.category = "chaos"; }
  else { for (int c = 0; c < NMODCATS; c++) known = known || p.category == moduleCategories()[c].id; if (!known) p.category = "experimental"; }
  int t;
  if (p.source) {
    SourceTypeTable& tt = sourceTypeTable();
    tt.t[tt.n] = SourceType{nullptr, nullptr, p.keep(p.category), &p}; t = tt.n++;
  } else {
    ModuleTypeTable& tt = moduleTypeTable();
    tt.t[tt.n] = ModuleType{nullptr, nullptr, p.keep(p.category), &p}; t = tt.n++;
  }
  if (old) old->retired = true;
  st = std::string(p.source ? "source " : "effect ") + p.id + " (" + p.name + ") " + (old ? "replaced" : "added");
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
  p.file = file ? file : path; p.origin = path;
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

// engine/core/plugin_js.h - the browser's plugin backend: the engine (rig.wasm) runs in the audio
// worklet beside each plugin's own WebAssembly module (its .wclap), which it can't call directly. So
// it calls these imports, and ui/worklet/wclap-host.js does the CLAP side: copies the audio and the
// changed knobs into the plugin's memory, calls its process(), and copies the result back.
// A plugin is a number the worklet gave it (`lib`); an instance is a number too.
#pragma once
#include "plugin.h"

#define RIG_IMPORT(name) extern "C" __attribute__((import_module("rig"), import_name(#name)))
RIG_IMPORT(plugin_create)  int   rig_js_plugin_create(int lib, float sr);
RIG_IMPORT(plugin_destroy) void  rig_js_plugin_destroy(int lib, int inst);
RIG_IMPORT(plugin_process) void  rig_js_plugin_process(int lib, int inst, const float* inL, const float* inR, float* oL, float* oR, int n, const float* norm, int nparams, float bpm);
RIG_IMPORT(plugin_output)  float rig_js_plugin_output(int lib, int inst);
RIG_IMPORT(plugin_meters)  int   rig_js_plugin_meters(int lib, int inst, float* out, int max);
#undef RIG_IMPORT

namespace rig {

struct JsPlugin : PluginBackend {
  int lib;
  explicit JsPlugin(int l) : lib(l) {}
  // Instance numbers start at 1, so a handle is never nullptr.
  void* create(float sr) override { const int h = rig_js_plugin_create(lib, sr); return h > 0 ? (void*)(intptr_t)h : nullptr; }
  void destroy(void* inst) override { rig_js_plugin_destroy(lib, (int)(intptr_t)inst); }
  void process(void* inst, const float* inL, const float* inR, float* oL, float* oR, int n, const float* norm, int np, float bpm) override {
    rig_js_plugin_process(lib, (int)(intptr_t)inst, inL, inR, oL, oR, n, norm, np, bpm);
  }
  float output(void* inst) override { return rig_js_plugin_output(lib, (int)(intptr_t)inst); }
  int meters(void* inst, float* out, int max) override { return rig_js_plugin_meters(lib, (int)(intptr_t)inst, out, max); }
};

}  // namespace rig

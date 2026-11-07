/* sdk/rig_clap_ext.h - RIG/01's extension to CLAP, shared by plugins (sdk/rigfx.h) and the engine's
 * hosts (engine/core/plugin_clap.h natively, ui/worklet/wclap-host.js in the browser).
 *
 * CLAP describes knobs by a range and a text readout. RIG/01 knows more about a knob (a log curve, a
 * unit, named stops) and has modulation sources, which CLAP has no notion of. A RIG plugin answers
 * clap_plugin->get_extension(plugin, RIG_EXT_DESCRIBE) with this struct:
 *
 *   json(plugin)     the description, as engine/core/plugin.h reads it (see docs/PLUGINS.md):
 *                    {"rig":1, "kind":"effect"|"source", "id", "name", "sub", "category", "tags":[..],
 *                     "author", "version", "meters":[..], "params":[{"symbol","label","min","max","def",
 *                     "unit","curve":"lin"|"log","decimals","sign","stops":[[value,"name"],..]}, ..]}
 *                    Its CLAP params are the same knobs in the same order, ids 0..n-1, normalised 0..1.
 *   meters(p, out, max)  up to `max` display values after the last process(); returns how many.
 *   output(p)        a source's value after the last process(), -1..1 (effects: 0).
 *
 * Plain CLAP plugins (no extension) are effects whose knobs come from clap.params alone.
 */
#ifndef RIG_CLAP_EXT_H
#define RIG_CLAP_EXT_H
#include <clap/clap.h>

#ifdef __cplusplus
extern "C" {
#endif

static const char RIG_EXT_DESCRIBE[] = "rig01.describe/1";
static const char RIG_FEATURE_MOD_SOURCE[] = "rig01-mod-source";   /* in a source's descriptor features */

typedef struct rig_plugin_describe {
  const char*(CLAP_ABI* json)(const clap_plugin_t* plugin);
  uint32_t(CLAP_ABI* meters)(const clap_plugin_t* plugin, float* out, uint32_t max);
  float(CLAP_ABI* output)(const clap_plugin_t* plugin);
} rig_plugin_describe_t;

#ifdef __cplusplus
}
#endif
#endif

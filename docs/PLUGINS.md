# Plugins: effects and sources as files

A plugin is an effect or a modulation source that comes as one file, a `.rigfx` bundle, instead of being
compiled into the rig. Put it in the library's `plugins/` folder (or drop it on the page). It shows up in
the **Add an effect** / **Add a source** panels, patching, the protocol and presets like a built-in.
The rig isn't rebuilt.

Plugins are [CLAP](https://github.com/free-audio/clap) plugins, the open plugin standard, and
[WebCLAP](https://github.com/WebCLAP): the same plugin compiled to WebAssembly, which the browser runs.
A bundle holds both, built from one source file.

| File in the bundle | Who runs it |
|---|---|
| `clap/x86_64-windows.clap` | rig-host on Windows (native, full speed) |
| `clap/x86_64-linux.clap` | rig-host on Linux, and the native tests |
| `module.wclap` | the browser test page, in its audio worklet (about 1.5x the CPU of native) |
| `manifest.json` | the library: name, kind, category, tags, author, version, and the full description |
| `screen.js` (optional) | the page: the plugin's face screen |

Because the native `.clap` files are standard CLAP plugins, other CLAP hosts (Bitwig, REAPER...) can
load a RIG effect too. The rig-host also runs other makers' CLAP effects: put a plain `.clap` (an effect
with an audio input and output) in `plugins/`. Its first 8 automatable parameters become its knobs, in
the plugin's own range. The browser runs `.rigfx` bundles only, since it can't run native code.

## Writing one

Writing one in another chat or tool? [WRITING_A_PLUGIN_EFFECT.md](WRITING_A_PLUGIN_EFFECT.md) is a
self-contained brief: the API, the rules, the helpers and how to test it.

An effect plugin is written exactly like a built-in effect
([ADDING_A_MODULE.md](ADDING_A_MODULE.md)), and a source plugin like a built-in source
([ADDING_A_SOURCE.md](ADDING_A_SOURCE.md)). Make a folder in `plugins/` holding the header and a
`plugin.json`:

```
plugins/trem/
  trem.h         the effect: ParamDesc knobs, a ModuleDesc, struct Trem : Module (as a built-in)
  plugin.json    {"struct": "rig::Trem", "kind": "effect", "category": "modulation",
                  "tags": ["tremolo", "volume"], "author": "you", "version": "1.0.0"}
  screen.js      optional: screen('trem', (c, b, m, h) => { ... })  (as ui/screens/<type>.js)
```

Then build it:

```
python3 tools/build_plugin.py plugins/trem         # -> dist/plugins/trem.rigfx
```

The build compiles three times with zig (the `ziglang` pip package `./build.sh` already uses): Windows
and Linux CLAP, and WebAssembly. It reads the plugin's description back from the Linux build into the
manifest, so the two can't disagree. `#include "module.h"` (or `"source.h"`): the build puts
`engine/core` on the include path, so `dsp.h`, `eq.h`, `vaf.h` and the rest are there too.

Two examples ship, built by `./build.sh` into `dist/plugins/`:

- `plugins/trem`: the tremolo from `docs/templates/module_template.h`, as an effect plugin.
- `plugins/rand`: the sample-and-hold from `docs/templates/source_template.h`, as a source plugin.

The rules are the built-ins' rules: nothing in `process()` / `update()` allocates, all state lives in
members, and knobs are smoothed so they never click. `kind` is `effect` or `source`; `category` is a
folder name from `engine/modules/categories.txt` (effects) or `engine/sources/categories.txt`
(sources). Anything else goes in Experimental (effects) or Physics & chaos (sources). The id (the
`ModuleDesc` / `SourceDesc` id) must not be one the rig already has. Up to 8 knobs and 2 meters.

What a source plugin doesn't have yet: settings (`nsettings`), tables, taps (per-patch PHASE/DIV),
`ext` input and `view()`. Those stay built-in only for now. Effect plugins have no file slot yet.

## How it fits together

```
sdk/rigfx.h            wraps a rig::Module or rig::Source as a CLAP plugin (params, audio ports, state,
                       and RIG's extension, sdk/rig_clap_ext.h)
tools/build_plugin.py  builds a plugin folder into a .rigfx bundle
engine/core/plugin.h   a plugin as an engine type: PluginModule / PluginSource, its description
engine/core/plugin_clap.h   the native backend: dlopen / LoadLibrary, CLAP hosting (any CLAP effect)
engine/core/plugin_js.h     the browser backend: imports the worklet answers
ui/worklet/wclap-host.js    hosts .wclap modules in the audio worklet
engine/plugins.h       adds a loaded plugin to the type tables
ui/js/61-plugins.js    the page: reads bundles, loads library plugins before the rig is sent
hosts/rig-host         takes a bundle over the socket, loads its .clap on the loader thread
```

**RIG's CLAP extension** (`rig01.describe/1`): CLAP describes a knob by a range and a readout. The rig
also wants its curve, unit and named stops, and has modulation sources, which CLAP has no notion of.
A RIG plugin answers `get_extension("rig01.describe/1")` with three functions: `json()` (the
description), `meters()` and `output()` (a source's value after each block). Its CLAP parameters are
its knobs, ids 0..n-1, normalised 0..1, and their readout is in real units, so other CLAP hosts show
them properly.

**Each block**, the engine sends only the knobs that moved since the last block, as CLAP parameter
events at the block's start (a built-in sees its knobs the same way, once per block). In the browser
the worklet copies the audio into the plugin's memory and back; nothing is allocated per block on
either side.

**Loading**, the page reads each library plugin's manifest when the library is read. The add panels
and presets know a plugin from then on, before the engine has it. At Start (and when the rig-host
reconnects) every library plugin is handed to the engine before the rig is sent, so a preset that
uses one comes back whole. A preset names each plugin's file. Opened where that plugin is missing, the
preset says which file to add and keeps its settings in the file.

**Threads**: in the rig-host the `.clap` is opened on the loader thread (it may take a moment) and its
type is added between audio blocks. In the browser the worklet compiles the `.wclap` itself (a few
milliseconds for the examples).

## Tests

- `tests/native/test_plugins.cpp` (170-171) loads the examples' Linux `.clap`:
  - the effect matches its template's own test;
  - no allocation while it plays with a knob moving;
  - two instances; loading the same plugin twice is refused;
  - the source steps 2 per beat.
- `tests/browser/plugins.cjs` runs both bundles' `.wclap` in Chromium's audio worklet:
  - imported and listed before Start;
  - played, metered and patched;
  - saved in a preset, then restored after a reload.
- `tests/browser/host.cjs` section 7 gives the rig-host a bundle over the socket. Natively that loads
  the Linux `.clap`; with `RIG_HOST_BIN="wine dist/rig-host.exe"`, the Windows one.

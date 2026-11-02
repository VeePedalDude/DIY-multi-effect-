# Architecture

```
          ┌──────────────────────── ui/ (browser) ───────────────────────┐
guitar →  │ 3D stage · panel · matrix · presets ──text commands──┐       │
 2i2      │      ▲ meters + description                          ▼       │
          │      └──────────── worklet (ui/worklet) ◄── rig.wasm ────────┤ → 2i2 out
          └──────────────────────────────────────────────────────────────┘
                      rig.wasm = engine/ compiled to WebAssembly

Later on the iMac:  ui/ in a kiosk browser ──WebSocket──▶ hosts/linux (rig-host) ──JACK──▶ 2i2
                    same text commands, same engine/ source compiled natively
```

## Layers

| Folder | What lives there | Knows about |
|---|---|---|
| `engine/core/` | DSP building blocks (`dsp.h`), the `Module` and `Source` contracts | nothing project-specific |
| `engine/modules/` | one effect per file + `registry.h` (chain order) | `core/` only |
| `engine/sources/` | one modulation source per file + `registry.h` | `core/` only |
| `engine/engine.h` | the chain: routing, modulation, bypass, master, limiter, meters, `describe()` | the two registries, never a specific effect |
| `engine/api/` | flat C API used by WebAssembly and future hosts | `engine.h` |
| `ui/worklet/` | runs `rig.wasm` in the browser, parses the text protocol | the engine's description |
| `ui/js/` | page logic, one file per job (model, bridge, audio, stage, faces, HUD, controllers, presets, main) | the engine's description |
| `ui/screens/` | the small display on each module's face, one file per module id | its own module |
| `ui/js/52–56`, `72` | library storage backends, file helpers, library, file-slot UI, preset format ([LIBRARY.md](LIBRARY.md)) | the engine's description |
| `tests/ui/` | Node unit tests for the library and preset format; `tests/fixtures/` holds the golden preset | `ui/js` pure files |
| `tests/native/` | engine tests, one file per area | `engine/` |
| `tests/parity/` | proves the WebAssembly build matches native | C API |
| `tests/browser/` | headless Chromium tests of the built page | `dist/rig-test.html` |
| `hosts/linux/` | (next) native JACK host for the iMac | C API + protocol |
| `tools/` | build helpers | |

## The self-description

The engine publishes everything the UI needs as JSON (`rig_describe()`): modules with each
parameter's symbol, label, range, unit, curve and default; sources; and where each value
sits in the meter block. The worklet builds the protocol from it, and the UI builds the
stage, knobs, panel, matrix and number formatting from it. That's why adding an effect
needs no UI or protocol changes.

## Rules that keep it modular

- Modules and sources never include each other. They only include `core/`.
- `engine.h` never names a specific effect or source.
- The UI never hard-codes parameter positions; it looks values up by `module id` + `symbol`
  (`rig.eff('holo', 'feedback')`).
- Parameters cross every boundary normalised 0..1. Real units come from `ParamDesc::map()`,
  the same function the UI's formatter mirrors.

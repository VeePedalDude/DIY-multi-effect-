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
| `engine/core/` | DSP building blocks (`dsp.h`), the `Module` and `Source` contracts, the NAM glue (`nam.h`: captures and IRs), the click-free model swap (`model_slot.h`) and the strobe tuner (`tuner.h`) | nothing project-specific; `nam.h` and `tuner.h` use `third_party/` |
| `third_party/` | NeuralAmpModelerCore (plays every `.nam`, convolves IRs), Eigen, nlohmann/json, Q (the tuner's pitch detector): vendored, never edited ([README](../third_party/README.md)) | nothing |
| `engine/modules/` | one effect per file, in category folders (`delay/holo.h`); `categories.txt` (the tabs), `default_chain.txt` (a fresh rig); `registry.h` is generated from the folders | `core/` only |
| `engine/sources/` | one modulation source type per file, in category folders (`time/lfo.h`), the same way ([format](ADDING_A_SOURCE.md)) | `core/` only |
| `tools/gen_registry.py` | makes both `registry.h` files from the folders and checks them (tested by `tests/tools/gen_registry.test.py`) | the folders |
| `engine/fx_chain.h` | the effect chain's building blocks: module types as a table, the chain order packed into one atomic word | the modules registry, never a specific module |
| `engine/mod_rack.h` | the modulation rack: source instances added and removed while it plays, cross-modulation, safe freeing | the sources registry, never a specific source |
| `engine/engine.h` | the chain: module instances added, removed and moved while it plays (fade in, fade out, dip on a move), routing, modulation, bypass, master, limiter, meters, `describe()` | the chain and the rack, never a specific effect or source |
| `engine/api/` | flat C API used by WebAssembly and future hosts | `engine.h` |
| `ui/worklet/` | runs `rig.wasm` in the browser, parses the text protocol | the engine's description |
| `ui/js/` | page logic, one file per job (model, bridge, audio, stage, faces, HUD, controllers, presets, main) | the engine's description |
| `ui/screens/` | the small display on each module's face, and its card colour (`TILE.<id>`), one file per module id | its own module |
| `ui/assets/` | images embedded into the page for the built-in skins: the hall background, the three card frames | nothing |
| `ui/js/04-skin-core.js`, `57-skins.js` | skins: the default skin (also the schema), validation, `.rigskin` zips; applying a skin and the Skins menu ([SKINS.md](SKINS.md)) | the default skin |
| `ui/js/58-tuner.js` | the strobe tuner: the face-down mini card in the top bar, the sideways card it opens, T to open and collapse | the engine's meter layout, the skin's `tuner` section |
| `ui/js/24-chain.js` | the chain from the page's side: add, remove, duplicate, move, keeping in step with the engine's slots | the engine's description |
| `ui/js/49-picker.js` | the add panels' picker: a tab per category from the engine's description, a list per tab, search across all | the engine's description |
| `ui/js/22-rack.js` | the rack from the page's side: add, remove, duplicate, keeping in step with the engine's slots | the engine's description |
| `ui/js/51-source-panel.js` | the side panel for a modulation source (knobs, what moves each of them, live trace, what it moves) and the "+ Add a source" panel | the engine's description |
| `ui/js/52–56`, `72` | library storage backends, file helpers, library, file-slot UI, preset format ([LIBRARY.md](LIBRARY.md)) | the engine's description |
| `tests/ui/` | Node unit tests for the library and preset format; `tests/fixtures/` holds the golden preset | `ui/js` pure files |
| `tests/native/` | engine tests, one file per area; `test_conformance.cpp` checks the format and safety of every registered module and source type | `engine/` |
| `tests/parity/` | proves the WebAssembly build matches native | C API |
| `tests/browser/` | headless Chromium tests of the built page | `dist/rig-test.html` |
| `hosts/linux/` | (next) native JACK host for the iMac | C API + protocol |
| `tools/` | build helpers; `tools/wasm/` holds the shims that let NAM core build for WebAssembly ([README](../tools/wasm/README.md)) | |
| `tests/models/` | `.nam` captures of every architecture, used by the native, parity and browser tests ([README](../tests/models/README.md)) | |

## The self-description

The engine publishes everything the UI needs as JSON (`rig_describe()`): modules with each
parameter's symbol, label, range, unit, curve and default; sources; and where each value
sits in the meter block. The worklet builds the protocol from it, and the UI builds the
stage, knobs, panel, matrix and number formatting from it. That's why adding an effect
needs no UI or protocol changes.

## Rules that keep it modular

- Modules and sources never include each other. They only include `core/`.
- The look is data (a skin), the layout is code: UI code takes every colour, font and image from the skin.
- Vendored code in `third_party/` is never edited; WebAssembly differences live in `tools/wasm/`.
- Nothing in `process()` allocates. Files are parsed and built on the control thread and swapped in
  through a `ModelSlot`; a test counts heap calls inside `process()` while files load and swap.
- `engine.h` never names a specific effect or source. Modules are instances in the chain and sources
  instances in the rack; the registries are only the lists of types.
- Growing a module or source follows [EXPANDING.md](EXPANDING.md): append knobs, default to the old
  behaviour, prove it bit-identical, rename only through `PRESET_RENAMES`.
- The UI never hard-codes parameter positions; it looks values up by `module id` + `symbol`
  (`rig.eff('holo', 'feedback')`).
- Parameters cross every boundary normalised 0..1. Real units come from `ParamDesc::map()`,
  the same function the UI's formatter mirrors.

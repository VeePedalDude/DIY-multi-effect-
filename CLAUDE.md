# RIG/01 multi-effect

Read `docs/ARCHITECTURE.md` first. Adding things: `docs/ADDING_A_MODULE.md`, `docs/ADDING_A_SOURCE.md`. Protocol: `docs/PROTOCOL.md`.

- `third_party/` (NeuralAmpModelerCore, Eigen, nlohmann/json, Q) is vendored unchanged: never edit it. WebAssembly shims go in `tools/wasm/`, NAM glue in `engine/core/nam.h` (see `third_party/README.md`).
- Nothing in a module's `process()` may allocate: files are built in `assetCommit` and swapped through `ModelSlot` (`engine/core/model_slot.h`); test 26 counts heap calls.
- DSP: one effect per file in `engine/modules/`, one modulation source type per file in `engine/sources/`, each listed once in that folder's `registry.h`. `engine/engine.h` and `engine/mod_rack.h` must never name a specific effect or source. Sources are instances in the modulation rack (several of one type, added and removed live, cross-modulation). Formats: `docs/ADDING_A_SOURCE.md` (template in `docs/templates/`), `docs/ADDING_A_MODULE.md`; growing one: `docs/EXPANDING.md`. `tests/native/test_conformance.cpp` checks every type.
- UI: `ui/js/` (one job per file, numbered for load order), `ui/screens/<module id>.js` for face displays. The UI builds itself from the engine's `rig_describe()` JSON; never hard-code parameter positions.
- Skins (`docs/SKINS.md`): the look is data, the layout is code. Never write a literal colour, font or image in UI code: CSS uses `var(--…)`, canvas/three.js uses `SKIN.*`, face screens use `h.col.*`. Something new to theme = a new key in `DEFAULT_SKIN` (ui/js/04-skin-core.js) and the table in docs/SKINS.md.
- Every change runs through `./build.sh` (native tests at zero failures with -Wall -Wextra -Werror, WebAssembly build, native/WASM parity, page build and syntax checks).
- Browser tests: `PLAYWRIGHT_CORE=<path> THREE_JS=<three.min.js r128> node tests/browser/browser.cjs`, `blocked.cjs`, `library.cjs`, `skins.cjs`, `tuner.cjs` and `rack.cjs`.
- Library and presets: `docs/LIBRARY.md`. Preset files must keep loading: never change `tests/fixtures/*.rig.json`; when a knob or module is renamed, add it to `PRESET_RENAMES`; when the file layout changes, bump `PRESET_VERSION` and add a migration.
- Report measured numbers before and after a DSP change. Add a test for each new module or source.
- Rebuild `dist/rig-test.html` and commit it with every change so it can be downloaded straight from GitHub.

# RIG/01 multi-effect

Read `docs/ARCHITECTURE.md` first. Adding things: `docs/ADDING_A_MODULE.md`, `docs/ADDING_A_SOURCE.md`. Protocol: `docs/PROTOCOL.md`.

- The repository is GPLv3 (`LICENSE`), because rig-host.exe includes Steinberg's ASIO SDK under its GPLv3 option; anything added must be GPLv3-compatible.
- `third_party/` (NeuralAmpModelerCore, Eigen, nlohmann/json, Q, miniaudio, the ASIO SDK headers, CLAP, hypedfuzz) is vendored unchanged: never edit it. WebAssembly shims go in `tools/wasm/`, NAM glue in `engine/core/nam.h` (see `third_party/README.md`).
- Nothing in a module's `process()` may allocate: files are built in `assetCommit` and swapped through `ModelSlot` (`engine/core/model_slot.h`); test 26 counts heap calls.
- DSP: one effect per file in `engine/modules/<category>/`, one modulation source type per file in `engine/sources/<category>/`. The folders are the registry: `tools/gen_registry.py` (run by `./build.sh`) generates each `registry.h`; never edit those by hand. Categories (the tabs of the add panels) are `categories.txt`; what a fresh rig starts with is `default_chain.txt` / `default_rack.txt`; an optional `<category>/order.txt` orders a tab. `engine/engine.h`, `engine/fx_chain.h` and `engine/mod_rack.h` must never name a specific effect or source. Modules are instances in the effect chain (several of one type, added, removed and reordered live) and sources instances in the modulation rack (the same, plus cross-modulation); commands address both by instance id. Formats: `docs/ADDING_A_SOURCE.md` (template in `docs/templates/`), `docs/ADDING_A_MODULE.md`; growing one: `docs/EXPANDING.md`. `tests/native/test_conformance.cpp` checks every type.
- Plugins (`docs/PLUGINS.md`): an effect or source can also ship as a `.rigfx` bundle (CLAP for Windows and Linux + WebCLAP for the browser) built from `plugins/<id>/` by `tools/build_plugin.py`; the engine adds it as a type at run time (`engine/plugins.h`), so `engine.h`, `fx_chain.h` and `mod_rack.h` still never name one. `./build.sh` builds `plugins/*` into `dist/plugins/` (commit them, like the page) before the native tests, which load them.
- UI: `ui/js/` (one job per file, numbered for load order), `ui/screens/<module type>.js` for face displays (one per type, shared by its instances). The UI builds itself from the engine's `rig_describe()` JSON; never hard-code parameter positions.
- Skins (`docs/SKINS.md`): the look is data, the layout is code. Never write a literal colour, font or image in UI code: CSS uses `var(--…)`, canvas/three.js uses `SKIN.*`, face screens use `h.col.*`. Something new to theme = a new key in `DEFAULT_SKIN` (ui/js/04-skin-core.js) and the table in docs/SKINS.md.
- The text protocol is parsed once, in C++ (`rig_exec` in `engine/api/rig_api.cpp`), by the browser's worklet and by `hosts/rig-host` (native host: Windows ASIO or WASAPI, Linux ALSA/JACK; commit `dist/rig-host.exe` with every change, like the page). New commands go there, with a case in `tests/api/protocol.cpp`.
- Every change runs through `./build.sh` (native tests at zero failures with -Wall -Wextra -Werror, WebAssembly build, native/WASM parity, page build and syntax checks).
- Browser tests: `PLAYWRIGHT_CORE=<path> THREE_JS=<three.min.js r128> node tests/browser/browser.cjs`, `blocked.cjs`, `library.cjs`, `skins.cjs`, `tuner.cjs`, `rack.cjs`, `chain.cjs`, `geo.cjs`, `seq.cjs`, `controllers.cjs`, `polyv.cjs`, `viz.cjs`, `plugins.cjs` and `host.cjs` (rig-host end to end; `RIG_HOST_BIN="wine dist/rig-host.exe"` for the Windows build, plus `RIG_HOST_AUDIO="--asio rig"` for its ASIO path on the stand-in driver `tests/asio/asio.sh` registers; build.sh runs `asio.sh` when wine is installed).
- Library and presets: `docs/LIBRARY.md`. Preset files must keep loading: never change `tests/fixtures/*.rig.json`; when a knob or module is renamed, add it to `PRESET_RENAMES`; when the file layout changes, bump `PRESET_VERSION` and add a migration.
- Report measured numbers before and after a DSP change. Add a test for each new module or source.
- Rebuild `dist/rig-test.html` and commit it with every change so it can be downloaded straight from GitHub.

## How to work (the Karpathy rules)

From [andrej-karpathy-skills](https://github.com/forrestchang/andrej-karpathy-skills) (MIT; Andrej
Karpathy's observations on LLM coding mistakes, written up by Forrest Chang), as published:

Behavioral guidelines to reduce common LLM coding mistakes. Merge with project-specific instructions as needed.

**Tradeoff:** These guidelines bias toward caution over speed. For trivial tasks, use judgment.

### 1. Think Before Coding

**Don't assume. Don't hide confusion. Surface tradeoffs.**

Before implementing:
- State your assumptions explicitly. If uncertain, ask.
- If multiple interpretations exist, present them - don't pick silently.
- If a simpler approach exists, say so. Push back when warranted.
- If something is unclear, stop. Name what's confusing. Ask.

### 2. Simplicity First

**Minimum code that solves the problem. Nothing speculative.**

- No features beyond what was asked.
- No abstractions for single-use code.
- No "flexibility" or "configurability" that wasn't requested.
- No error handling for impossible scenarios.
- If you write 200 lines and it could be 50, rewrite it.

Ask yourself: "Would a senior engineer say this is overcomplicated?" If yes, simplify.

### 3. Surgical Changes

**Touch only what you must. Clean up only your own mess.**

When editing existing code:
- Don't "improve" adjacent code, comments, or formatting.
- Don't refactor things that aren't broken.
- Match existing style, even if you'd do it differently.
- If you notice unrelated dead code, mention it - don't delete it.

When your changes create orphans:
- Remove imports/variables/functions that YOUR changes made unused.
- Don't remove pre-existing dead code unless asked.

The test: Every changed line should trace directly to the user's request.

### 4. Goal-Driven Execution

**Define success criteria. Loop until verified.**

Transform tasks into verifiable goals:
- "Add validation" → "Write tests for invalid inputs, then make them pass"
- "Fix the bug" → "Write a test that reproduces it, then make it pass"
- "Refactor X" → "Ensure tests pass before and after"

For multi-step tasks, state a brief plan:
```
1. [Step] → verify: [check]
2. [Step] → verify: [check]
3. [Step] → verify: [check]
```

Strong success criteria let you loop independently. Weak criteria ("make it work") require constant clarification.

**These guidelines are working if:** fewer unnecessary changes in diffs, fewer rewrites due to overcomplication, and clarifying questions come before implementation rather than after mistakes.

### How they apply here

- **Asking** (this narrows rule 1): ask only when the task can't be done safely or accurately without an
  answer. Otherwise
  state the assumption at the top of the work and go. A wrong guess that is cheap to redo is not a
  reason to stop.
- **Verify** means this repo's checks, not "it looks right":
  - `./build.sh` passes at zero failures;
  - the browser suite for the area you touched passes;
  - for DSP, measured numbers before and after the change;
  - for UI, a screenshot you have looked at.
- **Surgical:**
  - A change to one effect stays in that effect's file.
  - Generated files (`registry.h`) and `third_party/` are never hand-edited.
  - If you find a bug next to your change, report it in the summary rather than fixing it quietly.
- **Simplicity:** to grow an effect, follow `docs/EXPANDING.md`'s rule: append, default to the old
  behaviour, and prove the result is bit-identical. Don't add a knob, setting or option nobody asked
  for.

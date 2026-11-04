# Hyped Fuzz DSP v8 — Handoff for the RIG/01 Integration Agent

## What this package is

A **standalone C++17 circuit-derived** Hyped Fuzz DSP model, with a reference MNA solver, a three-stage real-time facade, selectable nonlinear solvers, component and semiconductor parameters, and optional experimental op-amp effects. It has not been installed in the RIG/01 repository and does not assume RIG/01's plugin interface.

**Not a verified physical clone:** semiconductor parameters and op-amp macromodels are estimates, and no calibrated physical-device measurements were used. Preserve that distinction in the UI/docs.

**Source of truth:** `src/HypedFuzzRealtime.h` + `src/HypedFuzz.h` (include-only C++17). Other headers provide optional controls.

## Instructions to the integration agent

1. Read the target repo's `CLAUDE.md`, `docs/ARCHITECTURE.md`, `docs/ADDING_A_SOURCE.md`, `docs/EXPANDING.md`, and `docs/PROTOCOL.md` if present; adapt to the **actual** effect interface, not an assumed one. The docs about *modulation sources* do not imply that this should be registered as a modulation source: it is an **audio effect**.
2. Place the four primary headers under an appropriate effects path (e.g. `engine/effects/hyped_fuzz/`), respecting project code structure. Do not replace existing modules.
3. Implement a thin host adapter matching the actual effect interface, with host-managed sample rate, oversampling, bypass, wet/output level, parameter automation and persistence. Preserve the core untouched initially.
4. Create/allocate DSP objects when the engine initializes or reconfigures, **not in the audio callback**. `Realtime::processBlock` consumes mono `float*` and emits mono `float*`. For stereo, decide explicitly whether to run independent L/R instances or convert from/to mono according to the host's expected semantics.
5. Normalize gain/bass/treble/level to `[0,1]`. `Mode` is Full / Scooped / Boost. Setters are intended for control-thread / block-boundary use; they are **not thread-safe to call concurrently** with audio rendering.
6. Suggested starting configuration is 48 kHz, 2x oversampling, `LinearSolver::Auto` (default), exact nonlinear math (default). Enable `CompactNewton` and `setFastNonlinearMath(true)` only after comparing performance/accuracy on the target host.
7. Expose baseline controls first; optional controls can be grouped under *Advanced Circuit* and *Op-Amp Lab*. Do not pass arbitrary string-based component setters directly through sample-rate audio callbacks.
8. Add effect registry entry, serialization and defaults, UI descriptors, WASM build/test bench support, preset compatibility and tests using the repo's conventions.
9. Run `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j && ctest --test-dir build --output-on-failure` in this standalone package before integrating. After integration, run native and WebAssembly builds, tests, and a CPU budget check on target hardware.
10. Keep a physical-circuit reference path; never silently replace exact-math output with a surrogate or force lower oversampling when under load.

## Required source headers

- `src/HypedFuzz.h` — component netlist, nonlinearity, solver modes, nominal controls.
- `src/HypedFuzzRealtime.h` — `hyped::Realtime` audio engine and advanced setters.
- `src/HypedFuzzPerformance.h` — optional high-value component macros.
- `src/HypedFuzzOpAmpPerformance.h` — optional slew, bandwidth, headroom, recovery and jitter macros.

All four are from the tested v8 source, without DSP edits in this handoff.

## Minimal API usage

```cpp
#include "HypedFuzzRealtime.h"
#include "HypedFuzzPerformance.h"
#include "HypedFuzzOpAmpPerformance.h"

hyped::Realtime fuzz(48000.0, 2);       // construct off audio thread
hyped::Controls c;
c.mode   = hyped::Mode::Full;
c.gain   = 0.72f;
c.bass   = 0.37f;
c.treble = 0.68f;
c.level  = 0.8f;
fuzz.setControls(c);                    // schedule outside concurrent rendering
fuzz.setInputVoltsPerUnit(0.25);        // interface calibration is host-specific

hyped::PerformanceControls macros(fuzz);
macros.octaveCharacter(0.5f);          // nominal R14
macros.fullBite(0.0f);                 // nominal C11

hyped::OpAmpPerformance amps(fuzz);
// optional, aggressive experiment: on Full gain amplifier only
amps.slewCrush(hyped::AmpStage::FullGain, 0.25f);

// In the audio callback (mono; no allocations):
fuzz.processBlock(input, output, frameCount);
```

The example above is API illustration, not a ready-to-drop replacement for RIG/01's host effect interface. A standalone compiling smoke example is in `examples/standalone_smoke.cpp`.

## Controls and optional sound-design parameters

| UI | Core API | Notes |
|---|---|---|
| Gain | `Controls::gain` | 0..1 normalized |
| Bass | `Controls::bass` | 0..1 normalized |
| Treble | `Controls::treble` | 0..1 normalized |
| Level | `Controls::level` | 0..1 normalized |
| Mode | `Controls::mode` | Full / Scooped / Boost |
| Octave Character | `PerformanceControls::octaveCharacter` | alters R14 loading; fuzz modes |
| Full Bite | `PerformanceControls::fullBite` | C11; Full mode |
| Scoop Edge | `PerformanceControls::scoopEdge` | C13; Scooped mode |
| Scoop Contour | `PerformanceControls::scoopContour` | C14; Scooped mode |
| Transistor Character | `PerformanceControls::transistorCharacter` | Q2 beta |
| Rail Voltage | `PerformanceControls::railVoltage` | changes static 9→7.5 V, not dynamic sag |
| Diode Asymmetry | `PerformanceControls::diodeAsymmetry` | D1 diode model |
| Fuzz Drift | `PerformanceControls::fuzzDrift` | seeds a slow R14 change; call **only** on configuration change |
| Slew Crush | `OpAmpPerformance::slewCrush(stage, x)` | stage-specific, optional |
| Bandwidth Limit | `OpAmpPerformance::bandwidthLimit(stage, x)` | stage-specific, optional |
| Headroom Crush | `OpAmpPerformance::headroomCrush(stage, x)` | stage-specific |
| Overload Recovery | `OpAmpPerformance::recovery(stage, x)` | experimental, uncalibrated |
| Imperfection | `OpAmpPerformance::imperfection(stage, ...)` | seeded slew/offset jitter; reconfig only |

Op-amp stages: `InputBuffer`, `BoostPreamp`, `FullGain`, `ScoopedGain`, `EqBassBuffer`, `EqMain`.

More low-level hooks on `Realtime`: `setResistorOhms`, `setCapacitanceFarads`, `setComponentScale`, `setComponentJitter`, `setTransistorParameters`, `setDiodeParameters`, `setOpAmpParameters`, `setSupplyVoltage`, `setReferenceRatio`, `setTemperatureCelsius`, `setOpAmpSlewRate`, `setOpAmpBandwidth`, `setOpAmpOutputSwing`, `setOpAmpRecovery`, `setOpAmpJitter`. See header for names/ranges.

## Important safety/engineering constraints

- Audio path uses mono `float` samples; host sample-rate changes require an appropriately reconfigured/reconstructed DSP object.
- The model maps full-scale digital input to analog equivalent volts (default `0.25 Vpeak` per unit); compare with real pickup levels before relying on gain calibration.
- `Realtime` may do substantial work in `processBlock`; a previous container benchmark showed ~45% of one 48 kHz/128-frame callback budget for one 2x instance on the benchmark host. Hardware must be profiled independently. Do not imply four instances are safe.
- Avoid sample-by-sample UI setters: string lookups and coefficient updates are control-rate activities, and changing component jitter every block resets randomness.
- Mode toggles, bypass and parameter jumps may require host-side de-click smoothing / crossfades; validate rather than assuming the simulator handles every discontinuity.
- `CompactNewton` and `setFastNonlinearMath` are optional performance paths. Auto/default is conservative.
- This package's MIT license covers the new software, not the PedalPCB schematic or other parties' assets.

## Acceptance criteria for integration

- Native and browser/WASM builds succeed with no additional external runtime dependencies.
- Full, Scooped and Boost yield finite non-silent output and respond to controls.
- Presets serialize/restore all host-exposed controls.
- No allocations or exceptions on the real-time audio callback.
- Automated mode-switch and parameter-automation tests pass; no new nonlinear solver failures.
- Profiling on actual RIG/01 hardware documents CPU at 1x, 2x and 4x; no oversampling downgrade without user-visible indication.
- New advanced controls are marked experimental; the user can reset to nominal circuit values.

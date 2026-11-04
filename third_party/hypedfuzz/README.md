# Hyped Fuzz DSP — Efficiency v8 (Nonlinear Math)

C++17 standalone circuit-derived PedalPCB Hyped Fuzz DSP emulator. Supports Full, Scooped and Boost, the schematic-derived resistors and capacitors, transistor/diode models, TL072/JRC4558 op-amp behavior, component jitter and the adjustable controls added in earlier versions.

**Important:** This is a schematic-derived *model*, not a measured physical pedal clone. The existing v7 `CompactNewton` solver is available unchanged. RIG/01 integration is NOT included.

## What's new

1. `boundedExp()` avoids invoking `std::exp()` when its argument is outside the **existing** -40 ... +35 saturation clamps. Returns the equivalent clamped constant, preserving the earlier mathematical model up to floating-point rounding. This optimization is always enabled.
2. Optional `setFastNonlinearMath(true)` replaces `std::tanh(x)` only when `|x| <= 1.5` with a 7/7 Padé rational approximation. Outside this interval, the original `std::tanh()` is used; the existing ±20 hard-rail guard remains in place. The deviation is < 3.4e-10 for `tanh` in the approximation interval, tested on a dense numerical sweep. This is a numerical approximation (not a new hardware parameter) and is **opt-in**. Default is OFF.
3. Added regression tests covering fast-vs-exact waveforms, a no-allocation fast-math callback, a repeatable nonlinear benchmark, an op-amp stress matrix and a serial multi-instance processing load test.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Usage

```cpp
#include "HypedFuzzRealtime.h"

hyped::Realtime fuzz(48000.0, 2);
fuzz.setLinearSolver(hyped::LinearSolver::CompactNewton);
fuzz.setFastNonlinearMath(true); // optional, exact path is default

hyped::Controls c;
c.mode = hyped::Mode::Full;
c.gain = 0.72f;
c.bass = 0.37f;
c.treble = 0.68f;
c.level = 0.8f;
fuzz.setControls(c);
fuzz.processBlock(input, output, frames);
```

Do not call parameter setters concurrently with the audio thread. Fast-math uses no allocation in the tested callback path. Retain `LinearSolver::Auto` for maximum confidence; `CompactNewton` is optional and falls back if it fails to converge, or when passive-component jitter makes cached reduction expensive.

## Test results

- **17/17 CTest tests pass** under Release and a separate assertions-enabled build.
- **27 exact-vs-fast comparisons** (1×, 2×, 4×; three modes; ordinary, op-amp modified and passive-jitter/modulation variants with a mode switch during rendering) show maximum float sample difference 0 for those finite-duration tests, zero failed solves and zero mismatched fallback counts.
- **648 cases** in fast-math op-amp stress matrix: 216 each at 1×, 2×, 4×; **zero solver failures**.
- **Three alternating timing trials per configuration**, 6,000 frames each, 18 scenarios. The median v7-to-v8-fast speedup is **1.053×** (range 0.969×–1.143×; 15/18 faster). Median fast-to-exact v8 speedup **1.031×**. Exact-only exponential-guard change had ~1.003× median versus v7, effectively negligible. These are local container benchmarks with CPU noise, **not a guaranteed gain**.
- Serial multi-instance 128-frame block benchmark at 48 kHz: one v8 fast-math effect at 2× consumed ~45% of one callback budget on the test host; one at 4× consumed ~81%; two at 2× consumed ~93%; four at 2× consumed >200%. This benchmark uses multiple Hyped Fuzz instances, NOT actual RIG/01 effects or its older iMac.

## Files

- `src/HypedFuzz.h`: detailed circuit model and both nonlinear evaluation paths.
- `src/HypedFuzzRealtime.h`: three-stage real-time facade and new setter.
- `tests/nonlinear_math_regression.cpp`: exact-versus-fast waveform comparisons.
- `tests/nonlinear_math_benchmark.cpp`: single-solver timed benchmark.
- `tests/solver_matrix_math.cpp`: reproducible 216-case solver stress harness.
- `tests/nonlinear_allocations.cpp`: no-heap-allocation check.
- `tests/multieffect_load.cpp`: serial instance stress / callback budget.
- `results/math_benchmark_raw.csv`, `results/math_benchmark_summary.csv`: measured timings.
- `results/fast_opamp_{1,2,4}x.csv`: convergence sweeps.
- `results/multieffect_load.txt`: callback budget comparisons.
- `docs/EFFICIENCY_V8.md`: analysis and fidelity limitations.

### Engineering conclusion

A few-percent speedup in `exp`/`tanh` doesn't address the main cost: the nonlinear Newton solve still often requires several evaluations and Jacobian factorizations per oversampled frame. The next materially different architecture should be a validated reduced-order surrogate / multi-rate stage, with the current MNA model retained as the oracle for comparisons. Do not automatically disable oversampling under CPU load without telling the host, because it changes aliasing and sound.

# Efficiency v8 — nonlinear function micro-optimizations

## Reference

The source is based on the v7 compact-Newton model. The 7 / 14 / 9 nonlinear-boundary dimensions and the MNA equations are unchanged; v8 only changes how transcendental functions are evaluated. This is an engineering-comparison release, not a measured clone of a physical Boss/PedalPCB circuit.

## Changes

**Clamped exponential:** `exp(clamp(x, -40, 35))` now immediately returns precomputed values outside the range (including equality). For intermediate inputs, it uses `std::exp(x)` as before. It preserves the existing current and Jacobian equations, subject only to sub-ULP platform-specific constants at the clamp endpoints. Enabled in both dense and compact solvers.

**Optional fast op-amp hyperbolic tangent:** with `setFastNonlinearMath(true)`, when `|x|<=1.5`:

```
tanh(x) ~= x (135135 + 17325 x^2 + 378 x^4 + x^6)
              / (135135 + 62370 x^2 + 3150 x^4 + 28 x^6)
```

The approximation has maximum absolute deviation about 3.36e-10 for tanh(x) over that interval, verified by dense numerical sampling. Outside the interval, the original `std::tanh(x)` remains. The approximate transfer is used in the existing op-amp equations in both dense and compact evaluation. Its modeled Jacobian retains `1 - transfer*transfer`; mathematically this is the derivative of tanh, rather than the exact derivative of the rational approximation, so v8 marks the setting experimental and opt-in. The original hard saturation at |x|>=20 remains.

## Verification

17/17 tests passed under both Release and an assertions-enabled build, including the added real-time no-allocation test for fast nonlinear math.

Exact-versus-fast waveform tests: 27 runs (three modes × three oversampling rates × three scenario presets) at 3,072 frames each, including a mid-signal mode/control switch. The maximum absolute output float difference was 0 for these runs. The output level is in physical-modeled volts mapped to normalized floating point, so this isn't a proof of numerical identity at every internal state, input, or sample rate.

Op-amp stress matrix: 216 stage/mode/parameter combinations × 1×/2×/4× = 648; 1,200 frames each; 0 failing solves with the optional fast math path. CSV files are in `results/fast_opamp_1x.csv`, `fast_opamp_2x.csv`, and `fast_opamp_4x.csv`.

## Benchmark design

- 48 kHz, 2×/4× oversampling, 6,000 precomputed input frames per trial.
- Three sequentially interleaved trials per variant per condition; medians reported.
- Compare actual v7 source build, v8 exact, and v8 optional fast-math.
- 18 scenarios: Full, Scooped and Boost × ordinary, op-amp modified, passive-component jitter × 2×/4×.
- Host: AMD EPYC 9V74 Linux container, compiled -O3; on shared infrastructure, timings are inherently variable.

Results: v8 fast vs v7 median 1.053× (15/18 faster, minimum 0.969×, maximum 1.143×); v8 fast vs v8 exact median 1.031× (15/18 faster). Exact clamp shortcut alone yielded median 1.003× relative to v7 and is effectively neutral. Do not extrapolate these modest improvements to the user's older iMac without measuring them there.

See `results/math_benchmark_summary.csv` for each mode and `results/math_benchmark_raw.csv` for individual trials.

## Serial chain / callback-budget trial

128-frame blocks at 48 kHz, giving a 2.667 ms deadline; 250 blocks per test; one, two and four serial Hyped Fuzz instances (not other RIG/01 effect types). On this host, v8 fast used approximately 45% of the average single audio-thread deadline with one 2× instance, 93% with two 2× instances, and 81% with one 4× instance. With four 2× instances, average processing exceeded the budget by >2×. The p95 and worst observed blocks can substantially exceed the average, and no hard real-time deadline is guaranteed.

## Recommendation

Keep `fastNonlinearMath=false` for numerical-reference renders and calibrated hardware comparison. Permit `true` as a selectable efficiency knob in RIG/01, after validation with users' own audio material. At present 2× oversampling is significantly more workable than 4×. For a substantial future improvement, replace or hybridize the repeated Newton solutions with a tested reduced-order circuit surrogate or selectively precomputed nonlinear port, rather than layering more transcendental approximations. Preserve v7/v8 dense and compact solvers as correctness references.

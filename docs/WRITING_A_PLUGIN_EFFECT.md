# Writing an effect for RIG/01 (a brief for another chat)

You are writing a new **effect** (or **modulation source**) for RIG/01, a guitar multi-effect. It runs
in a browser page and in a native host (`rig-host`, Windows and Linux). The effect ships as a
**plugin**: one `.rigfx` file that the player drops into the rig. The rig is not rebuilt.

**What you deliver** is a folder of two or three small files. The owner builds it with one command.
Write C++17 against the API below. Everything you need is in this document.

```
plugins/<id>/
  <id>.h         the effect: knob table, description, one struct (REQUIRED)
  plugin.json    kind, category, tags, author, version (REQUIRED)
  screen.js      a custom face drawing for the 3D stage (optional)
```

The owner then runs `python3 tools/build_plugin.py plugins/<id>`, which:
- compiles the header three times with zig: Windows CLAP, Linux CLAP and WebAssembly (WebCLAP);
- reads the description back from the build;
- writes `dist/plugins/<id>.rigfx`.

You don't write any CLAP or plugin code: `sdk/rigfx.h` wraps your struct automatically.

---

## How to work

These are the repo's working rules (`CLAUDE.md`, after
[andrej-karpathy-skills](https://github.com/forrestchang/andrej-karpathy-skills), MIT):

1. **Think before coding.** State your assumptions. If the effect's description could mean two
   things, say which one you built. Ask only when you can't build it correctly without an answer.
2. **Simplicity first.** Build exactly the knobs and behaviour asked for. Nothing speculative, no
   options nobody requested. If 200 lines could be 50, rewrite it.
3. **Surgical changes.** When revising a plugin, change only what the revision needs. Never rename a
   knob symbol or the id (presets store them).
4. **Goal-driven.** Turn each claim into a measurement (section 6). Then loop until the numbers show
   it, and report them.

## 1. The contract: an effect

Your header includes `"module.h"` (the build puts `engine/core/` on the include path). It defines one
knob table, one `ModuleDesc` and one `struct <Name> : Module`, all inside `namespace rig`.

```cpp
// plugins/trem/trem.h - TREMOLO: volume swells from sine to square. DEPTH 0-100%,
// RATE 0.5-16 Hz, SHAPE sine (0) to square (1). Default 50% at 3 Hz, sine.
#pragma once
#include "module.h"

namespace rig {

//                 symbol   label    min    max    default unit  curve       decimals sign
inline constexpr ParamDesc TREM_PARAMS[] = {
  {"depth", "DEPTH",  0.f,  100.f, 0.5f,  "%",  Curve::Lin, 0, false},
  {"rate",  "RATE",   0.5f, 16.f,  0.4f,  "Hz", Curve::Log, 1, false},
  {"shape", "SHAPE",  0.f,  1.f,   0.f,   "",   Curve::Lin, 2, false},
};
//                                       id      face title  subtitle          knobs            meters
inline constexpr ModuleDesc TREM_DESC = {"trem", "TREMOLO", "sine to square", 3, TREM_PARAMS, 1, {"gain", nullptr}};

struct Trem : Module {
  // All state in members: the chain can hold several of these, and nothing in process() may allocate.
  float ph = 0, gain = 1; OnePole depthS, rateS;
  const ModuleDesc& desc() const override { return TREM_DESC; }
  void init(float s) override { sr = s; depthS.setMs(sr, 20); rateS.setMs(sr, 20); reset(); }
  void reset() override { ph = 0; gain = 1; depthS.y = 0.5f; rateS.y = 3.f; }
  // e[i] = knob i after modulation (0..1), for this block; map(i, e[i]) gives real units.
  void process(const float* inL, const float* inR, float* oL, float* oR, int n,
               const float* e, const Context&) override {
    const float depthT = map(0, e[0]) * 0.01f, rateT = map(1, e[1]), sh = e[2];
    for (int i = 0; i < n; i++) {
      const float depth = depthS.next(depthT), hz = rateS.next(rateT);    // smoothed: a jumping knob never clicks
      ph += hz / sr; if (ph >= 1) ph -= 1;
      const float s = sinf(2 * PI_F * ph), lfo = (1 - sh) * s + sh * (s > 0 ? 1.f : -1.f);
      gain = 1.f - depth * 0.5f * (1.f + lfo);
      oL[i] = inL[i] * gain; oR[i] = inR[i] * gain;
    }
  }
  void meters(float* out) const override { out[0] = gain; }        // up to 2 display values, named in the desc
};

} // namespace rig
```

and its `plugin.json`:

```json
{
  "struct": "rig::Trem",
  "kind": "effect",
  "category": "modulation",
  "tags": ["tremolo", "volume", "lfo"],
  "author": "KC",
  "version": "1.0.0"
}
```

### The API (verbatim from `engine/core/module.h` and `param.h`)

```cpp
constexpr int MAXPAR = 8;          // knobs per module or source
constexpr int MAXMODMETERS = 2;    // display values a module can report
enum class Curve { Lin, Log };
struct ParamStop { float v; const char* name; };   // a named stop, in real units

struct ParamDesc {
  const char* symbol;              // protocol/preset name: [a-z][a-z0-9_]*, up to 16 chars, unique in the effect
  const char* label;               // knob label in the UI
  float min, max;                  // real-world range (max > min; Log needs min > 0)
  float def;                       // default, NORMALISED 0..1
  const char* unit;                // "", "dB", "ms", "s", "Hz", "%", "x", "bpm", "beats", "per beat", "st", "ct", "deg"
  Curve curve;                     // Lin, or Log
  int decimals;                    // 0..4 digits shown
  bool sign;                       // show a leading + for positive values
  const ParamStop* stops = nullptr; // optional named stops, ascending, inside min..max
  int nstops = 0;
  float map(float v) const { return curve == Curve::Log ? min * powf(max / min, v) : min + (max - min) * v; }
};

struct ModuleDesc {
  const char* id;                  // short stable id: "trem"  ([a-z][a-z0-9_]*, keep it <= 12 chars)
  const char* name;                // face title: "TREMOLO"
  const char* kind;                // subtitle: "sine to square"
  int nparams;
  const ParamDesc* params;
  int nmeters;
  const char* meters[MAXMODMETERS];  // names of values reported by meters()
  // (an AssetSlot field follows; plugins leave it empty, see "Limits")
};

struct Context { float sr = 48000; float bpm = 120; };   // per block; bpm follows tap tempo

struct Module {
  float sr = 48000;
  virtual const ModuleDesc& desc() const = 0;
  virtual void init(float s) = 0;     // sample rate; set up smoothers, clear state
  virtual void reset() = 0;           // back to silence (clear delay lines, filters, phases)
  // e[] = effective normalised knobs (0..1, already modulated) for this block. n <= 128.
  virtual void process(const float* inL, const float* inR, float* oL, float* oR, int n,
                       const float* e, const Context& ctx) = 0;
  virtual void meters(float* out) const {}          // optional, fills nmeters values
  float map(int i, float v) const { return desc().params[i].map(v); }   // knob i in real units
};
```

### Rules (the build and the conformance test enforce them)

1. **No allocation, locks or I/O in `process()`.**
   - No `new`, `std::vector` growth, `std::string` or `printf`.
   - Size every buffer as a fixed member array, e.g. `DelayLine<65536>` or `float buf[48000]`.
   - A test counts heap calls during `process()` and must see 0.
2. **All state lives in members, never in `static` variables.** The chain can hold two instances of
   your effect (`trem`, `trem2`), and they must not affect each other.
3. **Smooth every knob that would click** when it jumps. Use `OnePole` at 20–30 ms per sample, or a
   per-16-sample smoother. Modulation can move any knob every block (128 samples, about 2.7 ms).
4. **Output stays finite and reasonable on any input and any knob setting.** The conformance test:
   - feeds full-scale noise, quiet noise and silence;
   - tries knobs at their defaults, all at 0, all at 1, and random settings;
   - fails on NaN or Inf, or output above +36 dBFS.

   In feedback paths, apply `undenorm()` and keep loop gain below 1. Clamp anything that could run away.
5. **`process()` is stereo in and out**, with `n` ≤ 128 samples per call. The input may be mono
   duplicated to both sides. Don't assume the input and output buffers are different: read
   `inL[i]` and `inR[i]` before writing `oL[i]` and `oR[i]`.
6. **Knob defaults are normalised.**
   - Centre the range on the default where possible.
   - On a Log knob, a default of √(min × max) sits at 0.5.
   - In `reset()`, set smoothers to the default's real value so nothing sweeps at start-up.
7. **Limits:** up to **8 knobs** and **2 meters**. The first four knobs go on the front of the
   card and the rest on its back, so list the most-used knobs first. Meters are display values, named in the desc and
   read by the face screen. Typical meters: the gain now, the delay time in ms, a gate's state.
8. **Pick an id the rig doesn't already have.** The built-in effect ids are:
   - amp, hyped, gate, cab, holo, tess
   - bax, dyneq, geq, peq, prog, stack, tilt
   - comb, diode, formant, ladder, ota, sallen, svf
   - polyv

   The built-in source ids are:
   - lfo, env, adsr, orbit, heart, ctrl, geo, seq

   The example plugins already use `trem` and `rand`.
9. **The header file is named after the folder** (`plugins/<id>/<id>.h`). The `struct` in
   `plugin.json` is the fully qualified name, `rig::<Name>`.
10. **Write a header comment:** one line saying what it does, then its knobs and defaults, as in the
    example.

### Categories (`plugin.json` → `category`)

Effects go in one of these, which are the tabs of the Add an effect panel:

| Category | What goes there |
|---|---|
| `dynamics` | gates, compressors, limiters |
| `drive` | overdrive, distortion, fuzz |
| `cab` | speaker cabinets |
| `filter` | EQs, wahs, filters |
| `synth` | classic synth filters |
| `voice` | chords played by other instruments |
| `modulation` | chorus, flanger, phaser, tremolo, vibrato |
| `pitch` | octaves, harmonies, shifting |
| `delay` | echoes and repeats |
| `reverb` | rooms, halls, plates, spaces |
| `utility` | volume, splits, tools |
| `experimental` | anything else |

Anything not on this list lands in Experimental. Tags are free-form, lower case, and a few per effect.
They show as filter chips in the add panel.

---

## 2. Helpers you can use (`dsp.h` is included by `module.h`)

All of these are allocation-free and safe in `process()`.

```cpp
constexpr int MAXBLOCK = 128; constexpr float PI_F;
float clampf(float x, float a, float b);
float db2lin(float dB);
float undenorm(float x);                              // flush tiny values to 0 (use in feedback/state)

struct OnePole { float y, a; void setMs(float sr, float ms); float next(float target); };  // knob smoother
struct Biquad  { void lp(sr,f,q); void hp(sr,f,q); void peak(sr,f,q,dB); float tick(float x); void reset(); };
struct DCBlock { float tick(float x); void reset(); };
template <int N> struct DelayLine {                   // N = power of two; ~N*4 bytes, put it in a member
  void clear(); void push(float x); float tap(float samplesAgo) const;   // linear-interp, d >= 1
};
struct Allpass { void init(int len, float g); void clear(); float tick(float x); };       // len < 4096
```

Include these as needed (same folder):
- **`eq.h`**:
  - `Svf`, a stereo state-variable filter: `set(Svf::LP|HP|BP|BELL|LSHELF|HSHELF, sr, f, q, dB)`,
    `tick(ch, x)`. It stays clean while modulated.
  - `Shelf1` (first-order shelves and tilt) and `Iir3`.
  - `StepSmooth`, which retunes every 16 samples.
- **`vaf.h`**:
  - `VaLoop<N>`, a zero-delay-feedback analog filter loop.
  - `vaSat()`, a soft saturator.
  - `Os2`, 2x oversampling.
  - `EnvAmount`, `VaNoise`, `vaG()`.

  This is the place for resonant filters and saturating stages.

A large delay buffer is fine as a member (e.g. `DelayLine<131072>` holds 2.7 s at 48 kHz in 512 KB).
Keep the total per instance to a few MB.

---

## 3. A modulation source instead (optional)

A source makes one value in −1..1 per block. It moves the knobs it's patched to. Include
`"source.h"`. In `plugin.json`, use `"kind": "source"` and one of these categories: `time`, `playing`,
`chaos`, `outside`.

```cpp
//                 symbol   label    min    max     default unit        curve       decimals sign
inline constexpr ParamDesc RAND_PARAMS[] = {
  {"rate", "RATE", 0.5f, 8.f,   0.5f, "per beat", Curve::Log, 1, false},   // 2 per beat
  {"slew", "SLEW", 0.f,  500.f, 0.f,  "ms",       Curve::Lin, 0, false},
};
//                                       id      orb label  one-line description  ext      meters                  knobs
inline constexpr SourceDesc RAND_DESC = {"rand", "RANDOM", "sample and hold", nullptr, 1, {"steps", nullptr}, 2, RAND_PARAMS};

struct RandomSH : Source {
  float ph = 0, target = 0, value = 0; unsigned seed = 0x9E3779B9u; int steps = 0;
  const SourceDesc& desc() const override { return RAND_DESC; }
  void init(float s) override { sr = s; }
  float update(const SourceContext& c) override {         // once per block; return -1..1, always finite
    ph += c.dt * c.bpm / 60.f * knob(c, 0);                // knob(c, i) = real value, after cross-modulation
    if (ph >= 1.f) { ph -= floorf(ph); seed = seed * 1664525u + 1013904223u; target = (seed >> 8) / 8388608.f - 1.f; steps++; }
    const float slew = knob(c, 1) / 1000.f;
    value = slew <= 0.f ? target : value + (1.f - expf(-c.dt / slew)) * (target - value);
    return clampf(value, -1.f, 1.f);
  }
  void meters(float* out) const override { out[0] = (float)steps; }
};
```

`SourceContext` gives you:
- `in`: the dry guitar for this block;
- `n`;
- `sr`;
- `dt`: the block length in seconds;
- `bpm`;
- `p`: the normalised knobs.

The source id must be at most 12 characters.

The same rules as effects apply: no allocation, state in members, finite output. Also, a jumping knob
must not make the output jump on its own, so phase-accumulate rates.

Source plugins **can't** yet use settings, tables, taps (per-patch phase and division), `ext()` input
or `view()`. Leave those fields out.

---

## 4. A face screen (optional `screen.js`)

Without a screen, the stage face shows one bar per knob. To draw something specific, use this:

```js
// screen.js - what it shows, in one line.
screen('trem', (c, b, m, h) => {
  // c: canvas 2D context; b: {x, y, w, h} box; m.id: this instance's id
  // h.eff(i): live knob i (0..1, after modulation); h.param(i).real(v): real units
  // h.meter('gain', fallback): a meter by name; h.T: seconds (for animation)
  // h.col.*: theme colours. NEVER write a literal colour: use h.col.text, h.col.accent...
  const g = h.meter('gain', 1);
  c.fillRect(b.x + 20, b.y + b.h - 12 - g * (b.h - 40), b.w - 40, g * (b.h - 40));
  c.fillStyle = h.col.text; c.fillText((g * 100).toFixed(0) + '%', b.x + 20, b.y + 26);
});
```

Draw from knobs and meters only; don't re-run the DSP in JavaScript.

---

## 5. Limits for plugins (for now)

- **No file slot:** an effect plugin can't load a NAM capture, an IR or a sample. Anything it needs
  must be generated in `init()` or be a `constexpr` table.
- **Up to 8 knobs and 2 meters.** There are no sub-pages or hidden settings for effects. Fold choices
  into a knob with named `stops` (e.g. a MODE knob with stops `{0,"CLEAN"},{1,"DIRTY"}`) and round
  it in `process()`.
- **Everything must compile** for `wasm32-wasi` (`-msimd128`), `x86_64-windows` and `x86_64-linux`
  with `-Wall -Wextra -Werror`. Use standard C++17 and `<math.h>`. Don't use threads, files,
  platform headers or intrinsics.
- **Browser CPU:** the browser runs the WebAssembly copy at about 1.5x native CPU. Keep heavy loops
  modest, e.g. FFTs of a few thousand points at most per block.

---

## 6. Test it before handing it over

The owner's standard: build and run it, don't just read it. Measure what it claims, with numbers and
units.

The header compiles stand-alone against the four engine headers above (`param.h`, `dsp.h`, `module.h`
and `source.h`, all shown in full or in part here). If you don't have the repo, recreate them from
section 1 (the struct definitions are complete). Then test with a small driver:

```cpp
// test_myfx.cpp: g++ -std=c++17 -O2 -Wall -Wextra -Werror -I. test_myfx.cpp && ./a.out
#include "myfx.h"
#include <cstdio>
#include <vector>
#include <cmath>
using namespace rig;
int main() {
  const float SR = 48000; MyFx fx; fx.init(SR);
  float e[MAXPAR]; for (int k = 0; k < fx.desc().nparams; k++) e[k] = fx.desc().params[k].def;
  Context ctx; ctx.sr = SR;
  std::vector<float> in(2 * (int)SR), L(in.size()), R(in.size());
  for (size_t i = 0; i < in.size(); i++) in[i] = 0.3f * sinf(2 * PI_F * 110 * i / SR) * expf(-(float)(i % 24000) / 9000.f);  // plucked A2
  for (size_t i = 0; i < in.size(); i += 128) fx.process(&in[i], &in[i], &L[i], &R[i], 128, e, ctx);
  float pk = 0; bool finite = true; for (float v : L) { pk = fmaxf(pk, fabsf(v)); finite &= std::isfinite(v); }
  printf("peak %.3f, finite %d\n", pk, finite);
}
```

Check at least the following, and report the numbers:

- **Hard cases:**
  - every knob at 0, at 1, and random settings;
  - full-scale white noise, silence and DC;
  - knobs jumping every block, which must not click: measure the largest sample-to-sample step;
  - sample rates of 44.1 and 48 kHz.
- **That it does its job:**
  - a tremolo: its level swing at full depth, and its rate in Hz;
  - a delay: the echo time in ms and the decay per repeat;
  - a filter: its response at a few frequencies.
- **Two instances side by side:** run two copies with different settings to show they don't share
  state.
- **No allocation:** in a test build, override `operator new` to count calls during `process()`. The
  count must be 0.

Deliver the final `<id>.h`, `plugin.json` and optional `screen.js` with the debug code removed. Say
what you measured and what you only inferred.

---

## 7. What happens after you hand it over

1. KC puts the folder in `plugins/<id>/` and runs `python3 tools/build_plugin.py plugins/<id>`, which
   writes `dist/plugins/<id>.rigfx`.
2. KC drops the `.rigfx` on the page or into the library's `plugins/` folder. It appears in Add an
   effect under its category, with its tags. It can be patched from any source and saved in presets
   like a built-in.
3. `./build.sh` rebuilds every plugin folder in `plugins/` with `-Werror`. The native tests load
   the two examples; a new plugin gets its own test in `tests/native/test_plugins.cpp`, following
   the `trem` one there (load the bundle, run it, check its numbers, count heap calls).

To change a released plugin later:
- **Append** knobs, with defaults that keep the old sound.
- **Never rename** a knob `symbol` or the id: presets store them.
- **Bump `version`** in `plugin.json`.

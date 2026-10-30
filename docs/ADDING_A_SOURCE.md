# Adding a modulation source

A source turns anything (tempo, your picking, physics, a sensor) into one number from −1 to 1, once
per block (128 samples, about every 2.7 ms). Sources live in the **modulation rack**
(`engine/mod_rack.h`): what you write is a source *type*. The rack can hold several instances of it
(`lfo`, `lfo2`...), each with its own knobs, and any instance's knobs can be moved by another source
(cross-modulation). You get all of that, plus the panel, the matrix, presets, scenes and the text
protocol, from the description alone.

Three steps: one new file in the right folder, a test, and the docs table. There is no list to edit:
the folders are the registry (`tools/gen_registry.py` makes `engine/sources/registry.h` from them). The format below is
checked by `tests/native/test_conformance.cpp` for every registered type, so a mistake fails the
build with the type's name and what's wrong.

## Where sources live

```
engine/sources/
  categories.txt     the tabs of the "Add a source" panel, in order: folder | tab label | what goes here
  default_rack.txt   what a fresh rig's rack starts with, in order (source ids)
  time/lfo.h         one source per file, in its category's folder
  playing/env.h  playing/adsr.h
  chaos/orbit.h
  outside/heart.h  outside/ctrl.h
  registry.h         GENERATED from the folders (./build.sh runs the generator)
```

## 1. Write `engine/sources/<category>/<id>.h`

Start from [`templates/source_template.h`](templates/source_template.h), a working sample-and-hold
source (it was built and run through every test). The shape:

```cpp
// engine/sources/rand.h - RANDOM: one line saying what it does, its output and its defaults.
#pragma once
#include "../../core/source.h"
namespace rig {
//                 symbol   label    min    max     default unit        curve       decimals sign
inline constexpr ParamDesc RAND_PARAMS[] = {
  {"rate", "RATE", 0.5f, 8.f,   0.5f, "per beat", Curve::Log, 1, false},   // 2 per beat
  {"slew", "SLEW", 0.f,  500.f, 0.f,  "ms",       Curve::Lin, 0, false},   // jumps
};
inline constexpr SourceDesc RAND_DESC = {"rand", "RANDOM", "sample and hold", nullptr, 1, {"steps", nullptr}, 2, RAND_PARAMS};
struct RandomSH : Source {
  float ph = 0, target = 0, value = 0; unsigned seed = 0x9E3779B9u; int steps = 0;
  const SourceDesc& desc() const override { return RAND_DESC; }
  float update(const SourceContext& c) override { /* ... */ return value; }
  void meters(float* out) const override { out[0] = (float)steps; }
};
}
```

### The description (`SourceDesc`)

| Field | Rule (checked by test 90) |
|---|---|
| `id` | the type id: `[a-z][a-z0-9_]*`, up to 12 characters (instances add a number: `rand2`), unique, and not a module's id |
| `name` | orb label, 1–16 characters, capitals by convention: `"RANDOM"` |
| `sub` | one short line under the name: `"sample and hold"` |
| `ext` | `nullptr`, or what `ext <id> <value>` means (`"value -1..1"`), if it takes outside data |
| `nmeters`, `meters` | up to 2 display values, each named; `meters()` fills them |
| `nparams`, `params` | up to 8 knobs (`MAXPAR`) |
| `nsettings`, `settings` | optional, up to 24 settings (`MAXCFG`): described like knobs and saved in presets, but nothing patches them (a shape, a seed, a direction). Read with `setting(c, i)`; set with `src_cfg`. The panel shows the first four beside the knobs and folds the rest under **More settings**. A symbol can't be a knob's too |
| `rows`, `ncols`, `cols` | optional table: up to 32 rows of up to 6 columns (`MAXROWS`, `MAXCOLS`), each column described like a knob (per step, per vertex...). Read with `cell(c, row, col)`; set with `src_cell`; presets keep the cells that differ from the default |

### Knobs (`ParamDesc`, `engine/core/param.h`)

| Field | Rule |
|---|---|
| `symbol` | `[a-z][a-z0-9_]*`, unique in the source. It's the name in presets and the protocol: **never change it** once released (see [Expanding](EXPANDING.md)) |
| `label` | what the panel shows: `"RATE"` |
| `min`, `max` | the real range; `max > min`; a `Curve::Log` knob needs `min > 0` |
| `def` | the default, **normalised** 0..1. Centre the range on it: on a log curve, a default value of √(min × max) sits at 0.5 |
| `unit` | one of `"" dB ms s Hz % x bpm beats "per beat" st ct deg` |
| `decimals` | 0–4 digits in the readout; `sign` shows a leading `+` |
| `stops`, `nstops` | optional named stops in real units, ascending (`{0.5f, "1/8"}, {1.f, "1/4"}`). Turning the knob by hand lands on them and the readout shows the name; cross-modulation still moves it continuously. A whole-number knob with no unit (`""`, 0 decimals) is a selector: round it in `update()` (`lrintf`) and crossfade a change, since cross-modulation can switch it every block. See `time/lfo.h` |

### Behaviour (`update()`)

| Rule | Why, and what checks it |
|---|---|
| Return −1..1, always finite, for any knob setting and any input (silence, full-scale noise, DC) | it moves other knobs by ± depth; test 91 tries 6 knob settings × 5 inputs × 3 block sizes × 2 sample rates |
| All state in members; no `static` variables | two instances must not affect each other; test 92 |
| Nothing in `update()` allocates or locks | it runs on the audio thread; tests 85 and 92 count heap calls |
| Read knobs with `knob(c, i)` every block | cross-modulation can move any knob every block. Work coefficients out per block, cheaply (no tables rebuilt) |
| A jumping knob must not make the output jump on its own | phase-accumulate rates (`ph += dt × rate`), smooth what would click |
| Use the full −1..1 range; centre "nothing happening" sensibly | bipolar (LFO: −1..1 around 0) or unipolar (envelope: −1 at rest, +1 at full); say which in the header |
| `c.dt` is the block length in seconds; `c.bpm` the tempo; `c.in`/`c.n` the dry guitar | tempo-synced sources follow tap tempo for free |

### Drawing it in its panel (`view()`, `ui/sources/<id>.js`)

A source can add its own picture to its side panel: `ui/sources/<id>.js` registers `SRCVIEW.<id> = { html(s),
mount(s), frame(s), meters(s) }` (GEO PULSE's 3D view is the example). What it draws should come from the
engine, not be worked out again in JavaScript: implement `view(knobs, cfg, out, max)` (pure: it reads only
its arguments) and the page calls it through `rig_view()` on its own copy of the engine; anything that moves
comes from the meters, read in `meters(s)` on every meter packet (about 31 a second), never from a clock of the view's own.

### Per-destination phase and multiple (`taps`)

A source with a repeating shape (the LFO) can set `taps = true` at the end of its `SourceDesc` and
implement `cycle(n, ph)` (whole cycles done and the phase 0..1 after this block's `update()`) and
`at(ph, n)` (its output at phase `ph` of cycle `n`, with this block's settings; `n` picks random
values). Then every patch from it gets its own PHASE and DIV (`core/tap.h`): the rack and the chain read
it at `frac(cycles x DIV + PHASE/360)`, locked to the source's cycle, and glide when either changes.
A patch at 0 degrees, 1x reads the source's own value, so nothing changes for anyone who doesn't use
them. Test 91 checks `at()` stays in -1..1 and finite for every phase.

`ext(float)` receives outside values (a strap, a gamepad) on the control thread: store them, and
use them in `update()`. Sources that only need slow outside data can also live outside the engine
and just send `ext ctrl <value>`.

## 2. Run `./build.sh`

It makes the registry from the folders and stops with what to fix if the file is in a folder that
isn't in `categories.txt`, doesn't hold exactly one `struct <Name> : Source`, or its id isn't its file
name. The new source appears in its tab of the **Add a source** panel. It isn't added to a fresh rack
unless you add its id to `default_rack.txt` (at most 16), so the library can grow without the
default rig changing.

## 3. Test it: `tests/native/test_<id>.cpp`

Start from [`templates/test_source_template.cpp`](templates/test_source_template.cpp). The
conformance tests already cover the format and safety; your test shows the source **does its job,
with numbers**: rates in Hz, times in ms, counts. Helpers: `e.addSource("rand", nullptr)` gives a
slot, `e.rack.slot[k].base[p]` sets a knob (0..1), `e.rack.slot[k].val` is its output this block,
`e.rack.meters(k, m)` its meters, `riffSignal()` a guitar riff.

## 4. Document it

Add a row to the sources table in `docs/PROTOCOL.md` and the README's "Current effects and sources".
A skin can give the type a colour (`sources.rand` in `docs/SKINS.md`); otherwise it gets the next
fallback colour.

Run `./build.sh`. The new source appears in the "+ ADD" panel, can be added any number of times, has
its own panel, appears in the matrix as a row and its knobs as columns, can move and be moved by
every other source, and saves in presets.

## Changing an existing source

See [EXPANDING.md](EXPANDING.md): adding knobs and meters, renaming, and keeping old presets sounding
the same.

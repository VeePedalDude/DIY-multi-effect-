# Expanding an existing module or source

How to grow an effect module (`engine/modules/`) or a modulation source (`engine/sources/`) without
breaking anything: saved presets keep loading and sounding the same, hosts that use the C API keep
working, and the rest of the rig picks the change up by itself.

The short version: **append, default to the old behaviour, prove it bit-identical, rename only
through the rename table.**

## What you can change, and how

| Change | Do | Don't |
|---|---|---|
| Add a knob | Append it to the end of the `ParamDesc` list (up to 8). Pick its default so the module or source does exactly what it did before. | Insert it before existing knobs: the C API (`rig_param(m, p, v)`, `rig_src_param`) addresses knobs by position. |
| Widen or narrow a knob's range | Fine: presets store real units (ms, dB, Hz), so a saved 300 ms stays 300 ms. Keep every value people might have saved inside the new range (values outside are clamped, with a note). | Change the curve or range just to move the default: change `def` instead. |
| Change a default | Fine for new presets. Old presets store every knob, so they don't change. Scenes and fresh rigs do. Say so in the PR. | |
| Rename a knob symbol | Add the old name to `PRESET_RENAMES` in `ui/js/72-preset-format.js`: module knobs in `params: { 'amp.oldsym': 'amp.newsym' }` (by type), source knobs in `sourceKnobs: { 'lfo.oldsym': 'newsym' }` (by type). Every instance is covered. | Rename without the table: old presets lose that setting, with a note. |
| Rename a module or source type id | `PRESET_RENAMES.modules: { old: 'new' }` or `sourceTypes: { old: 'new' }`. Instance ids in presets keep working (an instance id is just a name). | Reuse an old id for something different. |
| Add a display meter | Up to 2 per module or source; name it in the desc, fill it in `meters()`. Face screens read meters by name (`h.meter('name')`). | |
| Take outside data (sources) | Set `ext` in the desc to what the value means; implement `ext(float)`. | Block or allocate in `ext()`. |
| Add a file slot (modules) | See ADDING_A_MODULE.md, "a file slot". | |
| Change what the sound does at the same settings | Only on purpose, with before/after numbers in the PR (CLAUDE.md: report measured numbers before and after a DSP change). | |

The preset file layout itself only changes when the shape of the file changes (new sections). Then
bump `PRESET_VERSION` and add a migration in `PRESET_MIGRATIONS` that turns the previous version
into the new one, plus a hand-written golden file in `tests/fixtures/` that must load forever.

## Proving "the default sounds the same"

The rule for every expansion: at the new knob's default, the output is **bit-identical** to before.
That's how the source knobs and the modulation rack were added. Two ways to prove it:

1. **In a test:** render the riff through two engines, one with the old behaviour forced and one at
   defaults, and compare with `==` (see test 80 in `test_rack.cpp`, or test 73 in `test_tuner.cpp`).
2. **Against the build before your change:** copy `engine/` from the previous commit into a scratch
   folder, build a small renderer against each (`#include "engine/api/rig_api.cpp"`, render a
   scenario, write the output), and `cmp` the two files. Report CPU for both while you're there.

Tip for bit-exactness: if the old code used a constant like `0.3f` (300 ms), write the new code as
`knob(c, i) / 1000.f` rather than `knob(c, i) * 0.001f`. `x / 1000.f` and `x * 0.001f` round
differently.

## What updates itself

Once the description changes, these follow with no other edits: the stage card or orb, the panel,
the matrix (rows and columns), patching from and to it, cross-modulation, presets and scenes (in
real units), the text protocol (`param_set`, `src_set`, `src_mod`), the worklet, and the
conformance tests (format, range, finiteness, allocation, independence).

Still yours to do:
- **Tests:** a test showing the new knob does its job, with numbers. `./build.sh` must stay at zero
  failures with `-Wall -Wextra -Werror`.
- **Protocol docs:** the symbols tables in `docs/PROTOCOL.md`, and the README tables.
- **Face screen (modules):** draw the new knob's effect in the face screen if it should show.
- **Download:** rebuild `dist/rig-test.html` and commit it with the change.

## Checklist

- [ ] New knobs appended, defaults reproduce the old behaviour (bit-identical render shown)
- [ ] No symbol or id renamed without a `PRESET_RENAMES` entry
- [ ] Nothing new allocates in `process()` / `update()` (tests 26, 85, 92, 93 stay at 0)
- [ ] A test for what the change does, with measured numbers; before/after CPU
- [ ] PROTOCOL.md and README tables updated; `dist/rig-test.html` rebuilt

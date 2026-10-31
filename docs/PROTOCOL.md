# Engine protocol

The commands below are parsed by the engine itself (`rig_exec` in `engine/api/rig_api.cpp`): the
browser's worklet and `hosts/rig-host` both pass lines to it, so every host speaks the same protocol.
rig-host adds two of its own: `host_input riff|interface` (the built-in test riff, or the input) and
`latency_test` (five clicks out and back through a loopback cable; answered with a
`{type:'latency', ok, samples, ms, runs}` message). Over rig-host's WebSocket (`/ws`), the page sends
`{type:'cmd', id, line}`, `{type:'asset', inst, ref, bytes}` followed by the file as one binary
message, and `{type:'asset_clear', inst}`; the host sends `host`, `describe`, `rack`, `chain`, `resp`,
`asset`, `meters` and `latency` messages (`ui/js/31-host.js` reads them).

One text line per command. The browser sends them to the worklet; the Linux host will
accept the same lines over a WebSocket. Modeled on mod-host.

| Command | Meaning |
|---|---|
| `param_set <inst> <symbol> <0..1>` | set a knob (normalised) |
| `bypass <inst> <0\|1>` | bypass with a 10 ms crossfade |
| `mod_connect <inst> <symbol> <source> <depth> [phase] [div]` | patch a source; the knob swings ±depth. From a source that taps (the LFO), `phase` (degrees, default 0) and `div` (`1/4 1/3 1/2 2/3 1 3/2 2 3 4 8`, or a number; default 1) give this patch its own offset and multiple of the source's cycle: `mod_connect holo feedback lfo 0.3 90 3/2`. Other sources ignore them. Leaving them out means 0 and 1 |
| `mod_disconnect <inst> <symbol>` | unpatch |
| `set_bpm <30..300>` | tempo for synced effects and the LFO |
| `master <-60..6>` | master gain in dB |
| `src_set <source> <symbol> <0..1>` | set a modulation source's knob (`src_set lfo rate 0.25`) |
| `src_add <type> [id]` | add a source to the modulation rack (`src_add lfo lfo2`). Without an id the engine picks the next free one (`lfo2`, `lfo3`...) |
| `src_remove <source>` | take a source out of the rack, with every patch to and from it |
| `src_mod <source> <symbol> <from source> <depth> [phase] [div]` | cross-modulation: `from` moves this source's knob ±depth (`src_mod lfo rate env 0.3`); a source may move its own knobs. `phase` and `div` as for `mod_connect` |
| `src_unmod <source> <symbol>` | unpatch a source's knob |
| `src_cfg <source> <symbol> <0..1>` | set a source's setting (normalised): like a knob, but not a patch target (`src_cfg geo shape 0.125`: STAR) |
| `src_cell <source> <row> <symbol> <0..1>` | set one cell of a source's table (`src_cell geo 3 on 0`: vertex 4 off) |
| `ext <source> <value>` | feed an outside value to a source (`ext heart 72`, `ext ctrl -0.4`) |
| `tuner <0\|1> [mute 0\|1]` | strobe tuner on the dry input; with mute (the default) the output fades out while it's on |
| `tuner_ref <400..480>` | the tuner's A4 reference in Hz (default 440) |
| `input_channel <0\|1>` | browser only: which interface input to use |
| `asset_load <inst> <kind>/<file>` | load a library file into a module's slot (`asset_load cab ir/V30 SM57.wav`) |
| `asset_clear <inst>` | back to the module's built-in (e.g. the built-in cab) |
| `fx_add <type> [id] [position]` | add an effect to the chain (`fx_add holo holo2 0`: a second HOLOGRAM, first in the chain). Without an id the engine picks the next free one; without a position it goes at the end. It fades in over 10 ms |
| `fx_remove <inst>` | take an effect out of the chain: it fades out over 10 ms, then is freed; patches to it go with it |
| `fx_move <inst> <position>` | move an effect (0 = first). The sound dips for 10 ms either side of the swap, so nothing clicks or gets recorded into a delay |

`<inst>` is the module's id (`holo`, `holo2`) or its position in the chain (`3`; positions change
when the chain does, so hosts should use ids). Module ids follow the same rules as source ids, and
the two never clash. A fresh engine's chain is one of each type in registry order, ids = type ids.
`<source>` is the id of an instance in
the modulation rack (`lfo`, `lfo2`, or one chosen with `src_add`: `[a-z][a-z0-9_]*`, up to 15
characters, never a module's id). A fresh engine's rack holds one of each type, ids = type ids.
Symbols and types come from the engine's description; the current set:

| fresh position | type (= id) | symbols |
|---|---|---|
| 0 | gate | thresh, release |
| 1 | amp | input, output |
| 2 | cab | locut, mix |
| 3 | holo | feedback, halo |
| 4 | tess | size, fold |

Types in the library but not a fresh chain (add them with `fx_add`):

| type | symbols |
|---|---|
| tilt | tilt, pivot, level |
| bax | bass, treble, bfreq, tfreq, level |
| geq | b100, b200, b400, b800, b1k6, b3k2, b6k4, level |
| peq | low, f1, g1, q1, f2, g2, q2, high |
| stack | bass, mid, treble, model, level |
| prog | lfreq, lboost, latten, hfreq, hboost, bw, hatten, drive |
| dyneq | freq, q, thresh, range, ratio, attack, release (meters: gain, level) |
| svf | cutoff, reso, mode, env, drive, level |
| comb | freq, feedback, damp, env, mix |
| formant | vowel, shift, reso, env, mix |
| ota | cutoff, reso, poles, env, drive, level |
| ladder | cutoff, reso, env, drive, comp, level |
| diode | cutoff, reso, env, drive, level |
| sallen | hpcut, hppeak, lpcut, lppeak, env, drive, level |
| polyv | model, effect, dry, attack, release, tone, mod, octave (meters: voices, notes) |

The synth filters' meters: `cutoff` (Hz, where it is now, after ENV) and `env` (0..1, the level ENV reads).
POLY VOICE's: `voices` (how many are sounding) and `notes`, its three loudest as MIDI note numbers packed
into one number, `(n1 + 1) + 129 (n2 + 1) + 129² (n3 + 1)` (0 for none). Its `model` has named stops
(TAPE STRINGS, TAPE FLUTE, TAPE CHOIR, SYNTH SAW, SYNTH PULSE, SYNTH BRASS) and `octave` is -1, 0 or +1.

Source types and their knobs (`src_set`, `src_mod`). A knob may have named stops (`stops` in the
description: `[[real value, "name"], ...]`): turning it by hand lands on them and its readout shows the
name; a whole-number knob without a unit (WAVE, SYNC, POLAR) is a selector the engine rounds.

| type | knobs |
|---|---|
| lfo | rate (beats per cycle, used with sync; stops on note values 1/8 … 32 bars), shape (sine → triangle → square: the MORPH wave), wave (MORPH, SINE, TRIANGLE, SQUARE, SAW UP, SAW DOWN, STEP RANDOM, SMOOTH RANDOM, EXP RISE), depth (0–100 %), steps (OFF, 2–32 per cycle: a staircase), hz (0.05–20 Hz, used when free), sync (FREE / SYNC), polar (BIPOLAR / UNI + / UNI −). Each patch from it can have its own phase and div. Meter: `hz` |
| env | attack, release, floor |
| adsr | attack, decay, sustain, release |
| orbit | speed (simulated time when free), orbit (CLASSIC: today's wandering figure-eight; or a known periodic solution with its own masses: FIGURE-8, BUTTERFLY I, BUTTERFLY III, MOTH I, GOGGLES, YIN-YANG, DRAGONFLY, BUMBLEBEE, YARN, HEAVY THIRD, LIGHT THIRD), out (DISTANCE, BODY X, SPEED, CLOSENESS), rate (beats per orbit when synced, note-value stops), sync (FREE / SYNC), chaos (0 % repeats the orbit exactly, more nudges each cycle, 100 % runs free). Meters: `period` (s), `cycles` |
| heart | rest (simulated bpm when no strap is connected) |
| ctrl | smooth, deadzone. Its value comes in as `ext <id> <-1..1>`; in the browser each instance can be bound to one control on a USB gamepad / joystick / flight stick or a MIDI input (LEARN in its panel), and preset files keep that binding as the source's `input` |
| seq | knobs: rate (step length, note values 1/32 – 4 bars), gate (% of the step), prob, swing, fill (EUCLID hits), shift (rotate the pattern, steps), slew (VALUE glide, ms). Settings: steps (1–32), pattern (GRID / EUCLID), dir (FORWARD, REVERSE, PING-PONG, RANDOM), resync (OFF, 1–16 bars), seed, offset (ms), out (GATE, TRIGGER, ENVELOPE, VELOCITY, VALUE, STEP), polar, retrig, attack, decay. Table, a row per step: on, vel, prob, ratchet (1–4), gate (% x GATE), value (−100..+100 %). `ext seq 0` stops, `1` plays on, `2` back to step 1. Meters: `step`, `fired` |
| geo | knobs: rate (beats per step, note-value stops), gate (% of each step), prob, curve (HYPERBOLIC: 0 = Euclidean), warp, timing (UNIFORM → ANGULAR → GEODESIC, a blend), phase (degrees of the cycle), depth (3D). Settings: shape (POLYGON, STAR, WARPED, HYPERBOLIC, TETRAHEDRON, CUBE, OCTAHEDRON, DODECAHEDRON, ICOSAHEDRON), verts (3–32), skip, rot, cycles (ONE / ALL when skip and verts share a factor), dir (FORWARD, REVERSE, PING-PONG, RANDOM), length (AUTO or beats), swing, offset (ms), seed, out (GATE, TRIGGER, ENVELOPE, VELOCITY, PHASE, VERTEX), polar, smooth, retrig, attack, decay, accent, tilt and trail (the 3D view only). Table, a row per vertex: on, prob, vel, accent, gate. `ext geo 0` stops, `1` plays on, `2` goes back to the start. Meters: `step`, `fired` |

In the browser the page reads the file from the library and sends it to the engine (a binary
message: a `.nam` file's text as-is, or an IR decoded to the `RIR1` header plus float samples,
see `engine/core/nam.h`); the engine log shows the same `asset_load` line. On the Linux host the
text line is the whole command, and the host reads the file from its library folder.

## The modulation rack

Up to 16 source instances (`maxSources` in the description), any mix of types. Each knob, on a module
or a source, takes one source with its own depth. Cross-modulation is worked out from every
source's value one block earlier (2.7 ms), so any patch is well defined: chains, loops (A moves B,
B moves A) and a source moving its own knob.

In the browser every change to the rack is answered with a `{ type: 'rack', rack: [{ id, type, slot }] }`
message: the meter block is laid out by slot. `rig_describe()` lists `sourceTypes` (every kind, with
its knobs and meters), `sources` (the fresh rack) and the meter offsets: `src` (one value per slot),
`srcm` (meters per slot), `seff` (each slot's knobs after cross-modulation).

## The effect chain

Up to 12 effect instances (`maxModules` in the description), any mix of types, in any order.
`rig_describe()` lists `moduleTypes` (every kind, with its knobs, meters and file slot) and `modules`
(the fresh chain). In the browser every change to the chain is answered with a
`{ type: 'chain', chain: [{ id, type, slot }] }` message, in chain order: the meter block is laid out
by slot (`eff`, `mod`).

Replies: `resp <status>`. 0 means OK; negative numbers follow mod-host:
−1 bad instance (or, for `asset_load`, a file the module refused; for `src_add` / `fx_add`, an unknown type),
−2 id taken or not allowed, −3 rack or chain full, −103 bad symbol, −207 value
out of range, −902 unknown command. `asset_load` replies also carry the engine's status line:
what loaded (`WaveNet · 48 kHz · levelled`, `stereo · 3.00 s`) or why it was refused.

Under the text protocol, hosts use the C API in `engine/api/rig_api.cpp`. Modules by slot (`rig_chain()`
lists the chain in order with each instance's slot: `{"max":12,"modules":[{"id","type","slot"}]}`;
a fresh chain uses slots 0.. in order). The chain: write `"<type> [id|-] [position]"` to `rig_text()`,
then `rig_fx_add()` returns the slot (or −1 unknown type, −2 full, −3 bad id); `rig_fx_remove(slot)`,
`rig_fx_move(slot, position)`. A removed module is freed by `rig_collect()` (or any other chain call)
once the audio thread has finished with it; the browser calls it about 30 times a second. Sources by rack slot: `rig_src_param(slot, p, v)`, `rig_ext(slot, v)`, `rig_mod(m, p, slot, depth)`;
the rack: write `"<type> [id]"` to `rig_text()`, then `rig_src_add()` returns the slot (or −1 unknown
type, −2 full, −3 bad id); `rig_src_remove(slot)`, `rig_src_mod(slot, p, fromSlot, depth)` (−1
unpatches), `rig_src_cfg(slot, i, v)` and `rig_src_cell(slot, row, col, v)` (settings and table cells,
normalised; the description lists each type's `settings` and `table: {rows, cols}`), `rig_view(type)` (for
drawing only, on the page's own copy of the engine: write knobs and settings to `rig_view_buf()`, read
what the type draws from `rig_view_buf() + 64`), `rig_mod_tap(m, p, degrees, div)` and `rig_src_tap(slot, p, degrees, div)` (a patch's
PHASE and DIV; `tapDivs` in the description lists the choices, the nearest is taken; a new source on
the knob starts again at 0 degrees, 1×), `rig_rack()` (JSON: what's in the rack and where). Adding and removing allocate: call
them between `rig_process` calls or from a control thread (a removed instance is freed once the
audio thread has moved on). For files:
`p = rig_asset_buf(m, bytes)` reserves room (call it before taking a view of WebAssembly memory,
since reserving can grow it), copy the payload to `p`, then `rig_asset_commit(m, bytes)`;
`rig_asset_status(m)` returns the status line, `rig_asset_clear(m)` goes back to built-in.

The tuner's readings are in the meter block at `meters.tun` (from the description; `tunLen` values,
`tunBands` strobe bands), in this order: on, input level (dBFS), frequency (Hz), note (MIDI number,
−1 for none), cents, target note's frequency, each band's phase (radians), each band's strength
(0..1), A4, mute, detector confidence. The bands compare the string with the target at 1×, 2× and
4× its pitch: each phase turns at harmonic × (string − target) Hz, forwards when sharp. C API:
`rig_tuner(on, mute)`, `rig_tuner_ref(a4)`. Meter packets in the browser also carry `t`, the audio
clock when they were made.

When the real rig moves to mod-host, the control hub translates
`mod_connect` into `cv_map` with min = knob − depth and max = knob + depth.

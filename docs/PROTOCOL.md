# Engine protocol

One text line per command. The browser sends them to the worklet; the Linux host will
accept the same lines over a WebSocket. Modeled on mod-host.

| Command | Meaning |
|---|---|
| `param_set <inst> <symbol> <0..1>` | set a knob (normalised) |
| `bypass <inst> <0\|1>` | bypass with a 10 ms crossfade |
| `mod_connect <inst> <symbol> <source> <depth>` | patch a source; the knob swings ±depth |
| `mod_disconnect <inst> <symbol>` | unpatch |
| `set_bpm <30..300>` | tempo for synced effects and the LFO |
| `master <-60..6>` | master gain in dB |
| `src_set <source> <symbol> <0..1>` | set a modulation source's knob (`src_set lfo rate 0.25`) |
| `src_add <type> [id]` | add a source to the modulation rack (`src_add lfo lfo2`). Without an id the engine picks the next free one (`lfo2`, `lfo3`...) |
| `src_remove <source>` | take a source out of the rack, with every patch to and from it |
| `src_mod <source> <symbol> <from source> <depth>` | cross-modulation: `from` moves this source's knob ±depth (`src_mod lfo rate env 0.3`); a source may move its own knobs |
| `src_unmod <source> <symbol>` | unpatch a source's knob |
| `ext <source> <value>` | feed an outside value to a source (`ext heart 72`, `ext ctrl -0.4`) |
| `tuner <0\|1> [mute 0\|1]` | strobe tuner on the dry input; with mute (the default) the output fades out while it's on |
| `tuner_ref <400..480>` | the tuner's A4 reference in Hz (default 440) |
| `input_channel <0\|1>` | browser only: which interface input to use |
| `asset_load <inst> <kind>/<file>` | load a library file into a module's slot (`asset_load cab ir/V30 SM57.wav`) |
| `asset_clear <inst>` | back to the module's built-in (e.g. the built-in cab) |

`<inst>` is the chain index (`3`) or the module id (`holo`). `<source>` is the id of an instance in
the modulation rack (`lfo`, `lfo2`, or one chosen with `src_add`: `[a-z][a-z0-9_]*`, up to 15
characters, never a module's id). A fresh engine's rack holds one of each type, ids = type ids.
Symbols and types come from the engine's description; the current set:

| inst | id | symbols |
|---|---|---|
| 0 | gate | thresh, release |
| 1 | amp | input, output |
| 2 | cab | locut, mix |
| 3 | holo | feedback, halo |
| 4 | tess | size, fold |

Source types and their knobs (`src_set`, `src_mod`):

| type | knobs |
|---|---|
| lfo | rate (beats per cycle), shape (sine → triangle → square) |
| env | attack, release, floor |
| adsr | attack, decay, sustain, release |
| orbit | speed |
| heart | rest (simulated bpm when no strap is connected) |
| ctrl | smooth, deadzone |

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

Replies: `resp <status>`. 0 means OK; negative numbers follow mod-host:
−1 bad instance (or, for `asset_load`, a file the module refused; for `src_add`, an unknown type),
−2 source id taken or not allowed, −3 rack full, −103 bad symbol, −207 value
out of range, −902 unknown command. `asset_load` replies also carry the engine's status line:
what loaded (`WaveNet · 48 kHz · levelled`, `stereo · 3.00 s`) or why it was refused.

Under the text protocol, hosts use the C API in `engine/api/rig_api.cpp`. Modules by chain index,
sources by rack slot: `rig_src_param(slot, p, v)`, `rig_ext(slot, v)`, `rig_mod(m, p, slot, depth)`;
the rack: write `"<type> [id]"` to `rig_text()`, then `rig_src_add()` returns the slot (or −1 unknown
type, −2 full, −3 bad id); `rig_src_remove(slot)`, `rig_src_mod(slot, p, fromSlot, depth)` (−1
unpatches), `rig_rack()` (JSON: what's in the rack and where). Adding and removing allocate: call
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

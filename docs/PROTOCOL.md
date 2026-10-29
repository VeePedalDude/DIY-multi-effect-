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
| `ext <source> <value>` | feed an outside value to a source (`ext heart 72`, `ext ctrl -0.4`) |
| `input_channel <0\|1>` | browser only: which interface input to use |

`<inst>` is the chain index (`3`) or the module id (`holo`). Symbols and source ids come
from the engine's description; the current set:

| inst | id | symbols |
|---|---|---|
| 0 | gate | thresh, release |
| 1 | amp | input, output |
| 2 | cab | locut, mix |
| 3 | holo | feedback, halo |
| 4 | tess | size, fold |

Sources: `lfo env adsr orbit heart ctrl`.

Replies: `resp <status>`. 0 means OK; negative numbers follow mod-host:
−1 bad instance, −103 bad symbol, −207 value out of range, −902 unknown command.

When the real rig moves to mod-host, the control hub translates
`mod_connect` into `cv_map` with min = knob − depth and max = knob + depth.

# Library and presets

## The folder

The library is a normal folder you choose. In the rig: menu → **Library → Choose folder…**.
Pick or create something like `Documents/RIG Library`. The rig creates one subfolder per file kind:

```
RIG Library/
  ir/        cabinet impulse responses (.wav)
  nam/       Neural Amp Modeler captures (.nam)
  presets/   RIG presets (.rig.json)
```

These are ordinary files. Copy IRs straight into `ir/` from Explorer or Finder and press **Rescan**,
back the folder up, or share it. Anything imported through the rig is copied into the right
subfolder. The kinds come from the engine: each module declares which kind of file it takes,
so a future module with a new kind (say, `reverb-ir/`) gets its own folder automatically.

Chrome remembers the folder. After a restart it may ask again; press **Reconnect folder**.

**No folder chosen** (or a browser without folder access): files are kept in the browser's own
storage, which survives reloads but isn't visible as files. Choosing a folder later copies them across.
**In an app preview** neither works, so imports last only until the page reloads.

## Adding files

- Drop files onto a module on the stage: a `.wav` onto the cab, a `.nam` onto the amp.
  They're imported and loaded into that module.
- Drop files onto empty stage space: each file is sorted into the library by type.
- In a module's panel: drop onto **Drop … files here**, or press **Import…**.
- Menu → **Import files…**, or **Import a folder…** to bring in a whole folder tree (four levels deep).

Imports are checked first: a WAV must parse as a WAV, and a `.nam` must parse as a NAM capture.
An identical file already in the library (same content, any name) isn't copied twice. A
different file with a taken name gets ` (2)` added.

## Choosing a file for a module

Select the module (click it, or press its number). Its panel lists every file of its kind with
details: sample rate, channels and length for IRs; architecture, gear, tone and author for NAM
captures. Type in the search box: every word must match the name or the details.
**Use built-in** returns to the module's default. **Remove file** deletes the file from the
library; it asks you to press again to confirm.

**IRs** of any length up to 20 s, mono or stereo, play in full: a 20 ms cabinet, a room, or a long
reverb. The browser decodes the WAV (16/24/32-bit or float, any sample rate it reads) and resamples
it to the engine's rate; a stereo IR gives a stereo output. Every IR is level-matched by its energy,
so swapping a cab for a hall doesn't jump in volume. Longer than 20 s, the first 20 s are used.

**NAM captures** of every architecture play in the amp: WaveNet (A1 standard/lite/feather, A2 and
slimmable A2), LSTM, ConvNet, Linear and Sequential, using NeuralAmpModelerCore itself
(`third_party/`). Captures that carry loudness data are levelled to −18 dB like the NAM plugin.
A file the engine can't play (damaged, an unknown architecture, more than 2 channels) is refused
and the amp's panel and the engine log say why; whatever was playing keeps playing.

In the browser, loading a capture briefly pauses audio (about 0.1 s for a large one; a 20 s stereo IR
about half a second) because the engine runs on the audio thread there. The swap itself is
a click-free crossfade. The Linux host loads on a background thread instead.

## Preset files

Presets are `.rig.json` files in `presets/`. They are versioned and built to keep working as the rig changes:

```json
{ "format": "rig-preset", "version": 1, "name": "Ambient Drive",
  "chain": [ { "id": "holo", "bypass": false, "params": { "feedback": 42.75, "halo": 60 } },
             { "id": "cab",  "bypass": false, "params": { "locut": 42.27, "mix": 90 },
               "asset": { "kind": "ir", "name": "V30 SM57.wav", "size": 8236, "hash": "1a2b3c4d" } } ],
  "mods": [ { "target": "holo.feedback", "source": "lfo", "depth": 0.22 } ],
  "tempo": 120, "master": -12,
  "scenes": { "A": { "chain": [ ... ], "mods": [ ... ], "tempo": 120 } } }
```

- **Matched by name, not position:** modules by `id`, knobs by `symbol`. Reordering or adding
  modules doesn't scramble a preset.
- **Real units** (%, ms, dB, Hz, s). If a knob's range changes later, the sound stays the same.
  Values outside the new range are set to the nearest end, with a note.
- **Files are referenced by name, size and content hash.** A file that was renamed is still found.
  A missing file loads the built-in instead, with a note, and the reference stays in the preset
  so re-saving doesn't lose it.
- **Modules this build doesn't have** are kept in the file and written back when you re-save.
- **Renamed knobs or modules** go in `PRESET_RENAMES` (`ui/js/72-preset-format.js`), so older files keep loading.
- **Changes to the file layout itself** bump `PRESET_VERSION` and add a `PRESET_MIGRATIONS` step.
- `tests/fixtures/ambient-drive.v1.rig.json` is a hand-written version-1 file. The build fails if it
  ever stops loading cleanly, so a change that would break existing presets can't slip through.

## On the iMac

The Linux host will use the same folder layout on disk (for example `~/RIG Library`). Files can
be added over the network or from a USB stick, and the host loads them with
`asset_load <inst> <kind>/<file name>`.

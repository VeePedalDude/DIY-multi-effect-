# Library and presets

## The folder

The library is a normal folder you choose. In the rig: menu → **Library → Choose folder…**.
Pick or create something like `Documents/RIG Library`. The rig creates one subfolder per file kind:

```
RIG Library/
  ir/        cabinet impulse responses (.wav)
  nam/       Neural Amp Modeler captures (.nam)
  presets/   RIG presets (.rig.json)
  skins/     looks for the page (.rigskin)
  plugins/   effects and sources as files (.rigfx bundles; in the rig-host also plain .clap): docs/PLUGINS.md
  library.json   your tags, favorites and notes (below)
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

Skins (`.rigskin`) live in the library's `skins/` folder too; see [SKINS.md](SKINS.md).

Imports are checked first: a WAV must parse as a WAV, and a `.nam` must parse as a NAM capture.
An identical file already in the library (same content, any name) isn't copied twice. A
different file with a taken name gets ` (2)` added.

## Choosing a file for a module

Click the file strip under the module's name on its card (it says what's loaded, and BROWSE), or
**Browse…** in its panel. The file browser opens in the middle of the page. It lists every file of
the module's kind with details: sample rate, channels and length for IRs; architecture, gear, tone
and author for NAM captures. Type in the search box: every word must match the name or the details.
A click loads a file straight away and the browser stays open, so you can try captures one after
another; **Done**, Escape or a click outside closes it. Files dropped on it are imported.
**Use built-in** returns to the module's default. **Remove file** deletes the file from the
library; it asks you to press again to confirm. The module's panel keeps just the current file,
these buttons and a drop area.

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

## The pedalboard: scenes and presets

Presets live on the pedalboard in the bottom-left corner. **Scenes** are banks of presets; you build
each one with as many presets as you want.

- **▼ / ▲** step to the previous / next scene. The board shows the scene's name, its position and how
  many presets it has.
- **The footswitches** are the scene's presets: click one to put it on. Its LED lights; an orange dot
  means it has unsaved edits. **+** adds a preset to the scene.
- **Name and SAVE**: type the preset's name in the box (Enter saves too). **SAVE** writes the preset
  you're on, under that name.
- **Every preset is its own sound.** A new one starts blank: gate, amp and cab with their knobs at
  defaults, no modulation. Edit it as you like.
- **Edits stay with each preset** while you move around the board, so you can try one, step to another
  and come back. They last until the page restarts; only **SAVE** keeps them.
- **The tree** (**Tree**) shows every scene with its presets. There you can rename scenes and presets,
  move them up and down, add presets (**+ Preset**) and scenes (**+ Scene**), and delete them.
  Deleting asks for a second click.

After a restart the board comes back on the preset you were on, as saved.

The board is `presets/board.json` in the library: the scenes in order, each preset's name and file,
and where you were. Each preset is its own `.rig.json` file beside it, in the format below. Preset
files from before the board stay in the folder but aren't on it. Their A–D `scenes` still read, but
aren't used.

## Preset files

Presets are `.rig.json` files in `presets/`. They are versioned and built to keep working as the rig changes:

```json
{ "format": "rig-preset", "version": 4, "name": "Ambient Drive",
  "chain": [ { "id": "holo", "type": "holo", "bypass": false, "params": { "feedback": 42.75, "halo": 60 } },
             { "id": "echo", "type": "holo", "bypass": true, "params": { "feedback": 70, "halo": 80 } },
             { "id": "cab",  "type": "cab", "bypass": false, "params": { "locut": 42.27, "mix": 90 },
               "asset": { "kind": "ir", "name": "V30 SM57.wav", "size": 8236, "hash": "1a2b3c4d" } } ],
  "sources": [ { "id": "lfo", "type": "lfo", "params": { "rate": 8, "shape": 0 } },
               { "id": "lfo2", "type": "lfo", "params": { "rate": 0.5 } } ],
  "mods": [ { "target": "holo.feedback", "source": "lfo", "depth": 0.22 },
            { "target": "lfo2.rate", "source": "env", "depth": 0.3 } ],
  "tempo": 120, "master": -12,
  "scenes": { "A": { "chain": [ ... ], "mods": [ ... ], "sources": [ ... ], "tempo": 120 } } }
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
- **Version 2** added `sources` (the modulation sources' knobs, in real units). Version 1 files
  migrate to it with every source at its default, which is exactly how they sounded.
- **Version 3** made `sources` the modulation rack: every instance in order, with its `type`
  (several of one type is fine), and `mods` targets can be source knobs (`lfo2.rate`:
  cross-modulation). The rack belongs to the preset; scenes (in older files) change the knobs and patches of the
  instances in it. Version 2 files migrate to the six fixed sources they were made with.
- **Version 4** made `chain` the effect chain in order: every instance with its `type` (several of
  one type is fine, any order, any subset). Version 3 files migrate to the five fixed modules in the
  order they ran then. Module types this build doesn't have are kept and written back on re-save.
- **Source types this build doesn't have** are kept (with their patches) and written back on re-save.
  Renamed source types and knobs go in `PRESET_RENAMES.sourceTypes` and `.sourceKnobs`.
- `tests/fixtures/ambient-drive.v1.rig.json` (version 1), `source-knobs.v2.rig.json` (version 2)
  `mod-rack.v3.rig.json` (version 3) and `fx-chain.v4.rig.json` (version 4) are hand-written. The build fails if any ever stops loading
  cleanly, so a change that would break existing presets can't slip through.

## On the iMac

The Linux host will use the same folder layout on disk (for example `~/RIG Library`). Files can
be added over the network or from a USB stick, and the host loads them with
`asset_load <inst> <kind>/<file name>`.

## Tags, favorites and notes

Every file in the library and every kind of effect and source can be found by tag:

- **Automatic tags** come from what a thing is:
  - a NAM capture's gear type, tone, architecture, maker and who modeled it;
  - an IR's length (up to 150 ms *cab*, up to 800 ms *room*, longer *reverb*), mono or stereo, and the
    gear its file name mentions (V30, Greenback, SM57, 4x12…);
  - a plugin's own tags, its category and *plugin*;
  - a built-in effect or source is tagged *built-in*.
- **Your tags and a note**: in the file browser, type tags (comma separated) and a note under the
  loaded file's name.
- **Favorites**: the ☆ beside any file, effect or source.

Each list gets a row of chips: ★ (favorites only), then the tags used in that list, most used first.
Choosing chips narrows the list to what has all of them. In the add panels a chosen chip searches every
tab. The search box matches tags and notes too.

Your tags, favorites and notes live in `library.json` at the top of the library folder. The files
themselves are never changed, and the tags travel when you copy or back up the folder. The file is
plain JSON:

```json
{ "format": "rig-library", "version": 1,
  "files": { "nam/JCM800 crunch.nam": { "tags": ["rhythm"], "fav": true, "note": "for the verse" } },
  "types": { "fx:holo": { "fav": true } } }
```

## Plugins in presets

A preset that uses a plugin effect or source names its file (`"plugin": { "file": "trem.rigfx",
"version": "1.0.0" }` on that chain or rack entry; older builds ignore it). Opened where the plugin
isn't in the library, the preset says which file to add, and keeps that module's settings in the file.

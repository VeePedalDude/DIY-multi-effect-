# Views: screens as files

A **view** is a screen that comes as one file, a `.rigview` bundle, instead of being built into the page:
a tab in the top bar beside **3D Stage**, **Visualizer** and **Chord Chart** that swaps the stage for one
card of its own. Theory tools, practice screens and visualizers are views. Effects and modulation sources
are the other two kinds of add-on ([PLUGINS.md](PLUGINS.md)); each kind loads only as itself, so a view can
never be added as an effect or a source, or the other way round.

Views don't ship with RIG/01. Put a `.rigview` in the library's `Views` folder (`Documents/RIG01/Views` on
the rig-host, `views/` in a library folder or the browser's storage), or drop it on the page: its tab
appears straight away. Take the file out and the tab goes. Two are built here as working examples:
the Scale Atlas (`views/scale-atlas/`, a full tab that only draws) and Quick Controls
(`views/quick-controls/`, a small docked panel that switches presets, sets the tempo and bypasses
effects); both land in `dist/views/`.

The tuner stays built in: it mutes the output and has to be there when you need it.

## The bundle

A zip with these files:

| File | |
|---|---|
| `manifest.json` | what the view is (below) |
| `view.js` | its code, run in the view's own frame |
| `view.css` (optional) | its styles, added after the shared card styles |

```json
{ "format": "rigview", "formatVersion": 1, "id": "scale-atlas", "name": "Scale Atlas",
  "tab": "Scale Atlas", "key": "s", "version": "1.0.0", "author": "RIG/01",
  "description": "A scale on the fretboard, degrees to highlight, and the chords that fit it.",
  "hint": "pick a key and a scale on the left of the card ...", "feeds": [] }
```

- `id`: lowercase letters, digits and `-`, at most 40. A newer file with the same id replaces the old
  one (an import takes its place, as with plugins). Two files with one id: the first by name wins.
- `tab`: the button's label, at most 24 characters. `key` (optional): one letter that opens and closes
  it, ignored if the rig or another view already uses it (`v c t k p` and digits are the rig's).
- `feeds`: the live data it wants (below).
- `permissions` (optional): what it wants to do to the rig (below). Without any, a view can only draw.
- `place` (optional): `"tab"` (the default: a card that takes the stage's place) or `"dock"` (a small
  panel on the stage), with `"dock": { "w": 300, "h": 168 }` for its size in pixels (160 to 420 wide,
  80 to 360 high; 280 x 160 if left out).
- The whole bundle is at most 4 MB; `view.js` and `view.css` must be UTF-8.

`tools/build_view.py views/<id>/` checks the manifest and writes `dist/views/<id>.rigview`; `./build.sh`
builds every folder in `views/`.

## What a view runs in

Each view runs in a sandboxed frame laid over its card: scripts allowed, nothing else. It has no
access to the page, the library, the engine, other views, cookies or this browser's storage, and a
Content-Security-Policy stops it reaching the network (the rig-host on `localhost` included). It talks to
the rig only through the `rig` object below, by messages the page checks. A view downloaded from
anywhere can draw, listen to its feeds and keep its own settings; anything more is a permission its
manifest names and the player allows (below).

The frame is the card's size in **card units**: 518 high and `rig.card.L` long (1300 to 1700, stretched to
the shape of the window, as the Chord Chart's is). The page draws the card's frame (the skin's tile, turned
sideways) under the transparent frame and scales both together. Lay out in card units: the left part
(x 112 to 642) is where the built-in views put their controls, the right part starts at `rig.card.RX`
(662) and is `rig.card.RW` wide; keep to y 52 to 470 inside the frame's rails.

A **docked** view (`"place": "dock"`) is a panel of its own size at the bottom left of the 3D stage,
above the pedalboard; several stack upwards. Its button in the top bar (and its `key`) switches it on and
off, which is remembered; it shows only while the 3D stage does. Its frame is the panel: `rig.card` is
`{ L: w, H: h, RX: 0, RW: w, k: 1, dpr, dock: true }`, in pixels, and the page draws the panel's glass
behind it.

Shared in every view's frame:
- the page's card styles (`.cchip` buttons, `.crow` rows, `.cfoot` footers, `select`, `.btn`, `.mono`) and
  the skin's CSS variables (`var(--accent)`, `var(--raise)`...), so a view's controls look like the rig's;
- the theory kit: `15-chordbook.js` (`chordSpell`, `chordVoicings`, `CB_TUNINGS`, `cbPc`, `cbNote`...) and
  `ui/views/scales.js` (`scaleSpell`, `scaleChords`, `SC_SCALES`, `SC_EXOTIC`), as globals.

## The `rig` object

| | |
|---|---|
| `rig.card` | `{ L, H, RX, RW, k, dpr }`: the card's length and height in card units, where the right part starts and its width, and the scale and device pixel ratio it's drawn at (a canvas `L * k * dpr` pixels across is sharp) |
| `rig.skin` | the active skin's data: `ui` (fonts, colours), `card` and `tile` (the card's resolved title / text / rim / accent colours), `screen`, `chart` (the theory views' colours: glass, grid, dot, root, hi, open, mute, text) |
| `rig.settings` | what the view last saved (`{}` the first time) |
| `rig.save(obj)` | keep settings for next time (JSON, at most 64 KB; in this browser, per view) |
| `rig.on(event, fn)` | `'init'` (once: `rig.card`, `rig.skin`, `rig.settings` and `rig.permissions` are set; draw now), `'show'`, `'hide'`, `'layout'` (`rig.card` changed), `'skin'` (`rig.skin` changed), `'permissions'` (the player answered), and each feed by name |
| `rig.permissions`, `rig.can(name)` | the permissions the player has allowed |
| `rig.ready()` | call once the view has drawn after `'init'`: the page shows the frame then |

Feeds (named in the manifest; sent about 30 times a second while the view is showing, never while it's hidden):

| Feed | Each message | |
|---|---|---|
| `notes` | `{ notes: [{ midi, level }], chord: 'Am7' or '' }` | what's being played on the dry input, loudest first, and its name; the rig's note listener runs only while a view (or the Visualizer) wants it |
| `sources` | `[{ id, name, color, value }]` | every modulation source in the rack, value -1..1 |
| `levels` | `{ in, out }` | input and output peaks, 0..1 |
| `chain` | `[{ id, type, name, bypass, params: [{ symbol, name, value, text }] }]` | the effect chain in order; each knob's position 0..1 and its readout |
| `presets` | `{ scene, preset, name, scenes: [{ name, presets: [name] }] }` | the pedalboard and where it stands |
| `tempo` | `{ bpm }` | |

`chain`, `presets` and `tempo` are sent when the view is shown and then only when they change. Reading
them needs no permission.

## Permissions

A view that wants to change the rig names what it needs in its manifest's `permissions`. The first time
it is shown the page asks the player, over the view, in the rig's own words (“Quick Controls
(quick-controls.rigview) asks to switch presets and scenes, set the tempo…”): **Allow** or **Not now**.
The answer is kept in this browser per view; a newer file that asks for more is asked about the new ones
only. Until allowed, and for anything not declared, the calls below do nothing.

| Permission | The player is asked to let it… | Calls |
|---|---|---|
| `params` | turn knobs | `rig.setParam(moduleId, symbol, value)`: a knob to 0..1 (ids and symbols as in the `chain` feed) |
| `chain` | add, remove and bypass effects | `rig.bypass(moduleId, on)`, `rig.addEffect(type)`, `rig.removeEffect(moduleId)` |
| `presets` | switch presets and scenes | `rig.preset(sceneIndex, presetIndex)` |
| `tempo` | set the tempo | `rig.setTempo(bpm)`: 30 to 300 |

The page checks every call: the permission, that the module, knob or preset exists, that numbers are
numbers (clamped to their range), and no more than 120 calls a second from one view. Each then goes
through the same code as a click on the page, so presets, the unsaved-edit mark and the 3D stage follow.
There is no call that sends a protocol line, reads a file or reaches another view, and there won't be:
a new ability is a new named permission here (`VIEW_PERMS` in `66-views.js`, the runtime's call, the table
above, a case in `tests/browser/viewctl.cjs`).

Keys pressed in a view (outside its text fields and menus) reach the rig, so the tab shortcuts and **P**
keep working.

## Not yet

The tuner's reading, the audio itself (a spectrum or waveform feed), moving effects in the chain, saving
presets, a strip in the bottom bar, and settings kept in the library instead of this browser.

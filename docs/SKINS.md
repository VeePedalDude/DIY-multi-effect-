# Skins

A skin changes how RIG/01 looks: colours, images, fonts, and the style of every menu, panel and
table. It never changes the layout. Cards, knobs, the stage, the panels and the menus stay where
they are, so every skin works with every module, preset and future feature. It's the Winamp idea.

## Using skins

- **Settings (☰) → Skins** lists the built-in skins (Astral Hall, Classic) and every skin in your
  library's `skins/` folder. **Use** puts one on.
- **★** marks favourites. **K** steps through your favourites (or through every skin, if none are
  marked).
- **Drop a `.rigskin` file anywhere on the stage**, or use **Import skin…**. It's copied into the
  library and put on straight away. A file that isn't a usable skin is skipped, with the reason in
  the Engine log.
- The browser remembers your choice.

## Making a skin

1. Put on the skin closest to what you want, then **Export as template**. You get
   `<name> template.rigskin`: a zip with a `skin.json` that spells out **every** setting, plus the
   skin's images.
2. Unzip it, edit `skin.json` and the images (and add fonts or a `skin.css` if you like).
3. Zip the folder again (Windows: right-click → Send to → Compressed folder; macOS: Compress) and
   rename `.zip` to `.rigskin`.
4. Drop it on the stage.

Anything you leave out of `skin.json` falls back to the default skin, Astral Hall. So the smallest
useful skin is:

```json
{ "format": "rig-skin", "version": 1, "name": "Hot Pink", "ui": { "accent": "#ff2bd6" } }
```

Mistakes don't stop a skin from loading: a bad colour or a missing image is skipped, and the
Skins menu and Engine log say which setting and why.

## skin.json reference

Colours are CSS colours: `#rgb`, `#rrggbb`, `#rrggbbaa`, `rgb()`/`rgba()`, `hsl()`/`hsla()` or
a colour name. Images and fonts are file names inside the `.rigskin`.

| Section | What it styles |
|---|---|
| `name`, `author`, `description` | shown in the Skins menu |
| `ui` | menus, panels, tables, bars, dialogs, buttons. These become the page's CSS variables: `bg`, `glass` (panel and bar background, usually translucent), `raise` (buttons, inputs), `line` / `line2` (borders), `fg` / `muted` / `soft` (text), `accent`, `ink` (text on accent), `ok` / `warn` / `bad`, `off`, `item` (list rows), `selected`, `well` (log, wells), `overlay`, `dialog`, `onBg` / `onLine` (Active button), `bypBg` / `bypLine` (Bypassed button), `fontDisplay`, `fontMono` (CSS font lists) |
| `stage` | the 3D stage: `background` (an image filling the window, or `null`), `backgroundColor`, `parallax` (0–1: how much the background shifts as you orbit and zoom, so a flat picture sits in the scene; 0 keeps it still), `fog` and `fogDensity` (0–0.2), `flow` (the signal line), `slab` (card backs), `carry` (the cable you're carrying), `pillBg` / `pillLine` / `pillText` (IN/OUT labels) |
| `screen` | the dark display on every module face: `glass`, `accent`, `text`, `bright`, `dim`, `warn`, `ok`, `track`, `off`, `alt`, `grid` |
| `card` | text and plain parts of a module card: `title`, `text`, `powerOn`, `powerOff`, `bypassed`; `plain` / `plainLine` for a tile without an image |
| `knob` | `track`, `bodyLight` / `bodyMid` / `bodyDark` (the knob's shading), `rim`, `pointer`, `label`, `value` |
| `tiles` | named card styles. Each has `image` (a card frame, or `null` for a plain card), `accent` (knob arcs), `rim` (around the display), `glow` (behind the focused card and on the floor), and optional `card` / `knob` overrides with the same keys as above. **Replaces** the default's tiles entirely. |
| `modules` | which tile each module uses, by type or by instance id: `{ "amp": "gold", "holo2": "teal" }` (an instance id wins over its type). Modules not listed (including future ones) cycle through `tileOrder`. |
| `tileOrder` | the tile names to cycle through |
| `sources` | orb, cable and label colour per modulation source id: `lfo env adsr orbit heart ctrl geo` (GEOMETRIC PULSE's 3D view draws in it too) |
| `sourceFallback` | colours for sources that aren't listed (future ones) |
| `labels` | the source name labels above the orbs: `bg`, `text` |
| `tuner` | the strobe tuner. `tile`: which of your `tiles` it's drawn on (turned sideways). `glass` (its display), `stripe` (the strobe bands), `inTune` / `flat` / `sharp` (note, arrows and marker), `text` / `bright`. The face-down mini card in the top bar: `back`, `backLine` (border and lattice), `backMark` (the little strobe on it) |
| `fonts` | fonts in the packet: `[{ "family": "Orbitron", "file": "orbitron.woff2", "weight": "700" }]`. Use the family name in `ui.fontDisplay` / `ui.fontMono`. Formats: woff2, woff, ttf, otf. |
| `css` | a stylesheet in the packet, applied after the built-in one, for anything the settings don't cover. Selectors to use: `.top`, `.bottom`, `.panel`, `.drawer`, `.tuner`, `.tunerMini`, `.tabs`, `.tab`, `.pick-search`, `.matrix` (and its `table`, `th`, `.cell`), `.btn`, `.scene`, `.skinrow`, `.asset`, `.patch`, `.overlay .card`. |

### Card frame images

Cards are drawn 518 × 754 pixels. Use that shape (any resolution with the same proportions), with
transparent rounded corners. The title sits at the upper left (y ≈ 120–200), the display at
x 50–468, y 206–374, and the knobs below it. Leave those areas plain enough to read over.
Modules with more than four knobs stretch the strip between y 400 and 470.

## The packet

A `.rigskin` file is a zip. `skin.json` sits at the top, or inside one folder (what zipping a folder
gives you). Other files can be in subfolders; refer to them by their path from `skin.json`.
Packets are limited to 25 MB.

## For developers

- `ui/js/04-skin-core.js`: the default skin (also the schema), the Classic skin, validation
  (`readSkinJson`), merging, and the zip reader and writer. It has no DOM, so it is unit-tested in
  `tests/ui/skin.test.mjs`.
- `ui/js/57-skins.js`: applies a skin (CSS variables, fonts, stylesheet, the 3D stage, faces), and
  the Skins menu, favourites, K, import and export.
- New UI code takes colours from the skin, never literal values: CSS uses the `var(--…)` names;
  canvas and three.js code uses `SKIN.*`; face screens use `h.col.*`. A new kind of themable thing
  means a new key in `DEFAULT_SKIN` (and this table), so old skins keep working: they just get the
  default for it.
- `tests/browser/skins.cjs` exercises the whole loop in Chromium: built-ins, favourites and K,
  exporting a template, editing it into a new skin, dropping it on the stage, a broken skin, and a
  reload.

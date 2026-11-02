// 04-skin-core.js - skins: the look of the rig, separate from its layout (Winamp style).
// A skin is a `.rigskin` file: a zip holding `skin.json` plus any images, fonts and an optional
// skin.css it names. It can change every colour, font and image (background, card frames, knobs,
// face screens, source orbs, menus, panels, tables), never where things are. Anything a skin leaves
// out comes from DEFAULT_SKIN, so a skin can be as small as one colour. Format: docs/SKINS.md.
// No DOM here (zip reading, validation, merging), so it is unit-tested in Node; 57-skins.js applies it.
const SKIN_FORMAT = 'rig-skin', SKIN_VERSION = 1;
const SKIN_MAX_BYTES = 25 << 20;

const DEFAULT_SKIN = {
  format: SKIN_FORMAT, version: SKIN_VERSION,
  name: 'Astral Hall', author: 'KC', description: 'A crystal hall under a dark sky; modules on silver cards in purple, teal and gold.',
  // menus, panels, tables, bars, dialogs: CSS variables (see the --names in ui/index.html)
  ui: {
    bg: '#07050D', glass: 'rgba(11,9,21,.86)', raise: '#0F1524', line: '#1F2A40', line2: '#2A3550',
    fg: '#E6ECF5', muted: '#94A3BD', soft: '#C9D3E6', accent: '#4FE3F0', ink: '#06080E',
    ok: '#9BE15D', warn: '#FFB547', bad: '#FF5A6E', off: '#3A4766',
    item: '#0B1120', selected: '#0E2530', well: '#070B14', overlay: 'rgba(5,7,12,.78)', dialog: '#0A0E17',
    onBg: '#12301F', onLine: '#2C6B45', bypBg: '#2A1A1F', bypLine: '#6B2C3A',
    fontDisplay: '"Chakra Petch","Rajdhani",system-ui,sans-serif', fontMono: '"IBM Plex Mono",ui-monospace,Consolas,monospace',
  },
  // the 3D stage
  stage: {
    background: 'hall.webp', backgroundColor: '#0b0716', fog: '#0b0716', fogDensity: 0.014,
    parallax: 0.6,                                        // how much the background shifts as you orbit and zoom (0 = fixed, 1 = most)
    flow: '#4FE3F0', slab: '#1a1528', carry: '#ffffff',
    pillBg: 'rgba(15,21,36,.9)', pillLine: '#2A3550', pillText: '#E6ECF5',
  },
  // the dark display on every module face (ui/screens/*.js draw with these)
  screen: { glass: '#070A14', accent: '#4FE3F0', text: '#94A3BD', bright: '#E6ECF5', dim: '#5E6B85', warn: '#FFB547', ok: '#9BE15D', track: '#1C2538', off: '#3A4766', alt: '#7C9CFF', grid: '#1F2A40' },
  // text and plain parts of a module card (a tile can override any of these)
  card: { title: '#20253A', text: '#596079', powerOn: '#3FBF6A', powerOff: '#9AA0AE', bypassed: '#9A5260', plain: '#ECEDF1', plainLine: '#B9BECB' },
  knob: { track: '#D3D7E0', bodyLight: '#FFFFFF', bodyMid: '#E3E6EC', bodyDark: '#AEB4C2', rim: '#8D94A4', pointer: '#20253A', label: '#20253A', value: '#596079' },
  // card frames: image (518 x 754, transparent corners) or none for a plain card, plus colours
  tiles: {
    purple: { image: 'tile-purple.webp', accent: '#7B5BD6', rim: '#B9A6F2', glow: '#9D7CFF' },
    teal:   { image: 'tile-teal.webp',   accent: '#159BAA', rim: '#7FD6DF', glow: '#3FD6E6' },
    gold:   { image: 'tile-gold.webp',   accent: '#A9822A', rim: '#E2C98A', glow: '#E0B85A' },
  },
  tileOrder: ['teal', 'gold', 'purple'],                 // modules not listed in `modules` cycle through these
  modules: { gate: 'teal', amp: 'gold', cab: 'gold', holo: 'purple', tess: 'purple' },
  sources: { lfo: '#4FE3F0', env: '#FFB547', adsr: '#FF5FA2', orbit: '#A78BFA', heart: '#FF7A6B', ctrl: '#9BE15D' },
  sourceFallback: ['#7DD3FC', '#F9A8D4', '#FDE68A', '#86EFAC', '#C4B5FD', '#FCA5A5'],
  labels: { bg: 'rgba(12,9,22,.88)', text: '#A9B3C9' },
  fonts: [],                                              // [{ family, file, weight?, style? }]
  css: null,                                              // optional stylesheet file in the packet
};
// The original look: dark navy faces, no images. Written the way a skin file is: only what differs.
const CLASSIC_SKIN = {
  format: SKIN_FORMAT, version: SKIN_VERSION, name: 'Classic', author: 'RIG/01', description: 'The original dark console: navy faces, cyan accents, no images.',
  ui: { bg: '#05070C', glass: 'rgba(9,13,22,.86)' },
  stage: { background: null, backgroundColor: '#05070c', fog: '#05070c', fogDensity: 0.026, slab: '#0a0e17' },
  card: { title: '#E6ECF5', text: '#94A3BD', powerOn: '#9BE15D', powerOff: '#3A4766', bypassed: '#FF5A6E', plain: '#0C1220', plainLine: '#2A3550' },
  knob: { track: '#263149', bodyLight: '#1E2740', bodyMid: '#151C2E', bodyDark: '#0F1524', rim: '#2A3550', pointer: '#E6ECF5', label: '#E6ECF5', value: '#94A3BD' },
  tiles: { console: { image: null, accent: '#4FE3F0', rim: '#1F2A40', glow: '#4FE3F0' } },
  tileOrder: ['console'], modules: {},
  labels: { bg: 'rgba(10,14,23,.88)', text: '#94A3BD' },
};

// ------------------------------------------------------------------ validation and merging
// Sections whose value replaces the default's outright instead of merging key by key.
const SKIN_REPLACE = ['tiles', 'tileOrder', 'modules', 'fonts', 'sourceFallback'];
const isColor = (v) => typeof v === 'string' && /^(#[0-9a-f]{3,8}|(rgb|hsl)a?\([^()]{3,60}\)|[a-z]{3,20}|transparent)$/i.test(v.trim());
const IMAGE_EXT = /\.(webp|png|jpe?g|svg|gif|avif)$/i, FONT_EXT = /\.(woff2?|ttf|otf)$/i;
const TEXT_KEYS = new Set(['name', 'author', 'description']);

// skin.json -> { skin (only valid parts), warnings }. `has(name)` says whether the packet holds a file.
function readSkinJson(j, has = () => true) {
  const warnings = [], warn = (w) => { if (!warnings.includes(w)) warnings.push(w); };
  if (!j || typeof j !== 'object' || j.format !== SKIN_FORMAT) return { error: 'not a RIG skin (skin.json needs "format": "rig-skin")' };
  if (Number.isInteger(j.version) && j.version > SKIN_VERSION) warn(`made for a newer skin format (${j.version}); using what this build understands`);
  const out = { format: SKIN_FORMAT, version: SKIN_VERSION };
  const image = (path, v) => {
    if (v === null) return null;
    if (typeof v !== 'string' || !IMAGE_EXT.test(v)) { warn(`${path}: “${v}” isn't an image file name`); return undefined; }
    if (!has(v)) { warn(`${path}: ${v} isn't in the skin`); return undefined; }
    return v;
  };
  const colorOr = (path, v) => { if (isColor(v)) return v.trim(); warn(`${path}: “${v}” isn't a colour`); return undefined; };
  for (const [k, v] of Object.entries(j)) {
    if (k === 'format' || k === 'version') continue;
    if (TEXT_KEYS.has(k)) { if (typeof v === 'string') out[k] = v.slice(0, 200); continue; }
    if (!(k in DEFAULT_SKIN)) { warn(`“${k}” isn't a skin setting; ignored`); continue; }
    const base = DEFAULT_SKIN[k];
    if (k === 'css') { if (v === null || (typeof v === 'string' && /\.css$/i.test(v) && has(v))) out.css = v; else warn(`css: ${v} isn't a stylesheet in the skin`); continue; }
    if (k === 'fonts') {
      if (!Array.isArray(v)) { warn('fonts: should be a list'); continue; }
      out.fonts = v.filter((f) => {
        const ok = f && typeof f.family === 'string' && /^[\w \-]{1,60}$/.test(f.family) && typeof f.file === 'string' && FONT_EXT.test(f.file) && has(f.file);
        if (!ok) warn(`fonts: ${JSON.stringify(f).slice(0, 60)} needs a family name and a .woff2/.woff/.ttf/.otf file in the skin`);
        return ok;
      }).map((f) => ({ family: f.family, file: f.file, weight: String(f.weight || '400'), style: f.style === 'italic' ? 'italic' : 'normal' }));
      continue;
    }
    if (k === 'tileOrder') { if (Array.isArray(v) && v.every((x) => typeof x === 'string')) out.tileOrder = v.slice(0, 32); else warn('tileOrder: should be a list of tile names'); continue; }
    if (k === 'sourceFallback') { if (Array.isArray(v)) out.sourceFallback = v.filter((c, i) => colorOr(`sourceFallback[${i}]`, c) !== undefined); continue; }
    if (k === 'modules') {
      if (!v || typeof v !== 'object') { warn('modules: should map module ids to tile names'); continue; }
      out.modules = Object.fromEntries(Object.entries(v).filter(([, t]) => typeof t === 'string')); continue;
    }
    if (k === 'tiles') {
      if (!v || typeof v !== 'object') { warn('tiles: should map tile names to settings'); continue; }
      out.tiles = {};
      for (const [name, t] of Object.entries(v)) {
        if (!t || typeof t !== 'object') { warn(`tiles.${name}: should be an object`); continue; }
        const tile = {};
        for (const [tk, tv] of Object.entries(t)) {
          const p = `tiles.${name}.${tk}`;
          if (tk === 'image') { const im = image(p, tv); if (im !== undefined) tile.image = im; }
          else if (tk === 'card' || tk === 'knob') { const sec = {}; for (const [sk, sv] of Object.entries(tv || {})) { if (!(sk in DEFAULT_SKIN[tk])) { warn(`${p}.${sk}: not a ${tk} setting`); continue; } const c = colorOr(`${p}.${sk}`, sv); if (c !== undefined) sec[sk] = c; } tile[tk] = sec; }
          else if (['accent', 'rim', 'glow'].includes(tk)) { const c = colorOr(p, tv); if (c !== undefined) tile[tk] = c; }
          else warn(`${p}: not a tile setting; ignored`);
        }
        if (!('image' in tile)) tile.image = null;
        out.tiles[name] = tile;
      }
      continue;
    }
    if (!v || typeof v !== 'object' || Array.isArray(v)) { warn(`${k}: should be an object`); continue; }
    const sec = {};
    for (const [sk, sv] of Object.entries(v)) {
      const p = `${k}.${sk}`;
      if (k !== 'sources' && !(sk in base)) { warn(`${p}: not a setting; ignored`); continue; }
      if (k === 'stage' && sk === 'background') { const im = image(p, sv); if (im !== undefined) sec[sk] = im; continue; }
      if (k === 'stage' && sk === 'fogDensity') { if (typeof sv === 'number' && sv >= 0 && sv <= 0.2) sec[sk] = sv; else warn(`${p}: should be a number from 0 to 0.2`); continue; }
      if (k === 'stage' && sk === 'parallax') { if (typeof sv === 'number' && sv >= 0 && sv <= 1) sec[sk] = sv; else warn(`${p}: should be a number from 0 to 1`); continue; }
      if (k === 'ui' && (sk === 'fontDisplay' || sk === 'fontMono')) { if (typeof sv === 'string' && sv.length < 200 && !/[;{}<>]/.test(sv)) sec[sk] = sv; else warn(`${p}: not a usable font list`); continue; }
      const c = colorOr(p, sv); if (c !== undefined) sec[sk] = c;
    }
    out[k] = sec;
  }
  return { skin: out, warnings };
}
// Default + skin: objects merge key by key, SKIN_REPLACE sections are taken whole.
function mergeSkin(base, over) {
  const out = JSON.parse(JSON.stringify(base));
  for (const [k, v] of Object.entries(over || {})) {
    if (v && typeof v === 'object' && !Array.isArray(v) && !SKIN_REPLACE.includes(k) && out[k] && typeof out[k] === 'object') out[k] = { ...out[k], ...v };
    else out[k] = JSON.parse(JSON.stringify(v));
  }
  return out;
}
// The colours a module's card is drawn with: the skin's card and knob colours, then its tile's.
function tileStyle(skin, name) {
  const t = skin.tiles[name] || Object.values(skin.tiles)[0] || { image: null, accent: skin.ui.accent, rim: skin.card.plainLine, glow: skin.ui.accent };
  return { name, image: t.image || null, accent: t.accent || skin.ui.accent, rim: t.rim || skin.card.plainLine, glow: t.glow || t.accent || skin.ui.accent,
    card: { ...skin.card, ...(t.card || {}) }, knob: { ...skin.knob, ...(t.knob || {}) } };
}
function tileNameFor(skin, moduleId, index) {
  const named = skin.modules[moduleId];
  if (named && skin.tiles[named]) return named;
  const order = skin.tileOrder.filter((n) => skin.tiles[n]);
  return order.length ? order[index % order.length] : Object.keys(skin.tiles)[0];
}
// Every file a skin uses (to pack it, or to check a packet is complete).
function skinFiles(skin) {
  const f = new Set();
  if (skin.stage && skin.stage.background) f.add(skin.stage.background);
  Object.values(skin.tiles || {}).forEach((t) => { if (t.image) f.add(t.image); });
  (skin.fonts || []).forEach((x) => f.add(x.file));
  if (skin.css) f.add(skin.css);
  return [...f];
}

// ------------------------------------------------------------------ .rigskin packets (zip)
const CRC_TABLE = (() => { const t = new Uint32Array(256); for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; } return t; })();
function crc32(b) { let c = 0xffffffff; for (let i = 0; i < b.length; i++) c = CRC_TABLE[(c ^ b[i]) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; }
// files: [[name, Uint8Array]] -> zip bytes (stored: images are already compressed)
function zipFiles(files) {
  const enc = new TextEncoder(), local = [], central = []; let off = 0;
  for (const [name, data] of files) {
    const nm = enc.encode(name), crc = crc32(data), h = new DataView(new ArrayBuffer(30));
    h.setUint32(0, 0x04034b50, true); h.setUint16(4, 20, true); h.setUint16(6, 0x0800, true); h.setUint16(8, 0, true);
    h.setUint32(14, crc, true); h.setUint32(18, data.length, true); h.setUint32(22, data.length, true); h.setUint16(26, nm.length, true);
    local.push(new Uint8Array(h.buffer), nm, data);
    const c = new DataView(new ArrayBuffer(46));
    c.setUint32(0, 0x02014b50, true); c.setUint16(4, 20, true); c.setUint16(6, 20, true); c.setUint16(8, 0x0800, true);
    c.setUint32(16, crc, true); c.setUint32(20, data.length, true); c.setUint32(24, data.length, true); c.setUint16(28, nm.length, true); c.setUint32(42, off, true);
    central.push(new Uint8Array(c.buffer), nm);
    off += 30 + nm.length + data.length;
  }
  const cdSize = central.reduce((a, b) => a + b.length, 0), end = new DataView(new ArrayBuffer(22));
  end.setUint32(0, 0x06054b50, true); end.setUint16(8, files.length, true); end.setUint16(10, files.length, true); end.setUint32(12, cdSize, true); end.setUint32(16, off, true);
  const parts = [...local, ...central, new Uint8Array(end.buffer)], out = new Uint8Array(parts.reduce((a, b) => a + b.length, 0));
  let p = 0; for (const x of parts) { out.set(x, p); p += x.length; }
  return out;
}
async function inflateRaw(data) {
  const s = new Blob([data]).stream().pipeThrough(new DecompressionStream('deflate-raw'));
  return new Uint8Array(await new Response(s).arrayBuffer());
}
// zip bytes -> Map(name -> Uint8Array). Stored and deflated entries (what every zip tool writes).
async function unzip(bytes) {
  const u = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes), v = new DataView(u.buffer, u.byteOffset, u.byteLength);
  let e = -1; for (let i = u.length - 22; i >= Math.max(0, u.length - 65557); i--) if (v.getUint32(i, true) === 0x06054b50) { e = i; break; }
  if (e < 0) throw new Error('not a zip file');
  const n = v.getUint16(e + 10, true); let p = v.getUint32(e + 16, true);
  const dec = new TextDecoder(), out = new Map();
  for (let k = 0; k < n; k++) {
    if (v.getUint32(p, true) !== 0x02014b50) throw new Error('damaged zip directory');
    const method = v.getUint16(p + 10, true), csize = v.getUint32(p + 20, true), size = v.getUint32(p + 24, true);
    const nl = v.getUint16(p + 28, true), xl = v.getUint16(p + 30, true), cl = v.getUint16(p + 32, true), lo = v.getUint32(p + 42, true);
    const name = dec.decode(u.subarray(p + 46, p + 46 + nl)); p += 46 + nl + xl + cl;
    if (name.endsWith('/')) continue;
    if (size > SKIN_MAX_BYTES) throw new Error(`${name} is too large`);
    const start = lo + 30 + v.getUint16(lo + 26, true) + v.getUint16(lo + 28, true), raw = u.subarray(start, start + csize);
    if (method === 0) out.set(name, raw.slice());
    else if (method === 8) out.set(name, await inflateRaw(raw));
    else throw new Error(`${name}: unsupported zip compression (${method})`);
  }
  return out;
}
// .rigskin bytes -> { json, files: Map(name -> bytes), skin, warnings } or { error }.
// skin.json may sit at the top or inside one folder (how most zip tools pack a folder).
async function readSkinPacket(bytes) {
  if (bytes.byteLength > SKIN_MAX_BYTES) return { error: `larger than ${SKIN_MAX_BYTES >> 20} MB` };
  let all; try { all = await unzip(bytes); } catch (e) { return { error: e.message }; }
  const top = [...all.keys()].find((k) => /(^|\/)skin\.json$/i.test(k) && k.split('/').length <= 2);
  if (!top) return { error: 'no skin.json inside' };
  const prefix = top.slice(0, top.length - 'skin.json'.length), files = new Map();
  for (const [k, b] of all) if (k.startsWith(prefix) && k !== top) files.set(k.slice(prefix.length), b);
  let json; try { json = JSON.parse(new TextDecoder().decode(all.get(top))); } catch (e) { return { error: 'skin.json isn’t valid JSON: ' + e.message }; }
  const r = readSkinJson(json, (name) => files.has(name));
  if (r.error) return r;
  return { json, files, skin: r.skin, warnings: r.warnings };
}

// The skin in use (fully merged); 57-skins.js replaces it when the viewer picks another.
let SKIN = mergeSkin(DEFAULT_SKIN, {});
// Colour of a source's orb, cable and label: the skin's colour for its id, else the next fallback.
function sourceColor(id, i) { return SKIN.sources[id] || SKIN.sourceFallback[i % Math.max(1, SKIN.sourceFallback.length)] || SKIN.ui.accent; }

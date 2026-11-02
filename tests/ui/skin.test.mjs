// tests/ui/skin.test.mjs - skins (ui/js/04-skin-core.js): validation, merging, .rigskin packets.
import zlib from 'zlib';
import { loadUi, check, done } from './load.mjs';
const u = await loadUi();
const enc = (s) => new TextEncoder().encode(s), png = new Uint8Array([0x89, 0x50, 0x4e, 0x47, 1, 2, 3]);

console.log('built-in skins');
let r = u.readSkinJson(u.DEFAULT_SKIN);
check(!r.error && r.warnings.length === 0, `Astral Hall (the default) is a valid skin file${r.warnings && r.warnings.length ? ': ' + r.warnings.join(' | ') : ''}`);
r = u.readSkinJson(u.CLASSIC_SKIN);
check(!r.error && r.warnings.length === 0, 'Classic is a valid skin file');
const classic = u.mergeSkin(u.DEFAULT_SKIN, r.skin);
check(Object.keys(classic.tiles).join() === 'console' && classic.ui.accent === u.DEFAULT_SKIN.ui.accent && classic.ui.bg === '#05070C' && classic.stage.background === null,
  'merge: Classic replaces the tiles, overrides some colours, keeps the rest of the default');
check(u.tileNameFor(classic, 'amp', 1) === 'console' && u.tileNameFor(u.DEFAULT_SKIN, 'amp', 0) === 'gold' && u.tileNameFor(u.DEFAULT_SKIN, 'newfx', 4) === 'gold',
  'tile per module: from the skin’s module map, else cycling its tile order');

console.log('checking a skin file');
check(u.readSkinJson({ name: 'x' }).error && u.readSkinJson({ format: 'rig-preset' }).error, 'not a skin: refused');
const odd = { format: 'rig-skin', version: 1, name: 'Odd', ui: { accent: '#ff00aa', fg: 'not-a-colour!', wobble: '#fff' }, sparkles: true,
  stage: { background: 'missing.png', fogDensity: 3 }, tiles: { red: { image: 'red.png', accent: 'red', knob: { pointer: '#000', spin: 1 } } }, css: 'x.js' };
r = u.readSkinJson(odd, (n) => n === 'red.png');
const w = r.warnings.join(' | ');
check(r.skin.ui.accent === '#ff00aa' && r.skin.ui.fg === undefined && r.skin.tiles.red.image === 'red.png' && r.skin.tiles.red.knob.pointer === '#000',
  'good parts kept: accent, tile image and colours');
check(['ui.fg', 'ui.wobble', 'sparkles', 'stage.background: missing.png', 'stage.fogDensity', 'tiles.red.knob.spin', 'css'].every((k) => w.includes(k)),
  `every problem named: ${r.warnings.length} notes (“${r.warnings[0]}” …)`);
const merged = u.mergeSkin(u.DEFAULT_SKIN, r.skin), st = u.tileStyle(merged, 'red');
check(st.knob.pointer === '#000' && st.knob.track === u.DEFAULT_SKIN.knob.track && st.card.title === u.DEFAULT_SKIN.card.title && merged.stage.background === 'hall.webp',
  'a tile’s knob colours override the skin’s; a broken background falls back to the default');

r = u.readSkinJson({ format: 'rig-skin', version: 1, name: 'Tuned', tuner: { tile: 'gold', stripe: '#ff00aa', flat: 'not a colour!', tile2: 'x' } });
const tw = r.warnings.join(' | '), tm = u.mergeSkin(u.DEFAULT_SKIN, r.skin);
check(tm.tuner.tile === 'gold' && tm.tuner.stripe === '#ff00aa' && tm.tuner.flat === u.DEFAULT_SKIN.tuner.flat && tm.tuner.back === u.DEFAULT_SKIN.tuner.back
  && tw.includes('tuner.flat') && tw.includes('tuner.tile2') && u.tileStyle(tm, tm.tuner.tile).image === 'tile-gold.webp',
  'tuner: tile and colours taken, a bad colour and an unknown key named, the rest from the default');
check(u.tileStyle(classic, classic.tuner.tile).image === null && classic.tuner.back === '#0C1220', 'Classic’s tuner sits on its plain console card with a dark back');

console.log('.rigskin packets (zip)');
const json = { format: 'rig-skin', version: 1, name: 'Packed', author: 'Test', ui: { accent: '#ffcc00' }, stage: { background: 'bg.png' }, tiles: { one: { image: 'one.png', accent: '#123456' } }, tileOrder: ['one'] };
let zip = u.zipFiles([['skin.json', enc(JSON.stringify(json))], ['bg.png', png], ['one.png', png]]);
let p = await u.readSkinPacket(zip);
check(!p.error && p.warnings.length === 0 && p.skin.ui.accent === '#ffcc00' && p.files.get('bg.png').length === png.length && u.skinFiles(p.skin).join() === 'bg.png,one.png', 'written and read back: settings and both images');
zip = u.zipFiles([['My Skin/skin.json', enc(JSON.stringify(json))], ['My Skin/bg.png', png], ['My Skin/one.png', png]]);
p = await u.readSkinPacket(zip);
check(!p.error && p.files.has('one.png'), 'skin.json inside one folder (how zipping a folder packs it)');
// a deflated zip, as Windows "Send to > Compressed folder", macOS Compress or `zip` write them
const deflated = (() => {
  const files = [['skin.json', enc(JSON.stringify(json))], ['bg.png', png], ['one.png', png]], loc = [], cen = []; let off = 0;
  for (const [name, data] of files) {
    const nm = enc(name), comp = zlib.deflateRawSync(Buffer.from(data)), h = Buffer.alloc(30);
    h.writeUInt32LE(0x04034b50, 0); h.writeUInt16LE(20, 4); h.writeUInt16LE(8, 8); h.writeUInt32LE(u.crc32(data), 14); h.writeUInt32LE(comp.length, 18); h.writeUInt32LE(data.length, 22); h.writeUInt16LE(nm.length, 26);
    loc.push(h, Buffer.from(nm), comp);
    const c = Buffer.alloc(46); c.writeUInt32LE(0x02014b50, 0); c.writeUInt16LE(8, 10); c.writeUInt32LE(u.crc32(data), 16); c.writeUInt32LE(comp.length, 20); c.writeUInt32LE(data.length, 24); c.writeUInt16LE(nm.length, 28); c.writeUInt32LE(off, 42);
    cen.push(c, Buffer.from(nm)); off += 30 + nm.length + comp.length;
  }
  const cd = Buffer.concat(cen), e = Buffer.alloc(22); e.writeUInt32LE(0x06054b50, 0); e.writeUInt16LE(files.length, 8); e.writeUInt16LE(files.length, 10); e.writeUInt32LE(cd.length, 12); e.writeUInt32LE(off, 16);
  return new Uint8Array(Buffer.concat([...loc, cd, e]));
})();
p = await u.readSkinPacket(deflated);
check(!p.error && p.skin.name === 'Packed' && p.files.get('one.png').every((b, i) => b === png[i]), 'deflate-compressed zip read correctly');
check((await u.readSkinPacket(enc('hello'))).error === 'not a zip file', 'not a zip: refused');
check(/no skin.json/.test((await u.readSkinPacket(u.zipFiles([['readme.txt', enc('hi')]]))).error), 'zip without skin.json: refused');
p = await u.readSkinPacket(u.zipFiles([['skin.json', enc(JSON.stringify(json))], ['bg.png', png]]));
check(!p.error && p.warnings.some((x) => x.includes('one.png isn\'t in the skin')), `a missing image is named: “${p.warnings[0]}”`);
done('skin');

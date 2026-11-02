// tests/ui/load.mjs - load selected ui/js files into a sandbox (no browser) for unit tests.
import fs from 'fs';
import vm from 'vm';
const FILES = ['00-core.js', '10-model.js', '52-vfs.js', '53-assets.js', '54-library-core.js', '72-preset-format.js'];
const EXPORTS = ['state', 'DEPTH', 'MODS', 'PARAMS', 'SRCMAP', 'loadModel', 'extOf', 'kindForName', 'parseWavInfo', 'parseNamMeta', 'metaLine',
  'matchesQuery', 'uniqueName', 'safeFileName', 'fnv1a', 'KvDir', 'memStore', 'LibraryCore', 'buildPreset', 'readPreset', 'currentSnapshot',
  'PRESET_VERSION', 'PRESET_RENAMES'];
export async function loadUi() {
  const stubEl = { textContent: '', appendChild() {}, removeChild() {}, childNodes: [], scrollTop: 0 };
  const ctx = vm.createContext({
    console, TextDecoder, TextEncoder, WebAssembly, Blob, File, DOMException, atob, Math, JSON, Date, Number, Object, Array, Map, Set, Promise,
    matchMedia: () => ({ matches: false }), document: { getElementById: () => stubEl, createElement: () => ({}) },
    RIG_WASM_B64: fs.readFileSync('build/rig.wasm').toString('base64'),
  });
  const code = FILES.map((f) => fs.readFileSync('ui/js/' + f, 'utf8')).join('\n') + `\nglobalThis.__x = { ${EXPORTS.map((n) => `${n}: typeof ${n} === 'undefined' ? undefined : ${n}`).join(', ')}, get MODS() { return MODS; }, get PARAMS() { return PARAMS; }, get SRCMAP() { return SRCMAP; } };`;
  vm.runInContext(code, ctx, { filename: 'ui-bundle.js' });
  return ctx.__x;
}
let fails = 0;
export const check = (ok, msg) => { console.log(`  [${ok ? 'PASS' : 'FAIL'}] ${msg}`); if (!ok) fails++; };
export const done = (label) => { console.log(fails ? `${label}: ${fails} FAILED` : `${label}: all passed`); if (fails) process.exit(1); };
// WAV writer for tests: { sr, ch, bits: 16 | 32 (float), frames, extraChunk }
export function wav({ sr = 48000, ch = 1, bits = 16, frames = 1024, extraChunk = false, fill = (i) => (i === 0 ? 0.5 : 0) }) {
  const bps = bits / 8, dataBytes = frames * ch * bps, extra = extraChunk ? 8 + 10 : 0;
  const b = Buffer.alloc(44 + extra + dataBytes); let o = 0;
  const str = (s) => { b.write(s, o, 'ascii'); o += 4; }, u32 = (v) => { b.writeUInt32LE(v, o); o += 4; }, u16 = (v) => { b.writeUInt16LE(v, o); o += 2; };
  str('RIFF'); u32(b.length - 8); str('WAVE'); str('fmt '); u32(16); u16(bits === 32 ? 3 : 1); u16(ch); u32(sr); u32(sr * ch * bps); u16(ch * bps); u16(bits);
  if (extraChunk) { str('LIST'); u32(10); o += 10; }
  str('data'); u32(dataBytes);
  for (let i = 0; i < frames; i++) for (let c = 0; c < ch; c++) { const v = fill(i); if (bits === 32) { b.writeFloatLE(v, o); o += 4; } else { b.writeInt16LE(Math.round(v * 32767), o); o += 2; } }
  return new Uint8Array(b);
}
export const nam = (md = {}, arch = 'WaveNet') => JSON.stringify({ version: '0.5.2', architecture: arch, config: {}, sample_rate: 48000, metadata: md, weights: [0.1, 0.2, 0.3] });

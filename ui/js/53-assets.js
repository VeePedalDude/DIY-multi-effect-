// 53-assets.js - pure helpers for library files: no DOM, no storage (unit-tested in Node).
// File kinds come from the engine (each module's asset slot) plus 'presets'.

function extOf(name) { const n = name.toLowerCase(); if (n.endsWith('.rig.json')) return '.rig.json'; const i = n.lastIndexOf('.'); return i < 0 ? '' : n.slice(i); }

// kinds: { ir: ['.wav'], nam: ['.nam'], presets: ['.rig.json'] } -> kind for a file name, or null
function kindForName(name, kinds) { const e = extOf(name); for (const k in kinds) if (kinds[k].includes(e)) return k; return null; }

// Reads a WAV header (PCM, float or extensible). Returns null if it isn't a WAV.
function parseWavInfo(buf) {
  const v = new DataView(buf); if (buf.byteLength < 12) return null;
  const tag = (o) => String.fromCharCode(v.getUint8(o), v.getUint8(o + 1), v.getUint8(o + 2), v.getUint8(o + 3));
  if (tag(0) !== 'RIFF' || tag(8) !== 'WAVE') return null;
  let o = 12, fmt = null, dataBytes = null;
  while (o + 8 <= buf.byteLength) {
    const id = tag(o), size = v.getUint32(o + 4, true);
    if (id === 'fmt ' && o + 24 <= buf.byteLength) fmt = { format: v.getUint16(o + 8, true), channels: v.getUint16(o + 10, true), sampleRate: v.getUint32(o + 12, true), bits: v.getUint16(o + 22, true) };
    if (id === 'data') { dataBytes = Math.min(size, buf.byteLength - o - 8); break; }
    o += 8 + size + (size & 1);
  }
  if (!fmt || dataBytes === null || !fmt.channels || !fmt.bits) return null;
  const frames = Math.floor(dataBytes / (fmt.channels * fmt.bits / 8));
  return { ...fmt, frames, ms: fmt.sampleRate ? 1000 * frames / fmt.sampleRate : 0 };
}

// Summary of a .nam capture (Neural Amp Modeler JSON). Returns null if it doesn't parse.
function parseNamMeta(text) {
  let j; try { j = JSON.parse(text); } catch (e) { return null; }
  if (!j || typeof j !== 'object' || !('architecture' in j || 'weights' in j)) return null;
  const md = j.metadata || {}, gear = [md.gear_make, md.gear_model].filter(Boolean).join(' ');
  return { version: j.version || '', architecture: j.architecture || 'unknown', sampleRate: j.sample_rate || null,
    name: md.name || '', modeledBy: md.modeled_by || '', gear, gearType: md.gear_type || '', toneType: md.tone_type || '', loudness: md.loudness ?? null };
}

function metaLine(e) {
  const m = e.meta;
  if (!m) return `${(e.size / 1024).toFixed(0)} KB`;
  if (e.kind === 'ir') return `${(m.sampleRate / 1000).toFixed(1)} kHz · ${m.channels} ch · ${m.ms.toFixed(0)} ms`;
  if (e.kind === 'nam') return [m.architecture, m.gear || m.name, m.toneType, m.modeledBy && 'by ' + m.modeledBy].filter(Boolean).join(' · ');
  return `${(e.size / 1024).toFixed(1)} KB`;
}
function searchText(e) {
  const m = e.meta || {};
  return [e.name, m.name, m.modeledBy, m.gear, m.gearType, m.toneType, m.architecture].filter(Boolean).join(' ').toLowerCase();
}
// Every word of the query must appear somewhere in the file's name or details.
function matchesQuery(e, q) { const words = q.toLowerCase().split(/\s+/).filter(Boolean); const t = searchText(e); return words.every((w) => t.includes(w)); }

// "Mesa V30.wav" taken -> "Mesa V30 (2).wav"
function uniqueName(taken, name) {
  if (!taken.has(name)) return name;
  const e = extOf(name), base = name.slice(0, name.length - e.length);
  for (let i = 2; ; i++) { const n = `${base} (${i})${e}`; if (!taken.has(n)) return n; }
}
// File names must be safe on Windows, macOS and Linux.
function safeFileName(name) { return name.replace(/[\\/:*?"<>|\u0000-\u001f]/g, '_').replace(/^\.+/, '').slice(0, 120) || 'untitled'; }

// FNV-1a 32-bit content hash (hex): identifies a file even if it is renamed.
function fnv1a(bytes) { let h = 0x811c9dc5; for (let i = 0; i < bytes.length; i++) { h ^= bytes[i]; h = Math.imul(h, 0x01000193) >>> 0; } return h.toString(16).padStart(8, '0'); }

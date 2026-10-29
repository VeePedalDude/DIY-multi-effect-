// 00-core.js - shared constants, helpers and UI state.
// All ui/js files and ui/screens files are concatenated into one scope by tools/build_web.py
// (ui/js in name order, ui/screens inserted before 50-*), so top-level names must be unique.
const ACC = '#4FE3F0';
const RM = matchMedia('(prefers-reduced-motion: reduce)').matches;
const clamp = (x, a, b) => Math.max(a, Math.min(b, x));
const $ = (id) => document.getElementById(id);
const DEPTH = 0.22;                       // modulation swing per patch, +/- in normalised units

// Orb colours per source id; sources without an entry take the next fallback colour.
const SOURCE_COLORS = { lfo: '#4FE3F0', env: '#FFB547', adsr: '#FF5FA2', orbit: '#A78BFA', heart: '#FF7A6B', ctrl: '#9BE15D' };
const FALLBACK_COLORS = ['#7DD3FC', '#F9A8D4', '#FDE68A', '#86EFAC', '#C4B5FD', '#FCA5A5'];

// Per-module face screens: ui/screens/<id>.js calls screen('<id>', fn). Modules without one get a generic screen.
const SCREENS = {};
const NOTES = {};                         // optional focus-panel note per module id, set from ui/screens/<id>.js
const TILE = {};                          // optional card colour per module id ('purple' | 'teal' | 'gold'), set from ui/screens/<id>.js
const MODDATA = {};                       // per-module UI data shared between panels and screens (e.g. MODDATA.cab.irResp)
function screen(id, fn) { SCREENS[id] = fn; }

const state = {
  carrying: null, focus: null, view: 'stage', bpm: 120, master: -12,
  conns: { 'amp.input': 'env', 'holo.feedback': 'lfo', 'holo.halo': 'orbit', 'tess.fold': 'heart' },
  bypass: {}, scenes: {}, scene: null, storing: false, ctrl: 0,
  depths: {},                             // per-patch swing; missing = DEPTH
  assets: {},                             // module id -> { kind, name, size, hash } chosen from the library
  presetExtras: [],                       // chain entries for modules this build doesn't have, kept so re-saving loses nothing
};
const depthOf = (pid) => state.depths[pid] ?? DEPTH;
let meters = null;                        // latest engine meter packet
let T = 0;                                // animation clock, seconds

function logLine(text, err) {
  const el = $('log'); const d = document.createElement('div'); d.textContent = text; if (err) d.className = 'err';
  el.appendChild(d); while (el.childNodes.length > 200) el.removeChild(el.firstChild); el.scrollTop = el.scrollHeight;
}
function b64ToBytes(b64) { const s = atob(b64); const u = new Uint8Array(s.length); for (let i = 0; i < s.length; i++) u[i] = s.charCodeAt(i); return u; }

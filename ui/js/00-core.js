// 00-core.js - shared constants, helpers and UI state.
// All ui/js files and ui/screens files are concatenated into one scope by tools/build_web.py
// (ui/js in name order, ui/screens inserted before 50-*), so top-level names must be unique.
let ACC = '#4FE3F0';                      // face-screen accent; set by the active skin (04-skin-core.js, 57-skins.js)
const RM = matchMedia('(prefers-reduced-motion: reduce)').matches;
const clamp = (x, a, b) => Math.max(a, Math.min(b, x));
const $ = (id) => document.getElementById(id);
const DEPTH = 0.22;                       // modulation swing per patch, +/- in normalised units


// Per-module-type face screens: ui/screens/<type>.js calls screen('<type>', fn); every instance of the type uses it. Types without one get a generic screen.
const SCREENS = {};
const NOTES = {};                         // optional focus-panel note per module type, set from ui/screens/<type>.js
const MODDATA = {};                       // per-module UI data shared between panels and screens (e.g. MODDATA.cab.irResp)
function screen(id, fn) { SCREENS[id] = fn; }

const state = {
  carrying: null, focus: null, view: 'stage', bpm: 120, master: -12,
  conns: { 'amp.input': 'env', 'holo.feedback': 'lfo', 'holo.halo': 'orbit', 'tess.fold': 'heart' },
  bypass: {}, scenes: {}, scene: null, storing: false, ctrl: 0,
  inputs: {},                             // CONTROLLER instance id -> its hardware binding (12-inputs-core.js)
  depths: {},                             // per-patch swing; missing = DEPTH
  taps: {},                               // per-patch PHASE (degrees) and DIV, for sources that tap (an LFO); missing = 0 degrees, 1x
  assets: {},                             // module id -> { kind, name, size, hash } chosen from the library
  presetExtras: [],                       // chain entries for modules this build doesn't have, kept so re-saving loses nothing
  sourceExtras: [],                       // the same for sources of a type this build doesn't have
};
const depthOf = (pid) => state.depths[pid] ?? DEPTH;
const tapOf = (pid) => state.taps[pid] || { phase: 0, div: 1 };
let meters = null;                        // latest engine meter packet
let T = 0;                                // animation clock, seconds

function logLine(text, err) {
  const el = $('log'); const d = document.createElement('div'); d.textContent = text; if (err) d.className = 'err';
  el.appendChild(d); while (el.childNodes.length > 200) el.removeChild(el.firstChild); el.scrollTop = el.scrollHeight;
}
function b64ToBytes(b64) { const s = atob(b64); const u = new Uint8Array(s.length); for (let i = 0; i < s.length; i++) u[i] = s.charCodeAt(i); return u; }

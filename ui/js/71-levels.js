// 71-levels.js - how loud the rig plays: the PATCH level (each preset's own, saved in it) and the MASTER
// (the rig's, kept in this browser), the level meters under them, and Level, which sets a preset's level
// for you. The engine measures both stages (engine.h LevelMeter): the patch after its level and before
// the master, and the output after the master and the limiter, which also says how hard the limiter
// is pulling (it should be 0 most of the time; a few dB on peaks is fine, more is squashing).
//  - Level: play for LEVEL_SECS; the patch's loudness while you were playing (the 80th percentile of
//    its 300 ms RMS, above LEVEL_FLOOR, so pauses don't count) is brought to LEVEL_TARGET. Every preset
//    levelled this way plays about as loud as the others, and the master sets how loud they all are.
const LEVEL_TARGET = -18, LEVEL_FLOOR = -60, LEVEL_SECS = 5;      // dBFS RMS; quieter than the floor is a pause
const fmtDb = (db) => `${db > 0 ? '+' : ''}${Number.isInteger(db) ? db : db.toFixed(1)} dB`;
const lvl = { busy: false, vals: [], t0: 0 };
function levelReading() {
  if (!meters || !LAYOUT || LAYOUT.lvl === undefined) return null;
  const m = meters.m, o = LAYOUT.lvl;
  return { patchPk: m[o], patchRms: m[o + 1], outPk: m[o + 2], outRms: m[o + 3], limit: m[o + 4] };
}
// Every few frames (90-main.js): the readouts, and Level's listening.
function drawLevels() {
  const r = levelReading(); if (!r) return;
  const show = (el, rms, pk) => { el.textContent = rms <= -119 ? '–' : `${rms.toFixed(1)} rms · ${pk.toFixed(1)} pk`; };
  show($('lvPatch'), r.patchRms, r.patchPk);
  show($('lvOut'), r.outRms, r.outPk);
  if (r.limit >= 0.5) $('lvOut').textContent += ` · limiting ${r.limit.toFixed(1)}`;
  $('lvOut').className = r.limit >= 3 ? 'clip' : r.limit >= 0.5 ? 'hot' : '';
  $('lvPatch').className = r.patchPk > 0 ? 'clip' : r.patchPk > -1 ? 'hot' : '';
  if (lvl.busy) {
    if (meters !== lvl.last) { lvl.last = meters; if (r.patchRms > LEVEL_FLOOR) lvl.vals.push(r.patchRms); }
    const left = LEVEL_SECS - (performance.now() - lvl.t0) / 1000;
    if (left > 0) { $('autoLevel').textContent = `Play… ${Math.ceil(left)}`; return; }
    levelDone();
  }
}
function levelStart() {
  if (!rig.node && !HOST) { flashHint('Start the engine first, then play while Level listens'); return; }
  Object.assign(lvl, { busy: true, vals: [], t0: performance.now(), last: null });
  $('autoLevel').classList.add('busy'); $('autoLevel').setAttribute('aria-pressed', 'true');
}
// -> the new level, or null if there wasn't enough playing to go on (also used by the tests)
function levelFrom(vals, level) {
  if (vals.length < 15) return null;                                   // ~0.5 s of playing at ~31 meter packets a second
  const s = vals.slice().sort((a, b) => a - b), loud = s[Math.min(s.length - 1, Math.floor(0.8 * s.length))];
  return { level: clamp(Math.round((level + LEVEL_TARGET - loud) * 2) / 2, -24, 24), measured: loud };
}
function levelDone() {
  lvl.busy = false; $('autoLevel').textContent = 'Level'; $('autoLevel').classList.remove('busy'); $('autoLevel').setAttribute('aria-pressed', 'false');
  const r = levelFrom(lvl.vals, state.level);
  if (!r) { flashHint('Level heard too little: press it again and play for the whole five seconds'); return; }
  const was = state.level; setLevel(r.level); updateEdited();
  const capped = Math.abs(r.level) === 24 ? ' (as far as it goes: check the gain inside the chain)' : '';
  flashHint(`Patch level ${fmtDb(was)} → ${fmtDb(r.level)}: it played at ${r.measured.toFixed(1)} dB RMS, now about ${(r.measured + r.level - was).toFixed(1)}${capped}. Save to keep it.`);
}
function wireLevels() {
  $('autoLevel').onclick = () => (lvl.busy ? levelDone() : levelStart());
  $('autoLevel').setAttribute('aria-pressed', 'false');
  $('levelVal').textContent = fmtDb(state.level);
}
rig.levels = { reading: levelReading, from: levelFrom, state: lvl, target: LEVEL_TARGET };

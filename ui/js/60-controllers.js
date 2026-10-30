// 60-controllers.js - outside-world inputs feeding sources.
//  - USB gamepads, joysticks, flight sticks and HOTAS (the Gamepad API: every axis, hat and button) and MIDI
//    inputs (Web MIDI: CC, pitch bend, keys, aftertouch) bound to CONTROLLER instances, one control each
//    (state.inputs; reading and LEARN are in 12-inputs-core.js). Unbound instances read nothing; the bottom
//    bar's CONTROLLER slider is the on-screen control for `ctrl`.
//  - A Bluetooth heart strap -> `heart`.
// Bindings are kept with presets and in this browser (localStorage), by device name, so they come back
// when the device does. Every optional browser feature fails softly (an app preview can block gamepads,
// MIDI and Bluetooth).
const INPUTS = { pads: [], midi: null, midiState: 'off', midiNote: '', padBlocked: false, learning: null, learnBase: null, learnUntil: 0,
  toggles: {}, lastRaw: {}, raw: {}, pending: {}, sentAt: {}, sentVal: {} };
const INPUT_STORE = 'rig01.inputs', MIDI_STORE = 'rig01.midi';
const ctrlInstances = () => SRC.filter((s) => s.type === 'ctrl');
function saveInputs() { try { localStorage.setItem(INPUT_STORE, JSON.stringify(state.inputs)); } catch (e) { /* storage blocked: presets still keep them */ } }
function loadInputs() {
  try { const j = JSON.parse(localStorage.getItem(INPUT_STORE) || '{}'); for (const [id, b] of Object.entries(j)) { const c = sanitizeBinding(b); if (c) state.inputs[id] = c; } } catch (e) { /* none saved */ }
}
function bindInput(id, b) { if (b) state.inputs[id] = b; else delete state.inputs[id]; delete INPUTS.toggles[id]; delete INPUTS.lastRaw[id]; delete INPUTS.raw[id]; saveInputs(); }

// A value for a CONTROLLER instance; sent at most every 25 ms (the latest one always goes out).
function queueCtrl(id, v) { if (v === null) return; INPUTS.pending[id] = v; if (id === 'ctrl') { state.ctrl = v; $('ctrlVal').textContent = v.toFixed(2); $('ctrlSlider').value = v; } }
function flushCtrl() {
  const now = performance.now();
  for (const [id, v] of Object.entries(INPUTS.pending)) {
    if (!SRCMAP[id]) { delete INPUTS.pending[id]; continue; }
    if (now - (INPUTS.sentAt[id] || 0) < 25) continue;
    if (INPUTS.sentVal[id] !== v) rig.cmd(`ext ${id} ${v.toFixed(3)}`);
    INPUTS.sentAt[id] = now; INPUTS.sentVal[id] = v; delete INPUTS.pending[id];
  }
}
function setCtrl(v) { queueCtrl('ctrl', v); }                 // the bottom bar's slider
// One raw reading of a bound control -> its value (with toggle edges for buttons and keys).
function feed(id, bind, raw) {
  INPUTS.raw[id] = raw;
  if (isPress(bind) && bind.mode === 'toggle' && raw > 0.5 && !((INPUTS.lastRaw[id] ?? 0) > 0.5)) INPUTS.toggles[id] = !INPUTS.toggles[id];
  INPUTS.lastRaw[id] = raw;
  queueCtrl(id, inputValue(bind, raw, INPUTS.toggles[id]));
}
function learnDone(b) {
  const id = INPUTS.learning; INPUTS.learning = null; if (!id || !SRCMAP[id]) return;
  bindInput(id, newBinding(b)); logLine(`${SRCMAP[id].name} ← ${describeBinding(state.inputs[id])}`); update();
}
function startLearn(id) { INPUTS.learning = id; INPUTS.learnBase = padSnapshot(readPads()); INPUTS.learnUntil = performance.now() + 15000; }

// ---------------------------------------------------------------- gamepads, joysticks, flight sticks
function readPads() {
  if (INPUTS.padBlocked || !navigator.getGamepads) return [];
  try { const p = navigator.getGamepads(); return p ? Array.from(p).filter(Boolean) : []; }
  catch (e) { INPUTS.padBlocked = true; $('padNote').textContent = 'Gamepads are blocked where this page is open. Open the file directly in Chrome to use one; the CONTROLLER slider still works.'; logLine('gamepad blocked: ' + e.message, true); return []; }
}
// 125 times a second, on its own timer (not the drawing loop, so a quick tap is never missed when frames
// are slow or the page is hidden): read the pads (the Gamepad API has no events for values), LEARN,
// send what changed.
let padPolls = 0;
function pollPad() {
  const pads = readPads(); INPUTS.pads = pads;
  if (INPUTS.learning) {
    if (performance.now() > INPUTS.learnUntil) { INPUTS.learning = null; update(); }
    else { const b = padLearn(INPUTS.learnBase, pads); if (b) learnDone(b); }
  }
  for (const s of ctrlInstances()) {
    const b = state.inputs[s.id]; if (!b || b.kind !== 'pad') continue;
    const pad = pads.find((p) => p.id === b.device), raw = padRaw(pad, b);
    if (raw === null) { INPUTS.raw[s.id] = null; continue; }
    if (raw !== INPUTS.raw[s.id]) feed(s.id, b, raw);
  }
  flushCtrl();
  if (padPolls++ % 125 === 0 && !INPUTS.padBlocked) $('padNote').textContent = inputSummary();
}

// ---------------------------------------------------------------- MIDI
function onMidi(input, ev) {
  const m = midiDecode(ev.data); if (!m) return;
  if (INPUTS.learning) { const b = midiLearn(input.name, m); if (b) { learnDone(b); return; } }
  for (const s of ctrlInstances()) { const b = state.inputs[s.id]; if (midiMatches(b, input.name, m)) feed(s.id, b, m.raw); }
  flushCtrl();
}
function attachMidi() {
  if (!INPUTS.midi) return;
  INPUTS.midi.inputs.forEach((input) => { input.onmidimessage = (ev) => onMidi(input, ev); });
}
async function enableMidi(quiet) {
  if (INPUTS.midi) return true;
  if (!navigator.requestMIDIAccess) { INPUTS.midiState = 'none'; INPUTS.midiNote = 'This browser has no Web MIDI. Use Chrome or Edge.'; return false; }
  try {
    INPUTS.midi = await navigator.requestMIDIAccess({ sysex: false }); INPUTS.midiState = 'on'; INPUTS.midiNote = '';
    INPUTS.midi.onstatechange = () => { attachMidi(); update(); };
    attachMidi(); try { localStorage.setItem(MIDI_STORE, '1'); } catch (e) { /* not remembered */ }
    return true;
  } catch (e) {
    INPUTS.midiState = 'blocked';
    INPUTS.midiNote = /permission|policy|denied|security/i.test(e.name + ' ' + e.message) ? 'MIDI is blocked here (or permission was refused). Open the file directly in Chrome and allow MIDI.' : 'MIDI unavailable: ' + e.message;
    if (!quiet) logLine(INPUTS.midiNote, true);
    return false;
  }
}
const midiNames = () => (INPUTS.midi ? Array.from(INPUTS.midi.inputs.values(), (i) => i.name) : []);
// Is this binding's device here now?
function inputPresent(b) { return !!b && (b.kind === 'pad' ? INPUTS.pads.some((p) => p.id === b.device) : midiNames().includes(b.device)); }
function inputSummary() {
  const pads = INPUTS.pads.map((p) => p.id.replace(/\s*\(.*?Vendor.*?\)\s*/i, ' ').trim()), midi = midiNames();
  return `Gamepads and joysticks: ${pads.length ? pads.join(', ') : 'none (press a button on one to wake it)'} · MIDI: ${INPUTS.midi ? (midi.length ? midi.join(', ') : 'no inputs') : 'off'}. Open a CONTROLLER source and press LEARN to use one.`;
}

function wireControllers() {
  loadInputs();
  setInterval(() => { try { pollPad(); } catch (e) { logLine('controllers: ' + e.message, true); } }, 8);
  try { if (localStorage.getItem(MIDI_STORE) === '1') enableMidi(true); } catch (e) { /* storage blocked */ }
  RACK_HOOKS.push(() => { let gone = false; Object.keys(state.inputs).forEach((id) => { if (!SRCMAP[id] || SRCMAP[id].type !== 'ctrl') { delete state.inputs[id]; gone = true; } }); if (gone) saveInputs(); });
  window.addEventListener('gamepadconnected', () => update()); window.addEventListener('gamepaddisconnected', () => update());
  if (!SRCMAP.heart) { $('heartBtn').hidden = true; $('heartNote').hidden = true; }
  $('heartBtn').onclick = async () => {
    if (!navigator.bluetooth) { $('heartNote').textContent = 'This browser has no Web Bluetooth. Use Chrome or Edge on Windows, Mac or Android.'; return; }
    try {
      const dev = await navigator.bluetooth.requestDevice({ filters: [{ services: ['heart_rate'] }] });
      const ch = await (await (await dev.gatt.connect()).getPrimaryService('heart_rate')).getCharacteristic('heart_rate_measurement');
      await ch.startNotifications();
      ch.addEventListener('characteristicvaluechanged', (ev) => { const v = ev.target.value, bpm = (v.getUint8(0) & 1) ? v.getUint16(1, true) : v.getUint8(1); rig.cmd(`ext heart ${bpm}`); $('heartNote').textContent = `Heart source: ${dev.name || 'strap'} · ${bpm} bpm`; SRCMAP.heart.sub = `${bpm} bpm · live`; });
      $('heartNote').textContent = `Connected to ${dev.name || 'strap'}; waiting for the first beat…`;
    } catch (err) { $('heartNote').textContent = /permissions policy/i.test(err.message) ? 'Bluetooth is blocked where this page is open. Open the file directly in Chrome.' : 'Strap not connected: ' + err.message; }
  };
}
rig.inputs = { state: () => ({ inputs: state.inputs, learning: INPUTS.learning, midi: INPUTS.midiState, pads: INPUTS.pads.map((p) => p.id), midiInputs: midiNames(), raw: { ...INPUTS.raw } }), learn: startLearn, enableMidi };

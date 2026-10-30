// 12-inputs-core.js - outside controls (USB gamepads and joysticks, MIDI) as values for CONTROLLER
// sources. Pure functions, no DOM or devices (unit-tested in Node); 60-controllers.js does the devices.
//
// A binding ties one CONTROLLER instance to one control on one device:
//   { kind: 'pad',  device: '<Gamepad.id>',    type: 'axis' | 'button', num }
//   { kind: 'midi', device: '<MIDIInput.name>', type: 'cc' | 'pb' | 'note' | 'at' | 'pat', num, ch }   (ch 1-16, 0 = any)
//   plus how to read it: invert (bool), range 'full' (-1..+1) | 'pos' (0..+1), mode 'momentary' | 'toggle'
//   (buttons and keys). Devices are found by name, so a binding comes back when its device is plugged in.
// Raw values: an axis -1..+1 (centred), a button 0..1, a CC 0..1 (value / 127), pitch bend -1..+1
// (centred), a key's velocity 0..1 while held (0 on release), aftertouch 0..1.

const INPUT_TYPES = { axis: 'axis', button: 'button', cc: 'CC', pb: 'pitch bend', note: 'note', at: 'aftertouch', pat: 'key pressure' };
const CENTRED = { axis: true, pb: true };                  // raw controls that rest in the middle
const isPress = (b) => b && (b.type === 'button' || b.type === 'note');

// One MIDI message -> { type, ch (1-16), num, raw } or null (clock, sysex... are ignored).
function midiDecode(d) {
  if (!d || d.length < 2) return null;
  const st = d[0] & 0xF0, ch = (d[0] & 0x0F) + 1, a = d[1] & 0x7F, b = d.length > 2 ? d[2] & 0x7F : 0;
  if (st === 0xB0) return { type: 'cc', ch, num: a, raw: b / 127 };
  if (st === 0xE0) { const v = (b << 7) | a; return { type: 'pb', ch, num: 0, raw: v === 8192 ? 0 : (v - 8192) / (v > 8192 ? 8191 : 8192) }; }
  if (st === 0x90) return { type: 'note', ch, num: a, raw: b / 127 };                 // velocity 0 = note off
  if (st === 0x80) return { type: 'note', ch, num: a, raw: 0 };
  if (st === 0xD0) return { type: 'at', ch, num: 0, raw: a / 127 };
  if (st === 0xA0) return { type: 'pat', ch, num: a, raw: b / 127 };
  return null;
}
const midiMatches = (bind, deviceName, m) => !!(bind && m && bind.kind === 'midi' && bind.device === deviceName && bind.type === m.type && bind.num === m.num && (!bind.ch || bind.ch === m.ch));

// A gamepad's control -> raw value (axis -1..+1, button 0..1), or null if it has no such control.
function padRaw(pad, bind) {
  if (!pad || !bind) return null;
  if (bind.type === 'axis') { const v = pad.axes && pad.axes[bind.num]; return typeof v === 'number' && isFinite(v) ? v : null; }
  const b = pad.buttons && pad.buttons[bind.num]; if (!b) return null;
  return typeof b === 'object' ? (typeof b.value === 'number' ? b.value : (b.pressed ? 1 : 0)) : Number(b) || 0;
}

// Raw -> the -1..+1 sent to the engine (`ext <id> <value>`), for this binding's range and inversion.
// `toggled` is the held state of a toggle button / key (60-controllers keeps it).
function inputValue(bind, raw, toggled) {
  if (raw === null || raw === undefined || !isFinite(raw)) return null;
  let u;                                                   // 0..1 across the control's travel
  if (isPress(bind) && bind.mode === 'toggle') u = toggled ? 1 : 0;
  else u = CENTRED[bind.type] ? (raw + 1) / 2 : raw;
  u = Math.min(1, Math.max(0, u));
  if (bind.invert) u = 1 - u;
  return bind.range === 'pos' ? u : 2 * u - 1;
}

// LEARN for gamepads: compare each pad with how it looked when LEARN started; the first control that
// moves far enough (an axis by 0.5 of its travel, a button pressed past halfway) is the one.
function padSnapshot(pads) { return (pads || []).filter(Boolean).map((p) => ({ id: p.id, index: p.index, axes: Array.from(p.axes || []), buttons: Array.from(p.buttons || [], (b) => padRaw({ buttons: [b] }, { type: 'button', num: 0 })) })); }
function padLearn(before, pads) {
  for (const p of (pads || []).filter(Boolean)) {
    const b = before.find((x) => x.index === p.index && x.id === p.id); if (!b) continue;
    for (let i = 0; i < (p.axes || []).length; i++) if (Math.abs(p.axes[i] - (b.axes[i] ?? 0)) > 0.5) return { kind: 'pad', device: p.id, type: 'axis', num: i };
    for (let i = 0; i < (p.buttons || []).length; i++) { const v = padRaw(p, { type: 'button', num: i }); if (v > 0.5 && (b.buttons[i] ?? 0) <= 0.5) return { kind: 'pad', device: p.id, type: 'button', num: i }; }
  }
  return null;
}
// LEARN for MIDI: the first CC, bend, key or pressure message (a key's release doesn't count).
function midiLearn(deviceName, m) {
  if (!m || (m.type === 'note' && m.raw === 0)) return null;
  return { kind: 'midi', device: deviceName, type: m.type, num: m.num, ch: m.ch };
}
// A fresh binding reads -1..+1 like every other source, not inverted; buttons and keys are momentary.
function newBinding(b) { return b ? { ...b, invert: false, range: 'full', mode: 'momentary' } : null; }
// Clean a binding read from a preset or storage (null if it isn't one).
function sanitizeBinding(b) {
  if (!b || typeof b !== 'object' || typeof b.device !== 'string' || !b.device || !INPUT_TYPES[b.type]) return null;
  if (b.kind === 'pad' && !['axis', 'button'].includes(b.type)) return null;
  if (b.kind === 'midi' && ['axis', 'button'].includes(b.type)) return null;
  if (b.kind !== 'pad' && b.kind !== 'midi') return null;
  const num = Number.isInteger(b.num) && b.num >= 0 && b.num < 128 ? b.num : null; if (num === null) return null;
  const out = { kind: b.kind, device: b.device.slice(0, 120), type: b.type, num, invert: !!b.invert, range: b.range === 'pos' ? 'pos' : 'full', mode: b.mode === 'toggle' ? 'toggle' : 'momentary' };
  if (b.kind === 'midi') out.ch = Number.isInteger(b.ch) && b.ch >= 0 && b.ch <= 16 ? b.ch : 0;
  return out;
}
// "nanoKONTROL2 · CC 14 · ch 1", "Logitech Extreme 3D · axis 3"
function describeBinding(b) {
  if (!b) return 'not bound';
  const dev = b.device.replace(/\s*\(.*?Vendor.*?\)\s*/i, ' ').trim().slice(0, 40);
  const ctl = b.type === 'pb' || b.type === 'at' ? INPUT_TYPES[b.type] : `${INPUT_TYPES[b.type]} ${b.type === 'axis' || b.type === 'button' ? b.num + 1 : b.num}`;
  return `${dev} · ${ctl}${b.kind === 'midi' && b.ch ? ` · ch ${b.ch}` : ''}`;
}

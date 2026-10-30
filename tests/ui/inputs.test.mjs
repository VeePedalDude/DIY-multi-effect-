// tests/ui/inputs.test.mjs - reading USB gamepads / joysticks and MIDI for CONTROLLER sources (12-inputs-core.js),
// and bindings in preset files.
import { loadUi, check, done } from './load.mjs';
const u = await loadUi(); await u.loadModel();
const close = (a, b, e = 1e-9) => Math.abs(a - b) < e;

console.log('MIDI messages');
const cc = u.midiDecode([0xB0, 14, 127]), pb = u.midiDecode([0xE3, 0, 64]), pbLo = u.midiDecode([0xE0, 0, 0]), pbHi = u.midiDecode([0xE0, 127, 127]);
const on = u.midiDecode([0x92, 60, 100]), off0 = u.midiDecode([0x92, 60, 0]), off = u.midiDecode([0x82, 60, 40]), at = u.midiDecode([0xD0, 90]);
check(cc.type === 'cc' && cc.ch === 1 && cc.num === 14 && cc.raw === 1, 'CC 14 = 127 on ch 1 -> 1');
check(pb.type === 'pb' && pb.ch === 4 && pb.raw === 0 && pbLo.raw === -1 && pbHi.raw === 1, `pitch bend: centre 0 (ch 4), lowest -1, highest ${pbHi.raw}`);
check(on.type === 'note' && on.num === 60 && close(on.raw, 100 / 127) && off0.raw === 0 && off.raw === 0 && at.type === 'at' && close(at.raw, 90 / 127), 'keys: velocity while held, 0 on note-off (and note-on at velocity 0); channel aftertouch');
check(u.midiDecode([0xF8]) === null && u.midiDecode([0xF0, 1, 2, 0xF7]) === null, 'clock and sysex ignored');
const b = { kind: 'midi', device: 'nanoKONTROL2', type: 'cc', num: 14, ch: 1 };
check(u.midiMatches(b, 'nanoKONTROL2', cc) && !u.midiMatches(b, 'Other', cc) && !u.midiMatches(b, 'nanoKONTROL2', u.midiDecode([0xB1, 14, 3])) && u.midiMatches({ ...b, ch: 0 }, 'nanoKONTROL2', u.midiDecode([0xB1, 14, 3])),
  'a binding matches its device, control and channel (channel 0 = any)');

console.log('values');
const ax = { type: 'axis', range: 'full' }, ccb = { type: 'cc', range: 'full' };
check(u.inputValue(ax, -1) === -1 && u.inputValue(ax, 0) === 0 && u.inputValue(ax, 1) === 1 && u.inputValue({ ...ax, invert: true }, 0.5) === -0.5, 'axis -1..+1 as is; INVERT flips it');
check(u.inputValue({ ...ax, range: 'pos' }, 0) === 0.5 && u.inputValue(ccb, 0) === -1 && u.inputValue(ccb, 1) === 1 && u.inputValue({ ...ccb, range: 'pos' }, 0.25) === 0.25, 'RANGE 0..+1: a centred axis rests at 0.5; a fader 0..127 -> -1..+1 or 0..+1');
const btn = { type: 'button', range: 'full', mode: 'momentary' };
check(u.inputValue(btn, 0) === -1 && u.inputValue(btn, 1) === 1 && u.inputValue({ ...btn, mode: 'toggle' }, 0, true) === 1 && u.inputValue({ ...btn, mode: 'toggle' }, 1, false) === -1, 'buttons: momentary follows the press; toggle follows the held state');
check(u.inputValue(ax, NaN) === null && u.inputValue(ax, null) === null, 'no reading -> nothing sent');

console.log('gamepads and LEARN');
const pad = (axes, buttons) => ({ id: 'Logitech Extreme 3D (Vendor: 046d Product: c215)', index: 0, axes, buttons: buttons.map((v) => ({ value: v, pressed: v > 0.5 })) });
const before = u.padSnapshot([pad([0, 0, 0, -1], [0, 0, 0])]);
check(u.padLearn(before, [pad([0.1, -0.2, 0, -1], [0, 0.3, 0])]) === null, 'small wobbles and a half-pressed button are not learned');
const la = u.padLearn(before, [pad([0, 0, 0.7, -1], [0, 0, 0])]), lb = u.padLearn(before, [pad([0, 0, 0, -1], [0, 0, 1])]);
check(la && la.type === 'axis' && la.num === 2 && lb && lb.type === 'button' && lb.num === 2 && la.device.startsWith('Logitech'), `twist the stick -> ${u.describeBinding(la)}; press a button -> ${u.describeBinding(lb)}`);
check(u.padRaw(pad([0, 0, 0.25, -1], [0, 0, 1]), la) === 0.25 && u.padRaw(pad([0], [0, 0, 1]), lb) === 1 && u.padRaw(pad([0], []), lb) === null, 'reading: the axis, the button, and nothing when the control is missing');
check(u.midiLearn('Keys', on).type === 'note' && u.midiLearn('Keys', off0) === null, 'MIDI LEARN takes a key press, not a release');
check(u.describeBinding({ kind: 'midi', device: 'nanoKONTROL2', type: 'cc', num: 14, ch: 1 }) === 'nanoKONTROL2 · CC 14 · ch 1' && u.describeBinding({ kind: 'midi', device: 'K', type: 'pb', num: 0, ch: 0 }) === 'K · pitch bend', 'names: “nanoKONTROL2 · CC 14 · ch 1”, “K · pitch bend”');

console.log('bindings from files');
check(u.sanitizeBinding({ kind: 'pad', device: 'X', type: 'cc', num: 1 }) === null && u.sanitizeBinding({ kind: 'midi', device: '', type: 'cc', num: 1 }) === null && u.sanitizeBinding({ kind: 'midi', device: 'X', type: 'cc', num: 300 }) === null,
  'refused: a gamepad CC, no device name, a control number out of range');
const sb = u.sanitizeBinding({ kind: 'midi', device: 'X', type: 'cc', num: 7, ch: 3, invert: 1, range: 'pos', mode: 'odd', extra: 'x' });
check(sb.invert === true && sb.range === 'pos' && sb.mode === 'momentary' && sb.ch === 3 && !('extra' in sb), 'cleaned: options normalised, unknown fields dropped');
u.state.inputs = { ctrl: u.newBinding({ kind: 'pad', device: 'Thrustmaster T.16000M', type: 'axis', num: 3 }) };
const p = JSON.parse(JSON.stringify(u.buildPreset('Sticks'))), e = p.sources.find((x) => x.id === 'ctrl');
const r = u.readPreset(p);
check(e.input.device === 'Thrustmaster T.16000M' && e.input.num === 3 && r.inputs.ctrl.type === 'axis' && r.warnings.length === 0 && !p.sources.find((x) => x.id === 'lfo').input, 'a preset keeps the CONTROLLER’s binding, by device name, and reads it back');
const bad = JSON.parse(JSON.stringify(p)); bad.sources.find((x) => x.id === 'ctrl').input = { kind: 'usb', device: 'x' };
const rb = u.readPreset(bad);
check(!rb.inputs.ctrl && rb.warnings.some((w) => w.includes('left unbound')), `an unreadable binding is left out with a note: “${rb.warnings[0]}”`);
done('inputs');

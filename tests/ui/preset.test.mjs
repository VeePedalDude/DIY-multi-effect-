// tests/ui/preset.test.mjs - the .rig.json preset format against the real engine description
import fs from 'fs';
import { loadUi, check, done } from './load.mjs';
const u = await loadUi(); await u.loadModel();
const P = u.PARAMS, close = (a, b, eps = 1e-6) => Math.abs(a - b) < eps;

console.log('golden file (format 1, written by hand in real units)');
const golden = JSON.parse(fs.readFileSync('tests/fixtures/ambient-drive.v1.rig.json', 'utf8'));
let r = u.readPreset(golden);
check(!r.error && r.warnings.length === 0, `loads with no notes${r.warnings && r.warnings.length ? ': ' + r.warnings.join(' | ') : ''}`);
check(close(r.snap.params['holo.feedback'], 42.75 / 95) && close(r.snap.params['gate.thresh'], (-59 + 80) / 60) && close(r.snap.params['cab.locut'], 0.25, 1e-5), 'real units map back to the same knob positions');
check(r.snap.conns['holo.feedback'] === 'lfo' && r.snap.conns['amp.input'] === 'env' && close(r.snap.depths['tess.fold'], 0.3), 'modulations and their depths');
check(r.assets.cab && r.assets.cab.name === 'V30 SM57.wav' && r.assets.amp && r.assets.amp.kind === 'nam', 'file references for the cab IR and the NAM capture');
check(r.scenes.A && close(r.scenes.A.params['holo.halo'], 0.9) && r.master === -12 && r.snap.bpm === 120, 'scene A, master and tempo');

console.log('round trip');
Object.values(P).forEach((p, i) => { p.v = (i * 0.137) % 1; });
u.state.conns = { 'holo.halo': 'orbit', 'cab.mix': 'ctrl' }; u.state.depths = { 'cab.mix': 0.5 }; u.state.bypass = { amp: true };
u.state.assets = { cab: { kind: 'ir', name: 'x.wav', size: 10, hash: 'abcd1234' } }; u.state.scenes = { B: u.currentSnapshot() }; u.state.bpm = 97; u.state.master = -7;
const built = JSON.parse(JSON.stringify(u.buildPreset('Round Trip')));
r = u.readPreset(built);
const worst = Math.max(...Object.values(P).map((p) => Math.abs(r.snap.params[p.id] - p.v)));
check(r.warnings.length === 0 && worst < 1e-5, `every knob survives save + load (worst error ${worst.toExponential(1)})`);
check(r.snap.bypass.amp === true && r.snap.conns['cab.mix'] === 'ctrl' && r.snap.depths['cab.mix'] === 0.5 && r.snap.bpm === 97 && r.master === -7 && r.assets.cab.hash === 'abcd1234' && !!r.scenes.B, 'bypass, patches, depth, tempo, master, file reference, scene');
check(built.format === 'rig-preset' && built.version === u.PRESET_VERSION && typeof built.chain[0].params === 'object', `file says format rig-preset version ${built.version}`);

console.log('older and stranger files');
const old = JSON.parse(JSON.stringify(golden)); old.chain.find((c) => c.id === 'holo').params = { fb: 30, halo: 50 };
r = u.readPreset(old, { modules: {}, params: { 'holo.fb': 'holo.feedback' } });
check(r.warnings.length === 0 && close(r.snap.params['holo.feedback'], 30 / 95), 'a renamed knob loads through the rename table');
r = u.readPreset(old);
check(r.warnings.some((w) => w.includes('no knob called “fb”')), `without the rename it says so: “${r.warnings[0]}”`);
const odd = JSON.parse(JSON.stringify(golden)); odd.chain.push({ id: 'trem', bypass: false, params: { depth: 50 } }); odd.chain.find((c) => c.id === 'holo').params.feedback = 120;
r = u.readPreset(odd);
check(r.extras.length === 1 && r.extras[0].id === 'trem' && r.warnings.some((w) => w.startsWith('trem:')), 'unknown module kept (re-saving keeps it)');
check(r.snap.params['holo.feedback'] === 1 && r.warnings.some((w) => w.includes('120% is outside 0%–95%')), 'out-of-range value clamped with a note');
u.state.presetExtras = r.extras;
check(u.buildPreset('again').chain.some((c) => c.id === 'trem'), 're-saving writes the unknown module back');
check(u.readPreset({ ...golden, version: 9 }).warnings.some((w) => w.includes('newer version')), 'a newer file loads with a note');
check(u.readPreset({ hello: 1 }).error === 'not a RIG preset file', 'a non-preset file is refused');
done('preset');

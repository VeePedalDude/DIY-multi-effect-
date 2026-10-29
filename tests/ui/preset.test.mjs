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
check(Object.keys(r.snap.srcParams).length === 0 && Object.keys(r.scenes.A.srcParams).length === 0, 'migrated from format 1: modulation sources stay at their defaults (what format 1 meant)');

console.log('golden file (format 2: source knobs, written by hand in real units)');
const g2 = JSON.parse(fs.readFileSync('tests/fixtures/source-knobs.v2.rig.json', 'utf8'));
r = u.readPreset(g2);
const knob = (sid, sym) => u.SRCMAP[sid].params.find((q) => q.sym === sym);
const at = (snap, sid, sym) => knob(sid, sym).real(snap.srcParams[`${sid}.${sym}`]);
check(!r.error && r.warnings.length === 0, `loads with no notes${r.warnings && r.warnings.length ? ': ' + r.warnings.join(' | ') : ''}`);
check(close(at(r.snap, 'lfo', 'rate'), 2, 1e-4) && close(at(r.snap, 'lfo', 'shape'), 50, 1e-4) && close(at(r.snap, 'adsr', 'attack'), 120, 1e-3) && close(at(r.snap, 'env', 'floor'), -72, 1e-4),
  'LFO 2 beats / 50% shape, ADSR attack 120 ms, envelope floor -72 dB');
check(r.snap.srcParams['adsr.decay'] === undefined && close(at(r.scenes.B, 'lfo', 'rate'), 32, 1e-3), 'knobs left out stay default; scene B has its own LFO rate');
const g2odd = JSON.parse(JSON.stringify(g2)); g2odd.sources.push({ id: 'theremin', params: { pitch: 3 } }); g2odd.sources[0].params.rate = 1000;
r = u.readPreset(g2odd);
check(r.warnings.some((w) => w.includes('source theremin')) && r.warnings.some((w) => w.includes('lfo.rate: 1000 beats is outside')) && r.snap.srcParams['lfo.rate'] === 1, `unknown source and out-of-range knob: noted (“${r.warnings[0]}”)`);

console.log('round trip');
Object.values(P).forEach((p, i) => { p.v = (i * 0.137) % 1; });
u.SRC.forEach((s, i) => s.params.forEach((q, j) => { q.v = ((i + 1) * 0.211 + j * 0.37) % 1; }));
u.state.conns = { 'holo.halo': 'orbit', 'cab.mix': 'ctrl' }; u.state.depths = { 'cab.mix': 0.5 }; u.state.bypass = { amp: true };
u.state.assets = { cab: { kind: 'ir', name: 'x.wav', size: 10, hash: 'abcd1234' } }; u.state.scenes = { B: u.currentSnapshot() }; u.state.bpm = 97; u.state.master = -7;
const built = JSON.parse(JSON.stringify(u.buildPreset('Round Trip')));
r = u.readPreset(built);
const worst = Math.max(...Object.values(P).map((p) => Math.abs(r.snap.params[p.id] - p.v)));
check(r.warnings.length === 0 && worst < 1e-5, `every knob survives save + load (worst error ${worst.toExponential(1)})`);
const qs = u.SRC.flatMap((s) => s.params), worstSrc = Math.max(...qs.map((q) => Math.abs(r.snap.srcParams[q.id] - q.v)));
check(qs.length > 0 && worstSrc < 1e-5 && Object.keys(r.scenes.B.srcParams).length === qs.length, `every source knob survives too (${qs.length} knobs, worst error ${worstSrc.toExponential(1)}), scenes included`);
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

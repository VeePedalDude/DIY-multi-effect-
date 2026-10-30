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

console.log('golden file (format 3: a rack with two LFOs, a renamed orbit and cross-modulation)');
const g3 = JSON.parse(fs.readFileSync('tests/fixtures/mod-rack.v3.rig.json', 'utf8'));
r = u.readPreset(g3);
const real3 = (snap, id, sym) => { const q = u.TYPES[r.rack.find((x) => x.id === id).type].params.find((x) => x.symbol === sym); return q.curve === 'log' ? q.min * Math.pow(q.max / q.min, snap.srcParams[`${id}.${sym}`]) : q.min + (q.max - q.min) * snap.srcParams[`${id}.${sym}`]; };
check(!r.error && r.warnings.length === 0, `loads with no notes${r.warnings && r.warnings.length ? ': ' + r.warnings.join(' | ') : ''}`);
check(r.rack.map((x) => `${x.id}:${x.type}`).join() === 'env:env,lfo:lfo,lfo2:lfo,drift:orbit', `the rack, in order: ${r.rack.map((x) => x.id).join(', ')} (four instances, two LFOs, an orbit called “drift”)`);
check(close(real3(r.snap, 'lfo2', 'rate'), 0.5, 1e-4) && close(real3(r.snap, 'lfo2', 'shape'), 100, 1e-4) && close(real3(r.snap, 'drift', 'speed'), 0.2, 1e-5) && close(real3(r.snap, 'lfo', 'rate'), 4, 1e-4), 'each instance has its own knob settings');
check(r.snap.conns['lfo.rate'] === 'env' && r.snap.conns['lfo2.shape'] === 'drift' && r.snap.conns['drift.speed'] === 'drift' && r.snap.conns['holo.halo'] === 'lfo2' && close(r.snap.depths['lfo2.shape'], 0.5),
  'cross-modulation: envelope -> lfo rate, drift -> lfo2 shape, drift -> its own speed; lfo2 -> a module knob');
check(r.scenes.A.conns['lfo.rate'] === 'lfo2' && close(real3(r.scenes.A, 'lfo2', 'rate'), 2, 1e-4), 'scene A re-patches lfo.rate and has its own lfo2 rate');
r = u.readPreset(g2);
check(r.rack.map((x) => x.id).join() === 'lfo,env,adsr,orbit,heart,ctrl' && r.rack.every((x) => x.id === x.type), `format 2 migrates to the six fixed sources it was made with (${r.rack.length})`);
const odd3 = JSON.parse(JSON.stringify(g3));
odd3.sources.push({ id: 'wave', type: 'wavetable', params: { morph: 3 } }, { id: 'lfo', type: 'lfo' }, { id: 'Bad Id', type: 'lfo' }, { id: 'amp', type: 'lfo' });
odd3.mods.push({ target: 'holo.feedback', source: 'wave', depth: 0.2 }, { target: 'wave.morph', source: 'lfo', depth: 0.2 }, { target: 'lfo.nope', source: 'env', depth: 0.2 });
r = u.readPreset(odd3);
const w3 = r.warnings.join(' | ');
check(r.sourceExtras.length === 1 && r.sourceExtras[0].type === 'wavetable' && w3.includes('no “wavetable” source') && w3.includes('listed twice') && w3.includes('not a usable id') && w3.includes('clashes with a module'),
  'an unknown type is kept for re-saving; a duplicate, a bad id and a module’s id are skipped, each with a note');
check(!w3.includes('wave → ') && !w3.includes('lfo → wave') && w3.includes('env → lfo.nope') && r.snap.conns['holo.feedback'] === 'lfo',
  'patches to or from the unknown source are kept quietly with it; a knob that doesn’t exist is noted; the rest load');
const ren = JSON.parse(JSON.stringify(g3)); ren.sources[3].type = 'threebody'; ren.sources[3].params = { tempo: 0.2 };
r = u.readPreset(ren, { sourceTypes: { threebody: 'orbit' }, sourceKnobs: { 'orbit.tempo': 'speed' } });
check(r.warnings.length === 0 && r.rack[3].type === 'orbit' && close(real3(r.snap, 'drift', 'speed'), 0.2, 1e-5), 'a renamed source type and knob load through the rename table');

console.log('golden file (format 4: a chain with no gate, the amp first and a second HOLOGRAM called “echo”)');
const g4 = JSON.parse(fs.readFileSync('tests/fixtures/fx-chain.v4.rig.json', 'utf8'));
r = u.readPreset(g4);
const mreal = (snap, id, sym) => { const t = r.chain.find((x) => x.id === id).type, q = u.MTYPES[t].params.find((x) => x.symbol === sym), v = snap.params[`${id}.${sym}`]; return q.curve === 'log' ? q.min * Math.pow(q.max / q.min, v) : q.min + (q.max - q.min) * v; };
check(!r.error && r.warnings.length === 0, `loads with no notes${r.warnings && r.warnings.length ? ': ' + r.warnings.join(' | ') : ''}`);
check(r.chain.map((x) => `${x.id}:${x.type}`).join() === 'amp:amp,holo:holo,echo:holo,cab:cab,tess:tess', `the chain, in order: ${r.chain.map((x) => x.id).join(' → ')}`);
check(close(mreal(r.snap, 'echo', 'feedback'), 70, 1e-3) && close(mreal(r.snap, 'holo', 'feedback'), 30, 1e-3) && r.snap.bypass.echo === true && r.snap.bypass.holo === false, 'each HOLOGRAM has its own knobs and bypass');
check(r.snap.conns['echo.feedback'] === 'lfo' && close(r.snap.depths['echo.feedback'], 0.25) && r.snap.conns['holo.halo'] === 'env', 'patches to the second delay');
check(r.scenes.A.bypass.echo === false && close(mreal(r.scenes.A, 'echo', 'feedback'), 85, 1e-3), 'scene A turns “echo” on with more feedback');
r = u.readPreset(g3);
check(r.chain.map((x) => x.id).join() === 'gate,amp,cab,holo,tess' && r.chain.every((x) => x.id === x.type) && r.warnings.length === 0, `format 3 migrates to the five fixed modules it was made with, in order (${r.chain.length})`);
const odd4 = JSON.parse(JSON.stringify(g4));
odd4.chain.push({ id: 'flange', type: 'flanger', params: { rate: 2 } }, { id: 'echo', type: 'holo' }, { id: '9x', type: 'holo' });
odd4.sources.push({ id: 'cab', type: 'lfo' });
odd4.mods.push({ target: 'flange.rate', source: 'lfo', depth: 0.2 });
r = u.readPreset(odd4); const w4 = r.warnings.join(' | ');
check(r.extras.length === 1 && r.extras[0].type === 'flanger' && w4.includes('no “flanger” module') && w4.includes('module echo: listed twice') && w4.includes('module 9x: not a usable id') && w4.includes('source cab: clashes with a module'),
  'an unknown module type is kept for re-saving; a duplicate, a bad id and a source using a module’s id are noted');
check(!w4.includes('flange.rate'), 'a patch to the unknown module is kept quietly with it');
const ren4 = JSON.parse(JSON.stringify(g4)); ren4.chain[2].type = 'delay'; ren4.chain[2].params = { fb: 70 };
r = u.readPreset(ren4, { modules: { delay: 'holo' }, params: { 'holo.fb': 'holo.feedback' } });
check(r.warnings.length === 0 && r.chain[2].type === 'holo' && close(mreal(r.snap, 'echo', 'feedback'), 70, 1e-3), 'a renamed module type and knob load through the rename table');

console.log('round trip');
u.setChain([{ id: 'tess', type: 'tess' }, { id: 'amp', type: 'amp' }, { id: 'holo', type: 'holo' }, { id: 'holo2', type: 'holo' }, { id: 'cab', type: 'cab' }, { id: 'gate', type: 'gate' }]);
u.setRack([...u.SRC.map((x) => ({ id: x.id, type: x.type })), { id: 'lfo2', type: 'lfo' }, { id: 'wobble', type: 'orbit' }]);
Object.values(u.PARAMS).forEach((p, i) => { p.v = (i * 0.137) % 1; });
u.SRC.forEach((s, i) => s.params.forEach((q, j) => { q.v = ((i + 1) * 0.211 + j * 0.37) % 1; }));
u.state.conns = { 'holo.halo': 'orbit', 'cab.mix': 'ctrl', 'lfo2.rate': 'wobble', 'wobble.speed': 'lfo2', 'lfo.shape': 'lfo' }; u.state.depths = { 'cab.mix': 0.5, 'lfo2.rate': 0.7 }; u.state.bypass = { amp: true };
u.state.assets = { cab: { kind: 'ir', name: 'x.wav', size: 10, hash: 'abcd1234' } }; u.state.scenes = { B: u.currentSnapshot() }; u.state.bpm = 97; u.state.master = -7;
const built = JSON.parse(JSON.stringify(u.buildPreset('Round Trip')));
r = u.readPreset(built);
const worst = Math.max(...Object.values(u.PARAMS).map((p) => Math.abs(r.snap.params[p.id] - p.v)));
check(r.warnings.length === 0 && worst < 1e-5, `every knob survives save + load (worst error ${worst.toExponential(1)})`);
const qs = u.SRC.flatMap((s) => s.params), worstSrc = Math.max(...qs.map((q) => Math.abs(r.snap.srcParams[q.id] - q.v)));
check(qs.length > 0 && worstSrc < 1e-5 && Object.keys(r.scenes.B.srcParams).length === qs.length, `every source knob survives too (${qs.length} knobs, worst error ${worstSrc.toExponential(1)}), scenes included`);
check(r.chain.map((x) => x.id).join() === 'tess,amp,holo,holo2,cab,gate' && r.chain[3].type === 'holo' && close(r.snap.params['holo2.feedback'], u.PARAMS['holo2.feedback'].v, 1e-5), 'the chain (reordered, with holo2) survives, with holo2’s own knobs');
check(r.rack.map((x) => x.id).join() === 'lfo,env,adsr,orbit,heart,ctrl,lfo2,wobble' && r.rack[7].type === 'orbit' && r.snap.conns['lfo2.rate'] === 'wobble' && r.snap.conns['wobble.speed'] === 'lfo2' && r.snap.conns['lfo.shape'] === 'lfo' && r.snap.depths['lfo2.rate'] === 0.7,
  'the rack (with lfo2 and an orbit called wobble) and its cross-patches, including a self-patch, survive');
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
const withExtra = u.readPreset(odd3); u.state.sourceExtras = withExtra.sourceExtras;
check(u.buildPreset('x').sources.some((e) => e.type === 'wavetable'), 're-saving writes the unknown source back too');
check(u.buildPreset('again').chain.some((c) => c.id === 'trem'), 're-saving writes the unknown module back');
console.log('per-destination PHASE and DIV (LFO patches)');
u.state.presetExtras = []; u.state.sourceExtras = [];
u.state.conns = { 'holo.feedback': 'lfo', 'holo.halo': 'lfo', 'cab.mix': 'env' }; u.state.depths = {};
u.state.taps = { 'holo.halo': { phase: 90, div: 1.5 } }; u.state.scenes = { C: u.currentSnapshot() };
const tapped = JSON.parse(JSON.stringify(u.buildPreset('Taps')));
const mh = tapped.mods.find((m) => m.target === 'holo.halo'), mf = tapped.mods.find((m) => m.target === 'holo.feedback');
check(mh.phase === 90 && mh.div === 1.5 && !('phase' in mf) && !('div' in mf), 'saved only where set: holo.halo 90 degrees, 3/2; holo.feedback has no new fields');
r = u.readPreset(tapped);
check(r.warnings.length === 0 && r.snap.taps['holo.halo'].phase === 90 && r.snap.taps['holo.halo'].div === 1.5 && !r.snap.taps['holo.feedback'] && r.scenes.C.taps['holo.halo'].div === 1.5, 'loads back, scenes included');
const badTap = JSON.parse(JSON.stringify(tapped)); badTap.mods.find((m) => m.target === 'holo.halo').div = -2;
r = u.readPreset(badTap);
check(!r.snap.taps['holo.halo'] && r.warnings.some((w) => w.includes("isn't usable")), `an unusable DIV loads as 1x with a note: “${r.warnings[0]}”`);
r = u.readPreset(golden);
check(Object.keys(r.snap.taps).length === 0, 'the golden file (no phase or div) loads with every patch at 0 degrees, 1x');
u.state.taps = {};
const lfoRate = u.SRC.find((s) => s.id === 'lfo').params.find((q) => q.sym === 'rate'), wave = u.SRC.find((s) => s.id === 'lfo').params.find((q) => q.sym === 'wave');
check(lfoRate.fmt(lfoRate.def) === '2 bars' && lfoRate.fmt(lfoRate.snap(0.2)) === '1/4.' && lfoRate.fmt(0.2) !== '1/4.' && wave.fmt(wave.snap(0.5)) === 'SAW UP',
  `named stops: RATE ${lfoRate.fmt(lfoRate.def)} by default, turned to 1.51 beats lands on ${lfoRate.fmt(lfoRate.snap(0.2))}; WAVE at 0.5 is ${wave.fmt(wave.snap(0.5))}`);
console.log('source settings and tables (GEOMETRIC PULSE)');
u.setRack([...u.SRC.filter((x) => x.type !== 'geo').map((x) => ({ id: x.id, type: x.type })), { id: 'geo', type: 'geo' }]);
const g = u.SRCMAP.geo, set = (sym, real) => { const q = g.settings.find((x) => x.sym === sym); q.v = (real - q.desc.min) / (q.desc.max - q.desc.min); };
set('shape', 1); set('verts', 12); set('skip', 5); set('seed', 42);
g.table.v[3][g.table.cols.findIndex((c) => c.sym === 'accent')] = 1; g.table.v[5][0] = 0; g.table.v[7][2] = 0.3;
u.state.conns = {}; u.state.scenes = { D: u.currentSnapshot() };
const gp = JSON.parse(JSON.stringify(u.buildPreset('Geo'))), ge = gp.sources.find((e) => e.id === 'geo');
check(ge.settings.shape === 1 && ge.settings.verts === 12 && ge.settings.skip === 5 && ge.settings.seed === 42 && Object.keys(ge.table).join() === '3,5,7' && ge.table[3].accent === 1 && ge.table[5].on === 0 && ge.table[7].vel === 30,
  `saved in real units, only the cells that differ: settings shape ${ge.settings.shape}, verts ${ge.settings.verts}; table ${JSON.stringify(ge.table)}`);
r = u.readPreset(gp);
const cfgOk = g.settings.every((q) => Math.abs(r.snap.srcCfg[q.id] - q.v) < 1e-6), cellsOk = g.table.v.every((row, i) => row.every((x, j) => Math.abs(r.snap.srcCells.geo[i][j] - x) < 1e-6));
check(r.warnings.length === 0 && cfgOk && cellsOk && r.scenes.D.srcCells.geo[3][3] === 1, `every setting (${g.settings.length}) and cell (${g.table.rows} x ${g.table.cols.length}) loads back, scenes included`);
const badG = JSON.parse(JSON.stringify(gp)); const be = badG.sources.find((e) => e.id === 'geo'); be.table = { 40: { on: 0 }, 2: { wobble: 1 } }; be.settings.flavour = 3;
r = u.readPreset(badG);
check(r.warnings.some((w) => w.includes('row “40”')) && r.warnings.some((w) => w.includes('column “wobble”')) && r.warnings.some((w) => w.includes('no setting called “flavour”')), `bad rows, columns and settings are named: ${r.warnings.filter((w) => w.startsWith('geo')).length} notes`);
const verts = g.settings.find((q) => q.sym === 'verts'), shape = g.settings.find((q) => q.sym === 'shape');
check(shape.fmt(shape.v) === 'STAR' && verts.fmt(verts.v) === '12', `readouts: SHAPE “${shape.fmt(shape.v)}”, VERTICES “${verts.fmt(verts.v)}”`);
check(u.readPreset({ ...golden, version: 9 }).warnings.some((w) => w.includes('newer version')), 'a newer file loads with a note');
check(u.readPreset({ hello: 1 }).error === 'not a RIG preset file', 'a non-preset file is refused');
done('preset');

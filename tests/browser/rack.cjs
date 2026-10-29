// tests/browser/rack.cjs - the modulation rack in headless Chromium: adding sources from the "+" orb,
// knobs and cross-modulation from the panel, carrying a cable orb to orb, the matrix's source-knob
// columns, duplicating, removing, a full rack, and a preset that brings its rack back.
const { chromium } = require(process.env.PLAYWRIGHT_CORE || 'playwright-core');
const path = require('path'), fs = require('fs');
const root = path.resolve(__dirname, '../..');
let fails = 0;
const check = (ok, msg) => { console.log(`  [${ok ? 'PASS' : 'FAIL'}] ${msg}`); if (!ok) fails++; };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

(async () => {
  const browser = await chromium.launch({ args: ['--autoplay-policy=no-user-gesture-required', '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'] });
  const context = await browser.newContext({ viewport: { width: 1600, height: 900 } });
  const page = await context.newPage(), errors = [];
  page.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });
  page.on('pageerror', (e) => errors.push('pageerror: ' + e.message));
  await context.route('**/cdnjs.cloudflare.com/**three.min.js', (r) => r.fulfill({ body: fs.readFileSync(process.env.THREE_JS), contentType: 'text/javascript' }));
  await context.route('**/fonts.googleapis.com/**', (r) => r.fulfill({ body: '', contentType: 'text/css' }));
  const shots = process.env.SHOTS || path.join(root, 'build');
  await page.goto('file://' + path.join(root, 'dist/rig-test.html'));
  await page.waitForFunction(() => window.__rig && window.__rig.ready && window.__rig.library && window.__rig.library.core, null, { timeout: 10000 });
  await page.check('input[name=src][value=riff]'); await page.click('#startBtn');
  await page.waitForFunction(() => window.__rig.meters && window.__rig.engineRack, null, { timeout: 10000 }); await sleep(1200);
  const R = () => page.evaluate(() => ({ ui: window.__rig.rack.list(), engine: window.__rig.engineRack, cables: window.__rig.cables(), conns: { ...window.__rig.state.conns } }));
  // engine values from a meter packet made after this call (a busy headless page can hand them over late)
  const fresh = async () => { const t0 = await page.evaluate(() => window.__rig.ctx.currentTime); await page.waitForFunction((t0) => window.__rig.meters.t > t0 + 0.2, t0, { timeout: 10000 }); };
  const spread = (expr, ms) => page.evaluate(async ([expr, ms]) => { const f = new Function('return ' + expr); let lo = 1e9, hi = -1e9; const t0 = performance.now(); while (performance.now() - t0 < ms) { const v = f(); lo = Math.min(lo, v); hi = Math.max(hi, v); await new Promise((r) => setTimeout(r, 30)); } return [lo, hi]; }, [expr, ms]);
  const click = async (pt) => { await page.mouse.click(pt.x, pt.y); await sleep(500); };
  const setSlider = (sel, v) => page.evaluate(([sel, v]) => { const el = document.querySelector(sel); el.value = v; el.dispatchEvent(new Event('input', { bubbles: true })); }, [sel, v]);
  const select = (sel, v) => page.evaluate(([sel, v]) => { const el = document.querySelector(sel); el.value = v; el.dispatchEvent(new Event('change', { bubbles: true })); }, [sel, v]);

  console.log('1. The default rack, and the "+" orb');
  let r = await R();
  check(r.ui.map((s) => s.id).join() === 'lfo,env,adsr,orbit,heart,ctrl' && r.engine.length === 6 && r.ui.every((s, i) => s.slot === r.engine.find((e) => e.id === s.id).slot), `six sources, slots from the engine (${r.engine.map((e) => e.id + '@' + e.slot).join(' ')})`);
  await page.evaluate(() => window.__rig.overview && 0); 
  const plus = await page.evaluate(() => window.__rig.addScreen());
  await click(plus);
  check(await page.isVisible('[data-add-type="lfo"]') && /Add a source/.test(await page.textContent('#panel')), 'clicking “+ ADD” opens the add panel with every kind of source');

  console.log('2. Add an LFO: it plays, with its own knobs');
  await page.click('[data-add-type="lfo"]'); await sleep(800); await fresh();
  r = await R();
  const l2 = r.engine.find((e) => e.id === 'lfo2');
  check(r.ui.length === 7 && l2 && l2.type === 'lfo' && r.ui.find((s) => s.id === 'lfo2').slot === l2.slot && /LFO 2/.test(await page.textContent('#panel h2')), `added “LFO 2” (engine slot ${l2 && l2.slot}) and opened its panel`);
  await setSlider('[data-spid="lfo2.rate"]', 0.25); await sleep(300); await fresh();
  const rate = await page.evaluate(() => [window.__rig.srcParam('lfo2', 'rate'), window.__rig.srcEff('lfo2', 'rate')]);
  const [lo2, hi2] = await spread("window.__rig.src('lfo2')", 2500);
  check(Math.abs(rate[0] - 2) < 0.01 && Math.abs(rate[1] - 0.25) < 1e-4 && hi2 - lo2 > 1, `its RATE knob reaches the engine (${rate[0].toFixed(2)} beats), and it swings ${lo2.toFixed(2)}..${hi2.toFixed(2)}`);

  console.log('3. Cross-modulation from the panel: the envelope moves LFO 2\'s rate');
  await select('[data-spatch="lfo2.rate"]', 'env'); await sleep(600);
  r = await R();
  const [elo, ehi] = await spread("window.__rig.srcEff('lfo2', 'rate')", 3000);
  check(r.conns['lfo2.rate'] === 'env' && r.cables.includes('env>lfo2.rate') && ehi - elo > 0.05, `patched (a cable runs orb to orb); the engine's effective RATE now moves ${elo.toFixed(3)}..${ehi.toFixed(3)} with the riff`);
  check(/→/.test(await page.textContent('[data-sdisp="lfo2.rate"]')), `the knob shows setting → live value (“${await page.textContent('[data-sdisp="lfo2.rate"]')}”)`);

  console.log('4. Carry a cable from the THREE-BODY orb to LFO 2, and pick a knob');
  await click(await page.evaluate(() => window.__rig.srcScreen('orbit', 'orb')));
  await click(await page.evaluate(() => window.__rig.srcScreen('lfo2', 'orb')));
  const here = await page.isVisible('[data-act="patch-here"][data-pid="lfo2.shape"]');
  await page.click('[data-act="patch-here"][data-pid="lfo2.shape"]'); await sleep(500);
  r = await R();
  check(here && r.conns['lfo2.shape'] === 'orbit' && r.cables.includes('orbit>lfo2.shape') && !(await page.evaluate(() => window.__rig.state.carrying)), 'the target source opens with “Patch THREE-BODY here” on its knobs; one click patches SHAPE');
  await page.screenshot({ path: path.join(shots, 'shot-rack-stage.png') });

  console.log('5. The matrix has a column for every source knob');
  await page.click('#vMatrix'); await sleep(500);
  const cols = await page.evaluate(() => document.querySelectorAll('#matrix thead tr:last-child th[scope=col]').length);
  const want = await page.evaluate(() => Object.keys(window.__rig.state.conns).length >= 0 && document.querySelectorAll('#matrix tbody tr').length);
  await page.click('.cell[data-pid="lfo.shape"][data-sid="adsr"]'); await sleep(500);
  r = await R();
  check(cols === 10 + 15 && want === 7 && r.conns['lfo.shape'] === 'adsr', `${cols} columns (10 module knobs + 15 source knobs), 7 rows; a dot patches ADSR -> LFO shape`);
  await page.screenshot({ path: path.join(shots, 'shot-rack-matrix.png') });
  await page.click('#vStage'); await sleep(300);

  console.log('6. Duplicate and remove');
  // open a source by its name in the matrix (on the stage it may sit under the side panel)
  const open = async (id) => { await page.click('#vMatrix'); await sleep(300); await page.click(`[data-edit-src="${id}"]`); await sleep(400); await page.click('#vStage'); await sleep(300); };
  await open('lfo2');
  await page.click('[data-act="dup-src"]'); await sleep(800);
  r = await R();
  check(r.ui.some((s) => s.id === 'lfo3') && Math.abs(await page.evaluate(() => window.__rig.srcParam('lfo3', 'rate')) - 2) < 0.01 && r.engine.some((e) => e.id === 'lfo3'), 'Duplicate makes LFO 3 with the same rate');
  await open('lfo2');
  await page.click('[data-act="remove-src"]'); await sleep(200);
  const armed = await page.textContent('[data-act="remove-src"]');
  await page.click('[data-act="remove-src"]'); await sleep(800);
  r = await R();
  check(armed === 'Confirm remove' && !r.ui.some((s) => s.id === 'lfo2') && !r.engine.some((e) => e.id === 'lfo2') && !Object.keys(r.conns).some((k) => k.startsWith('lfo2.')) && !r.cables.some((c) => c.includes('lfo2')),
    'Remove asks once more, then LFO 2 leaves the page and the engine with every patch to it');

  console.log('7. A full rack');
  for (let i = 0; i < 12; i++) await page.evaluate(() => window.__rig.rack.add('orbit'));
  await sleep(1000); r = await R();
  const extra = await page.evaluate(() => window.__rig.rack.add('lfo'));
  check(r.ui.length === 16 && r.engine.length === 16 && extra === null && (await page.evaluate(() => window.__rig.addScreen())) === null, `16 sources in the page and the engine; a 17th is refused and the “+” orb is gone`);
  await page.screenshot({ path: path.join(shots, 'shot-rack-full.png') });

  console.log('8. A preset brings its rack back');
  for (const id of ['orbit2', 'orbit3', 'orbit4', 'orbit5', 'orbit6', 'orbit7', 'orbit8', 'orbit9', 'orbit10', 'orbit11']) await page.evaluate((id) => window.__rig.rack.remove(id), id);
  await sleep(600);
  const before = (await R()).ui.map((s) => s.id).join();
  await page.click('#settingsBtn'); await page.fill('#presetInput', 'Rack Test'); await page.click('#presetSave'); await sleep(800);
  for (const id of ['lfo3', 'orbit12', 'orbit13', 'heart']) await page.evaluate((id) => window.__rig.rack.remove(id), id);
  await page.evaluate(() => window.__rig.rack.add('env')); await sleep(600);
  await page.selectOption('#presetSel', 'Rack Test.rig.json'); await page.click('#presetLoad'); await sleep(1500); await fresh();
  r = await R();
  const after = r.ui.map((s) => s.id).join();
  check(after === before && r.engine.length === r.ui.length && r.ui.every((s) => r.engine.some((e) => e.id === s.id && e.slot === s.slot)) && r.conns['lfo.shape'] === 'adsr' && r.conns['lfo2.rate'] === undefined,
    `loading it restores the rack (${after}) in the page and the engine, with its cross-patches`);
  check(Math.abs(await page.evaluate(() => window.__rig.srcParam('lfo3', 'rate')) - 2) < 0.01, 'and the knob settings of instances it had added back');

  check(errors.length === 0, `no console errors${errors.length ? ': ' + errors.join(' | ') : ''}`);
  await browser.close();
  console.log(fails ? `\n${fails} FAILED` : '\nALL RACK CHECKS PASSED');
  process.exit(fails ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(2); });

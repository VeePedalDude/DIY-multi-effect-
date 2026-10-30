// tests/browser/chain.cjs - the effect chain in headless Chromium: adding a module from the "+" card,
// its knobs, moving it with ◀ ▶, duplicating, patching a source to the new one, removing one, a full
// chain, the matrix, and a preset that brings its chain back in order.
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
  await page.waitForFunction(() => window.__rig.meters && window.__rig.engineChain, null, { timeout: 10000 }); await sleep(1200);
  const C = () => page.evaluate(() => ({ ui: window.__rig.chain.list(), engine: window.__rig.engineChain, conns: { ...window.__rig.state.conns } }));
  const ids = (l) => l.map((x) => x.id).join(' ');
  const inStep = (c) => ids(c.ui) === ids(c.engine) && c.ui.every((m) => c.engine.some((e) => e.id === m.id && e.slot === m.slot));
  const fresh = async () => { const t0 = await page.evaluate(() => window.__rig.ctx.currentTime); await page.waitForFunction((t0) => window.__rig.meters.t > t0 + 0.3, t0, { timeout: 10000 }); };
  const outPeak = () => page.evaluate(async () => { let p = 0; const t0 = performance.now(); while (performance.now() - t0 < 1500) { p = Math.max(p, window.__rig.meters.outPk); await new Promise((r) => setTimeout(r, 25)); } return p; });
  const open = async (id) => { await page.click('#vMatrix'); await sleep(200); await page.evaluate((id) => { window.__rig.state.focus = id; }, id); await page.click('#vStage'); await sleep(400); };
  const setSlider = (sel, v) => page.evaluate(([sel, v]) => { const el = document.querySelector(sel); el.value = v; el.dispatchEvent(new Event('input', { bubbles: true })); }, [sel, v]);

  console.log('1. The default chain, and the "+" card');
  let c = await C();
  check(ids(c.ui) === 'gate amp cab holo tess' && inStep(c), `five modules in order, slots from the engine (${c.engine.map((e) => e.id + '@' + e.slot).join(' ')})`);
  const plus = await page.evaluate(() => window.__rig.fxAddScreen());
  await page.mouse.click(plus.x, plus.y); await sleep(500);
  const tabs = await page.evaluate(() => [...document.querySelectorAll('[data-pick-tabs="fx"] [role=tab]')].map((t) => [t.textContent.trim(), t.getAttribute('aria-selected'), t.classList.contains('empty')]));
  check(/Add an effect/.test(await page.textContent('#panel')) && tabs.length === 11 && tabs[0][0].startsWith('Dynamics') && tabs[0][1] === 'true' && tabs.filter((t) => t[2]).length === 4,
    `the “+ ADD EFFECT” card opens a tab per kind of effect (${tabs.map((t) => t[0].replace(/\s+\d+$/, '')).join(', ')}); the first with something in it is open; 4 are empty for now`);
  await page.click('[data-pick-tab="fx:pitch"]'); await sleep(200);
  check(/Nothing in Pitch yet/.test(await page.textContent('[data-pick-list="fx"]')) && /engine\/modules\/pitch\//.test(await page.textContent('[data-pick-list="fx"]')), 'an empty tab says so, and which folder new ones go in');
  await page.fill('[data-pick-q="fx"]', 'reverb'); await sleep(300);
  const found = await page.evaluate(() => [...document.querySelectorAll('[data-pick-list="fx"] [data-addfx-type]')].map((b) => b.dataset.addfxType));
  const keepsFocus = await page.evaluate(() => document.activeElement && document.activeElement.dataset.pickQ === 'fx');
  check(found.join() === 'tess' && keepsFocus, `searching “reverb” across every tab finds ${found.join(', ')} (the search box keeps focus while you type)`);
  await page.fill('[data-pick-q="fx"]', ''); await page.click('[data-pick-tab="fx:delay"]'); await sleep(200);
  await page.focus('[data-pick-tab="fx:delay"]'); await page.keyboard.press('ArrowRight'); await sleep(200);
  const arrow = await page.evaluate(() => window.__rig.picker.tab('fx'));
  await page.keyboard.press('ArrowLeft'); await sleep(200);
  check(arrow === 'reverb' && (await page.evaluate(() => window.__rig.picker.tab('fx'))) === 'delay' && await page.isVisible('[data-addfx-type="holo"]'), 'arrow keys move between tabs; Delay lists HOLOGRAM');
  await page.click('[data-pick-tab="fx:filter"]'); await sleep(200);
  const eqs = await page.evaluate(() => [...document.querySelectorAll('[data-pick-list="fx"] [data-addfx-type]')].map((b) => b.dataset.addfxType).join(' '));
  check(eqs === 'tilt bax geq peq stack prog dyneq', `Filter & EQ lists its seven, simple to sophisticated (engine/modules/filter/order.txt): ${eqs}`);
  await page.click('[data-pick-tab="fx:synth"]'); await sleep(200);
  const synths = await page.evaluate(() => [...document.querySelectorAll('[data-pick-list="fx"] [data-addfx-type]')].map((b) => b.dataset.addfxType).join(' '));
  check(synths === 'svf comb formant ota ladder diode sallen', `Synth filters lists its seven, simplest model first: ${synths}`);
  await page.click('[data-pick-tab="fx:delay"]'); await sleep(200);

  console.log('2. Add a second HOLOGRAM: it plays, with its own knobs');
  await page.click('[data-addfx-type="holo"]'); await sleep(800); await fresh();
  c = await C();
  check(ids(c.ui) === 'gate amp cab holo tess holo2' && inStep(c) && /HOLOGRAM 2/.test(await page.textContent('#panel h2')) && (await page.evaluate(() => !!window.__rig.modScreen('holo2'))),
    `added “HOLOGRAM 2” at the end (engine slot ${c.engine.find((e) => e.id === 'holo2').slot}), with its own card and panel`);
  await setSlider('[data-pid="holo2.feedback"]', 0.8); await sleep(300); await fresh();
  const fb = await page.evaluate(() => [window.__rig.eff('holo2', 'feedback'), window.__rig.eff('holo', 'feedback')]);
  check(Math.abs(fb[0] - 0.8) < 1e-3 && Math.abs(fb[1] - 0.8) > 0.1, `its FEEDBACK reaches the engine (${fb[0].toFixed(3)}); the first HOLOGRAM keeps its own (${fb[1].toFixed(3)})`);

  console.log('3. Move it to the front with ◀');
  for (let i = 0; i < 5; i++) { await page.click('[data-act="fx-left"]'); await sleep(250); }
  await sleep(500); c = await C();
  const pk = await outPeak();
  check(ids(c.ui) === 'holo2 gate amp cab holo tess' && inStep(c) && await page.isDisabled('[data-act="fx-left"]') && pk > 0.01, `chain now ${ids(c.ui)} in the page and the engine; ◀ is disabled at the front; still playing (peak ${pk.toFixed(3)})`);
  await page.screenshot({ path: path.join(shots, 'shot-chain-stage.png') });
  await page.evaluate(() => { window.__rig.state.focus = 'addfx'; }); await page.click('#vMatrix'); await page.click('#vStage'); await sleep(400);
  check(await page.getAttribute('[data-pick-tab="fx:delay"]', 'aria-selected') === 'true', 'the add panel opens again on the tab used last (Delay)');
  await page.screenshot({ path: path.join(shots, 'shot-picker.png') });
  await open('holo2');

  console.log('4. Patch the LFO to HOLOGRAM 2\'s halo from its panel');
  await page.evaluate(() => { const el = document.querySelector('[data-patch="holo2.halo"]'); el.value = 'lfo'; el.dispatchEvent(new Event('change', { bubbles: true })); }); await sleep(500);
  c = await C();
  const cab = await page.evaluate(() => window.__rig.cables());
  check(c.conns['holo2.halo'] === 'lfo' && cab.includes('lfo>holo2.halo'), 'patched, with a cable to the new card');

  console.log('5. Duplicate the cab');
  await open('cab');
  await setSlider('[data-pid="cab.locut"]', 0.3); await sleep(200);
  await page.click('[data-act="fx-dup"]'); await sleep(800); c = await C();
  const lc = await page.evaluate(() => [window.__rig.eff('cab2', 'locut'), window.__rig.eff('cab', 'locut')]);
  check(ids(c.ui) === 'holo2 gate amp cab cab2 holo tess' && inStep(c) && Math.abs(lc[0] - 0.3) < 1e-3, `CAB IR 2 right after the cab, same LO CUT (${lc[0].toFixed(3)})`);

  console.log('6. Remove the amp');
  await open('amp');
  await page.click('[data-act="fx-remove"]'); await sleep(200);
  const armed = await page.textContent('[data-act="fx-remove"]');
  await page.click('[data-act="fx-remove"]'); await sleep(800); c = await C();
  const pk2 = await outPeak();
  check(armed === 'Confirm remove' && ids(c.ui) === 'holo2 gate cab cab2 holo tess' && inStep(c) && !c.conns['amp.input'] && pk2 > 1e-4, `Remove asks once more; the amp and its patch leave page and engine; still playing, quieter without its gain (peak ${pk2.toFixed(4)})`);

  console.log('7. The matrix has columns for the new modules');
  await page.click('#vMatrix'); await sleep(400);
  const heads = await page.evaluate(() => [...document.querySelectorAll('#matrix thead tr:last-child th[scope=col] small')].map((s) => s.textContent));
  check(heads.includes('HOLOGRAM 2') && heads.includes('CAB IR 2') && !heads.includes('AMP') && await page.isVisible('[data-matrix-addfx]'), `columns follow the chain (${[...new Set(heads)].slice(0, 6).join(', ')}…)`);
  await page.screenshot({ path: path.join(shots, 'shot-chain-matrix.png') });
  await page.click('#vStage'); await sleep(300);

  console.log('8. A preset brings its chain back, in order');
  await page.click('#settingsBtn'); await page.fill('#presetInput', 'Chain Test'); await page.click('#presetSave'); await sleep(800);
  const saved = ids((await C()).ui);
  await page.evaluate(() => { window.__rig.chain.remove('holo2'); window.__rig.chain.remove('cab2'); window.__rig.chain.add('amp', 0); window.__rig.chain.move('tess', 0); }); await sleep(800);
  await page.selectOption('#presetSel', 'Chain Test.rig.json'); await page.click('#presetLoad'); await sleep(1500); await fresh();
  c = await C();
  const fb2 = await page.evaluate(() => window.__rig.eff('holo2', 'feedback'));
  check(ids(c.ui) === saved && inStep(c) && Math.abs(fb2 - 0.8) < 1e-3 && c.conns['holo2.halo'] === 'lfo', `loading it restores ${ids(c.ui)} in the page and the engine, HOLOGRAM 2's knobs and its patch`);
  await page.click('#settingsBtn');

  console.log('9. Each EQ and synth filter added live draws its curve');
  const drawn = [];
  for (const t of ['tilt', 'bax', 'geq', 'peq', 'stack', 'prog', 'dyneq', 'svf', 'comb', 'formant', 'ota', 'ladder', 'diode', 'sallen']) {
    const id = await page.evaluate((t) => { const m = window.__rig.chain.add(t); return m && m.id; }, t); await sleep(600);
    const ok = await page.evaluate((id) => window.__rig.redrawFace(id), id);
    if (t === 'stack') { await open(id); await setSlider(`[data-pid="${id}.bass"]`, 0.9); await setSlider(`[data-pid="${id}.model"]`, 0.25); await sleep(400);
      await page.evaluate((id) => window.__rig.redrawFace(id), id); await page.screenshot({ path: path.join(shots, 'shot-eq-stack.png') }); }
    if (t === 'ladder') { await open(id); await setSlider(`[data-pid="${id}.reso"]`, 0.8); await sleep(400);
      await page.evaluate((id) => window.__rig.redrawFace(id), id); await page.screenshot({ path: path.join(shots, 'shot-synth-ladder.png') }); }
    if (ok) drawn.push(id);
    await page.evaluate((id) => window.__rig.chain.remove(id), id); await sleep(600);
  }
  const se = await page.evaluate(() => window.__rig.screenErrors);
  check(drawn.length === 14 && se.length === 0 && ids((await C()).ui) === saved, `added and removed ${drawn.join(', ')}: every face screen drew${se.length ? ' (errors: ' + se.join(' | ') + ')' : ''}; the chain is back to ${saved}`);

  console.log('10. A full chain');
  for (let i = 0; i < 8; i++) await page.evaluate(() => window.__rig.chain.add('gate'));
  await sleep(1000); c = await C();
  const extra = await page.evaluate(() => window.__rig.chain.add('gate'));
  check(c.ui.length === 12 && inStep(c) && extra === null && (await page.evaluate(() => window.__rig.fxAddScreen())) === null, '12 modules in the page and the engine; a 13th is refused and the “+” card is gone');
  await page.click('#overview'); await page.evaluate(() => window.__rig.camSnap()); await sleep(1500);
  const span = await page.evaluate(() => { const l = window.__rig.chain.list(); return [window.__rig.modScreen(l[0].id), window.__rig.modScreen(l[l.length - 1].id)]; });
  check(span[0].x > 0 && span[1].x < 1600 - 330, `Overview fits all 12 cards left of the panel (first at x ${Math.round(span[0].x)}, last at ${Math.round(span[1].x)})`);
  await page.screenshot({ path: path.join(shots, 'shot-chain-full.png') });

  check(errors.length === 0, `no console errors${errors.length ? ': ' + errors.join(' | ') : ''}`);
  await browser.close();
  console.log(fails ? `\n${fails} FAILED` : '\nALL CHAIN CHECKS PASSED');
  process.exit(fails ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(2); });

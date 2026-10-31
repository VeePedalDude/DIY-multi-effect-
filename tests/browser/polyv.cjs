// polyv.cjs - POLY VOICE in headless Chromium: added from the Synth voices tab, the riff in, voices and
// notes on its face, MODEL's named stops, a model change, EFFECT 0 / DRY 100, and no console errors.
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
  await page.waitForFunction(() => window.__rig && window.__rig.ready, null, { timeout: 10000 });
  await page.check('input[name=src][value=riff]'); await page.click('#startBtn');
  await page.waitForFunction(() => window.__rig.meters && window.__rig.engineChain, null, { timeout: 10000 }); await sleep(800);
  const setSlider = (sel, v) => page.evaluate(([sel, v]) => { const el = document.querySelector(sel); el.value = v; el.dispatchEvent(new Event('input', { bubbles: true })); }, [sel, v]);
  // watch the meters for a while: the most voices, and every note seen
  const watch = (ms) => page.evaluate(async (ms) => { let most = 0; const notes = new Set(); const t0 = performance.now();
    while (performance.now() - t0 < ms) { most = Math.max(most, window.__rig.modMeter('polyv', 'voices') || 0); let p = Math.round(window.__rig.modMeter('polyv', 'notes') || 0);
      for (let k = 0; k < 3; k++, p = Math.floor(p / 129)) if (p % 129) notes.add((p % 129) - 1); await new Promise((r) => setTimeout(r, 20)); }
    return { most, notes: [...notes].sort((a, b) => a - b) }; }, ms);

  console.log('1. The Synth voices tab lists POLY VOICE; adding it puts it in the chain');
  const plus = await page.evaluate(() => window.__rig.fxAddScreen()); await page.mouse.click(plus.x, plus.y); await sleep(500);
  await page.click('[data-pick-tab="fx:voice"]'); await sleep(200);
  const listed = await page.evaluate(() => [...document.querySelectorAll('[data-pick-list="fx"] [data-addfx-type]')].map((b) => b.dataset.addfxType).join(' '));
  await page.click('[data-addfx-type="polyv"]'); await sleep(1000);
  const chain = await page.evaluate(() => window.__rig.chain.list().map((m) => m.id).join(' '));
  check(listed === 'polyv' && /polyv/.test(chain), `Synth voices: ${listed}; chain now ${chain}`);

  console.log('2. The riff in: voices sound, notes on the face');
  await page.evaluate(() => { window.__rig.state.focus = 'polyv'; }); await page.click('#vMatrix'); await sleep(200); await page.click('#vStage'); await sleep(400);
  const names = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'], nm = (n) => names[n % 12] + (Math.floor(n / 12) - 1);
  const after = await watch(5000);
  await page.evaluate(() => window.__rig.chain.move('polyv', 0)); await sleep(800);
  const w = await watch(7000), riffPcs = [4, 11, 9, 2, 7, 0];   // the riff: E, A, B, D power chords and runs through G and C (and octaves)
  const off = w.notes.filter((n) => !riffPcs.includes(n % 12));
  check(after.most >= 1, `at the end of the chain (after the amp's distortion and the reverb) it still plays: ${after.notes.length} different notes seen, ${after.notes.map(nm).join(' ')}`);
  const roots = [40, 45, 47, 50].every((r) => w.notes.includes(r));
  // (the page's riff is Karplus-Strong started from noise: every harmonic at a random level, so some stick out as notes)
  check(w.most >= 1 && roots && off.length <= 0.4 * w.notes.length, `moved first in the chain (the clean guitar): up to ${w.most} voices; E2 A2 B2 D3 all played; ${w.notes.length - off.length} of ${w.notes.length} notes seen are the riff's${off.length ? ' (not: ' + off.map(nm).join(' ') + ')' : ''}`);
  const drew = await page.evaluate(() => window.__rig.redrawFace('polyv')), se = await page.evaluate(() => window.__rig.screenErrors);
  await page.screenshot({ path: path.join(shots, 'shot-polyv.png') });
  check(drew && se.length === 0, 'its face screen (a keyboard of the notes) draws');

  console.log('3. MODEL is a selector with named stops');
  const panel = await page.textContent('#panel');
  await setSlider('[data-pid="polyv.model"]', 0.6);
  // the panel's readout redraws every few frames, and headless Chromium draws about once a second: wait for it
  await page.waitForFunction(() => /SYNTH SAW/.test(document.querySelector('#panel').textContent), null, { timeout: 5000 }).catch(() => {});
  const shown = await page.evaluate(() => window.__rig.eff('polyv', 'model'));
  const panel2 = await page.textContent('#panel');
  check(/TAPE STRINGS/.test(panel) && /SYNTH SAW/.test(panel2) && Math.abs(shown - 0.6) < 1e-6, `TAPE STRINGS to SYNTH SAW (${/SYNTH SAW/.test(panel2) ? 'shown' : 'not shown'} in the panel)`);
  const w2 = await watch(2500);
  check(w2.most >= 1, `SYNTH SAW plays too: up to ${w2.most} voices`);

  console.log('4. EFFECT 0, DRY 100: only the guitar');
  await setSlider('[data-pid="polyv.effect"]', 0); await setSlider('[data-pid="polyv.dry"]', 1); await sleep(600);
  const log = await page.evaluate(() => window.__rig.log.filter((l) => /^param_set polyv (effect|dry) /.test(l)).slice(-2));
  check(log.length === 2, `the engine got ${log.join(', ')}`);

  check(errors.length === 0, `no console errors${errors.length ? ': ' + errors.join(' | ') : ''}`);
  await browser.close();
  console.log(fails ? `\n${fails} FAILED` : '\nALL POLY VOICE CHECKS PASSED');
  process.exit(fails ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(2); });

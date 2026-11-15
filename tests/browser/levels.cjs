// tests/browser/levels.cjs - how loud the rig plays, with the built-in riff: the PATCH and MASTER controls
// and their meters, Level (sets the patch level so it plays at the target), the level saved in the preset and
// the master kept by the rig across a reload, and the limiter's reading when pushed.
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
  const open = async () => {
    await page.goto('file://' + path.join(root, 'dist/rig-test.html'));
    await page.waitForFunction(() => window.__rig && window.__rig.ready, null, { timeout: 10000 });
    await page.check('input[name=src][value=riff]'); await page.click('#startBtn');
    await page.waitForFunction(() => window.__rig.meters, null, { timeout: 10000 }); await sleep(800);
  };
  const V = (expr) => page.evaluate(new Function('return (' + expr + ')'));
  const slide = (id, v) => page.evaluate(([id, v]) => { const el = document.getElementById(id); el.value = v; el.dispatchEvent(new Event('input', { bubbles: true })); }, [id, v]);
  // the loud part of the patch / output RMS over a few seconds of the riff (80th percentile of the meter packets)
  const loud = async (key, secs = 4) => { const v = [];
    for (let i = 0; i < secs * 10; i++) { await sleep(100); const r = await V('window.__rig.levels.reading()'); if (r[key] > -60) v.push(r[key]); }
    v.sort((a, b) => a - b); return v[Math.floor(0.8 * v.length)]; };
  await open();
  await page.evaluate(() => localStorage.removeItem('rig01.master'));

  console.log('1. The controls and meters');
  const rng = await V(`[document.getElementById('master').min, document.getElementById('master').max, document.getElementById('level').min, document.getElementById('level').max]`);
  check(rng.join() === '-60,12,-24,24', `MASTER -60..+12 dB, PATCH -24..+24 dB (${rng.join(', ')})`);
  const fresh = await V('window.__rig.state.level');
  check(fresh === 8 && (await V(`document.getElementById('level').value`)) === '8', `a fresh rig starts at PATCH +8 dB (the cab's normalisation made 0 dB quiet): ${fresh}`);
  await slide('master', 0); await slide('level', 0); await sleep(600);
  const p0 = await loud('patchRms'), o0 = await loud('outRms');
  const txt = await V(`[document.getElementById('lvPatch').textContent, document.getElementById('lvOut').textContent]`);
  check(p0 > -60 && Math.abs(o0 - p0) < 1 && /rms · .* pk/.test(txt[0]), `the default rig at master 0: patch ${p0.toFixed(1)}, output ${o0.toFixed(1)} dB RMS (“${txt[0]}” / “${txt[1]}”)`);
  await slide('level', 6); await sleep(800);
  const p6 = await loud('patchRms');
  check(Math.abs(p6 - p0 - 6) < 1.5, `PATCH +6 dB: the patch meter rises ${(p6 - p0).toFixed(1)} dB`);
  await slide('master', -10); await sleep(800);
  const diffs = []; for (let i = 0; i < 20; i++) { await sleep(100); const r = await V('window.__rig.levels.reading()'); if (r.patchRms > -60) diffs.push(r.outRms - r.patchRms); }   // read together, so the riff's own swings cancel
  const d = diffs.sort((a, b) => a - b)[Math.floor(diffs.length / 2)];
  check(diffs.length > 5 && Math.abs(d + 10) < 0.5, `MASTER -10 dB: the output reads 10 dB under the patch (${d.toFixed(2)} dB), the patch meter isn't moved by the master`);

  console.log('2. Level sets the patch so it plays at the target');
  await slide('master', 0); await slide('level', -9); await sleep(800);
  const target = await V('window.__rig.levels.target');
  await page.click('#autoLevel');
  await page.waitForFunction(() => !window.__rig.levels.state.busy, null, { timeout: 15000 });
  const lv = await V('window.__rig.state.level'); await sleep(800);
  const after = await loud('patchRms'), outAfter = await loud('outRms');
  check(lv > -9 && Math.abs(after - target) < 1.5, `from -9 dB, Level set the patch to ${lv} dB: it now plays at ${after.toFixed(1)} dB RMS (target ${target})`);
  check(Math.abs(outAfter - target) < 1.5, `and at master 0 dB the output is there too: ${outAfter.toFixed(1)} dB RMS`);
  const ed = await V(`!document.getElementById('presetEdited').hidden`);
  check(ed === (lv !== 8), `the preset shows edited when its level changed (the level is part of it): level ${lv} dB, edited ${ed}`);
  await page.screenshot({ path: path.join(shots, 'shot-levels.png') });

  console.log('3. Pushed into the limiter, the meter says so');
  await slide('level', 24); await slide('master', 12);
  await page.waitForFunction(() => window.__rig.levels.reading().limit > 3 && document.getElementById('lvOut').className === 'clip', null, { timeout: 8000 }).catch(() => {});   // on a loud note (the reading falls back between them)
  const r = await V('window.__rig.levels.reading()'), cls = await V(`document.getElementById('lvOut').className`);
  check(r.limit > 3 && r.outPk <= 0 && cls === 'clip' && /limiting/.test(await V(`document.getElementById('lvOut').textContent`)), `+24 / +12 dB: limiting ${r.limit.toFixed(1)} dB, output peaks ${r.outPk.toFixed(1)} dBFS, shown in red`);

  console.log('4. The level is the preset\'s, the master is the rig\'s');
  await slide('level', 4.5); await slide('master', -3); await sleep(300);
  const saved = await page.evaluate(() => { const p = JSON.parse(JSON.stringify(window.__rig.buildPreset ? window.__rig.buildPreset('x') : null)); return p; });
  const json = saved || await page.evaluate(async () => { document.getElementById('presetName').value = 'Levels'; document.getElementById('presetSave').click(); await new Promise((r) => setTimeout(r, 800)); const l = window.__rig.library; const e = (await l.core.list('presets')).find((x) => /\.rig\.json$/.test(x.name)); return JSON.parse(await (await l.core.file('presets', e.name)).text()); });
  check(json.level === 4.5 && json.master === undefined && json.version === 5, `a saved preset has its level (${json.level} dB), no master, format ${json.version}`);
  await open();
  check((await V('window.__rig.state.master')) === -3 && (await V(`document.getElementById('master').value`)) === '-3', 'after a reload the master is where it was (-3 dB)');
  check(errors.length === 0, `no console errors${errors.length ? ': ' + errors.join(' | ') : ''}`);
  await browser.close();
  console.log(fails ? `\n${fails} FAILED` : '\nALL LEVEL CHECKS PASSED');
  process.exit(fails ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(2); });

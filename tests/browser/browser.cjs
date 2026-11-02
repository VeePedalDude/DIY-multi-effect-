// browser.cjs - drive dist/rig-test.html in headless Chromium with a fake mic playing the riff
const { chromium } = require(process.env.PLAYWRIGHT_CORE || 'playwright-core');
const path = require('path'), fs = require('fs');
const root = path.resolve(__dirname, '../..');
const THREE = process.env.THREE_JS;
const shots = process.env.SHOTS || path.join(root, 'build');
let fails = 0;
const check = (ok, msg) => { console.log(`  [${ok ? 'PASS' : 'FAIL'}] ${msg}`); if (!ok) fails++; };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

(async () => {
  const browser = await chromium.launch({ args: [
    '--use-fake-ui-for-media-stream', '--use-fake-device-for-media-stream',
    `--use-file-for-fake-audio-capture=${path.join(root, 'build/riff48k.wav')}`,
    '--autoplay-policy=no-user-gesture-required', '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'] });
  const page = await browser.newPage({ viewport: { width: 1600, height: 900 } });
  const errors = [];
  page.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });
  page.on('pageerror', (e) => errors.push('pageerror: ' + e.message));
  await page.route('**/cdnjs.cloudflare.com/**three.min.js', (r) => r.fulfill({ body: fs.readFileSync(THREE), contentType: 'text/javascript' }));
  await page.route('**/fonts.googleapis.com/**', (r) => r.fulfill({ body: '', contentType: 'text/css' }));
  await page.goto('file://' + path.join(root, 'dist/rig-test.html'));
  await page.waitForFunction(() => window.__rig && window.__rig.ready, null, { timeout: 8000 });
  await page.waitForTimeout(500);
  await page.screenshot({ path: path.join(shots, 'shot-start.png') });

  console.log('1. Start with the audio-interface source (fake mic = riff)');
  await page.click('#startBtn');
  await page.waitForFunction(() => window.__rig && window.__rig.meters, null, { timeout: 8000 });
  await sleep(2500);
  let m = await page.evaluate(() => window.__rig.meters);
  check(m.inPk > 0.01, `input reaches the engine (peak ${(20 * Math.log10(m.inPk)).toFixed(1)} dBFS)`);
  check(m.outPk > 0.01 && m.outPk <= 0.98, `engine output (peak ${(20 * Math.log10(m.outPk)).toFixed(1)} dBFS at master -12 dB)`);
  const nan = await page.evaluate(() => window.__rig.nanResets());
  check(nan === 0, `no NaN resets (${nan})`);
  check(m.load > 0 && m.load < 0.5, `DSP load reported ${(m.load * 100).toFixed(1)}% of the audio thread`);
  const info = await page.evaluate(() => ({ sr: window.__rig.ctx.sampleRate, base: window.__rig.ctx.baseLatency, hidden: document.getElementById('startOverlay').hidden }));
  check(info.hidden, `start overlay closed; context ${info.sr} Hz, base latency ${(info.base * 1000).toFixed(1)} ms`);

  console.log('2. Text protocol');
  await page.evaluate(() => window.__rig.cmd('param_set 9 feedback 0.5'));
  await sleep(150);
  let r = await page.evaluate(() => window.__rig.lastResp);
  check(r.status === -1, `bad instance -> resp ${r.status}`);
  await page.evaluate(() => window.__rig.cmd('param_set 3 wobble 0.5'));
  await sleep(150);
  r = await page.evaluate(() => window.__rig.lastResp);
  check(r.status === -103, `bad symbol -> resp ${r.status}`);
  await page.evaluate(() => window.__rig.cmd('param_set 4 size 0.95'));
  await sleep(300);
  let v = await page.evaluate(() => window.__rig.eff('tess', 'size'));
  check(Math.abs(v - 0.95) < 0.01, `param_set 4 size 0.95 -> engine reports ${v.toFixed(3)}`);
  await page.evaluate(() => window.__rig.cmd('param_set tess fold 0.6'));
  await sleep(300);
  r = await page.evaluate(() => window.__rig.lastResp);
  v = await page.evaluate(() => window.__rig.eff('tess', 'fold'));
  check(r.status === 0 && Math.abs(v - 0.6) < 0.15, `instance by id: param_set tess fold 0.6 -> resp ${r.status}, engine ${v.toFixed(3)} (heart-modulated)`);

  console.log('3. UI: patch CONTROLLER -> CAB MIX in the matrix, then sweep the controller slider');
  await page.click('#vMatrix');
  await page.click('button.cell[data-pid="cab.mix"][data-sid="ctrl"]');
  const setSlider = (v) => page.evaluate((v) => { const s = document.getElementById('ctrlSlider'); s.value = v; s.dispatchEvent(new Event('input', { bubbles: true })); }, v);
  await setSlider(-1); await sleep(500);
  v = await page.evaluate(() => window.__rig.eff('cab', 'mix'));
  check(Math.abs(v - 0.68) < 0.02, `controller -1 -> cab mix ${v.toFixed(3)} (expect 0.68)`);
  await setSlider(1); await sleep(500);
  v = await page.evaluate(() => window.__rig.eff('cab', 'mix'));
  check(v > 0.99, `controller +1 -> cab mix ${v.toFixed(3)} (expect 1.00, clamped)`);
  await page.screenshot({ path: path.join(shots, 'shot-matrix.png') });
  await page.click('#vStage');

  console.log('4. UI: bypass the delay from the focus panel, check modulation is moving');
  await page.click('#panel [data-act="bypass"]');
  await sleep(200);
  const log = await page.evaluate(() => window.__rig.log.slice(-3));
  check(log.some((l) => l === 'bypass 3 1'), `panel sent "${log[log.length - 1]}"`);
  const fb = [];
  for (let i = 0; i < 12; i++) { fb.push(await page.evaluate(() => window.__rig.eff('holo', 'feedback'))); await sleep(250); }
  const span = Math.max(...fb) - Math.min(...fb);
  check(span > 0.02, `LFO-modulated delay feedback moves over 3 s (span ${span.toFixed(3)})`);
  const onsets = await page.evaluate(() => window.__rig.srcMeter('adsr', 'onsets'));
  check(onsets > 3, `pick detector firing on the fake-mic riff (${onsets} onsets so far)`);

  console.log('5. Built-in riff source + 3D render');
  await page.click('#settingsBtn');
  await page.selectOption('#srcSel', 'riff');
  await sleep(1500);
  await page.screenshot({ path: path.join(shots, 'shot-settings.png') });
  await page.click('#settingsBtn');
  await sleep(300);
  m = await page.evaluate(() => window.__rig.meters);
  check(m.inPk > 0.01, `riff source feeds the engine (peak ${(20 * Math.log10(m.inPk)).toFixed(1)} dBFS)`);
  await page.screenshot({ path: path.join(shots, 'shot-stage.png') });

  check(errors.length === 0, `no console errors${errors.length ? ': ' + errors.join(' | ') : ''}`);
  await browser.close();
  console.log(fails ? `\n${fails} FAILED` : '\nALL BROWSER CHECKS PASSED');
  process.exit(fails ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(2); });

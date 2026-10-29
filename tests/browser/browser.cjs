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
  // the heart moves this knob by up to +/-0.22 around 0.6, so read its centre over two beats
  v = await page.evaluate(async () => { const xs = []; for (let i = 0; i < 40; i++) { xs.push(window.__rig.eff('tess', 'fold')); await new Promise((r) => setTimeout(r, 50)); } xs.sort((a, b) => a - b); return (xs[0] + xs[xs.length - 1]) / 2; });
  check(r.status === 0 && Math.abs(v - 0.6) < 0.12, `instance by id: param_set tess fold 0.6 -> resp ${r.status}, engine centre ${v.toFixed(3)} (heart-modulated)`);

  console.log('3. UI: patch CONTROLLER -> CAB MIX in the matrix, then sweep the controller slider');
  await page.click('#vMatrix');
  await page.click('button.cell[data-pid="cab.mix"][data-sid="ctrl"]');
  const setSlider = (v) => page.evaluate((v) => { const s = document.getElementById('ctrlSlider'); s.value = v; s.dispatchEvent(new Event('input', { bubbles: true })); }, v);
  // a meter packet made at least 0.3 s (audio time) after now: a busy headless page can hand them over late
  const fresh = async () => { const t0 = await page.evaluate(() => window.__rig.ctx.currentTime); await page.waitForFunction((t0) => window.__rig.meters.t > t0 + 0.3, t0, { timeout: 10000 }); };
  await setSlider(-1); await sleep(200); await fresh();
  v = await page.evaluate(() => window.__rig.eff('cab', 'mix'));
  check(Math.abs(v - 0.68) < 0.02, `controller -1 -> cab mix ${v.toFixed(3)} (expect 0.68)`);
  await setSlider(1); await sleep(200); await fresh();
  v = await page.evaluate(() => window.__rig.eff('cab', 'mix'));
  check(v > 0.99, `controller +1 -> cab mix ${v.toFixed(3)} (expect 1.00, clamped)`);
  await page.screenshot({ path: path.join(shots, 'shot-matrix.png') });
  await page.click('#vStage');

  console.log('4. UI: bypass the delay from the focus panel, check modulation is moving');
  await page.click('#panel [data-act="bypass"]');
  await sleep(200);
  const log = await page.evaluate(() => window.__rig.log.slice(-3));
  check(log.some((l) => l === 'bypass holo 1'), `panel sent "${log[log.length - 1]}"`);
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
  m = { inPk: await page.evaluate(async () => { let p = 0; const t0 = performance.now(); while (performance.now() - t0 < 1500) { p = Math.max(p, window.__rig.meters.inPk); await new Promise((r) => setTimeout(r, 25)); } return p; }) };   // the riff has gaps: take the peak over 1.5 s
  check(m.inPk > 0.01, `riff source feeds the engine (peak ${(20 * Math.log10(m.inPk)).toFixed(1)} dBFS)`);
  await page.screenshot({ path: path.join(shots, 'shot-stage.png') });

  console.log('6. Modulation sources: open, edit, depth, patch');
  await page.click('#overview'); await sleep(1500);
  const lbl = await page.evaluate(() => window.__rig.srcScreen('lfo', 'lbl'));
  await page.mouse.click(lbl.x, lbl.y); await sleep(400);
  let st = await page.evaluate(() => ({ focus: window.__rig.state.focus, carrying: window.__rig.state.carrying, h2: document.querySelector('#panel h2').textContent }));
  check(st.focus === 'src:lfo' && !st.carrying && st.h2 === 'LFO', `clicking the LFO's name opens its panel without picking up a cable (${st.h2})`);
  const orb = await page.evaluate(() => window.__rig.srcScreen('env', 'orb'));
  await page.mouse.click(orb.x, orb.y); await sleep(400);
  st = await page.evaluate(() => ({ focus: window.__rig.state.focus, carrying: window.__rig.state.carrying }));
  check(st.focus === 'src:env' && st.carrying === 'env', 'clicking an orb opens that source and picks up a cable');
  await page.keyboard.press('Escape'); await page.mouse.click(lbl.x, lbl.y); await sleep(400);
  const crossings = () => page.evaluate(async () => { let n = 0, last = window.__rig.src('lfo'); for (let i = 0; i < 40; i++) { await new Promise((r) => setTimeout(r, 50)); const v = window.__rig.src('lfo'); if ((v >= 0) !== (last >= 0)) n++; last = v; } return n; });
  const slow = await crossings();
  await page.$eval('[data-spid="lfo.rate"]', (el) => { el.value = '0'; el.dispatchEvent(new Event('input', { bubbles: true })); });
  await sleep(300);
  const fast = await crossings(), rate = await page.evaluate(() => window.__rig.srcParam('lfo', 'rate'));
  const shown = await page.textContent('[data-sdisp="lfo.rate"]');
  check(Math.abs(rate - 0.5) < 1e-6 && fast >= 8 && slow <= 2 && shown.trim() === '0.5 beats', `RATE knob: 8 beats -> 0.5 beats ("${shown.trim()}"); LFO zero crossings in 2 s: ${slow} -> ${fast}`);
  await page.$eval('[data-depth="holo.feedback"]', (el) => { el.value = '0.05'; el.dispatchEvent(new Event('input', { bubbles: true })); });
  await sleep(200);
  const dlog = await page.evaluate(() => window.__rig.log.slice(-3));
  check(dlog.includes("mod_connect holo feedback lfo 0.0500") && (await page.textContent(`[data-ddisp="holo.feedback"]`)) === "±5%", `depth slider: ${dlog[dlog.length - 1]}`);
  await page.selectOption('[data-srcpatch="lfo"]', 'gate.release'); await sleep(300);
  check(await page.evaluate(() => window.__rig.state.conns['gate.release'] === 'lfo' && document.querySelectorAll('#panel .patch').length === 2), 'Patch to: LFO -> GATE RELEASE added; panel lists 2 patches');
  await page.click('[data-act="reset-src"]'); await sleep(200);
  check(Math.abs((await page.evaluate(() => window.__rig.srcParam('lfo', 'rate'))) - 8) < 1e-4, 'Defaults puts RATE back to 8 beats');
  await page.screenshot({ path: path.join(shots, 'shot-source.png') });

  console.log('7. Side panel hides and comes back (button and P), remembered');
  await page.click('#panelToggle'); await sleep(400);
  st = await page.evaluate(() => ({ vis: getComputedStyle(document.getElementById('panel')).visibility, tab: document.getElementById('panelToggle').textContent.trim(), saved: localStorage.getItem('rig01.panelHidden') }));
  check(st.vis === 'hidden' && st.tab.endsWith('LFO') && st.saved === '1', `hidden; the tab reads “${st.tab}”; remembered (${st.saved})`);
  await page.keyboard.press('p'); await sleep(400);
  check(await page.evaluate(() => getComputedStyle(document.getElementById('panel')).visibility === 'visible' && localStorage.getItem('rig01.panelHidden') === '0'), 'P brings it back');

  console.log('8. Hall background and card faces');
  const bg = await page.evaluate(() => document.getElementById('backdrop').style.backgroundImage.slice(0, 30));
  const card = await page.evaluate(() => window.__rig.facePixel('cab', 259, 440)), glass = await page.evaluate(() => window.__rig.facePixel('cab', 100, 300));
  check(bg.startsWith('url("data:image/webp') && card[0] > 200 && card[1] > 200 && card[2] > 200 && glass[0] < 60, `background is the hall image; cab face: card ${card.slice(0, 3)}, screen ${glass.slice(0, 3)}`);

  check(errors.length === 0, `no console errors${errors.length ? ': ' + errors.join(' | ') : ''}`);
  await browser.close();
  console.log(fails ? `\n${fails} FAILED` : '\nALL BROWSER CHECKS PASSED');
  process.exit(fails ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(2); });

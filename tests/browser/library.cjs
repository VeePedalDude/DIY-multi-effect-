// tests/browser/library.cjs - library + presets in headless Chromium (browser-storage backend):
// drop files on a panel and on the stage, search, load into the engine, save a preset file,
// reload the page, load it back, then remove a file and check the preset keeps its reference.
// Then real files: NAM captures of several architectures play, a broken one is refused with a
// reason, and a long stereo IR runs as a convolution reverb.
const { chromium } = require(process.env.PLAYWRIGHT_CORE || 'playwright-core');
const path = require('path'), fs = require('fs');
const root = path.resolve(__dirname, '../..');
let fails = 0;
const check = (ok, msg) => { console.log(`  [${ok ? 'PASS' : 'FAIL'}] ${msg}`); if (!ok) fails++; };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// Runs in the page: build files and drop them on an element or a screen point.
async function drop(page, target, files) {
  await page.evaluate(async ({ target, files }) => {
    function stereoFloatWav(secs, seed) {          // decaying noise, like a room / reverb IR
      const n = Math.round(secs * 48000), b = new ArrayBuffer(44 + n * 8), v = new DataView(b); let o = 0, s = seed;
      const str = (t) => { for (const c of t) v.setUint8(o++, c.charCodeAt(0)); }, u32 = (x) => { v.setUint32(o, x, true); o += 4; }, u16 = (x) => { v.setUint16(o, x, true); o += 2; };
      str('RIFF'); u32(36 + n * 8); str('WAVE'); str('fmt '); u32(16); u16(3); u16(2); u32(48000); u32(384000); u16(8); u16(32); str('data'); u32(n * 8);
      for (let i = 0; i < n; i++) for (let c = 0; c < 2; c++) { s = (s * 1103515245 + 12345) >>> 0; v.setFloat32(o, ((s / 4294967296) * 2 - 1) * Math.pow(10, -3 * i / (0.8 * n)), true); o += 4; }
      return b;
    }
    function wav(frames, seed) {
      const b = new ArrayBuffer(44 + frames * 2), v = new DataView(b); let o = 0, s = seed;
      const str = (t) => { for (const c of t) v.setUint8(o++, c.charCodeAt(0)); }, u32 = (x) => { v.setUint32(o, x, true); o += 4; }, u16 = (x) => { v.setUint16(o, x, true); o += 2; };
      str('RIFF'); u32(36 + frames * 2); str('WAVE'); str('fmt '); u32(16); u16(1); u16(1); u32(48000); u32(96000); u16(2); u16(16); str('data'); u32(frames * 2);
      for (let i = 0; i < frames; i++) { s = (s * 1103515245 + 12345) >>> 0; v.setInt16(o, Math.round(((s / 4294967296) * 2 - 1) * Math.exp(-i / 60) * 30000), true); o += 2; }
      return b;
    }
    const dt = new DataTransfer();
    for (const f of files) dt.items.add(new File([f.reverb ? stereoFloatWav(f.reverb, f.seed || 1) : f.wav ? wav(f.wav, f.seed || 1) : f.text], f.name));
    let el, x = 0, y = 0;
    if (target.sel) { el = document.querySelector(target.sel); const r = el.getBoundingClientRect(); x = r.left + r.width / 2; y = r.top + r.height / 2; }
    else { x = target.x; y = target.y; el = document.elementFromPoint(x, y); }
    for (const type of ['dragover', 'drop']) el.dispatchEvent(new DragEvent(type, { dataTransfer: dt, clientX: x, clientY: y, bubbles: true, cancelable: true }));
  }, { target, files });
}

(async () => {
  const browser = await chromium.launch({ args: ['--autoplay-policy=no-user-gesture-required', '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'] });
  const context = await browser.newContext({ viewport: { width: 1600, height: 900 } });
  const page = await context.newPage();
  const errors = [];
  page.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });
  page.on('pageerror', (e) => errors.push('pageerror: ' + e.message));
  await context.route('**/cdnjs.cloudflare.com/**three.min.js', (r) => r.fulfill({ body: fs.readFileSync(process.env.THREE_JS), contentType: 'text/javascript' }));
  await context.route('**/fonts.googleapis.com/**', (r) => r.fulfill({ body: '', contentType: 'text/css' }));
  const open = async () => {
    await page.goto('file://' + path.join(root, 'dist/rig-test.html'));
    await page.waitForFunction(() => window.__rig && window.__rig.ready && window.__rig.library && window.__rig.library.core, null, { timeout: 10000 });
    await page.check('input[name=src][value=riff]'); await page.click('#startBtn');
    await page.waitForFunction(() => window.__rig.meters, null, { timeout: 10000 }); await sleep(1500);
  };
  // a 512-sample IR recorded at 48 kHz is resampled to the audio context's rate
  const taps512 = () => page.evaluate(() => Math.round(512 * window.__rig.ctx.sampleRate / 48000));
  const tapsOk = async () => Math.abs((await page.evaluate(() => window.__rig.modMeter('cab', 'taps'))) - (await taps512())) <= 1;
  const lib = () => page.evaluate(() => Object.fromEntries(Object.entries(window.__rig.library.entries).map(([k, v]) => [k, v.map((e) => e.name)])));

  console.log('1. Library starts in browser storage (no folder chosen)');
  await open();
  const where = await page.textContent('#libWhere');
  check(/Stored in this browser/.test(where), `“${where.slice(0, 60)}…”`);

  console.log('2. Drop an IR on the cab panel: imported and loaded into the engine');
  await page.keyboard.press('3'); await sleep(400);
  await drop(page, { sel: '[data-drop="cab"]' }, [{ name: 'V30 SM57.wav', wav: 512, seed: 7 }]);
  for (let i = 0; i < 25 && !(await tapsOk()); i++) await sleep(200);
  check((await lib()).ir.includes('V30 SM57.wav'), 'file is in the library (ir/)');
  check(await tapsOk(), `engine cab reports ${await page.evaluate(() => window.__rig.modMeter('cab', 'taps'))} taps (512 at 48 kHz = ${await taps512()} at ${await page.evaluate(() => window.__rig.ctx.sampleRate)} Hz)`);

  console.log('3. Drop on the stage: an IR on empty space goes to the library only; a .nam on the amp face is assigned to the amp');
  await page.click('#overview'); await sleep(1500);
  await drop(page, { x: 800, y: 760 }, [{ name: 'Greenback 4x12.wav', wav: 300, seed: 3 }]);
  await sleep(600);
  const amp = await page.evaluate(() => window.__rig.modScreen('amp'));
  await drop(page, { x: amp.x, y: amp.y }, [{ name: 'Plexi crunch.nam', text: JSON.stringify({ version: '0.5.2', architecture: 'WaveNet', sample_rate: 48000, metadata: { name: 'Plexi', gear_make: 'Marshall', gear_model: '1959', tone_type: 'crunch' }, weights: [0.1] }) }]);
  await sleep(800);
  const L = await lib(), st = await page.evaluate(() => ({ cab: window.__rig.state.assets.cab && window.__rig.state.assets.cab.name, amp: window.__rig.state.assets.amp && window.__rig.state.assets.amp.name }));
  check(L.ir.includes('Greenback 4x12.wav') && st.cab === 'V30 SM57.wav', 'stage background: imported, cab keeps its IR');
  check(L.nam.includes('Plexi crunch.nam') && st.amp === 'Plexi crunch.nam', `amp face: NAM imported and assigned (${st.amp})`);

  console.log('4. Search inside the cab panel');
  await page.keyboard.press('3'); await sleep(400);
  await page.fill('#as-cab', 'green'); await sleep(200);
  const hits = await page.$$eval('#al-cab li button', (b) => b.map((x) => x.querySelector('span').textContent));
  check(hits.length === 1 && hits[0] === 'Greenback 4x12.wav', `“green” finds ${JSON.stringify(hits)}`);
  await page.fill('#as-cab', '');

  console.log('5. Save a preset file');
  await page.click('#settingsBtn'); await page.fill('#presetInput', 'Lib Test'); await page.click('#presetSave'); await sleep(500);
  const saved = await page.evaluate(async () => JSON.parse(await (await window.__rig.library.core.file('presets', 'Lib Test.rig.json')).text()));
  const cabEntry = saved.chain.find((c) => c.id === 'cab'), ampEntry = saved.chain.find((c) => c.id === 'amp');
  check(saved.format === 'rig-preset' && saved.version === 1 && cabEntry.asset.name === 'V30 SM57.wav' && /^[0-9a-f]{8}$/.test(cabEntry.asset.hash) && ampEntry.asset.kind === 'nam', 'presets/Lib Test.rig.json written with both file references');
  check(typeof cabEntry.params.locut === 'number' && saved.chain.find((c) => c.id === 'holo').params.feedback > 1, `values in real units (holo feedback ${saved.chain.find((c) => c.id === 'holo').params.feedback} %)`);
  await page.screenshot({ path: path.join(process.env.SHOTS || path.join(root, 'build'), 'shot-library.png') });

  console.log('6. Reload the page: library and preset persist, and the preset loads back');
  await open();
  const L2 = await lib();
  check(L2.ir.length === 2 && L2.nam.length === 1 && L2.presets.includes('Lib Test.rig.json'), `after reload: ${L2.ir.length} IRs, ${L2.nam.length} NAM, presets ${JSON.stringify(L2.presets)}`);
  check(!(await tapsOk()), 'fresh start uses the built-in cab');
  await page.click('#settingsBtn'); await page.selectOption('#presetSel', 'Lib Test.rig.json'); await page.click('#presetLoad');
  for (let i = 0; i < 25 && !(await tapsOk()); i++) await sleep(200);
  const note = await page.textContent('#presetNote');
  check((await tapsOk()) && /Loaded presets\/Lib Test/.test(note), `preset reloaded the IR into the engine; note: “${note}”`);
  check(await page.evaluate(() => window.__rig.state.assets.amp && window.__rig.state.assets.amp.name) === 'Plexi crunch.nam', 'and re-assigned the NAM capture');

  console.log('7. Remove the IR file: the preset still loads, says what is missing, and keeps the reference');
  await page.click('#settingsBtn'); await page.keyboard.press('3'); await sleep(400);
  await page.click('[data-act="asset-remove"][data-mod="cab"]'); await page.click('[data-act="asset-remove"][data-mod="cab"]'); await sleep(600);
  check(!(await lib()).ir.includes('V30 SM57.wav'), 'file removed from the library after confirming');
  await page.click('#settingsBtn'); await page.selectOption('#presetSel', 'Lib Test.rig.json'); await page.click('#presetLoad'); await sleep(800);
  const note2 = await page.textContent('#presetNote');
  check(/isn't in the library/.test(note2) && !(await tapsOk()), `loads on built-in with a note: “${note2.slice(0, 80)}…”`);
  await page.click('#presetSave'); await sleep(500);
  const resaved = await page.evaluate(async () => JSON.parse(await (await window.__rig.library.core.file('presets', 'Lib Test.rig.json')).text()));
  check(resaved.chain.find((c) => c.id === 'cab').asset.name === 'V30 SM57.wav', 're-saving keeps the missing file’s reference');

  console.log('8. Real captures play; a broken one is refused with a reason');
  const ampMeter = () => page.evaluate(() => window.__rig.modMeter('amp', 'rate'));
  const ampLine = () => page.textContent('[data-asset-mod="amp"] .cur');
  await page.keyboard.press('2'); await sleep(400);
  check(/couldn.t load/.test(await ampLine()) || (await ampMeter()) === 0, `the hand-made “Plexi crunch.nam” has no real network: amp stays built-in (“${(await ampLine()).trim().slice(0, 90)}”)`);
  for (const f of ['A2.nam', 'lstm.nam', 'wavenet_a1_standard.nam', 'convnet.nam']) {
    await drop(page, { sel: '[data-drop="amp"]' }, [{ name: f, text: fs.readFileSync(path.join(root, 'tests/models', f), 'utf8') }]);
    for (let i = 0; i < 30 && !(/48 kHz/.test(await ampLine()) && (await ampMeter()) === 48000); i++) await sleep(200);
    await sleep(1200);
    const r = await page.evaluate(() => ({ out: window.__rig.meters.outPk, nan: window.__rig.nanResets(), load: window.__rig.meters.load }));
    check((await ampMeter()) === 48000 && r.out > 0.001 && r.nan === 0, `${f}: plays (“${(await ampLine()).trim().slice(0, 70)}”, out peak ${r.out.toFixed(3)}, engine load ${(100 * r.load).toFixed(0)}%)`);
  }

  console.log('9. A 3 s stereo IR: convolution reverb');
  await page.keyboard.press('3'); await sleep(400);
  await drop(page, { sel: '[data-drop="cab"]' }, [{ name: 'Hall 3s.wav', reverb: 3, seed: 5 }]);
  const cabM = () => page.evaluate(() => [window.__rig.modMeter('cab', 'taps'), window.__rig.modMeter('cab', 'chans')]);
  for (let i = 0; i < 30 && (await cabM())[1] !== 2; i++) await sleep(200);
  await sleep(1200);
  const [taps, chans] = await cabM(), r9 = await page.evaluate(() => ({ nan: window.__rig.nanResets(), load: window.__rig.meters.load }));
  const cabLine = (await page.textContent('[data-asset-mod="cab"] .cur')).trim();
  check(taps === 144000 && chans === 2 && r9.nan === 0 && /stereo · 3\.00 s/.test(cabLine), `engine runs ${taps} taps × ${chans} channels (“${cabLine.slice(0, 60)}”, engine load ${(100 * r9.load).toFixed(0)}%)`);
  await page.screenshot({ path: path.join(process.env.SHOTS || path.join(root, 'build'), 'shot-nam-reverb.png') });

  check(errors.length === 0, `no console errors${errors.length ? ': ' + errors.join(' | ') : ''}`);
  await browser.close();
  console.log(fails ? `\n${fails} FAILED` : '\nALL LIBRARY CHECKS PASSED');
  process.exit(fails ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(2); });

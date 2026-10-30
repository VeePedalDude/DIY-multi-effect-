// controllers.cjs - CONTROLLER sources bound to USB devices: a fake flight stick (the Gamepad API) and a
// fake MIDI controller (Web MIDI) are plugged in; LEARN, the values reaching the engine, INVERT / RANGE /
// TOGGLE, unplugging, and bindings coming back after a reload.
const { chromium } = require(process.env.PLAYWRIGHT_CORE || 'playwright-core');
const path = require('path'), fs = require('fs');
const root = path.resolve(__dirname, '../..');
let fails = 0;
const check = (ok, msg) => { console.log(`  [${ok ? 'PASS' : 'FAIL'}] ${msg}`); if (!ok) fails++; };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const HOTAS = 'Fake HOTAS X (Vendor: 044f Product: b108)';

(async () => {
  const browser = await chromium.launch({ args: ['--autoplay-policy=no-user-gesture-required', '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'] });
  const context = await browser.newContext({ viewport: { width: 1600, height: 1000 } });
  await context.addInitScript((HOTAS) => {
    // a flight stick: 6 axes (x, y, throttle, twist, ...), 12 buttons; tests move it through window.__pads
    window.__pads = [{ id: HOTAS, index: 0, connected: true, axes: [0, 0, -1, 0, 0, 0], buttons: Array.from({ length: 12 }, () => ({ value: 0, pressed: false })) }];
    navigator.getGamepads = () => window.__pads.slice();
    window.__press = (i, on) => { window.__pads[0].buttons[i] = { value: on ? 1 : 0, pressed: on }; };
    // a MIDI controller with one input
    const input = { id: 'in1', name: 'Fake nanoKONTROL2', type: 'input', state: 'connected', onmidimessage: null };
    window.__midi = (bytes) => { if (input.onmidimessage) input.onmidimessage({ data: new Uint8Array(bytes) }); };
    navigator.requestMIDIAccess = async () => ({ inputs: new Map([['in1', input]]), outputs: new Map(), onstatechange: null, sysexEnabled: false });
  }, HOTAS);
  const page = await context.newPage();
  const errors = [];
  page.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });
  page.on('pageerror', (e) => errors.push('pageerror: ' + e.message));
  await page.route('**/cdnjs.cloudflare.com/**three.min.js', (r) => r.fulfill({ body: fs.readFileSync(process.env.THREE_JS), contentType: 'text/javascript' }));
  await page.route('**/fonts.googleapis.com/**', (r) => r.fulfill({ body: '', contentType: 'text/css' }));
  const start = async () => {
    await page.goto('file://' + path.join(root, 'dist/rig-test.html'));
    await page.waitForFunction(() => window.__rig && window.__rig.ready, null, { timeout: 8000 });
    await page.check('input[name=src][value=riff]'); await page.click('#startBtn');
    await page.waitForFunction(() => window.__rig.meters, null, { timeout: 8000 }); await sleep(600);
  };
  const open = async (id) => { const l = await page.evaluate((id) => window.__rig.srcScreen(id, 'lbl'), id); await page.mouse.click(l.x, l.y); await sleep(400); };
  const lastExt = (id) => page.evaluate((id) => window.__rig.log.filter((l) => l.startsWith(`ext ${id} `)).slice(-1)[0], id);
  const value = (id) => page.evaluate((id) => window.__rig.src(id), id);
  const settle = async (id, want) => { for (let i = 0; i < 40; i++) { const v = await value(id); if (Math.abs(v - want) < 0.02) return v; await sleep(50); } return value(id); };
  await start();

  console.log('1. The CONTROLLER panel lists what is plugged in');
  await open('ctrl');
  const t1 = await page.textContent('#inView');
  check(/Not bound/.test(t1) && t1.includes('Fake HOTAS X') && !t1.includes('Vendor') && /MIDI: off/.test(t1), 'not bound yet; lists “Fake HOTAS X”; MIDI off with ENABLE MIDI');

  console.log('2. LEARN a flight-stick axis');
  await page.click('[data-in="learn"]'); await sleep(200);
  await page.evaluate(() => { window.__pads[0].axes[3] = 0.8; }); await sleep(400);
  const t2 = await page.textContent('#inBound');
  await page.evaluate(() => { window.__pads[0].axes[3] = 0.5; }); await sleep(300);
  const v2 = await settle('ctrl', 0.5), e2 = await lastExt('ctrl');
  check(t2.includes('Fake HOTAS X · axis 4'), `twisting the stick binds it: “${t2.trim()}”`);
  check(e2 === 'ext ctrl 0.500' && Math.abs(v2 - 0.5) < 0.02, `twist to 0.5 -> ${e2}; the engine's CONTROLLER reads ${v2.toFixed(3)}`);
  await page.click('[data-in="invert"]'); await sleep(200); const v3 = await settle('ctrl', -0.5);
  await page.click('[data-in="range"]'); await sleep(200); const v4 = await settle('ctrl', 0.25);
  check(Math.abs(v3 + 0.5) < 0.02 && Math.abs(v4 - 0.25) < 0.02, `INVERT: ${v3.toFixed(2)}; RANGE 0…+1 (inverted): ${v4.toFixed(2)}`);
  await page.evaluate(() => { window.__pads[0].axes[0] = 0.9; window.__pads[0].axes[1] = -0.7; }); await sleep(300);
  check(Math.abs((await value('ctrl')) - 0.25) < 0.02, 'moving other axes changes nothing');

  console.log('3. A second CONTROLLER on a button: momentary and toggle');
  await page.evaluate(() => window.__rig.rack.add('ctrl')); await sleep(600); await open('ctrl2');
  await page.click('[data-in="learn"]'); await page.evaluate(() => window.__press(4, true)); await sleep(300);
  const t3 = await page.textContent('#inBound'); const p1 = await settle('ctrl2', 1);
  await page.evaluate(() => window.__press(4, false)); const p2 = await settle('ctrl2', -1);
  check(t3.includes('button 5') && p1 > 0.98 && p2 < -0.98, `“${t3.trim()}”: held +${p1.toFixed(2)}, released ${p2.toFixed(2)}`);
  await page.click('[data-in="mode"]'); await sleep(150);
  const tg = [];
  for (let i = 0; i < 2; i++) { await page.evaluate(() => window.__press(4, true)); await sleep(400); await page.evaluate(() => window.__press(4, false)); await sleep(150); tg.push(await settle('ctrl2', i ? -1 : 1)); }
  check(tg[0] > 0.98 && tg[1] < -0.98, `TOGGLE: first press latches +${tg[0].toFixed(2)}, the second releases ${tg[1].toFixed(2)}`);

  console.log('4. MIDI');
  await page.click('[data-in="midi"]'); await sleep(300);
  const t4 = await page.textContent('#inView');
  await page.click('[data-in="learn"]'); await page.evaluate(() => window.__midi([0xB0, 14, 100])); await sleep(200);
  const t5 = await page.textContent('#inBound');
  await page.evaluate(() => window.__midi([0xB0, 14, 0])); const m0 = await settle('ctrl2', -1);
  await page.evaluate(() => window.__midi([0xB0, 15, 127])); await sleep(200); const m1 = await value('ctrl2');
  await page.evaluate(() => window.__midi([0xB0, 14, 127])); const m2 = await settle('ctrl2', 1);
  check(t4.includes('Fake nanoKONTROL2') && t5.includes('Fake nanoKONTROL2 · CC 14 · ch 1'), `ENABLE MIDI lists it; LEARN on a knob: “${t5.trim()}”`);
  check(m0 < -0.98 && m1 < -0.98 && m2 > 0.98, `CC 14 at 0 -> ${m0.toFixed(2)}; CC 15 ignored (${m1.toFixed(2)}); CC 14 at 127 -> +${m2.toFixed(2)}`);
  await page.click('[data-in="learn"]'); await page.evaluate(() => window.__midi([0xE0, 0, 64])); await sleep(200);
  await page.evaluate(() => window.__midi([0xE0, 0, 96])); const pb = await settle('ctrl2', 0.5);
  check((await page.textContent('#inBound')).includes('pitch bend') && Math.abs(pb - 0.5) < 0.02, `pitch bend learned; bend to 3/4 -> ${pb.toFixed(2)}`);

  console.log('5. Unplug and plug back in');
  const boundText = (re, want) => page.waitForFunction(([re, want]) => { const el = document.getElementById('inBound'); return el && new RegExp(re).test(el.textContent) === want; }, [re, want], { timeout: 8000 }).then(() => true, () => false);
  await open('ctrl'); await page.evaluate(() => { window.__saved = window.__pads; window.__pads = []; });
  const gone = await boundText('waiting for it to be plugged in', true);
  await page.evaluate(() => { window.__pads = window.__saved; window.__pads[0].axes[3] = -1; });
  const back = await boundText('waiting', false), v6 = await settle('ctrl', 1);
  check(gone && back && v6 > 0.98, 'unplugged: “waiting for it to be plugged in”; back in, it reads again (+1 at full twist, inverted, 0…+1)');

  console.log('6. Bindings come back after a reload; the slider still moves an unbound CONTROLLER');
  await start();
  const st = await page.evaluate(() => window.__rig.inputs.state());
  check(st.inputs.ctrl && st.inputs.ctrl.device === HOTAS && st.inputs.ctrl.invert && st.inputs.ctrl.range === 'pos' && st.midi === 'on', `after a reload: ctrl ← ${st.inputs.ctrl && st.inputs.ctrl.type} ${st.inputs.ctrl && st.inputs.ctrl.num + 1}, inverted, 0…+1; MIDI back on by itself`);
  await open('ctrl'); await page.click('[data-in="unbind"]'); await sleep(200);
  await page.$eval('#ctrlSlider', (el) => { el.value = '0.3'; el.dispatchEvent(new Event('input', { bubbles: true })); }); await sleep(200);
  check((await lastExt('ctrl')) === 'ext ctrl 0.300' && /Not bound/.test(await page.textContent('#inView')), 'UNBIND, then the CONTROLLER slider: ext ctrl 0.300');

  check(errors.length === 0, `no console errors${errors.length ? ': ' + errors.join(' | ') : ''}`);
  await browser.close();
  console.log(fails ? `\n${fails} FAILED` : '\nALL CONTROLLER CHECKS PASSED');
  process.exit(fails ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(2); });

// blocked.cjs - simulate a host that blocks gamepads (the in-app preview error) and check the page survives
const { chromium } = require(process.env.PLAYWRIGHT_CORE || 'playwright-core');
const path = require('path'), fs = require('fs');
const root = path.resolve(__dirname, '../..');
let fails = 0; const check = (ok, msg) => { console.log(`  [${ok ? 'PASS' : 'FAIL'}] ${msg}`); if (!ok) fails++; };
(async () => {
  const browser = await chromium.launch({ args: ['--autoplay-policy=no-user-gesture-required', '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'] });
  const page = await browser.newPage({ viewport: { width: 1600, height: 900 } });
  const errors = []; page.on('pageerror', (e) => errors.push(e.message));
  await page.addInitScript(() => {
    navigator.getGamepads = () => { throw new DOMException("Failed to execute 'getGamepads' on 'Navigator': Access to the feature \"gamepad\" is disallowed by permissions policy.", 'SecurityError'); };
    window.__frames = 0; const raf = window.requestAnimationFrame.bind(window);
    window.requestAnimationFrame = (cb) => raf((t) => { window.__frames++; cb(t); });
  });
  await page.route('**/cdnjs.cloudflare.com/**three.min.js', (r) => r.fulfill({ body: fs.readFileSync(process.env.THREE_JS), contentType: 'text/javascript' }));
  await page.route('**/fonts.googleapis.com/**', (r) => r.fulfill({ body: '', contentType: 'text/css' }));
  await page.goto('file://' + path.join(root, 'dist/rig-test.html'));
  await page.waitForFunction(() => window.__rig && window.__rig.ready, null, { timeout: 8000 });
  await page.check('input[name=src][value=riff]');
  await page.click('#startBtn');
  await page.waitForTimeout(2500);
  const f1 = await page.evaluate(() => window.__frames); await page.waitForTimeout(3000);
  const f2 = await page.evaluate(() => window.__frames);
  const st = await page.evaluate(() => ({ note: document.getElementById('padNote').textContent, out: window.__rig.meters && window.__rig.meters.outPk, overlay: document.getElementById('startOverlay').hidden }));
  check(errors.length === 0, `no uncaught errors${errors.length ? ': ' + errors[0] : ''}`);
  check(f2 - f1 >= 2, `3D stage keeps rendering (${f2 - f1} frames in 3 s; before the fix this stopped at 0; headless software GL is slow either way)`);
  check(/blocked/.test(st.note), `settings explain it: "${st.note.slice(0, 60)}…"`);
  check(st.overlay && st.out > 0.01, `engine still runs on the test riff (out peak ${(20 * Math.log10(st.out)).toFixed(1)} dBFS)`);
  await browser.close(); console.log(fails ? `${fails} FAILED` : 'ALL PASSED'); process.exit(fails ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(2); });

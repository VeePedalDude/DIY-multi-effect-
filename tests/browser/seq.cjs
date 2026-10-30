// seq.cjs - GATE SEQ in the page: clicking and painting squares, the lanes, the tools, EUCLID dimming,
// and that the playhead and flashes follow the engine (a switched-off square never flashes).
const { chromium } = require(process.env.PLAYWRIGHT_CORE || 'playwright-core');
const path = require('path'), fs = require('fs');
const root = path.resolve(__dirname, '../..');
const shots = process.env.SHOTS || path.join(root, 'build');
let fails = 0;
const check = (ok, msg) => { console.log(`  [${ok ? 'PASS' : 'FAIL'}] ${msg}`); if (!ok) fails++; };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

(async () => {
  const browser = await chromium.launch({ args: ['--use-fake-ui-for-media-stream', '--use-fake-device-for-media-stream',
    `--use-file-for-fake-audio-capture=${path.join(root, 'build/riff48k.wav')}`, '--autoplay-policy=no-user-gesture-required', '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'] });
  const page = await browser.newPage({ viewport: { width: 1600, height: 1000 } });
  const errors = [];
  page.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });
  page.on('pageerror', (e) => errors.push('pageerror: ' + e.message));
  await page.route('**/cdnjs.cloudflare.com/**three.min.js', (r) => r.fulfill({ body: fs.readFileSync(process.env.THREE_JS), contentType: 'text/javascript' }));
  await page.route('**/fonts.googleapis.com/**', (r) => r.fulfill({ body: '', contentType: 'text/css' }));
  await page.goto('file://' + path.join(root, 'dist/rig-test.html'));
  await page.waitForFunction(() => window.__rig && window.__rig.ready, null, { timeout: 8000 });
  await page.click('#startBtn'); await page.waitForFunction(() => window.__rig.meters, null, { timeout: 8000 }); await sleep(800);
  const cells = () => page.evaluate(() => window.__rig.srcObj('seq').table.v.slice(0, 16).map((r) => (r[0] >= 0.5 ? 'x' : '.')).join(''));
  const lastCell = () => page.evaluate(() => ({ cmd: window.__rig.log.filter((l) => l.startsWith('src_cell seq')).slice(-1)[0], resp: window.__rig.lastResp.status }));

  console.log('1. Add it: 16 squares, all on');
  await page.evaluate(() => window.__rig.rack.add('seq')); await sleep(600);
  const lbl = await page.evaluate(() => window.__rig.srcScreen('seq', 'lbl')); await page.mouse.click(lbl.x, lbl.y); await sleep(500);
  const n = await page.evaluate(() => document.querySelectorAll('#seqGrid .sq').length), on0 = await cells();
  check(n === 16 && on0 === 'xxxxxxxxxxxxxxxx', `GATE SEQ panel: ${n} squares, ${on0}`);

  console.log('2. Click and paint');
  await page.click('[data-sq="3"]'); await sleep(150);
  const c1 = await lastCell(), p1 = await page.evaluate(() => document.querySelector('[data-sq="3"]').getAttribute('aria-pressed'));
  check(c1.cmd === 'src_cell seq 3 on 0.000000' && c1.resp === 0 && p1 === 'false', `click square 4: ${c1.cmd} (engine ${c1.resp}); the square shows off`);
  const a = await page.locator('[data-sq="8"]').boundingBox(), b = await page.locator('[data-sq="11"]').boundingBox();
  await page.mouse.move(a.x + a.width / 2, a.y + a.height / 2); await page.mouse.down();
  for (let k = 1; k <= 10; k++) await page.mouse.move(a.x + a.width / 2 + (b.x - a.x) * k / 10, a.y + a.height / 2);
  await page.mouse.up(); await sleep(200);
  const c2 = await cells();
  check(c2 === 'xxx.xxxx....xxxx', `drag across squares 9-12 paints them off: ${c2}`);
  await page.focus('[data-sq="3"]'); await page.keyboard.press('Enter'); await sleep(150);
  check((await cells())[3] === 'x', 'keyboard: Enter on square 4 switches it back on');

  console.log('3. Off squares never flash; the playhead follows the engine');
  await page.evaluate(() => { window.__rig.seqFlashes.length = 0; }); await sleep(4000);
  const fl = await page.evaluate(() => window.__rig.seqFlashes.filter((f) => f.id === 'seq'));
  const hit = new Set(fl.map((f) => f.square));
  check(fl.length >= 20 && ![8, 9, 10, 11].some((i) => hit.has(i)) && [0, 4, 12].every((i) => hit.has(i)), `${fl.length} flashes in 4 s on squares ${[...hit].sort((x, y) => x - y).join(',')}; never 9-12`);
  const ph = await page.evaluate(async () => { const seen = new Set(); for (let i = 0; i < 40; i++) { await new Promise((r) => setTimeout(r, 50)); const el = document.querySelector('#seqGrid .sq.now'); if (el) seen.add(Number(el.dataset.sq)); } return [...seen]; });
  check(ph.length >= 8, `the outlined square moves with the engine's step meter: ${ph.length} different squares in 2 s`);

  console.log('4. Lanes');
  await page.click('[data-seqlane="ratchet"]');
  const lb = await page.locator('#seqLane').boundingBox();
  await page.mouse.click(lb.x + lb.width * (2.5 / 8), lb.y + lb.height * 0.04); await sleep(150);
  const r = await page.evaluate(() => { const s = window.__rig.srcObj('seq'), c = s.table.cols.find((x) => x.sym === 'ratchet'); return c.real(s.table.v[2][c.j]); });
  const c3 = await lastCell();
  check(r === 4 && c3.cmd === 'src_cell seq 2 ratchet 1.000000', `RATCHET lane: clicking the top of step 3 sets 4 hits (${c3.cmd})`);
  await page.click('[data-seqlane="value"]');
  await page.mouse.click(lb.x + lb.width * (0.5 / 8), lb.y + lb.height * (0.25 - 0.2)); await sleep(150);
  const v = await page.evaluate(() => { const s = window.__rig.srcObj('seq'), c = s.table.cols.find((x) => x.sym === 'value'); return c.real(s.table.v[0][c.j]); });
  check(v > 60, `VALUE lane: near the top of step 1 sets ${v.toFixed(0)}%`);

  console.log('5. Tools, STEPS and EUCLID');
  await page.click('[data-seqtool="right"]'); await sleep(200);
  const c4 = await cells();
  check(c4 === 'xxxxxxxxx....xxx', `rotate right (from xxxxxxxx....xxxx): ${c4}`);
  await page.click('[data-seqtool="clear"]'); await sleep(200);
  await page.evaluate(() => { window.__rig.seqFlashes.length = 0; }); await sleep(1200);
  check((await cells()) === '................' && (await page.evaluate(() => window.__rig.seqFlashes.length)) === 0, 'CLEAR: all off, no flashes for 1.2 s');
  await page.click('[data-seqtool="all"]'); await sleep(150);
  await page.$eval('[data-scfg="seq~steps"]', (el) => { el.value = String(7 / 31); el.dispatchEvent(new Event('input', { bubbles: true })); });
  await page.waitForFunction(() => document.querySelectorAll('#seqGrid .sq').length === 8, null, { timeout: 5000 }).catch(() => {});
  const n8 = await page.evaluate(() => document.querySelectorAll('#seqGrid .sq').length);
  await page.selectOption('[data-scfg="seq~pattern"]', { label: 'EUCLID' }); await sleep(300);
  await page.$eval('[data-spid="seq.fill"]', (el) => { el.value = String(3 / 32); el.dispatchEvent(new Event('input', { bubbles: true })); });
  const pat = () => [...document.querySelectorAll('#seqGrid .sq')].map((b) => (b.classList.contains('mute') ? '.' : 'x')).join('');
  await page.waitForFunction((f) => eval(f)() === 'x..x..x.', pat.toString(), { timeout: 5000 }).catch(() => {});
  const mute = await page.evaluate((f) => eval(f)(), pat.toString());
  check(n8 === 8 && mute === 'x..x..x.', `STEPS 8: ${n8} squares; EUCLID, FILL 3: ${mute} (the rest dimmed)`);
  await page.evaluate(() => { window.__rig.seqFlashes.length = 0; }); await sleep(3000);
  const eh = new Set(await page.evaluate(() => window.__rig.seqFlashes.map((f) => f.square)));
  check([...eh].every((i) => [0, 3, 6].includes(i)) && eh.size === 3, `it plays only squares ${[...eh].sort().join(', ')}`);
  await page.screenshot({ path: path.join(shots, 'shot-seq.png') });

  check(errors.length === 0, `no console errors${errors.length ? ': ' + errors.join(' | ') : ''}`);
  await browser.close();
  console.log(fails ? `\n${fails} FAILED` : '\nALL SEQ CHECKS PASSED');
  process.exit(fails ? 1 : 0);
})();

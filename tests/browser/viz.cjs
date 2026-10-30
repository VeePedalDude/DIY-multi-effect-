// tests/browser/viz.cjs - the Visualizer view in headless Chromium: the button (and V) swap the 3D
// stage for the rings and stop drawing it; one ring per source in the rack, in rack order and its
// colour, filling with the source's recent output and moving; adding and removing a source adds and
// removes a ring; clicking a ring opens that source; cross-modulation draws a link; a skin change
// recolours it; the panel hidden gives it the width; back to the stage.
const { chromium } = require(process.env.PLAYWRIGHT_CORE || 'playwright-core');
const path = require('path'), fs = require('fs');
const root = path.resolve(__dirname, '../..');
let fails = 0;
const check = (ok, msg) => { console.log(`  [${ok ? 'PASS' : 'FAIL'}] ${msg}`); if (!ok) fails++; };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const hex = (h) => { const n = parseInt(h.replace('#', '').slice(0, 6), 16); return [(n >> 16) & 255, (n >> 8) & 255, n & 255]; };
const near = (px, h, tol = 60) => { const c = hex(h); return Math.abs(px[0] - c[0]) + Math.abs(px[1] - c[1]) + Math.abs(px[2] - c[2]) < tol; };

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
  await page.waitForFunction(() => window.__rig.meters && window.__rig.engineRack, null, { timeout: 10000 }); await sleep(1200);
  const V = (expr) => page.evaluate(new Function('return (' + expr + ')'));
  const rings = () => page.evaluate(() => { window.__rig.viz.draw(); return window.__rig.viz.rings(); });
  const select = (sel, v) => page.evaluate(([sel, v]) => { const el = document.querySelector(sel); el.value = v; el.dispatchEvent(new Event('change', { bubbles: true })); }, [sel, v]);

  console.log('1. The Visualizer button swaps the stage for the rings');
  await page.click('#vViz'); await sleep(600);
  const shown = await V(`[!document.getElementById('viz').hidden, getComputedStyle(document.getElementById('stage')).visibility, document.getElementById('vViz').getAttribute('aria-pressed'), document.getElementById('overview').disabled]`);
  check(shown[0] && shown[1] === 'hidden' && shown[2] === 'true' && shown[3], `the rings show, the 3D stage (cards and orbs) is hidden, the button is pressed, Overview is off`);
  const f0 = await V('window.__rig.viz.stageFrames()'); await sleep(1500); const f1 = await V('window.__rig.viz.stageFrames()');
  check(f1 === f0, `the 3D stage isn't drawn meanwhile (${f1 - f0} frames in 1.5 s)`);

  console.log('2. One ring per source, in rack order and colour, filling with its output');
  await sleep(3000);
  let r = await rings(); const rack = await V('window.__rig.rack.list().map((s) => s.id).join()');
  check(r.map((x) => x.id).join() === rack && r.every((x, i) => i === 0 || x.rb > r[i - 1].rb), `${r.length} rings, innermost first: ${r.map((x) => x.id).join(', ')}`);
  check(r.every((x) => x.points > 100), `each holds its recent output (${Math.min(...r.map((x) => x.points))}+ points, ~8 s at most)`);
  // draw, then read each head's pixel in the same step (a meter packet in between would move the heads)
  const heads = await page.evaluate(() => { const v = window.__rig.viz; v.draw(); return v.rings().map((x) => { const h = v.head(x.id); return { color: x.color, px: v.pixel(h.x, h.y) }; }); });
  const hits = heads.map((h) => near(h.px, h.color));
  check(hits.every(Boolean), `each ring's head (12 o'clock, at its value now) is drawn in its source's colour`);
  const a = await V(`window.__rig.src('lfo')`); await sleep(1200); const b = await V(`window.__rig.src('lfo')`);
  const hy = await V(`(window.__rig.viz.draw(), window.__rig.viz.head('lfo').y)`);
  check(Math.abs(a - b) > 0.01, `it moves: the LFO went from ${a.toFixed(2)} to ${b.toFixed(2)}, its head now at y ${hy.toFixed(0)}`);
  await page.screenshot({ path: path.join(shots, 'shot-viz.png') });

  console.log('3. Adding and removing a source adds and removes a ring');
  await page.evaluate(() => window.__rig.rack.add('orbit')); await sleep(1500); r = await rings();
  const added = r.map((x) => x.id);
  await page.evaluate(() => window.__rig.rack.remove('orbit2')); await sleep(1200); const removed = (await rings()).map((x) => x.id);
  check(added.length === 7 && added[6] === 'orbit2' && removed.length === 6 && !removed.includes('orbit2'), `+ THREE-BODY 2: ${added.length} rings (outermost orbit2); removed: ${removed.length}`);

  console.log('4. Click a ring to edit its source; a cross-patch draws a link');
  r = await rings(); const lfo = r.find((x) => x.id === 'lfo'), c = await V('window.__rig.viz.centre()');
  await page.mouse.click(c.x + lfo.rb, c.y); await sleep(600);
  const h2 = await page.textContent('#panel h2');
  check(/LFO/.test(h2), `clicking the LFO's ring opens it in the panel (“${h2.trim()}”)`);
  await select('[data-spatch="lfo.rate"]', 'env'); await sleep(800);
  const linkOk = await page.evaluate(() => { window.__rig.viz.draw(); return window.__rig.state.conns['lfo.rate'] === 'env'; });
  const lp = await page.evaluate(() => ({ ...window.__rig.viz.linkEnd('lfo', 0), env: window.__rig.viz.rings().find((x) => x.id === 'env').color }));
  const lpx = await V(`window.__rig.viz.pixel(${lp.x}, ${lp.y})`);
  check(linkOk && near(lpx, lp.env, 90), `ENV FOLLOW → LFO rate: a link in the envelope's colour runs through the middle to the LFO's ring`);
  await page.screenshot({ path: path.join(shots, 'shot-viz-link.png') });

  console.log('5. The panel hidden gives the rings the width; a skin change recolours them');
  const R0 = (await V('window.__rig.viz.centre()')).R;
  await page.keyboard.press('p'); await sleep(500); await page.evaluate(() => window.__rig.viz.draw());
  const R1 = (await V('window.__rig.viz.centre()')).R;
  await page.keyboard.press('p'); await sleep(400);
  check(R1 >= R0, `P hides the panel: the circle grows or keeps its size (radius ${R0.toFixed(0)} → ${R1.toFixed(0)}), centred in the space`);
  const corner0 = await V(`(window.__rig.viz.draw(), window.__rig.viz.pixel(30, 400))`);
  await page.keyboard.press('k'); await sleep(1500);
  const corner1 = await V(`(window.__rig.viz.draw(), window.__rig.viz.pixel(30, 400))`);
  check(corner0.join() !== corner1.join(), `K (next skin): the veil behind the rings changes (${corner0.slice(0, 3)} → ${corner1.slice(0, 3)})`);
  await page.keyboard.press('k'); await sleep(800);

  console.log('6. V goes back to the stage, and again to the rings');
  await page.mouse.click(800, 870); await page.keyboard.press('v'); await sleep(600);
  const back = await V(`[document.getElementById('viz').hidden, getComputedStyle(document.getElementById('stage')).visibility, window.__rig.viz.stageFrames()]`);
  await sleep(1200); const g1 = await V('window.__rig.viz.stageFrames()');
  await page.keyboard.press('v'); await sleep(400); const again = await V(`!document.getElementById('viz').hidden`);
  check(back[0] && back[1] === 'visible' && g1 > back[2] && again, `V: the stage is back and drawing again (${g1 - back[2]} frames); V again: the rings`);

  check(errors.length === 0, `no console errors${errors.length ? ': ' + errors.join(' | ') : ''}`);
  await browser.close();
  console.log(fails ? `\n${fails} FAILED` : '\nALL VISUALIZER CHECKS PASSED');
  process.exit(fails ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(2); });

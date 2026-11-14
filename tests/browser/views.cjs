// tests/browser/views.cjs - views (docs/VIEWS.md) in headless Chromium, with the Scale Atlas bundle
// (dist/views/scale-atlas.rigview): no tab until it's in the library; importing it adds the tab; the view
// runs in a sandbox that can't reach the page, storage or the network; it draws on the card the page lays
// out, and it works (key and scale, the Exotic menu, highlighting degrees and chords, labels, tunings);
// a skin change, its shortcut key from inside its frame, settings kept across a reload, a bad bundle
// refused, and the file removed takes the tab away.
const { chromium } = require(process.env.PLAYWRIGHT_CORE || 'playwright-core');
const path = require('path'), fs = require('fs');
const root = path.resolve(__dirname, '../..');
let fails = 0;
const check = (ok, msg) => { console.log(`  [${ok ? 'PASS' : 'FAIL'}] ${msg}`); if (!ok) fails++; };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const hexRgb = (h) => [1, 3, 5].map((i) => parseInt(h.slice(i, i + 2), 16));
const near = (px, hex, tol = 40) => hexRgb(hex).every((v, i) => Math.abs(v - px[i]) <= tol);

(async () => {
  const browser = await chromium.launch({ args: ['--autoplay-policy=no-user-gesture-required', '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'] });
  const context = await browser.newContext({ viewport: { width: 1600, height: 900 } });
  const page = await context.newPage(), errors = [];
  page.on('console', (m) => { if (m.type() === 'error' && !/Content Security Policy|Content-Security-Policy|sandboxed|Failed to fetch|ERR_/i.test(m.text())) errors.push(m.text()); });
  page.on('pageerror', (e) => errors.push('pageerror: ' + e.message));
  await context.route('**/cdnjs.cloudflare.com/**three.min.js', (r) => r.fulfill({ body: fs.readFileSync(process.env.THREE_JS), contentType: 'text/javascript' }));
  await context.route('**/fonts.googleapis.com/**', (r) => r.fulfill({ body: '', contentType: 'text/css' }));
  let probed = 0; await context.route('**/probe.example/**', (r) => { probed++; r.fulfill({ body: 'x' }); });
  const shots = process.env.SHOTS || path.join(root, 'build');
  const open = async () => {
    await page.goto('file://' + path.join(root, 'dist/rig-test.html'));
    await page.waitForFunction(() => window.__rig && window.__rig.ready, null, { timeout: 10000 });
    await page.check('input[name=src][value=riff]'); await page.click('#startBtn');
    await page.waitForFunction(() => window.__rig.meters, null, { timeout: 10000 }); await sleep(800);
  };
  const V = (expr) => page.evaluate(new Function('return (' + expr + ')'));
  const importBytes = (name, bytes) => page.evaluate(async ([n, b64]) => (await window.__rig.importFiles([new File([Uint8Array.from(atob(b64), (c) => c.charCodeAt(0))], n)])).map((r) => `${r.status} ${r.reason || ''}`), [name, Buffer.from(bytes).toString('base64')]);
  const frame = () => page.frames().find((f) => f.parentFrame() && /scale-atlas|Scale Atlas/.test(f.name() + '') || (f !== page.mainFrame() && f.url() === 'about:srcdoc'));
  const F = (expr) => frame().evaluate(new Function('return (' + expr + ')'));
  const fsel = (sel, v) => frame().evaluate(([sel, v]) => { const el = document.querySelector(sel); el.value = v; el.dispatchEvent(new Event('change', { bubbles: true })); }, [sel, v]);
  const fclick = (sel) => frame().click(sel);
  const dot = async (s, f) => { const p = await F(`atlasTest.at(${s}, ${f})`); return F(`atlasTest.pixel(${p.x}, ${p.y - 6})`); };
  await open();
  await page.evaluate(() => { localStorage.removeItem('rig01.view.scale-atlas'); });

  console.log('1. No view until it is in the library; importing it adds its tab');
  check(!(await V(`document.getElementById('v-scale-atlas')`)), 'no Scale Atlas tab in a library without the file');
  const r1 = await importBytes('scale-atlas.rigview', fs.readFileSync(path.join(root, 'dist/views/scale-atlas.rigview')));
  await page.waitForFunction(() => document.getElementById('v-scale-atlas'), null, { timeout: 5000 });
  const info = await V('window.__rig.viewsInfo()');
  check(/^(added|duplicate)/.test(r1[0]) && info.length === 1 && info[0].tab === 'Scale Atlas' && info[0].letter === 's', `imported (${r1[0].trim()}): a Scale Atlas tab, shortcut S`);
  const bad = await importBytes('broken.rigview', Buffer.from('not a zip'));
  const fx = await importBytes('scale-atlas.rigfx', fs.readFileSync(path.join(root, 'dist/views/scale-atlas.rigview')));
  check(/^skipped/.test(bad[0]) && /^skipped/.test(fx[0]) && (await V('window.__rig.viewsInfo().length')) === 1, `a broken bundle is refused (${bad[0].trim()}); a view can't come in as an effect (${fx[0].trim()})`);

  console.log('2. Opening it: the stage gives way to its card, and the view draws on it');
  await page.click('#v-scale-atlas');
  await page.waitForFunction(() => window.__rig.viewsInfo()[0].ready, null, { timeout: 8000 });
  const shown = await V(`[getComputedStyle(document.getElementById('stage')).visibility, document.getElementById('v-scale-atlas').getAttribute('aria-pressed'), document.getElementById('overview').disabled, document.getElementById('view-scale-atlas').hidden]`);
  check(shown[0] === 'hidden' && shown[1] === 'true' && shown[2] && !shown[3], 'the 3D stage is hidden, the tab is pressed, Overview is off, the card is up');
  const f0 = await V('window.__rig.viz.stageFrames()'); await sleep(1000);
  check((await V('window.__rig.viz.stageFrames()')) === f0, "the 3D stage isn't drawn meanwhile");
  let notes = await F('atlasTest.notes()');
  check(notes.join(' ') === 'A:1 C:♭3 D:4 E:5 G:♭7', `A minor pentatonic to start: ${notes.join(' ')}`);
  const P = await F('atlasTest.colors()');
  const a5 = await dot(0, 5), g3 = await dot(0, 3), f1 = await dot(0, 1);
  check(near(a5, P.root) && near(g3, P.dot) && near(f1, P.glass, 30), `low E: A at fret 5 in the root colour, G at 3 a scale note, nothing at 1 (${a5.slice(0, 3)} / ${g3.slice(0, 3)} / ${f1.slice(0, 3)})`);
  const frameBox = await V(`(() => { const r = document.querySelector('#view-scale-atlas iframe').getBoundingClientRect(), c = document.querySelector('#view-scale-atlas canvas').getBoundingClientRect(); return [r.left, r.top, r.width, c.left, c.top, c.width]; })()`);
  check(frameBox.slice(0, 3).every((v, i) => Math.abs(v - frameBox[i + 3]) < 1), 'the view\'s frame lies exactly over the card the page drew');
  await page.screenshot({ path: path.join(shots, 'shot-view-atlas.png') });

  console.log('3. The sandbox');
  const box = await F(`(() => { const r = {};
    try { r.parent = !!window.parent.document; } catch (e) { r.parent = false; }
    try { localStorage.getItem('x'); r.storage = true; } catch (e) { r.storage = false; }
    try { r.cookie = document.cookie !== undefined && document.cookie !== null; document.cookie = 'a=1'; } catch (e) { r.cookie = false; }
    return r; })()`);
  const net = await F(`fetch('https://probe.example/x').then(() => 'reached', () => 'blocked')`);
  const img = await F(`new Promise((ok) => { const i = new Image(); i.onload = () => ok('reached'); i.onerror = () => ok('blocked'); i.src = 'https://probe.example/y.png'; })`);
  check(!box.parent && !box.storage && !box.cookie, 'its code can\'t reach the page, this browser\'s storage or cookies');
  check(net === 'blocked' && img === 'blocked' && probed === 0, `nor the network: fetch ${net}, an image ${img}, ${probed} requests left the page`);
  const sbx = await V(`document.querySelector('#view-scale-atlas iframe').getAttribute('sandbox')`);
  check(sbx === 'allow-scripts', `sandbox="${sbx}"`);

  console.log('4. It works: key and scale, the Exotic menu, highlighting, labels, tunings');
  await fclick('[data-ch="key"][data-v="0"]'); await fclick('[data-ch="scale"][data-v="major"]'); await sleep(150);
  const ch = await F('atlasTest.chords()');
  check((await F('atlasTest.notes()')).map((x) => x.split(':')[0]).join(' ') === 'C D E F G A B' && ch.map((x) => x[0]).join() === 'C,Dm,Em,F,G,Am,Bdim', `C major and its chords: ${ch.map((x) => x[0]).join(', ')}`);
  await fsel('#exotic', 'dblharm'); await sleep(150);
  check((await F('atlasTest.notes()')).map((x) => x.split(':')[0]).join(' ') === 'C D♭ E F G A♭ B' && (await F(`document.querySelectorAll('[data-ch="scale"][aria-pressed="true"]').length`)) === 0, 'Exotic: double harmonic, no scale button pressed');
  await fclick('[data-ch="scale"][data-v="major"]'); await sleep(100);
  await fclick('[data-ch="hl"][data-v="2"]'); await sleep(100);
  const e0 = await dot(0, 0), g = await dot(0, 3);
  check(near(e0, P.hi) && !near(g, P.dot, 20), `the 3rd highlighted: open E in the highlight colour (${e0.slice(0, 3)}), the rest dimmed`);
  await fclick('[data-ch="chord"][data-v="4.1"]'); await sleep(100);
  check((await F('atlasTest.chord()')) === 'G7' && (await F('atlasTest.hl()')).join() === '1,3,4,6' && (await F(`document.getElementById('info').textContent`)) === 'G7: G B D F', 'G7 clicked: its notes light up (“G7: G B D F”)');
  await fsel('#labels', 'note'); await fsel('#tuning', 'dropd'); await sleep(100);
  const d0 = await dot(0, 0);
  check(near(d0, P.hi) && /^C Major \(Ionian\): C \(1\)/.test(await F(`document.getElementById('cv').getAttribute('aria-label')`)), 'note names; Drop D puts D (in G7) on the open low string');

  console.log('5. Layout, the skin, its key from inside the frame, a reload');
  const fr15 = await F('atlasTest.frets()');
  await page.setViewportSize({ width: 2400, height: 800 }); await sleep(600); const fr22 = await F('atlasTest.frets()');
  await page.setViewportSize({ width: 1600, height: 900 }); await sleep(500);
  check(fr15 >= 15 && fr22 > fr15, `the card follows the window: frets 0-${fr15}, 0-${fr22} in a wide one`);
  const gl = await F('atlasTest.glass()'), gp = [gl[0] + 8, gl[1] + gl[3] - 8];
  await page.keyboard.press('k'); await sleep(1200);
  const P1 = await F('atlasTest.colors()'), g2 = await F(`atlasTest.pixel(${gp[0]}, ${gp[1]})`);
  check(near(g2, P1.glass, 30), `K: redrawn in the new skin's glass (${g2.slice(0, 3)} for ${P1.glass})`);
  await page.keyboard.press('k'); await sleep(600);
  await frame().focus('#cv').catch(() => {}); await frame().evaluate(() => document.body.dispatchEvent(new KeyboardEvent('keydown', { key: 's', bubbles: true })));
  await sleep(400);
  check((await V(`document.getElementById('view-scale-atlas').hidden`)) && (await V(`getComputedStyle(document.getElementById('stage')).visibility`)) === 'visible', 'S pressed inside the view goes back to the stage');
  const before = JSON.stringify(await page.evaluate(() => JSON.parse(localStorage.getItem('rig01.view.scale-atlas'))));
  await open(); await page.waitForFunction(() => document.getElementById('v-scale-atlas'), null, { timeout: 8000 });
  await page.keyboard.press('s'); await page.waitForFunction(() => window.__rig.viewsInfo()[0] && window.__rig.viewsInfo()[0].ready, null, { timeout: 8000 });
  check(JSON.stringify(await F('atlasTest.pick()')) === before && /"scale":"major"/.test(before), `after a reload the view is still in the library and opens where it was left (${before})`);

  console.log('6. Removing the file takes the tab away');
  await page.evaluate(async () => { const l = window.__rig.library; for (const e of l.entries.views) await l.core.remove('views', e.name); await window.__rig.refreshLibrary(); });
  await sleep(500);
  check(!(await V(`document.getElementById('v-scale-atlas')`)) && (await V(`getComputedStyle(document.getElementById('stage')).visibility`)) === 'visible', 'tab gone, back on the stage');
  check(errors.length === 0, `no console errors${errors.length ? ': ' + errors.join(' | ') : ''}`);
  await browser.close();
  console.log(fails ? `\n${fails} FAILED` : '\nALL VIEW CHECKS PASSED');
  process.exit(fails ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(2); });

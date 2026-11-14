// tests/browser/chart.cjs - the Chord Chart view in headless Chromium: the button and C swap the stage for
// the two cards, the chord is picked on the left card and spelled and drawn on the right, options a formula
// can't take are greyed, fret positions, paging, tunings, a skin change, and the picks surviving a reload.
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
  const chip = (act, v) => page.click(`[data-ch="${act}"][data-v="${v}"]`);
  const select = (sel, v) => page.evaluate(([sel, v]) => { const el = document.querySelector(sel); el.value = v; el.dispatchEvent(new Event('change', { bubbles: true })); }, [sel, v]);
  const frets = (x) => x.match(/\(\d+\)|\d|x/g).map((y) => y === 'x' ? -1 : +y.replace(/[()]/g, ''));     // "x(10)9" -> [-1, 10, 9]
  const chord = () => V('window.__rig.chart.chord()'), shapes = () => V('window.__rig.chart.shapes()');
  await open();
  await page.evaluate(() => localStorage.removeItem('rig01.chart'));

  console.log('1. The Chord Chart button swaps the stage for the two cards');
  await page.click('#vChart'); await sleep(500);
  const shown = await V(`[!document.getElementById('chart').hidden, getComputedStyle(document.getElementById('stage')).visibility, document.getElementById('vChart').getAttribute('aria-pressed'), document.getElementById('overview').disabled, document.getElementById('vViz').getAttribute('aria-pressed')]`);
  check(shown[0] && shown[1] === 'hidden' && shown[2] === 'true' && shown[3] && shown[4] === 'false', 'the cards show, the 3D stage is hidden, the button is pressed (Visualizer not), Overview is off');
  const f0 = await V('window.__rig.viz.stageFrames()'); await sleep(1200); const f1 = await V('window.__rig.viz.stageFrames()');
  check(f1 === f0, `the 3D stage isn't drawn meanwhile (${f1 - f0} frames in 1.2 s)`);
  const boxes = await V(`['chartPickCard', 'chartShapeCard'].map((id) => { const r = document.getElementById(id).getBoundingClientRect(); return [r.left, r.top, r.right, r.bottom]; })`);
  const topB = await V(`document.getElementById('top').getBoundingClientRect().bottom`), panelL = await V(`document.getElementById('panel').getBoundingClientRect().left`), boardT = await V(`document.getElementById('board').getBoundingClientRect().top`);
  check(boxes.every((b) => b[1] > topB && b[2] < panelL && b[3] < boardT) && boxes[0][2] < boxes[1][0], `side by side, between the top bar, the panel and the pedalboard (scale ${(await V('window.__rig.chart.layout().k')).toFixed(2)})`);

  console.log('2. C major to start: spelled, and its open shapes, best first');
  let c = await chord(), s = await shapes();
  check(c.name === 'C' && c.degrees === '1-3-(5)' && c.spelled === 'C-E-(G)', `${c.name}: ${c.degrees} = ${c.spelled}`);
  check(s[0] === 'x32010' && s.length === await V('window.__rig.chart.count()'), `open position, ${s.length} shapes, first ${s[0]}`);
  const P = await V('window.__rig.chart.colors()');
  const px = async (i, st) => { const p = await V(`window.__rig.chart.noteAt(${i}, ${st})`); return { ...p, px: await V(`window.__rig.chart.pixel(${p.x}, ${p.y})`) }; };
  const rootN = await px(0, 1), third = await px(0, 2), mute = await px(0, 0);
  check(rootN.fret === 3 && near(rootN.px, P.root) && third.fret === 2 && near(third.px, P.dot), `x32010 drawn: C on the A string's 3rd fret in the root colour (${rootN.px.slice(0, 3)}), E on the D string's 2nd in the dot colour (${third.px.slice(0, 3)})`);
  const label = await V(`document.getElementById('chartShapeCv').getAttribute('aria-label')`);
  check(mute.fret === -1 && /^C: C-E-\(G\)\. Shapes, low string first: x32010/.test(label), `screen readers get the chord and its shapes (“${label.slice(0, 60)}…”)`);
  await page.screenshot({ path: path.join(shots, 'shot-chart.png') });

  console.log('3. Picking a chord on the left card');
  const greyed = await V(`['ext|9', 'alts|b9', 'alts|s11', 'alts|sus4'].map((k) => { const [a, v] = k.split('|'); return document.querySelector('[data-ch="' + a + '"][data-v="' + v + '"]').disabled; })`);
  check(greyed.join() === 'true,true,true,false', 'on a triad the extensions and ♭9/♯11 are greyed; sus4 is open');
  await chip('key', 2); await chip('keyAcc', -1); await chip('formula', '7'); await chip('ext', 13); await chip('alts', 'b9'); await sleep(200);
  c = await chord();
  check(c.name === 'E♭13♭9' && c.spelled === 'E♭-G-(B♭)-D♭-F♭-C', `E, ♭, 7, 13, ♭9: ${c.name} = ${c.spelled} (the ♭9 of E♭ is F♭)`);
  await chip('formula', 'maj'); await sleep(200); c = await chord();
  const keptB9 = await V('window.__rig.chart.pick().alts');
  check(c.name === 'E♭' && !keptB9.includes('b9') && (await V('window.__rig.chart.pick().ext')) === 0, `back to a triad, the 13 and ♭9 are dropped, not kept out of sight (${c.name})`);
  await chip('key', 0); await chip('bass', 2); await sleep(200);
  c = await chord(); s = await shapes();
  const lowest = (x) => Math.min(...frets(x).map((f, i) => f < 0 ? 99 : [40, 45, 50, 55, 59, 64][i] + f)) % 12;
  check(c.name === 'C/E' && s.includes('032010') && s.every((x) => lowest(x) === 4), `bass E: ${c.name}, every shape with E lowest (${s.slice(0, 3).join(', ')} …)`);
  await chip('bass', 2); await sleep(150);
  check((await chord()).name === 'C', 'clicking the bass again takes it back to the root');

  console.log('4. Fret positions, pages, tunings');
  await select('#chartPos', '8'); await sleep(200); s = await shapes();
  const ok8 = s.length && s.every((x) => { const f = frets(x).filter((y) => y >= 0); return Math.min(...f) === 8 && Math.max(...f) <= 11; });
  check(ok8 && s.includes('8(10)(10)988'), `fret 8: ${s.length} shapes, all on frets 8-11, the E-shape barre among them`);
  const opts = await V(`Array.from(document.getElementById('chartPos').options).map((o) => o.textContent)`);
  check(opts.length === 13 && opts[0].startsWith('Open') && /^Fret 8 · \d+$/.test(opts[8]), `the position menu counts each position's shapes (${opts[0]}, ${opts[8]})`);
  await chip('key', 4); await select('#chartPos', '0'); await sleep(200);
  const n = await V('window.__rig.chart.count()'), p1 = await shapes();
  await page.click('#chartNext'); await sleep(150); const p2 = await shapes(), pg = await V(`document.getElementById('chartPage').textContent`);
  check(n > 24 && p1.length === 24 && p2.length === n - 24 && p2[0] !== p1[0] && /· 2\/2$/.test(pg) && await V(`document.getElementById('chartNext').disabled`), `G open: ${n} shapes, 24 to a page; › shows the rest (“${pg}”)`);
  await page.click('#chartPrev'); await sleep(100);
  check((await shapes())[0] === p1[0], '‹ goes back');
  await select('#chartTuning', 'dropd'); await chip('key', 1); await chip('formula', '5'); await sleep(200);
  c = await chord(); s = await shapes();
  check(c.name === 'D5' && s.includes('000xxx'), `Drop D: ${c.name} has the open 000xxx`);
  await select('#chartPos', '3'); await chip('formula', 'dim7'); await sleep(200);
  const empty = await V(`document.getElementById('chartShapeCv').getAttribute('aria-label')`);
  check((await V('window.__rig.chart.count()')) > 0 || /No shapes/.test(empty), 'a position with no shapes says so');

  console.log('5. A skin change redraws the cards; C goes back to the stage; the picks are remembered');
  const [gx, gy] = [120, 250], g0 = await V(`window.__rig.chart.pixel(${gx}, ${gy})`);
  await page.keyboard.press('k'); await sleep(1200);
  const g1 = await V(`window.__rig.chart.pixel(${gx}, ${gy})`), P1 = await V('window.__rig.chart.colors()');
  check(near(g1, P1.glass, 30) && (g0.join() !== g1.join() || P.glass === P1.glass), `K: the display is drawn in the new skin's glass (${g1.slice(0, 3)} for ${P1.glass})`);
  await page.keyboard.press('k'); await sleep(600);
  await page.mouse.click(800, 870); await page.keyboard.press('c'); await sleep(500);
  const back = await V(`[document.getElementById('chart').hidden, getComputedStyle(document.getElementById('stage')).visibility, window.__rig.viz.stageFrames()]`);
  await sleep(800); const g2 = await V('window.__rig.viz.stageFrames()');
  check(back[0] && back[1] === 'visible' && g2 > back[2], `C: the stage is back and drawing (${g2 - back[2]} frames)`);
  const before = await V('JSON.stringify(window.__rig.chart.pick())');
  await open(); await page.keyboard.press('c'); await sleep(500);
  check((await V('JSON.stringify(window.__rig.chart.pick())')) === before && (await chord()).name === 'Ddim7', `after a reload the chart opens where it was left (${(await chord()).name}, Drop D, fret 3)`);

  console.log('6. A narrow window stacks the cards');
  await page.setViewportSize({ width: 900, height: 1100 }); await sleep(500);
  const L = await V('window.__rig.chart.layout()');
  check(L.stacked, `900 x 1100: stacked, at scale ${L.k.toFixed(2)}`);
  await page.screenshot({ path: path.join(shots, 'shot-chart-stacked.png') });
  check(errors.length === 0, `no console errors${errors.length ? ': ' + errors.join(' | ') : ''}`);
  await browser.close();
  console.log(fails ? `\n${fails} FAILED` : '\nALL CHORD CHART CHECKS PASSED');
  process.exit(fails ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(2); });

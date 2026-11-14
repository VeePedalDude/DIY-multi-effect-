// tests/browser/atlas.cjs - the Scale Atlas view in headless Chromium: the button and S swap the stage for
// the two cards, key and scale picks spell the scale and draw it on the neck, highlighting degrees and
// chords, degrees or note names, tunings, a skin change, and the picks surviving a reload.
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
  const select = (sel, v) => page.evaluate(([sel, v]) => { const el = document.querySelector(sel); el.value = v; el.dispatchEvent(new Event('change', { bubbles: true })); }, [sel, v]);
  // the colour drawn at string s (0 = low), fret f
  const dot = async (s, f) => { const p = await V(`window.__rig.atlas.at(${s}, ${f})`); return V(`window.__rig.atlas.pixel(${p.x}, ${p.y - 6})`); };
  await open();
  await page.evaluate(() => localStorage.removeItem('rig01.atlas'));

  console.log('1. The Scale Atlas button swaps the stage for the two cards');
  await page.click('#vAtlas'); await sleep(500);
  const shown = await V(`[!document.getElementById('atlas').hidden, getComputedStyle(document.getElementById('stage')).visibility, document.getElementById('vAtlas').getAttribute('aria-pressed'), document.getElementById('chart').hidden, document.getElementById('overview').disabled]`);
  check(shown[0] && shown[1] === 'hidden' && shown[2] === 'true' && shown[3] && shown[4], 'the cards show, the 3D stage and the Chord Chart are hidden, the button is pressed, Overview is off');
  const f0 = await V('window.__rig.viz.stageFrames()'); await sleep(1000);
  check((await V('window.__rig.viz.stageFrames()')) === f0, 'the 3D stage isn\'t drawn meanwhile');

  console.log('2. A minor pentatonic to start, on the neck');
  let notes = await V('window.__rig.atlas.notes()');
  check(notes.join(' ') === 'A:1 C:♭3 D:4 E:5 G:♭7', `notes and degrees: ${notes.join(' ')}`);
  const P = await V('window.__rig.chart.colors()');
  const a5 = await dot(0, 5), g3 = await dot(0, 3), f1 = await dot(0, 1);
  check(near(a5, P.root) && near(g3, P.dot) && near(f1, P.glass, 30), `low E string: A at fret 5 in the root colour, G at 3 as a scale note, nothing at fret 1 (F) (${a5.slice(0, 3)} / ${g3.slice(0, 3)} / ${f1.slice(0, 3)})`);
  await page.screenshot({ path: path.join(shots, 'shot-atlas.png') });

  console.log('3. Key and scale: C major and its chords');
  await page.click('[data-ch="key"][data-v="0"]'); await page.click('[data-ch="scale"][data-v="major"]'); await sleep(200);
  notes = await V('window.__rig.atlas.notes()'); const ch = await V('window.__rig.atlas.chords()');
  check(notes.map((x) => x.split(':')[0]).join(' ') === 'C D E F G A B' && ch.map((x) => x[0]).join() === 'C,Dm,Em,F,G,Am,Bdim', `C major: ${notes.join(' ')}; chords on each degree start ${ch.map((x) => x[0]).join(', ')}`);
  const cols = await V(`document.querySelectorAll('#atlasChords .cchip').length`);
  check(cols === ch.flat().length, `${cols} chord buttons under the neck, a column per degree`);

  console.log('4. Highlighting: a degree, a chord, clear');
  await page.click('[data-ch="hl"][data-v="2"]'); await sleep(150);
  const e0 = await dot(0, 0), g = await dot(0, 3);
  check(near(e0, P.hi) && !near(g, P.dot, 20), `the 3rd highlighted: open E in the highlight colour (${e0.slice(0, 3)}), the rest dimmed (G ${g.slice(0, 3)})`);
  await page.click('[data-at="chord"][data-v="4.1"]'); await sleep(150);
  const hl = await V('window.__rig.atlas.hl()'), info = await V(`document.getElementById('atlasInfo').textContent`);
  check((await V('window.__rig.atlas.chord()')) === 'G7' && hl.join() === '1,3,4,6' && info === 'G7: G B D F', `G7 clicked: its notes light up (degrees ${hl.map((i) => i + 1).join(', ')}; “${info}”)`);
  const b2 = await dot(1, 2), gfr = await dot(0, 3);
  check(near(b2, P.hi) && near(gfr, P.hi), `B (A string, fret 2) and G (low E, fret 3) in the highlight colour`);
  await page.click('[data-at="chord"][data-v="4.1"]'); await sleep(150);
  check((await V('window.__rig.atlas.hl()')).length === 0, 'clicking G7 again clears it');
  await page.click('[data-ch="hl"][data-v="0"]'); await page.click('[data-ch="clear"]'); await sleep(100);
  check((await V('window.__rig.atlas.hl()')).length === 0 && await V(`document.querySelector('[data-ch="clear"]').disabled`), 'clear empties the highlight (and greys itself)');

  console.log('5. Labels, tunings, the skin, S, and a reload');
  await select('#atlasLabels', 'note'); await sleep(100);
  const lab = await V(`document.getElementById('atlasNeckCv').getAttribute('aria-label')`);
  check(/^C Major \(Ionian\): C \(1\), D \(2\)/.test(lab) && (await V('window.__rig.atlas.pick().labels')) === 'note', `note names on the dots; screen readers get the scale (“${lab.slice(0, 40)}…”)`);
  await select('#atlasTuning', 'dropd'); await sleep(100);
  const d0 = await dot(0, 0), d2 = await dot(0, 2);
  check(near(d0, P.dot) && near(d2, P.dot) && (await V(`window.__rig.atlas.pixel(${(await V('window.__rig.atlas.at(0, 1)')).x}, ${(await V('window.__rig.atlas.at(0, 1)')).y - 6})`)).every((v, i) => i === 3 || Math.abs(v - hexRgb(P.glass)[i]) < 30),
    'Drop D: the low string is D, so D (open) and E (fret 2) are scale notes and D♯ (fret 1) isn\'t');
  const g1 = await V(`window.__rig.atlas.pixel(120, 200)`);
  await page.keyboard.press('k'); await sleep(1200);
  const P1 = await V('window.__rig.chart.colors()'), g2 = await V(`window.__rig.atlas.pixel(120, 200)`);
  check(near(g2, P1.glass, 30), `K: the neck's display redrawn in the new skin's glass (${g1.slice(0, 3)} → ${g2.slice(0, 3)})`);
  await page.keyboard.press('k'); await sleep(600);
  await page.mouse.click(800, 870); await page.keyboard.press('s'); await sleep(500);
  const back = await V(`[document.getElementById('atlas').hidden, getComputedStyle(document.getElementById('stage')).visibility]`);
  check(back[0] && back[1] === 'visible', 'S: back to the stage');
  const before = await V('JSON.stringify(window.__rig.atlas.pick())');
  await open(); await page.keyboard.press('s'); await sleep(500);
  check((await V('JSON.stringify(window.__rig.atlas.pick())')) === before, `after a reload it opens where it was left (${before})`);
  await page.keyboard.press('c'); await sleep(400);
  check(!(await V(`document.getElementById('chart').hidden`)) && (await V(`document.getElementById('atlas').hidden`)), 'C from here goes straight to the Chord Chart');
  check(errors.length === 0, `no console errors${errors.length ? ': ' + errors.join(' | ') : ''}`);
  await browser.close();
  console.log(fails ? `\n${fails} FAILED` : '\nALL SCALE ATLAS CHECKS PASSED');
  process.exit(fails ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(2); });

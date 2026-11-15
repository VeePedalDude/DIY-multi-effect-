// tests/browser/viewctl.cjs - what a view may do to the rig (docs/VIEWS.md, "Permissions" and "Placement"),
// with the Quick Controls bundle (dist/views/quick-controls.rigview) and a test view made here: a docked
// view sits on the stage; nothing it asks for happens until the player allows it; then presets, tempo and
// bypass go through the page; a permission it never declared stays shut; knobs with `params`; values are
// clamped, unknown modules ignored, a flood is cut off; "Not now" is remembered; a reload doesn't ask again.
const { chromium } = require(process.env.PLAYWRIGHT_CORE || 'playwright-core');
const path = require('path'), fs = require('fs'), zlib = require('zlib');
const root = path.resolve(__dirname, '../..');
let fails = 0;
const check = (ok, msg) => { console.log(`  [${ok ? 'PASS' : 'FAIL'}] ${msg}`); if (!ok) fails++; };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
// a zip with stored files, enough for a .rigview
function zip(files) {
  const parts = [], dir = []; let at = 0;
  for (const [name, text] of Object.entries(files)) {
    const n = Buffer.from(name), d = Buffer.from(text), h = Buffer.alloc(30), c = Buffer.alloc(46), crc = zlib.crc32(d);
    h.writeUInt32LE(0x04034b50, 0); h.writeUInt16LE(20, 4); h.writeUInt32LE(crc, 14); h.writeUInt32LE(d.length, 18); h.writeUInt32LE(d.length, 22); h.writeUInt16LE(n.length, 26);
    c.writeUInt32LE(0x02014b50, 0); c.writeUInt16LE(20, 4); c.writeUInt16LE(20, 6); c.writeUInt32LE(crc, 16); c.writeUInt32LE(d.length, 20); c.writeUInt32LE(d.length, 24); c.writeUInt16LE(n.length, 28); c.writeUInt32LE(at, 42);
    parts.push(h, n, d); dir.push(c, n); at += 30 + n.length + d.length;
  }
  const cd = Buffer.concat(dir), e = Buffer.alloc(22), k = Object.keys(files).length;
  e.writeUInt32LE(0x06054b50, 0); e.writeUInt16LE(k, 8); e.writeUInt16LE(k, 10); e.writeUInt32LE(cd.length, 12); e.writeUInt32LE(at, 16);
  return Buffer.concat([...parts, cd, e]);
}
const KNOBS = zip({ 'manifest.json': JSON.stringify({ format: 'rigview', formatVersion: 1, id: 'knob-test', name: 'Knob test', tab: 'Knobs', feeds: ['chain'], permissions: ['params'] }),
  'view.js': "rig.on('chain', (c) => { window.chain = c; }); rig.on('init', () => rig.ready());" });

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
  const importBytes = (name, bytes) => page.evaluate(async ([n, b64]) => (await window.__rig.importFiles([new File([Uint8Array.from(atob(b64), (c) => c.charCodeAt(0))], n)])).map((r) => `${r.status} ${r.reason || ''}`), [name, Buffer.from(bytes).toString('base64')]);
  const frameOf = async (id) => (await page.$(`#view-${id} iframe`)).contentFrame();
  const info = async (id) => (await V('window.__rig.viewsInfo()')).find((v) => v.id === id);
  await open();
  await page.evaluate(() => { Object.keys(localStorage).filter((k) => k.startsWith('rig01.view.')).forEach((k) => localStorage.removeItem(k)); });

  console.log('1. A docked view: a button in the top bar, a small panel on the stage, the stage still there');
  const r1 = await importBytes('quick-controls.rigview', fs.readFileSync(path.join(root, 'dist/views/quick-controls.rigview')));
  await page.waitForFunction(() => document.getElementById('v-quick-controls'), null, { timeout: 5000 });
  let q = await info('quick-controls');
  check(/^(added|duplicate)/.test(r1[0]) && q.dock && !q.shown && q.letter === 'q', `imported (${r1[0].trim()}): a docked view, off until its button is pressed, shortcut Q`);
  await page.click('#v-quick-controls');
  await page.waitForFunction(() => window.__rig.viewsInfo().find((v) => v.id === 'quick-controls').ready, null, { timeout: 8000 });
  const box = await (await page.$('#view-quick-controls')).boundingBox();
  const st = await V(`[getComputedStyle(document.getElementById('stage')).visibility, window.__rig.state.view, document.getElementById('v-quick-controls').getAttribute('aria-pressed')]`);
  check(st[0] !== 'hidden' && st[1] === 'stage' && st[2] === 'true' && Math.round(box.width) === 302 && Math.round(box.height) === 170 && box.x < 40 && box.y > 450, `on the stage, bottom left above the pedalboard, 300 x 168 as its manifest asks (${Math.round(box.width)} x ${Math.round(box.height)} with its border); the 3D stage stays`);

  console.log('2. Nothing it asks for happens until the player allows it');
  q = await info('quick-controls');
  const ask = await page.textContent('#view-quick-controls .vask p');
  check(q.asking && q.granted.length === 0 && /switch presets and scenes, set the tempo, add, remove and bypass effects/.test(ask), `it asks first: “${ask}”`);
  let f = await frameOf('quick-controls');
  await f.evaluate(() => { rig.setTempo(99); rig.bypass('amp', true); }); await sleep(300);
  check((await V('window.__rig.state.bpm')) === 120 && !(await V('window.__rig.state.bypass.amp')), 'before the answer its commands are ignored (tempo still 120, AMP still on)');
  await page.click('#view-quick-controls [data-ask=allow]'); await sleep(400);
  q = await info('quick-controls');
  check(!q.asking && q.granted.join() === 'presets,tempo,chain' && (await f.evaluate(() => rig.can('tempo') && !document.getElementById('faster').disabled)), 'Allow: the three permissions are granted and the view is told (its buttons come on)');

  console.log('3. Tempo, bypass and presets through the page');
  const shown = await f.evaluate(() => [document.getElementById('bpm').textContent, [...document.querySelectorAll('#fx button')].map((b) => b.textContent + ':' + b.getAttribute('aria-pressed')).join(' ')]);
  const chain = await V('window.__rig.chain.list().map((m) => m.id)');
  check(shown[0] === '120 BPM' && shown[1].split(' ').length >= chain.length && /:true/.test(shown[1]), `its feeds: ${shown[0]}; effects ${shown[1]}`);
  await f.click('#faster'); await f.click('#faster'); await sleep(300);
  check((await V('window.__rig.state.bpm')) === 122 && (await page.textContent('#bpm')).trim() === '122' && (await f.evaluate(() => document.getElementById('bpm').textContent)) === '122 BPM', 'two presses of +: the rig\'s tempo is 122, in the bottom bar and back in the view');
  await f.click(`#fx button[data-id=${chain[0]}]`); await sleep(400);
  const byp = await V(`window.__rig.state.bypass['${chain[0]}']`), chip = await f.evaluate((id) => document.querySelector(`#fx button[data-id=${id}]`).getAttribute('aria-pressed'), chain[0]);
  check(byp === true && chip === 'false', `its ${chain[0]} switch bypasses the module, and the view shows it off`);
  await f.click(`#fx button[data-id=${chain[0]}]`); await sleep(300);
  check(!(await V(`window.__rig.state.bypass['${chain[0]}']`)), 'and back on');
  await page.click('#boardTreeBtn'); await sleep(300); await page.click('[data-act="tree-add-scene"]'); await sleep(1500); await page.click('#boardTreeBtn'); await sleep(200);
  const at1 = await V('[window.__rig.board.s, window.__rig.board.scenes.length]');
  await f.click('#prev'); await page.waitForFunction(() => window.__rig.board.s === 0, null, { timeout: 5000 }).catch(() => {}); await sleep(600);
  const at2 = await V('window.__rig.board.s'), nm = await f.evaluate(() => document.getElementById('name').textContent);
  check(at1[0] === 1 && at1[1] === 2 && at2 === 0 && /Scene 1/.test(nm), `with a second scene, ‹ steps back to the first (the view shows “${nm}”)`);
  await f.evaluate(() => rig.preset(7, 0)); await sleep(300);
  check((await V('window.__rig.board.s')) === 0, 'a preset that isn\'t there is ignored');

  console.log('4. A permission it never declared stays shut');
  const k0 = await V(`window.__rig.knob('amp.input')`);
  await f.evaluate(() => rig.setParam('amp', 'input', 0.91)); await sleep(300);
  check((await V(`window.__rig.knob('amp.input')`)) === k0, 'Quick Controls has no `params`: its setParam does nothing');
  await page.screenshot({ path: path.join(shots, 'shot-view-dock.png') });

  console.log('5. Knobs with `params`: clamped, unknown modules ignored, a flood cut off; Not now is remembered');
  await importBytes('knob-test.rigview', KNOBS);
  await page.waitForFunction(() => document.getElementById('v-knob-test'), null, { timeout: 5000 });
  await page.click('#v-knob-test');
  await page.waitForFunction(() => window.__rig.viewsInfo().find((v) => v.id === 'knob-test').ready, null, { timeout: 8000 });
  check((await info('knob-test')).asking && !(await info('quick-controls')).shown, 'a tab view asks over its card; the docked view leaves with the stage');
  await page.click('#view-knob-test [data-ask=deny]'); await sleep(300);
  let kf = await frameOf('knob-test');
  await kf.evaluate(() => rig.setParam('amp', 'input', 0.91)); await sleep(300);
  check((await V(`window.__rig.knob('amp.input')`)) === k0 && (await info('knob-test')).granted.length === 0, 'Not now: nothing granted, the knob stays');
  await page.click('#vStage'); await sleep(200); await page.click('#v-knob-test'); await sleep(400);
  check(!(await info('knob-test')).asking, 'and it isn\'t asked again');
  await page.evaluate(() => localStorage.removeItem('rig01.view.allow.knob-test'));
  await page.click('#vStage'); await sleep(200); await page.click('#v-knob-test'); await sleep(400);
  await page.click('#view-knob-test [data-ask=allow]'); await sleep(300);
  const seen = await kf.evaluate(() => window.chain && window.chain.find((m) => m.id === 'amp').params.map((p) => p.symbol).join());
  await kf.evaluate(() => rig.setParam('amp', 'input', 0.91)); await sleep(300);
  const k1 = await V(`window.__rig.knob('amp.input')`);
  check(/input/.test(seen) && Math.abs(k1 - 0.91) < 1e-6, `allowed: the chain feed lists AMP's knobs (${seen}) and setParam turns INPUT to ${k1}`);
  await kf.evaluate(() => { rig.setParam('amp', 'input', 7); rig.setParam('nope', 'input', 0.2); rig.setParam('amp', 'nope', 0.2); rig.setParam('amp', 'output', 'x'); }); await sleep(300);
  check((await V(`window.__rig.knob('amp.input')`)) === 1, 'a value of 7 stops at 1; an unknown module, an unknown knob and a value that isn\'t a number do nothing');
  await sleep(1100);
  await kf.evaluate(() => { for (let i = 1; i <= 500; i++) rig.setParam('amp', 'input', i / 1000); }); await sleep(500);
  const k2 = await V(`window.__rig.knob('amp.input')`);
  check(Math.abs(k2 - 0.12) < 1e-6, `500 commands at once: 120 are taken, the rest dropped (INPUT ends at ${k2})`);

  console.log('6. A reload: the dock comes back on the stage, nothing is asked again');
  await page.click('#vStage'); await sleep(300);
  check((await info('quick-controls')).shown, 'back on the stage the docked view is back');
  await open();
  await page.waitForFunction(() => { const v = window.__rig.viewsInfo().find((x) => x.id === 'quick-controls'); return v && v.ready; }, null, { timeout: 10000 });
  q = await info('quick-controls');
  check(q.shown && q.docked && !q.asking && q.granted.length === 3, 'docked and allowed, as it was left');
  await page.click('#v-quick-controls'); await sleep(300);
  check(!(await info('quick-controls')).shown && (await V(`document.getElementById('v-quick-controls').getAttribute('aria-pressed')`)) === 'false', 'its button switches it off');
  check(errors.length === 0, `no console errors${errors.length ? ': ' + errors.slice(0, 3).join(' | ') : ''}`);

  await browser.close();
  console.log(fails ? `\n${fails} VIEW CONTROL CHECK(S) FAILED` : '\nALL VIEW CONTROL CHECKS PASSED');
  process.exit(fails ? 1 : 0);
})();

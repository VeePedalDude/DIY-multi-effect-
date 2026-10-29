// tests/browser/skins.cjs - skins in headless Chromium: the built-ins, favourites and K, exporting a
// template, turning it into a new skin and dropping it on the stage, a broken skin, and the choice
// surviving a reload.
const { chromium } = require(process.env.PLAYWRIGHT_CORE || 'playwright-core');
const path = require('path'), fs = require('fs');
const root = path.resolve(__dirname, '../..');
let fails = 0;
const check = (ok, msg) => { console.log(`  [${ok ? 'PASS' : 'FAIL'}] ${msg}`); if (!ok) fails++; };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

(async () => {
  const browser = await chromium.launch({ args: ['--autoplay-policy=no-user-gesture-required', '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'] });
  const context = await browser.newContext({ viewport: { width: 1600, height: 900 }, acceptDownloads: true });
  const page = await context.newPage(), errors = [];
  page.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });
  page.on('pageerror', (e) => errors.push('pageerror: ' + e.message));
  await context.route('**/cdnjs.cloudflare.com/**three.min.js', (r) => r.fulfill({ body: fs.readFileSync(process.env.THREE_JS), contentType: 'text/javascript' }));
  await context.route('**/fonts.googleapis.com/**', (r) => r.fulfill({ body: '', contentType: 'text/css' }));
  const shots = process.env.SHOTS || path.join(root, 'build');
  const open = async () => {
    await page.goto('file://' + path.join(root, 'dist/rig-test.html'));
    await page.waitForFunction(() => window.__rig && window.__rig.ready && window.__rig.library && window.__rig.library.core, null, { timeout: 10000 });
    await page.check('input[name=src][value=riff]'); await page.click('#startBtn');
    await page.waitForFunction(() => window.__rig.meters, null, { timeout: 10000 }); await sleep(1500);
  };
  const look = () => page.evaluate(() => ({ id: window.__rig.skinState.id, accent: getComputedStyle(document.documentElement).getPropertyValue('--accent').trim(),
    bg: getComputedStyle(document.documentElement).getPropertyValue('--bg').trim(), stageImg: document.getElementById('backdrop').style.backgroundImage.slice(0, 22),
    card: window.__rig.facePixel('cab', 259, 440), saved: localStorage.getItem('rig01.skin') }));
  const light = (p) => p[0] > 200 && p[1] > 200 && p[2] > 200, dark = (p) => p[0] < 40 && p[1] < 40 && p[2] < 50;
  // What is really on screen at a point of empty stage (left edge, mid height): checks the background
  // is painted, not just set.
  const screenPx = async (x = 30, y = 420) => {
    const b = await page.screenshot({ clip: { x, y, width: 1, height: 1 } });
    return page.evaluate(async (b64) => { const i = new Image(); i.src = 'data:image/png;base64,' + b64; await i.decode(); const c = document.createElement('canvas'); c.width = c.height = 1; const x = c.getContext('2d'); x.drawImage(i, 0, 0); return Array.from(x.getImageData(0, 0, 1, 1).data).slice(0, 3); }, b.toString('base64'));
  };

  console.log('1. Starts in the default skin, Astral Hall');
  await open();
  let L = await look();
  const hallPx = await screenPx();
  check(L.id === 'builtin:astral' && L.stageImg.startsWith('url("data:image/webp') && light(L.card), `hall background set, light cards (${L.card.slice(0, 3)})`);
  check(!(hallPx[0] === 11 && hallPx[1] === 7 && hallPx[2] === 22) && hallPx.some((v) => v > 30), `the hall is really painted on screen (pixel ${hallPx}; the plain background colour would be 11,7,22)`);
  const t0 = await page.evaluate(() => document.getElementById('backdrop').style.transform);
  await page.mouse.move(700, 600); await page.mouse.down(); await page.mouse.move(400, 520, { steps: 8 }); await page.mouse.up(); await sleep(400);
  const t1 = await page.evaluate(() => document.getElementById('backdrop').style.transform);
  check(t0 !== t1 && /translate\(/.test(t1), `orbiting shifts the background against the cards (parallax): “${t0}” -> “${t1}”`);

  console.log('2. Classic from the Skins menu: dark cards, no image, remembered');
  await page.click('#settingsBtn'); await sleep(300);
  await page.click('[data-skin-use="builtin%3Aclassic"]'); await sleep(800);
  L = await look();
  const classicPx = await screenPx();
  check(L.id === 'builtin:classic' && L.stageImg === 'none' && L.bg === '#05070C' && dark(L.card) && L.saved === 'builtin:classic' && classicPx.every((v) => v < 30),
    `classic: --bg ${L.bg}, card ${L.card.slice(0, 3)}, plain dark stage on screen (${classicPx}), saved “${L.saved}”`);
  await page.screenshot({ path: path.join(shots, 'shot-skin-classic.png') });

  console.log('3. Favourites and K');
  await page.click('[data-skin-fav="builtin%3Aastral"]'); await page.click('[data-skin-fav="builtin%3Aclassic"]'); await sleep(200);
  await page.click('#settingsBtn'); await page.mouse.move(800, 600);
  await page.keyboard.press('k'); await sleep(900);
  const afterK = (await look()).id;
  await page.keyboard.press('k'); await sleep(900);
  const afterK2 = (await look()).id;
  check(afterK === 'builtin:astral' && afterK2 === 'builtin:classic' && (await page.evaluate(() => JSON.parse(localStorage.getItem('rig01.skinFavs')).length)) === 2, `K: classic -> ${afterK} -> ${afterK2}`);
  await page.keyboard.press('k'); await sleep(900);

  console.log('4. Export the skin in use as a template');
  await page.click('#settingsBtn'); await sleep(300);
  const [dl] = await Promise.all([page.waitForEvent('download'), page.click('#skinExport')]);
  const bytes = fs.readFileSync(await dl.path());
  const listed = await page.evaluate(async (b64) => {
    const u = Uint8Array.from(atob(b64), (c) => c.charCodeAt(0)), files = await window.__rig.skinTools.unzip(u), j = JSON.parse(new TextDecoder().decode(files.get('skin.json')));
    return { names: [...files.keys()], sections: Object.keys(j), tiles: Object.keys(j.tiles) };
  }, bytes.toString('base64'));
  check(dl.suggestedFilename() === 'Astral Hall template.rigskin' && ['skin.json', 'hall.webp', 'tile-gold.webp', 'tile-teal.webp', 'tile-purple.webp', 'SKIN-README.txt'].every((n) => listed.names.includes(n))
    && ['ui', 'stage', 'screen', 'card', 'knob', 'tiles', 'sources', 'labels'].every((s) => listed.sections.includes(s)),
    `${dl.suggestedFilename()} (${(bytes.length / 1024).toFixed(0)} KB): ${listed.names.length} files, every setting spelled out`);

  console.log('5. Edit the template into a new skin and drop it on the stage');
  const made = await page.evaluate(async (b64) => {
    const T = window.__rig.skinTools, u = Uint8Array.from(atob(b64), (c) => c.charCodeAt(0)), files = await T.unzip(u);
    const j = JSON.parse(new TextDecoder().decode(files.get('skin.json')));
    j.name = 'Neon Test'; j.author = 'Tester'; j.ui.accent = '#ff2bd6';
    j.stage.background = null; j.stage.backgroundColor = '#120018';
    j.tiles = { neon: { image: 'tile-purple.webp', accent: '#ff2bd6', rim: '#ff9be8', glow: '#ff2bd6', card: { title: '#ff2bd6' } } };
    j.tileOrder = ['neon']; j.modules = {}; j.css = 'neon.css';
    const out = [['Neon Test/skin.json', new TextEncoder().encode(JSON.stringify(j))], ['Neon Test/tile-purple.webp', files.get('tile-purple.webp')], ['Neon Test/neon.css', new TextEncoder().encode('.brand{color:#ff2bd6}')]];
    const dt = new DataTransfer(); dt.items.add(new File([T.zipFiles(out)], 'Neon Test.rigskin'));
    for (const type of ['dragover', 'drop']) document.getElementById('stage').dispatchEvent(new DragEvent(type, { dataTransfer: dt, clientX: 800, clientY: 700, bubbles: true, cancelable: true }));
    return true;
  }, bytes.toString('base64'));
  for (let i = 0; i < 30 && (await look()).id !== 'lib:Neon Test.rigskin'; i++) await sleep(200);
  await sleep(600);
  L = await look();
  const brand = await page.evaluate(() => getComputedStyle(document.querySelector('.brand')).color);
  const title = await page.evaluate(() => window.__rig.facePixel('cab', 90, 152));
  const libSkins = await page.evaluate(() => (window.__rig.library.entries.skins || []).map((e) => e.name));
  check(made && L.id === 'lib:Neon Test.rigskin' && L.accent === '#ff2bd6' && L.stageImg === 'none' && libSkins.includes('Neon Test.rigskin'),
    `dropped: imported to skins/ and put on (accent ${L.accent}, library has ${JSON.stringify(libSkins)})`);
  check(brand === 'rgb(255, 43, 214)' && light(L.card), `its skin.css applies (brand ${brand}); purple card frame with pink text`);
  await page.screenshot({ path: path.join(shots, 'shot-skin-neon.png') });

  console.log('6. A broken skin is refused; the current one stays');
  await page.evaluate(() => {
    const dt = new DataTransfer(); dt.items.add(new File([window.__rig.skinTools.zipFiles([['readme.txt', new TextEncoder().encode('no skin here')]])], 'Broken.rigskin'));
    for (const type of ['dragover', 'drop']) document.getElementById('stage').dispatchEvent(new DragEvent(type, { dataTransfer: dt, clientX: 800, clientY: 700, bubbles: true, cancelable: true }));
  });
  await sleep(800);
  const logTxt = await page.textContent('#log');
  check((await look()).id === 'lib:Neon Test.rigskin' && /skipped Broken\.rigskin: not a usable skin: no skin\.json inside/.test(logTxt), 'Broken.rigskin skipped with its reason; Neon Test still on');

  console.log('7. Reload: the chosen skin comes back');
  await open(); await sleep(800);
  L = await look();
  check(L.id === 'lib:Neon Test.rigskin' && L.accent === '#ff2bd6', `after reload: ${L.id}`);

  check(errors.length === 0, `no console errors${errors.length ? ': ' + errors.join(' | ') : ''}`);
  await browser.close();
  console.log(fails ? `\n${fails} FAILED` : '\nALL SKIN CHECKS PASSED');
  process.exit(fails ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(2); });

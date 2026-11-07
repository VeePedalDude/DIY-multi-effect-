// tests/browser/plugins.cjs - effects and sources as files in headless Chromium: the example .rigfx
// bundles (dist/plugins, built by tools/build_plugin.py) imported into the library, listed before Start
// from their manifests, run at Start as WebCLAP in the audio worklet, patched, saved in a preset, and
// brought back after a reload. A native .clap is kept in the library but not run (the rig-host runs it).
const { chromium } = require(process.env.PLAYWRIGHT_CORE || 'playwright-core');
const path = require('path'), fs = require('fs');
const root = path.resolve(__dirname, '../..');
let fails = 0;
const check = (ok, msg) => { console.log(`  [${ok ? 'PASS' : 'FAIL'}] ${msg}`); if (!ok) fails++; };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const b64 = (f) => fs.readFileSync(path.join(root, f)).toString('base64');

(async () => {
  const browser = await chromium.launch({ args: ['--autoplay-policy=no-user-gesture-required', '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'] });
  const context = await browser.newContext({ viewport: { width: 1600, height: 900 } });
  let page = await context.newPage(); const errors = [];
  page.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });
  page.on('pageerror', (e) => errors.push('pageerror: ' + e.message));
  await context.route('**/cdnjs.cloudflare.com/**three.min.js', (r) => r.fulfill({ body: fs.readFileSync(process.env.THREE_JS), contentType: 'text/javascript' }));
  await context.route('**/fonts.googleapis.com/**', (r) => r.fulfill({ body: '', contentType: 'text/css' }));
  const boot = async () => {
    await page.goto('file://' + path.join(root, 'dist/rig-test.html'));
    await page.waitForFunction(() => window.__rig && window.__rig.ready && window.__rig.library && window.__rig.library.core, null, { timeout: 15000 });
  };
  const start = async () => {
    await page.check('input[name=src][value=riff]'); await page.click('#startBtn');
    await page.waitForFunction(() => window.__rig.meters && window.__rig.engineChain, null, { timeout: 20000 }); await sleep(800);
  };
  const V = (expr) => page.evaluate(expr);
  await boot();

  console.log('1. Plugin bundles go into the library and are listed before Start, from their manifests');
  const files = { 'trem.rigfx': b64('dist/plugins/trem.rigfx'), 'rand.rigfx': b64('dist/plugins/rand.rigfx'), 'trem-linux.clap': '' };
  files['trem-linux.clap'] = require('child_process').execSync(`unzip -p ${path.join(root, 'dist/plugins/trem.rigfx')} clap/x86_64-linux.clap`).toString('base64');
  const imp = await page.evaluate(async (files) => {
    const list = Object.entries(files).map(([n, b]) => new File([Uint8Array.from(atob(b), (c) => c.charCodeAt(0))], n));
    return (await window.__rig.importFiles(list)).map((r) => `${r.status} ${r.kind}/${r.name}`);
  }, files);
  const lib = await V(() => (window.__rig.library.entries.plugins || []).map((e) => [e.name, e.meta && (e.meta.name || e.meta.error), e.meta && e.meta.tags]));
  check(imp.every((x) => /^added plugins\//.test(x)) && lib.length === 3, `imported: ${imp.join(', ')}`);
  check(lib.some(([n, m, t]) => n === 'trem.rigfx' && m === 'TREMOLO' && t.includes('tremolo')), `the library reads each bundle's manifest: ${lib.map(([n, m]) => `${n} → ${m}`).join(', ')}`);
  const types = await V(() => [1, typeof window.__rig.typesKnown === 'function' ? window.__rig.typesKnown() : null]);
  check(types[1] && types[1].modules.includes('trem') && types[1].sources.includes('rand'), `the add panels know TREMOLO and RANDOM before Start (${types[1] ? types[1].modules.slice(-2).join(', ') + ' / ' + types[1].sources.slice(-2).join(', ') : '?'})`);

  console.log('2. At Start the bundles run as WebCLAP in the audio worklet');
  await start();
  const res = await V(() => window.__rig.plugins.results);
  check(res['trem.rigfx'] && res['trem.rigfx'].status >= 0 && res['rand.rigfx'] && res['rand.rigfx'].status >= 0, `the engine added them: ${Object.entries(res).map(([f, r]) => `${f}: ${r.note}`).join(' | ')}`);
  check(res['trem-linux.clap'] === undefined, 'the native .clap is kept in the library but not sent to the browser engine (the rig-host runs those)');
  const desc = await V(() => window.__rig.engineDesc);
  check(desc.moduleTypes.some((t) => t.type === 'trem' && t.plugin && t.plugin.file === 'trem.rigfx') && desc.sourceTypes.some((t) => t.type === 'rand' && t.plugin), 'the engine describes them, with the file each came from');

  console.log('3. TREMOLO in the chain: it plays, its knobs reach it');
  const added = await V(() => window.__rig.chain.add('trem'));
  await page.waitForFunction(() => window.__rig.engineChain.some((e) => e.id === 'trem'), null, { timeout: 5000 });
  check(added && added.id === 'trem', 'added from the page, in the engine\'s chain');
  await page.evaluate(() => { window.__rig.cmd('param_set trem depth 1'); window.__rig.cmd('param_set trem rate 0.4'); });
  const g = await V(async () => { let lo = 9, hi = -9; const t0 = performance.now(); while (performance.now() - t0 < 2000) { const v = window.__rig.modMeter('trem', 'gain'); if (v !== undefined) { lo = Math.min(lo, v); hi = Math.max(hi, v); } await new Promise((r) => setTimeout(r, 20)); } return [lo, hi]; });
  check(g[0] < 0.2 && g[1] > 0.8, `DEPTH 100%: its gain meter swings ${g[0].toFixed(2)}..${g[1].toFixed(2)} (the plugin's own meter, read from the worklet)`);
  const outPk = await V(async () => { let p = 0; const t0 = performance.now(); while (performance.now() - t0 < 1000) { p = Math.max(p, window.__rig.meters.outPk); await new Promise((r) => setTimeout(r, 25)); } return p; });
  check(outPk > 0.005, `the riff still comes out (peak ${outPk.toFixed(3)})`);

  console.log('4. RANDOM in the rack, patched to TREMOLO\'s RATE');
  const src = await V(() => window.__rig.rack.add('rand'));
  await page.waitForFunction(() => (window.__rig.engineRack || []).some((s) => s.id === 'rand'), null, { timeout: 5000 });
  const vals = await V(async () => { const seen = new Set(); const t0 = performance.now(); while (performance.now() - t0 < 2500) { seen.add(Math.round(window.__rig.src('rand') * 1000)); await new Promise((r) => setTimeout(r, 30)); } return [...seen]; });
  check(src && vals.length >= 3 && Math.min(...vals) < 900 && vals.every((v) => v >= -1000 && v <= 1000), `RANDOM steps through ${vals.length} values in 2.5 s (2 per beat), all in -1..1`);
  await V(() => window.__rig.cmd('mod_connect trem rate rand 0.3'));
  await sleep(300);
  const r = await V(() => window.__rig.lastResp);
  check(r && r.status === 0, 'patched: RANDOM moves TREMOLO\'s RATE (mod_connect accepted)');

  console.log('5. A preset with them comes back after a reload (the plugins load from the library first)');
  await page.click('#settingsBtn').catch(() => {}); await sleep(200);
  const saved = await V(async () => { const p = window.__rig.buildPreset ? window.__rig.buildPreset('Plugins') : null; return p; });
  check(saved && saved.chain.some((c) => c.id === 'trem' && c.plugin && c.plugin.file === 'trem.rigfx') && saved.sources.some((s) => s.id === 'rand' && s.plugin), 'the preset names each plugin\'s file');
  const json = JSON.stringify(saved);
  await boot(); await start();
  const back = await page.evaluate(async (json) => { const r = await window.__rig.loadPresetJson(json); await new Promise((r) => setTimeout(r, 800)); return { chain: window.__rig.engineChain.map((e) => e.id), rack: (window.__rig.engineRack || []).map((s) => s.id), note: document.getElementById('boardNote').textContent, err: r && r.error, results: Object.keys(window.__rig.plugins.results), lib: (window.__rig.library.entries.plugins || []).map((e) => e.name) }; }, json);
  check(back.chain.includes('trem') && back.rack.includes('rand'), `after a reload: chain ${back.chain.join(' ')}; rack ${back.rack.join(' ')}`);

  console.log('6. Load effect file… in Add an effect (a fresh rig, nothing in its library): one step to the chain');
  const ctx2 = await browser.newContext({ viewport: { width: 1600, height: 900 } }), page1 = page;
  page = await ctx2.newPage();
  page.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });
  page.on('pageerror', (e) => errors.push('pageerror: ' + e.message));
  await ctx2.route('**/cdnjs.cloudflare.com/**three.min.js', (r) => r.fulfill({ body: fs.readFileSync(process.env.THREE_JS), contentType: 'text/javascript' }));
  await ctx2.route('**/fonts.googleapis.com/**', (r) => r.fulfill({ body: '', contentType: 'text/css' }));
  await boot(); await start();
  const plus = await V(() => window.__rig.fxAddScreen());
  await page.mouse.click(plus.x, plus.y); await sleep(500);
  check(await page.isVisible('[data-pick-load="fx"]'), `the Add an effect panel has a ${JSON.stringify(await page.textContent('[data-pick-load="fx"]').catch(() => '?'))} button`);
  const [chooser] = await Promise.all([page.waitForEvent('filechooser'), page.click('[data-pick-load="fx"]')]);
  const acc = await V(() => document.getElementById('pluginFile').accept);
  await chooser.setFiles(path.join(root, 'dist/plugins/trem.rigfx'));
  await page.waitForFunction(() => window.__rig.engineChain.some((e) => e.id === 'trem'), null, { timeout: 10000 }).catch(() => {});
  const one = await V(() => ({ chain: window.__rig.engineChain.map((e) => e.id), lib: (window.__rig.library.entries.plugins || []).map((e) => e.name), focus: window.__rig.state ? window.__rig.state.focus : null }));
  check(acc === '.rigfx,.clap' && one.chain.includes('trem') && one.lib.includes('trem.rigfx'), `picked trem.rigfx (chooser shows ${acc}): in the library (${one.lib.join(', ')}) and in the chain (${one.chain.join(' ')})`);
  await page.screenshot({ path: path.join(process.env.SHOTS || path.join(root, 'build'), 'shot-plugin-load.png') });

  console.log('7. Load source file… in Add a source; an effect picked there says where it went');
  const splus = await V(() => window.__rig.addScreen());
  await page.mouse.click(splus.x, splus.y); await sleep(500);
  const [ch2] = await Promise.all([page.waitForEvent('filechooser'), page.click('[data-pick-load="src"]')]);
  await ch2.setFiles(path.join(root, 'dist/plugins/rand.rigfx'));
  await page.waitForFunction(() => (window.__rig.engineRack || []).some((s) => s.id === 'rand'), null, { timeout: 10000 }).catch(() => {});
  check((await V(() => (window.__rig.engineRack || []).map((s) => s.id))).includes('rand'), 'picked rand.rigfx: RANDOM is in the rack');
  await page.mouse.click(splus.x, splus.y); await sleep(500);
  const [ch3] = await Promise.all([page.waitForEvent('filechooser'), page.click('[data-pick-load="src"]')]);
  await ch3.setFiles(path.join(root, 'dist/plugins/trem.rigfx'));
  await sleep(1500);
  const note = await page.textContent('[data-pick-note="src"]').catch(() => '');
  check(/is an effect: it’s in Add an effect/.test(note), `an effect file in Add a source: “${note}”`);

  console.log('8. Library → Import files… shows every kind again after a module\'s own Import… narrowed it');
  await V(() => { const i = document.getElementById('libFiles'); i.accept = '.nam'; });
  await page.click('#settingsBtn').catch(() => {}); await sleep(200);
  const [ch4] = await Promise.all([page.waitForEvent('filechooser', { timeout: 3000 }).catch(() => null), page.evaluate(() => document.getElementById('libImport').click())]);
  const acc2 = await V(() => document.getElementById('libFiles').accept);
  check(ch4 && acc2 === '', `the library's chooser accepts any file (accept="${acc2}")`);
  if (ch4) await ch4.setFiles([]);
  void page1;

  check(errors.length === 0, `no console errors${errors.length ? ': ' + errors.join(' | ') : ''}`);
  await page.screenshot({ path: path.join(process.env.SHOTS || path.join(root, 'build'), 'shot-plugins.png') });
  await browser.close();
  console.log(fails ? `\n${fails} FAILED` : '\nALL PLUGIN CHECKS PASSED');
  process.exit(fails ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(2); });

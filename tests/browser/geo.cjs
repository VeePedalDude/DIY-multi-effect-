// geo.cjs - GEO PULSE in the page: its panel (settings, 3D view, vertex editing) and that every flash
// in the view is an event the engine fired (checked against the same pattern played by the page's own
// copy of the engine), with no clock of the view's own.
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

  console.log('1. Add it and open its panel');
  await page.evaluate(() => window.__rig.rack.add('geo')); await sleep(400);
  const lbl = await page.evaluate(() => window.__rig.srcScreen('geo', 'lbl')); await page.mouse.click(lbl.x, lbl.y); await sleep(500);
  const p = await page.evaluate(() => ({ canvas: !!document.querySelector('#panel canvas.geoview'), up: [...document.querySelectorAll('#panel > .params:not(.patches) [data-scfg]')].map((e) => e.dataset.scfg),
    more: document.querySelectorAll('#srcMore [data-scfg]').length, knobs: document.querySelectorAll('#panel [data-spid^="geo."]').length, name: document.querySelector('#panel h2').textContent }));
  check(p.canvas && p.name === 'GEO PULSE', `“${p.name}” panel with its 3D view`);
  check(p.up.join() === 'geo~shape,geo~verts,geo~skip,geo~rot' && p.knobs === 8 && p.more === 15, `up front: ${p.up.map((x) => x.split('~')[1]).join(', ')}; 8 knobs; ${p.more} more settings folded away`);

  console.log('2. Settings reach the engine');
  await page.selectOption('[data-scfg="geo~shape"]', { label: 'STAR' });
  const setSlider = async (sel, v) => page.$eval(sel, (el, v) => { el.value = String(v); el.dispatchEvent(new Event('input', { bubbles: true })); }, v);
  await setSlider('[data-scfg="geo~verts"]', 4 / 29 + 0.004); await setSlider('[data-scfg="geo~skip"]', 2 / 15);
  await page.click('#srcMore summary'); await setSlider('[data-scfg="geo~seed"]', 5 / 999); await page.selectOption('[data-scfg="geo~dir"]', { label: 'RANDOM' });
  await setSlider('[data-spid="geo.prob"]', 0.6); await sleep(300);
  const st = await page.evaluate(() => ({ log: window.__rig.log.filter((l) => l.startsWith('src_cfg geo')).slice(-5), shown: ['shape', 'verts', 'skip', 'seed', 'dir'].map((s) => document.querySelector(`[data-cdisp="geo~${s}"]`)?.textContent || document.querySelector(`[data-scfg="geo~${s}"]`).selectedOptions[0].textContent), resp: window.__rig.lastResp.status }));
  check(st.shown.join() === 'STAR,7,3,5,RANDOM' && st.resp === 0, `SHAPE ${st.shown[0]}, VERTICES ${st.shown[1]} (snapped to a whole number), SKIP ${st.shown[2]}, SEED ${st.shown[3]}, DIRECTION ${st.shown[4]}; engine answers ${st.resp}`);

  console.log('3. Every flash is an event the engine fired');
  await page.evaluate(() => window.__rig.cmd('ext geo 2')); await sleep(300); await page.evaluate(() => { window.__rig.geoPulses.length = 0; }); await sleep(6000);
  const cmp = await page.evaluate(() => {
    // the same source, set the same way, played by the page's own copy of the engine (not the audio one)
    const X = window.__rig.viewX(), s = window.__rig.srcObj('geo');
    const tx = new Uint8Array(X.memory.buffer, X.rig_text(), 64); tx.fill(0); tx.set(new TextEncoder().encode('geo sim')); const k = X.rig_src_add();
    const sent = (v) => Number(v.toFixed(6));       // exactly what the page sent the audio engine
    s.params.forEach((q, i) => X.rig_src_param(k, i, sent(q.v))); s.settings.forEach((q, i) => X.rig_src_cfg(k, i, sent(q.v)));
    s.table.v.forEach((row, r) => row.forEach((v, j) => X.rig_src_cell(k, r, j, sent(v)))); X.rig_tempo(window.__rig.state.bpm);
    const D = JSON.parse(new TextDecoder().decode(new Uint8Array(X.memory.buffer, X.rig_describe()).subarray(0, new Uint8Array(X.memory.buffer, X.rig_describe()).indexOf(0)))).meters;
    const want = []; let last = null;
    for (let b = 0; b < 48000 * 9 / 128; b++) { X.rig_process(128); const M = new Float32Array(X.memory.buffer, X.rig_meters(), X.rig_meters_len()); const f = M[D.srcm + k * D.perSrc + 1], c = Math.floor(f / 32);
      if (last === null) last = 0; if (c !== last) { want.push(f % 32); last = c; } }
    X.rig_src_remove(k);
    const got = window.__rig.geoPulses.filter((x) => x.id === 'geo');
    let at = -1; for (let o = 0; o < 4000 && at < 0; o++) if (got.every((g) => g.delta !== 1 || want[g.count - o - 1] === g.vertex)) at = o;
    return { got: got.length, one: got.filter((g) => g.delta === 1).length, want: want.length, at, verts: got.slice(0, 12).map((g) => g.vertex).join(',') };
  });
  check(cmp.one >= 15 && cmp.at >= 0, `${cmp.got} flashes in 6 s (${cmp.verts}...); all ${cmp.one} single steps match the engine's own run of the pattern (random order, 60% chance), event for event`);

  console.log('4. Stopped transport: nothing moves (the view has no clock of its own)');
  await page.evaluate(() => window.__rig.cmd('ext geo 0')); await sleep(1200);
  const a = await page.evaluate(() => ({ n: window.__rig.geoPulses.length, ind: window.__rig.geoView.info('geo').ind }));
  await sleep(1500);
  const b = await page.evaluate(() => ({ n: window.__rig.geoPulses.length, ind: window.__rig.geoView.info('geo').ind }));
  check(a.n === b.n && a.ind.every((x, i) => x === b.ind[i]), `1.5 s stopped: ${b.n - a.n} new flashes, the point stays at (${b.ind.map((x) => x.toFixed(3)).join(', ')})`);
  await page.evaluate(() => window.__rig.cmd('ext geo 1')); await sleep(800);
  const c = await page.evaluate(() => window.__rig.geoPulses.length);
  check(c > b.n, `playing again: ${c - b.n} flashes in 0.8 s`);

  console.log('5. Click a vertex to edit it');
  await page.selectOption('[data-scfg="geo~shape"]', { label: 'POLYGON' }); await page.selectOption('[data-scfg="geo~dir"]', { label: 'FORWARD' }); await setSlider('[data-spid="geo.prob"]', 1); await sleep(400);
  await page.evaluate(() => document.querySelector('canvas.geoview').scrollIntoView({ block: 'center' })); await sleep(300);
  const v3 = await page.evaluate(() => window.__rig.geoView.vertexScreen(3)); await page.mouse.click(v3.x, v3.y); await sleep(300);
  const ed = await page.evaluate(() => document.querySelector('#geoVedit b')?.textContent);
  await page.click('[data-vtog="3:0"]'); await sleep(300);
  const cl = await page.evaluate(() => ({ cmd: window.__rig.log.filter((l) => l.startsWith('src_cell geo')).slice(-1)[0], resp: window.__rig.lastResp.status }));
  await page.evaluate(() => { window.__rig.geoPulses.length = 0; }); await sleep(3000);
  const hits = await page.evaluate(() => { const h = [0, 0, 0, 0, 0, 0, 0]; window.__rig.geoPulses.forEach((g) => { if (g.vertex < 7) h[g.vertex]++; }); return h; });
  check(ed === 'VERTEX 4 of 7' && cl.cmd === 'src_cell geo 3 on 0.000000' && cl.resp === 0, `the editor opens on “${ed}”; switching it off sends: ${cl.cmd}`);
  check(hits[3] === 0 && hits.filter((x, i) => i !== 3).every((x) => x >= 2), `in the next 3 s it never fires; the others fire ${hits.filter((x, i) => i !== 3).join('/')} times`);
  await page.screenshot({ path: path.join(shots, 'shot-geo.png') });

  console.log('6. Duplicate keeps its settings and table');
  await page.click('[data-act="dup-src"]'); await sleep(400);
  const d = await page.evaluate(() => { const s = window.__rig.srcObj('geo2'); return s ? { seed: s.settings.find((q) => q.sym === 'seed').fmt(s.settings.find((q) => q.sym === 'seed').v), off: s.table.v[3][0] } : null; });
  check(d && d.seed === '5' && d.off === 0, `GEO PULSE 2: seed ${d && d.seed}, vertex 4 ${d && d.off === 0 ? 'off' : 'on'}`);

  check(errors.length === 0, `no console errors${errors.length ? ': ' + errors.join(' | ') : ''}`);
  await browser.close();
  console.log(fails ? `\n${fails} FAILED` : '\nALL GEO CHECKS PASSED');
  process.exit(fails ? 1 : 0);
})();

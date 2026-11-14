// tests/browser/switch.cjs - changing presets while the built-in riff plays: the output is recorded around
// each change (a recorder worklet on the engine's output) and measured: how far the loudness jumps above
// the louder of the two presets while it changes, the biggest sample-to-sample step (a click) against the
// steady playing either side, how long until the new preset is at its own level, and that no old tail
// plays into the new preset. Prints the numbers; checks them against the limits below.
const { chromium } = require(process.env.PLAYWRIGHT_CORE || 'playwright-core');
const path = require('path'), fs = require('fs');
const root = path.resolve(__dirname, '../..');
let fails = 0;
const check = (ok, msg) => { console.log(`  [${ok ? 'PASS' : 'FAIL'}] ${msg}`); if (!ok) fails++; };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const LIMITS = { overshootDb: 1.5, clickRatio: 1.5, settleMs: 400 };

// Preset A: the default rig. Preset B: a different chain, the A2 capture in the amp, hot delay, +6 dB.
const A = { format: 'rig-preset', version: 5, name: 'A', level: 0,
  chain: [{ id: 'gate', type: 'gate' }, { id: 'amp', type: 'amp' }, { id: 'cab', type: 'cab' }, { id: 'holo', type: 'holo' }, { id: 'tess', type: 'tess' }], sources: [], mods: [] };
const B = { format: 'rig-preset', version: 5, name: 'B', level: 6,
  chain: [{ id: 'gate', type: 'gate' }, { id: 'amp', type: 'amp', asset: { kind: 'nam', name: 'A2.nam' } }, { id: 'cab', type: 'cab' }, { id: 'holo', type: 'holo', params: { feedback: 85, mix: 60 } }],
  sources: [], mods: [] };
// C and D: the same chain, levelled the opposite ways: C a quiet amp turned up by its level, D a hot amp turned down
const C = { format: 'rig-preset', version: 5, name: 'C', level: 18, chain: [{ id: 'gate', type: 'gate' }, { id: 'amp', type: 'amp', params: { input: -12, output: -18 } }, { id: 'cab', type: 'cab' }], sources: [], mods: [] };
const D = { format: 'rig-preset', version: 5, name: 'D', level: -18, chain: [{ id: 'gate', type: 'gate' }, { id: 'amp', type: 'amp', params: { input: 24, output: 6 } }, { id: 'cab', type: 'cab' }], sources: [], mods: [] };
const PRESETS = { A, B, C, D };

(async () => {
  const browser = await chromium.launch({ args: ['--autoplay-policy=no-user-gesture-required', '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'] });
  const context = await browser.newContext({ viewport: { width: 1600, height: 900 } });
  const page = await context.newPage(), errors = [];
  page.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });
  page.on('pageerror', (e) => errors.push('pageerror: ' + e.message));
  await context.route('**/cdnjs.cloudflare.com/**three.min.js', (r) => r.fulfill({ body: fs.readFileSync(process.env.THREE_JS), contentType: 'text/javascript' }));
  await context.route('**/fonts.googleapis.com/**', (r) => r.fulfill({ body: '', contentType: 'text/css' }));
  await page.goto('file://' + path.join(root, 'dist/rig-test.html'));
  await page.waitForFunction(() => window.__rig && window.__rig.ready, null, { timeout: 10000 });
  await page.check('input[name=src][value=riff]'); await page.click('#startBtn');
  await page.waitForFunction(() => window.__rig.meters, null, { timeout: 10000 }); await sleep(800);
  await page.evaluate(async (b64) => window.__rig.importFiles([new File([Uint8Array.from(atob(b64), (c) => c.charCodeAt(0))], 'A2.nam')]), fs.readFileSync(path.join(root, 'tests/models/A2.nam')).toString('base64'));
  await page.evaluate(() => { const s = document.getElementById('master'); s.value = 0; s.dispatchEvent(new Event('input')); });
  // a recorder on the engine's output: every block, with the context's frame number
  await page.evaluate(async () => {
    const R = window.__rig, src = `registerProcessor('rec', class extends AudioWorkletProcessor { process(i) { const c = i[0] && i[0][0]; if (c) this.port.postMessage({ f: currentFrame, d: c.slice(0) }); return true; } });`;
    await R.ctx.audioWorklet.addModule('data:text/javascript,' + encodeURIComponent(src));
    const n = new AudioWorkletNode(R.ctx, 'rec'); R.node.connect(n);
    window.__rec = { blocks: [], on: false }; n.port.onmessage = (e) => { if (window.__rec.on) window.__rec.blocks.push(e.data); };
  });
  const sw = async (from, to) => {
    const label = `${from} → ${to}`;
    await page.evaluate((p) => window.__rig.loadPresetJson(p), PRESETS[from]);
    await sleep(3000);                                                           // settle on it first
    await page.evaluate(() => { window.__rec.blocks = []; window.__rec.on = true; });
    await sleep(2000);
    const t = await page.evaluate(() => Math.round(window.__rig.ctx.currentTime * window.__rig.ctx.sampleRate));
    const ms = await page.evaluate(async (p) => { const t0 = performance.now(); await window.__rig.loadPresetJson(p); return performance.now() - t0; }, PRESETS[to]);
    await sleep(3000);
    const blocks = await page.evaluate(() => { window.__rec.on = false; return window.__rec.blocks.map((b) => ({ f: b.f, d: Array.from(b.d) })); });
    return measure(blocks, t, label, ms);
  };
  const sr = await page.evaluate(() => window.__rig.ctx.sampleRate);
  function measure(blocks, t, label, ms) {
    const f0 = blocks[0].f, x = new Float32Array(blocks[blocks.length - 1].f + 128 - f0);
    for (const b of blocks) x.set(b.d, b.f - f0);
    const at = t - f0, win = Math.round(0.01 * sr), env = [];
    for (let i = 0; i + win <= x.length; i += win) { let s = 0; for (let k = i; k < i + win; k++) s += x[k] * x[k]; env.push(10 * Math.log10(s / win + 1e-20)); }
    const ei = (s) => Math.round((at + s * sr) / win), pct = (a, p) => { const s = a.slice().sort((p1, p2) => p1 - p2); return s[Math.floor(p * (s.length - 1))]; };
    const before = env.slice(ei(-1.8), ei(-0.05)), after = env.slice(ei(1.2), ei(2.9)), during = env.slice(ei(0), ei(1.2));
    const loudB = pct(before, 0.9), loudA = pct(after, 0.9), over = Math.max(...during) - Math.max(loudB, loudA);
    const step = (a, b) => { let m = 0; for (let i = Math.max(1, a); i < Math.min(x.length, b); i++) m = Math.max(m, Math.abs(x[i] - x[i - 1])); return m; };
    const steady = Math.max(step(at - 1.8 * sr, at - 0.05 * sr), step(at + 1.2 * sr, at + 2.9 * sr)), trans = step(at, at + 1.2 * sr);
    let gap = 0, run = 0; for (const v of during) { run = v < -80 ? run + 10 : 0; gap = Math.max(gap, run); }
    let settle = 0; for (let k = during.length - 1; k >= 0; k--) if (Math.abs(during[k] - loudA) > 6 && during[k] > loudA - 40) { settle = (k + 1) * 10; break; }
    console.log(`   ${label}: steady ${loudB.toFixed(1)} → ${loudA.toFixed(1)} dB; loudest while changing ${over >= 0 ? '+' : ''}${over.toFixed(1)} dB over the louder; biggest step ${(trans / steady).toFixed(2)}x the steady playing's; settled in ${settle} ms; silent ${gap} ms; the page took ${ms.toFixed(0)} ms`);
    return { over, click: trans / steady, settle, gap };
  }
  console.log('Switching presets while the riff plays (master 0 dB)');
  const ab = await sw('A', 'B'), ba = await sw('B', 'A'), cd = await sw('C', 'D'), dc = await sw('D', 'C');
  for (const [r, l] of [[ab, 'A → B'], [ba, 'B → A'], [cd, 'C → D'], [dc, 'D → C']]) {
    check(r.over <= LIMITS.overshootDb, `${l}: no jump in loudness while it changes (${r.over.toFixed(1)} dB over the louder preset; limit ${LIMITS.overshootDb})`);
    check(r.click <= LIMITS.clickRatio, `${l}: no click (biggest step ${r.click.toFixed(2)}x the playing's own; limit ${LIMITS.clickRatio})`);
  }
  check(ba.gap <= 200 && cd.gap <= 200 && dc.gap <= 200, `without files to load, the change is silent for at most 200 ms (${ba.gap}, ${cd.gap}, ${dc.gap} ms)`);
  check(ab.settle <= LIMITS.settleMs, `A → B: at the new preset's level within ${LIMITS.settleMs} ms (${ab.settle} ms), the capture loaded before it plays`);
  check(errors.length === 0, `no console errors${errors.length ? ': ' + errors.join(' | ') : ''}`);
  await browser.close();
  console.log(fails ? `\n${fails} FAILED` : '\nALL SWITCH CHECKS PASSED');
  process.exit(fails ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(2); });

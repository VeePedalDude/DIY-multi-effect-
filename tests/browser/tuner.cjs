// tests/browser/tuner.cjs - the strobe tuner in headless Chromium, with a steady A2 played 6 cents
// sharp into the fake microphone: the face-down mini card opens the sideways card, the note and
// cents are read, the output mutes, the stripes drift the right way at the right speed, the A4
// reference, collapsing to the live mini card (T), Off, a skin change, and settings surviving a reload.
const { chromium } = require(process.env.PLAYWRIGHT_CORE || 'playwright-core');
const path = require('path'), fs = require('fs');
const root = path.resolve(__dirname, '../..');
let fails = 0;
const check = (ok, msg) => { console.log(`  [${ok ? 'PASS' : 'FAIL'}] ${msg}`); if (!ok) fails++; };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// a held A2 (110 Hz) 6 cents sharp, with a few harmonics like a string: 16-bit, 48 kHz, 8 s (Chromium loops it)
const CENTS = 6, F = 110 * Math.pow(2, CENTS / 1200);
function toneWav(file) {
  const sr = 48000, n = sr * 8, b = Buffer.alloc(44 + n * 2);
  b.write('RIFF', 0); b.writeUInt32LE(36 + n * 2, 4); b.write('WAVEfmt ', 8); b.writeUInt32LE(16, 16); b.writeUInt16LE(1, 20); b.writeUInt16LE(1, 22);
  b.writeUInt32LE(sr, 24); b.writeUInt32LE(sr * 2, 28); b.writeUInt16LE(2, 32); b.writeUInt16LE(16, 34); b.write('data', 36); b.writeUInt32LE(n * 2, 40);
  // a whole number of cycles in the file, so the loop point doesn't jump
  const f = Math.round(F * 8) / 8;
  for (let i = 0; i < n; i++) { const t = i / sr, v = 0.25 * (Math.sin(2 * Math.PI * f * t) + 0.5 * Math.sin(4 * Math.PI * f * t + 0.4) + 0.3 * Math.sin(6 * Math.PI * f * t + 1.1) + 0.2 * Math.sin(8 * Math.PI * f * t + 2.3)); b.writeInt16LE(Math.round(v * 32767), 44 + i * 2); }
  fs.writeFileSync(file, b);
  return 1200 * Math.log2(f / 110);
}

(async () => {
  const wav = path.join(root, 'build/tone-a2.wav'), cents = toneWav(wav);
  const browser = await chromium.launch({ args: ['--use-fake-ui-for-media-stream', '--use-fake-device-for-media-stream', `--use-file-for-fake-audio-capture=${wav}`,
    '--autoplay-policy=no-user-gesture-required', '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'] });
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
    await page.click('#startBtn');
    await page.waitForFunction(() => window.__rig.meters, null, { timeout: 10000 }); await sleep(1200);
  };
  // the state, from a meter packet made at least 0.25 s (audio time) after asking: a busy headless
  // page can hand over packets late
  const read = async () => { const t0 = await page.evaluate(() => window.__rig.ctx.currentTime);
    await page.waitForFunction((t0) => window.__rig.meters && window.__rig.meters.t > t0 + 0.25, t0, { timeout: 10000 }); return snap(); };
  const snap = () => page.evaluate(() => { const r = window.__rig.tuner.reading(), s = window.__rig.tuner.state, m = window.__rig.meters;
    return { ...r, open: s.open, uiOn: s.on, ref: s.ref, mute: s.mute, outPk: m.outPk, shown: !document.getElementById('tuner').hidden,
      expanded: document.getElementById('tunerBtn').getAttribute('aria-expanded'), said: document.getElementById('tunerRead').textContent }; });
  // a pixel of the sideways card, in its own 754 x 518 coordinates
  const cardPx = (x, y) => page.evaluate(([x, y]) => { const c = document.getElementById('tunerCard'); return Array.from(c.getContext('2d').getImageData(x * c.width / 754, y * c.height / 518, 1, 1).data).slice(0, 3); }, [x, y]);
  const miniPx = (x, y) => page.evaluate(([x, y]) => { const c = document.getElementById('tunerMini'); return Array.from(c.getContext('2d').getImageData(x * 2, y * 2, 1, 1).data); }, [x, y]);
  const hex = (p) => '#' + p.slice(0, 3).map((v) => v.toString(16).padStart(2, '0')).join('').toUpperCase();

  console.log('1. Off at start: a face-down mini card in the top bar');
  await open();
  let r = await read();
  const back = await miniPx(29.5, 22), mark = await page.evaluate(() => window.__rig.tuner.skin().backMark);
  check(!r.on && !r.shown && r.expanded === 'false' && r.outPk > 0.01, `tuner off in the engine, card closed, output playing (peak ${r.outPk.toFixed(3)})`);
  check(back[3] === 255 && hex(back) === mark.toUpperCase(), `mini card shows the card back, with its mark (${hex(back)}, skin mark ${mark})`);

  console.log('2. Click it: the sideways card opens, reads A2 and its cents, output muted');
  await page.click('#tunerBtn'); await sleep(1500);
  r = await read();
  check(r.on && r.shown && r.expanded === 'true', 'engine tuner on, card shown');
  check(r.note === 45 && Math.abs(r.cents - cents) < 0.3, `reads A2 ${r.cents >= 0 ? '+' : ''}${r.cents.toFixed(2)} cents (played ${cents.toFixed(2)}), ${r.freq.toFixed(3)} Hz`);
  check(r.outPk < 1e-4, `output muted while tuning (peak ${r.outPk.toExponential(1)})`);
  check(/^A2, \d cents sharp$/.test(r.said), `screen readers hear “${r.said}”`);
  const frame = await cardPx(700, 330), glass = await cardPx(200, 130);
  check(frame.every((v) => v > 170) && glass.every((v) => v < 40), `drawn on the skin's card frame turned sideways (frame ${frame}, display ${glass})`);
  await page.screenshot({ path: path.join(shots, 'shot-tuner-open.png') });
  await page.locator('#tuner').screenshot({ path: path.join(shots, 'shot-tuner-card.png') });

  console.log('3. The stripes drift right (sharp) at harmonic x the tuning error');
  // the engine's band phases (what the stripes follow), sampled at every meter packet for 3 s, timed by
  // the audio clock (a loaded headless browser can run audio slower than the wall clock)
  const pk = await page.evaluate(async () => {
    // every meter packet, straight off the worklet's port (the page itself only keeps the latest)
    const rows = [], port = window.__rig.node.port, tb = window.__rig.tuner.layout();
    const on = (e) => { if (e.data.type === 'meters') { rows.push([e.data.t * 1000, e.data.m[tb + 6], e.data.m[tb + 7], e.data.m[tb + 8]]); } };
    port.addEventListener('message', on); await new Promise((r) => setTimeout(r, 3000)); port.removeEventListener('message', on);
    return { rows };
  });
  const want = 110 * (Math.pow(2, cents / 1200) - 1);
  // the median step per packet: Chromium's fake microphone jumps a fraction of a cycle where it loops the file
  const rates = [1, 2, 3].map((k) => { const v = []; for (let i = 1; i < pk.rows.length; i++) { let d = pk.rows[i][k] - pk.rows[i - 1][k]; d -= 2 * Math.PI * Math.round(d / (2 * Math.PI)); v.push(d / (2 * Math.PI) / ((pk.rows[i][0] - pk.rows[i - 1][0]) / 1000)); } v.sort((x, y) => x - y); return v[v.length >> 1]; });
  check(rates.every((v, k) => Math.abs(v - want * [1, 2, 4][k]) < 0.05 * want * [1, 2, 4][k]), `bands turn ${rates.map((v) => v.toFixed(3)).join(' / ')} per s (expect ${[1, 2, 4].map((h) => (h * want).toFixed(3)).join(' / ')}), ${pk.rows.length} packets`);
  // and the drawn x1 band follows (headless Chromium paints about once a second, so only x1 can't alias here)
  const dd0 = await page.evaluate(() => [window.__rig.tuner.state.disp[0], window.__rig.ctx.currentTime]); await sleep(4000);
  const dd1 = await page.evaluate(() => [window.__rig.tuner.state.disp[0], window.__rig.ctx.currentTime]), shown = (dd1[0] - dd0[0]) / (2 * Math.PI) / (dd1[1] - dd0[1]);
  check(shown > 0.1 * want, `the drawn x1 stripes move right (${shown.toFixed(3)} per s; the fake microphone's loop jump can land in this window, so only the direction is checked)`);

  console.log('4. Reference pitch and mute');
  for (let i = 0; i < 3; i++) await page.click('#tunerRefUp');
  await sleep(1200); r = await read();
  const expect443 = 1200 * Math.log2(110 * Math.pow(2, cents / 1200) / (110 * 443 / 440));
  check(r.ref === 443 && Math.abs(r.cents - expect443) < 0.3 && (await page.textContent('#tunerRef')) === 'A4 443', `A4 = 443: the same string reads ${r.cents.toFixed(2)} cents (expect ${expect443.toFixed(2)})`);
  await page.click('#tunerMute'); await sleep(500); r = await read();
  check(!r.mute && r.outPk > 0.01 && (await page.getAttribute('#tunerMute', 'aria-pressed')) === 'false', `Mute off: you hear yourself while tuning (peak ${r.outPk.toFixed(3)})`);
  await page.click('#tunerMute'); await sleep(400);

  console.log('5. T collapses it into the mini card, which keeps showing the note; output back on');
  await page.mouse.move(800, 700); await page.keyboard.press('t'); await sleep(800);
  r = await read();
  const face = await miniPx(29.5, 22);
  check(r.on && !r.open && !r.shown && r.outPk > 0.01, `collapsed: tuner still listening, card hidden, output playing (peak ${r.outPk.toFixed(3)})`);
  check(hex(face) !== hex(back), `mini card turned face up with the note (${hex(face)})`);
  await page.locator('#tunerBtn').screenshot({ path: path.join(shots, 'shot-tuner-mini.png') });
  await page.keyboard.press('t'); await sleep(1200); r = await read();
  check(r.open && r.shown && r.outPk < 1e-4, `T again: open and muted (open ${r.open}, peak ${r.outPk.toExponential(1)})`);

  console.log('6. Off');
  await page.click('#tunerOff'); await sleep(500); r = await read();
  check(!r.on && !r.uiOn && !r.shown && r.outPk > 0.01, 'tuner off in the engine, card closed, output playing');

  console.log('7. Skins: Classic draws the tuner on its plain dark card');
  await page.evaluate(() => window.__rig.useSkin('builtin:classic')); await sleep(600);
  await page.click('#tunerBtn'); await sleep(600);
  const cframe = await cardPx(700, 330), cback = await page.evaluate(() => window.__rig.tuner.skin().back);
  check(cframe.every((v) => v < 60), `Classic: plain dark card (${cframe}); its card back is ${cback}`);
  await page.locator('#tuner').screenshot({ path: path.join(shots, 'shot-tuner-classic.png') });
  await page.evaluate(() => window.__rig.useSkin('builtin:astral')); await sleep(600);

  console.log('8. Reload: reference and mute remembered');
  await page.click('#tunerMute'); await sleep(200);
  await open(); r = await read();
  check(r.ref === 443 && r.mute === false && !r.on, `after reload: A4 ${r.ref}, mute ${r.mute}, tuner starts off`);
  await page.click('#tunerBtn'); await sleep(1200); r = await read();
  check(r.on && Math.abs(r.cents - expect443) < 0.3 && r.outPk > 0.01, `opens with the saved reference (${r.cents.toFixed(2)} cents) and mute off`);

  check(errors.length === 0, `no console errors${errors.length ? ': ' + errors.join(' | ') : ''}`);
  await browser.close();
  console.log(fails ? `\n${fails} FAILED` : '\nALL TUNER CHECKS PASSED');
  process.exit(fails ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(2); });

// tests/browser/host.cjs - rig-host end to end: start the host (RIG_HOST_BIN, default build/rig-host;
// "wine dist/rig-host.exe" runs the Windows build) on the null audio backend with the test riff and a
// simulated 437-sample loopback, open the page it serves, and drive the native engine from the page:
// it connects instead of starting browser audio, builds itself from the host's description, meters
// flow, knobs, the chain and the rack reach the engine, modulation moves knobs, a NAM capture and an
// IR load over the socket, the round-trip test measures the loopback exactly, and a reload reconnects.
const { chromium } = require(process.env.PLAYWRIGHT_CORE || 'playwright-core');
const { spawn } = require('child_process');
const path = require('path'), fs = require('fs');
const root = path.resolve(__dirname, '../..');
let fails = 0;
const check = (ok, msg) => { console.log(`  [${ok ? 'PASS' : 'FAIL'}] ${msg}`); if (!ok) fails++; };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const LOOP = 437, PORT = 18000 + Math.floor(Math.random() * 1000);

(async () => {
  const bin = (process.env.RIG_HOST_BIN || path.join(root, 'build/rig-host')).split(' ');
  const args = [...bin.slice(1), '--backend', 'null', '--riff', '--sim-loopback', String(LOOP), '--port', String(PORT), '--no-browser'];
  const proc = spawn(bin[0], args, { cwd: root, env: { ...process.env, WINEDEBUG: '-all' } });
  let out = ''; proc.stdout.on('data', (d) => { out += d; }); proc.stderr.on('data', (d) => { out += d; });
  for (let i = 0; i < 200 && !/page:/.test(out); i++) await sleep(100);
  check(/page:\s+http:\/\/127\.0\.0\.1:\d+\//.test(out), `${path.basename(bin[bin.length - 1])} started: ${(out.match(/audio:[^\n]*/) || ['?'])[0].trim()}`);

  const browser = await chromium.launch({ args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'] });
  const context = await browser.newContext({ viewport: { width: 1600, height: 900 } });
  const page = await context.newPage(), errors = [];
  page.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });
  page.on('pageerror', (e) => errors.push('pageerror: ' + e.message));
  await context.route('**/cdnjs.cloudflare.com/**three.min.js', (r) => r.fulfill({ body: fs.readFileSync(process.env.THREE_JS), contentType: 'text/javascript' }));
  await context.route('**/fonts.googleapis.com/**', (r) => r.fulfill({ body: '', contentType: 'text/css' }));
  const shots = process.env.SHOTS || path.join(root, 'build');
  const V = (expr) => page.evaluate(new Function('return (' + expr + ')'));
  const fresh = async () => { const t0 = await V('window.__rig.meters ? window.__rig.meters.t : 0'); await page.waitForFunction((t0) => window.__rig.meters && window.__rig.meters.t > t0 + 0.1, t0, { timeout: 10000 }); };
  const spread = (expr, ms) => page.evaluate(async ([expr, ms]) => { const f = new Function('return ' + expr); let lo = 1e9, hi = -1e9; const t0 = performance.now(); while (performance.now() - t0 < ms) { const v = f(); lo = Math.min(lo, v); hi = Math.max(hi, v); await new Promise((r) => setTimeout(r, 30)); } return [lo, hi]; }, [expr, ms]);

  console.log('1. The page the host serves connects to it instead of starting browser audio');
  await page.goto(`http://127.0.0.1:${PORT}/`);
  await page.waitForFunction(() => window.__rig && window.__rig.ready, null, { timeout: 15000 });
  check(await V('!!window.RIG_HOST') && /NATIVE ENGINE/.test(await page.textContent('#startTitle')), 'the page knows it is served by rig-host (start card: “RIG/01 · NATIVE ENGINE”)');
  await page.check('input[name=src][value=riff]'); await page.click('#startBtn');
  await page.waitForFunction(() => window.__rig.meters && window.__rig.engineChain && window.__rig.engineRack, null, { timeout: 15000 });
  await sleep(800);
  const st = await V(`[document.getElementById('engineState').textContent, window.__rig.ctx && window.__rig.ctx.constructor.name, document.getElementById('hostInfo').textContent]`);
  check(/rig-host/.test(st[0]) && st[1] === 'OfflineAudioContext', `“${st[0]}”; no browser audio running (only an offline context, for decoding files)`);
  check(/Null/.test(st[2]) && /64 frames/.test(st[2]), `the drawer says what the host runs: “${st[2].slice(0, 90)}…”`);
  const chain = await V(`window.__rig.engineChain.map((m) => m.id).join(' ')`), rack = await V(`window.__rig.engineRack.map((s) => s.id).join(' ')`);
  check(chain === 'gate amp cab holo tess' && rack.split(' ').length >= 6, `built from the host's engine: chain ${chain}; rack ${rack}`);

  console.log('2. Meters flow; knobs, the chain, the rack and modulation reach the native engine');
  await fresh(); const m1 = await V('window.__rig.meters.t'); await sleep(1000); const m2 = await V('window.__rig.meters.t');
  const pk = await V('window.__rig.meters.outPk');
  check(m2 - m1 > 0.7 && pk > 0.005, `meter packets keep coming (${(m2 - m1).toFixed(2)} s of engine time in 1 s), the riff plays (output peak ${pk.toFixed(3)})`);
  // a knob nothing modulates (the default preset patches some), so the engine's value is the knob's
  const pid = await page.evaluate(() => Object.keys(window.__rig.state.conns).includes('tess.size') ? 'cab.mix' : 'tess.size');
  await page.evaluate((pid) => window.__rig.cmd(`param_set ${pid.replace('.', ' ')} 0.3`), pid);
  await sleep(300); await fresh();
  const fb = await page.evaluate((pid) => window.__rig.eff(...pid.split('.')), pid);
  check(Math.abs(fb - 0.3) < 1e-3, `${pid} set from the page: the engine runs it at ${fb.toFixed(3)}`);
  await page.evaluate(() => window.__rig.chain.add('tilt'));
  await page.waitForFunction(() => window.__rig.engineChain.some((m) => m.id === 'tilt'), null, { timeout: 10000 }).catch(() => {});
  const chain2 = await V(`window.__rig.engineChain.map((m) => m.id).join(' ')`);
  await page.evaluate(() => window.__rig.rack.add('lfo'));
  await page.waitForFunction(() => window.__rig.engineRack.some((s) => s.id === 'lfo2'), null, { timeout: 10000 }).catch(() => {});
  const rack2 = await V(`window.__rig.engineRack.map((s) => s.id).join(' ')`);
  check(/tilt/.test(chain2) && /lfo2/.test(rack2), `added live: chain now ${chain2}; rack now ${rack2}`);
  const [lo, hi] = await spread(`window.__rig.eff('holo', 'halo')`, 2500);
  check(await V(`window.__rig.state.conns['holo.halo']`) && hi - lo > 0.05, `a patched source moves a knob in the engine: HOLOGRAM halo ${lo.toFixed(2)}..${hi.toFixed(2)}`);

  console.log('3. Files load over the socket, on the host\'s loader thread: the audio never waits for them');
  const nam = fs.readFileSync(path.join(root, 'tests/models/A2.nam'));
  // the slowest audio callback while it loads (the host reports it with each meter packet)
  await page.evaluate(() => { window.__worst = 0; window.__rig.host.ws.addEventListener('message', (e) => { const d = JSON.parse(e.data); if (d.type === 'meters') { window.__cpu = d.worstCpuMs >= 0; window.__worst = Math.max(window.__worst, window.__cpu ? d.worstCpuMs : d.worstMs); } }); });
  const r1 = await page.evaluate(async (bytes) => {
    const got = new Promise((res) => { const old = window.__rig.host.ws.onmessage; window.__rig.host.ws.addEventListener('message', function h(e) { const d = JSON.parse(e.data); if (d.type === 'asset') { window.__rig.host.ws.removeEventListener('message', h); res(d); } }); });
    const data = new Uint8Array(bytes).buffer; window.__rig.node.port.postMessage({ type: 'asset', inst: 'amp', data, ref: 'nam/A2.nam' }, [data]);
    return got;
  }, [...nam]);
  await sleep(400); const worst = await V('window.__worst'), cpu = await V('window.__cpu');
  check(r1.status === 0 && /SlimmableContainer|WaveNet/.test(r1.note), `A2.nam (${(nam.length / 1024).toFixed(0)} KB): status ${r1.status}, “${r1.note}”, built in ${r1.ms} ms`);
  // Judged on the audio thread's own CPU time (Linux: its CPU clock; Windows: its CPU cycles). The old way,
  // building the model on the audio thread, cost it 25-30 ms. Where the host can't time its own CPU (Wine has
  // no cycle counter), wall time is all there is, and a busy test machine adds pauses of 25 ms or more with
  // nothing loading at all, so it's shown, not judged.
  if (cpu) check(worst < 5, `while it loaded, the audio thread's busiest callback took ${worst.toFixed(2)} ms of its own CPU (rig-host 0.1 built the model there: ~25-30 ms)`);
  else console.log(`  [INFO] while it loaded, the slowest callback took ${worst.toFixed(2)} ms of wall time (no CPU timer here, so not judged)`);
  // a module added straight after a file waits for the load, then goes in, in order
  const r3p = page.evaluate(async (bytes) => {
    const got = new Promise((res) => { window.__rig.host.ws.addEventListener('message', function h3(e) { const d = JSON.parse(e.data); if (d.type === 'asset') { window.__rig.host.ws.removeEventListener('message', h3); res(d); } }); });
    const data = new Uint8Array(bytes).buffer; window.__rig.node.port.postMessage({ type: 'asset', inst: 'amp', data, ref: 'nam/A2.nam' }, [data]);
    window.__rig.chain.add('bax');
    return got;
  }, [...nam]);
  const r3 = await r3p;
  await page.waitForFunction(() => window.__rig.engineChain.some((m) => m.id === 'bax'), null, { timeout: 10000 }).catch(() => {});
  check(r3.status === 0 && (await V(`window.__rig.engineChain.some((m) => m.id === 'bax')`)), 'a module added right after a file waits for the load, then joins the chain');
  const r2 = await page.evaluate(async () => {
    const n = 2048, out = new ArrayBuffer(16 + n * 4), h = new DataView(out); h.setInt32(0, 0x31524952, true); h.setInt32(4, 1, true); h.setInt32(8, n, true); h.setFloat32(12, 48000, true);
    const f = new Float32Array(out, 16, n); for (let i = 0; i < n; i++) f[i] = Math.exp(-i / 300) * Math.sin(i * 0.3);
    const got = new Promise((res) => { window.__rig.host.ws.addEventListener('message', function h2(e) { const d = JSON.parse(e.data); if (d.type === 'asset') { window.__rig.host.ws.removeEventListener('message', h2); res(d); } }); });
    window.__rig.node.port.postMessage({ type: 'asset', inst: 'cab', data: out, ref: 'ir/test.wav' }, [out]);
    return got;
  });
  await fresh(); const taps = await V(`window.__rig.modMeter('cab', 'taps')`);
  check(r2.status === 0 && taps === 2048, `a 2048-sample IR: status ${r2.status}, the cab reports ${taps} taps`);

  console.log('4. The round-trip test measures the loopback exactly');
  await page.click('#settingsBtn'); await sleep(300);
  await page.click('#latTest');
  await page.waitForFunction(() => window.__rig.host.latency, null, { timeout: 15000 });
  const lat = await V('window.__rig.host.latency'), note = await page.textContent('#latNote'), top = await page.textContent('#rateLat');
  check(lat.ok && lat.samples === LOOP && /9\.10 ms/.test(note) && /9\.1 ms measured/.test(top), `${lat.samples} samples (${lat.ms} ms) for a ${LOOP}-sample loopback; the drawer and top bar show it (“${top}”)`);
  await page.screenshot({ path: path.join(shots, 'shot-host.png') });

  console.log('5. A reload reconnects to the same host');
  await page.reload(); await page.waitForFunction(() => window.__rig && window.__rig.ready, null, { timeout: 15000 });
  await page.check('input[name=src][value=riff]'); await page.click('#startBtn');
  await page.waitForFunction(() => window.__rig.meters && window.__rig.engineChain, null, { timeout: 15000 }); await sleep(800);
  check(/rig-host/.test(await page.textContent('#engineState')) && (await V('window.__rig.meters.outPk')) > 0.005, 'connected again, and playing');

  check(errors.length === 0, `no console errors${errors.length ? ': ' + errors.join(' | ') : ''}`);
  await browser.close();
  proc.kill('SIGINT'); await sleep(500);
  console.log(fails ? `\n${fails} FAILED` : '\nALL HOST CHECKS PASSED');
  process.exit(fails ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(2); });

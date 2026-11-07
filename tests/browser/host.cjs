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
  // RIG_HOST_AUDIO replaces the simulated device, e.g. "--asio rig" (tests/asio/asio.sh's stand-in driver, also a 437-sample loop)
  const audio = process.env.RIG_HOST_AUDIO ? process.env.RIG_HOST_AUDIO.trim().split(/\s+/) : ['--backend', 'null', '--sim-loopback', String(LOOP)];
  const cfg = path.join(require('os').tmpdir(), `rig-host-test-${PORT}.cfg`);       // where the Driver menu's choice is remembered
  const cfgArg = /wine/.test(bin[0]) ? require('child_process').execSync(`winepath -w ${cfg}`).toString().trim() : cfg;
  const libDir = path.join(require('os').tmpdir(), `rig-host-lib-${PORT}`);            // stands in for Documents/RIG01
  fs.rmSync(libDir, { recursive: true, force: true });
  const libArg = /wine/.test(bin[0]) ? require('child_process').execSync(`winepath -w ${libDir}`).toString().trim() : libDir;
  const args = [...bin.slice(1), ...audio, '--riff', '--port', String(PORT), '--no-browser', '--config', cfgArg, '--library', libArg];
  const proc = spawn(bin[0], args, { cwd: root, env: { ...process.env, WINEDEBUG: '-all', LANG: process.env.LANG || 'C.UTF-8' } });   // Wine reads Linux file names in this locale
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
  const asio = /--asio/.test(process.env.RIG_HOST_AUDIO || '');
  check((asio ? /ASIO .* 256 frames .*in \+ out/ : /Null .* 64 frames/).test(st[2]), `the drawer says what the host runs: “${st[2].slice(0, 110)}…”`);
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

  console.log('6. The Driver menu: every device, a buffer size, switching while it plays, remembered');
  await page.waitForFunction(() => window.__rig.host.devices && document.querySelectorAll('#hostDev option').length > 0, null, { timeout: 10000 });
  const btn = await page.evaluate(() => { const b = document.getElementById('driverBtn'); return { shown: !b.hidden && b.offsetWidth > 0, text: b.textContent }; });
  await page.click('#driverBtn'); await sleep(300);
  const opened = await page.evaluate(() => !document.getElementById('drawer').hidden && document.activeElement === document.getElementById('hostDev'));
  check(btn.shown && /^Driver: .+ · \d+$/.test(btn.text) && opened, `the top bar's “${btn.text}” button opens the Driver menu`);
  const menu = await page.evaluate(() => ({ dev: [...document.querySelectorAll('#hostDev option')].map((o) => [o.textContent, o.selected]), shown: !document.getElementById('hostDevRow').hidden && !document.getElementById('hostBufRow').hidden, panel: !document.getElementById('hostPanel').hidden, d: window.__rig.host.devices }));
  check(menu.shown && menu.dev.some(([t, s]) => s && (asio ? /^ASIO · RIG Test ASIO/.test(t) : /choose input and output/.test(t))) && menu.panel === asio,
    `Driver menu: ${menu.dev.map(([t, s]) => (s ? '[' : '') + t + (s ? ']' : '')).join(' | ')}; Buffer menu shown${asio ? ', Driver settings button' : ''}`);
  const switchTo = async (sel, value) => {
    await page.selectOption(sel, value);
    await page.waitForFunction(() => !document.getElementById('hostDev').disabled, null, { timeout: 20000 }); await sleep(600);
    return page.evaluate(() => ({ info: window.__rig.host.info, d: window.__rig.host.devices, resp: window.__rig.host.lastSwitch }));
  };
  const want = asio ? 128 : 256;
  let r = await switchTo('#hostBuf', String(want));
  const t0 = await V('window.__rig.meters.t'); await sleep(800);
  check(r.info.frames === want && r.d.frames === want && (await V('window.__rig.meters.t')) > t0 + 0.4 && (await V('window.__rig.meters.outPk')) > 0.005,
    `Buffer ${want}: the host reopened at ${r.info.frames} frames, and plays on`);
  if (asio) {
    const absent = menu.d.asio.findIndex((x) => /Thunderbolt/.test(x.name));
    if (absent >= 0) {
      r = await switchTo('#hostDev', `asio:${absent}`);
      check(r.resp.status !== 0 && r.d.asioOn && /RIG Test/.test(menu.d.asio[r.d.drv].name) && (await V('window.__rig.meters.outPk')) > 0.005,
        `a driver whose interface isn't there (${menu.d.asio[absent].name}): refused, and RIG Test ASIO plays on`);
    }
  } else {
    r = await switchTo('#hostDev', 'device');
    check(r.resp.status === 0 && !r.d.asioOn && !(await page.isHidden('#inSelRow')) && (await V('window.__rig.meters.outPk')) > 0.005, `input / output menus: ${r.d.inputs.join(', ')} → ${r.d.outputs.join(', ')}; plays on`);
  }
  const saved = require('fs').existsSync(cfg) ? require('fs').readFileSync(cfg, 'utf8') : '';
  check(new RegExp(`frames=${want}`).test(saved) && /device=/.test(saved), `remembered in ${path.basename(cfg)}: ${saved.split('\n').filter((l) => /=/.test(l)).join(', ')}`);
  try { require('fs').unlinkSync(cfg); } catch (e) { /* */ }

  console.log('7. A plugin bundle from the library: the host loads its own platform\'s .clap and plays it');
  const rigfx = fs.readFileSync(path.join(root, 'dist/plugins/trem.rigfx')).toString('base64');
  const imp = await page.evaluate(async (b) => (await window.__rig.importFiles([new File([Uint8Array.from(atob(b), (c) => c.charCodeAt(0))], 'trem.rigfx')])).map((r) => r.status), rigfx);
  await page.waitForFunction(() => window.__rig.plugins.results['trem.rigfx'], null, { timeout: 20000 });
  const pr = await V("window.__rig.plugins.results['trem.rigfx']");
  check(imp[0] === 'added' && pr.status >= 0, `imported and loaded by the host: “${pr.note}”`);
  await page.waitForFunction(() => window.__rig.engineDesc.moduleTypes.some((t) => t.type === 'trem'), null, { timeout: 10000 });
  await V("window.__rig.chain.add('trem')");
  await page.waitForFunction(() => window.__rig.engineChain.some((e) => e.id === 'trem'), null, { timeout: 10000 });
  await page.evaluate(() => { window.__rig.cmd('param_set trem depth 1'); window.__rig.cmd('param_set trem rate 0.4'); });
  const g = await page.evaluate(async () => { let lo = 9, hi = -9, pk = 0; const t0 = performance.now(); while (performance.now() - t0 < 2000) { const v = window.__rig.modMeter('trem', 'gain'); if (v !== undefined) { lo = Math.min(lo, v); hi = Math.max(hi, v); } pk = Math.max(pk, window.__rig.meters.outPk || 0); await new Promise((r) => setTimeout(r, 20)); } return [lo, hi, pk]; });
  check(g[0] < 0.2 && g[1] > 0.8 && g[2] > 0.003, `TREMOLO in the host's chain at DEPTH 100%: its gain swings ${g[0].toFixed(2)}..${g[1].toFixed(2)}, and the rig plays on (output peak ${g[2].toFixed(3)})`);

  console.log('8. The library is a real folder (Documents/RIG01 by default): one folder per kind of file');
  const ls = (d) => { try { return fs.readdirSync(path.join(libDir, d)).filter((n) => !n.endsWith('.part')); } catch (e) { return null; } };
  check(['NAM', 'IR', 'Effects', 'Mod Sources', 'Skins', 'Presets'].every((d) => ls(d)), `the host made ${fs.readdirSync(libDir).sort().join(', ')}`);
  check(ls('Effects').includes('trem.rigfx') && await V("window.__rig.library.backend") === 'host', 'the page\'s library is that folder: TREMOLO, imported above, is in Effects');
  const randB64 = fs.readFileSync(path.join(root, 'dist/plugins/rand.rigfx')).toString('base64');
  await page.evaluate(async (b) => window.__rig.importFiles([new File([Uint8Array.from(atob(b), (c) => c.charCodeAt(0))], 'rand.rigfx')]), randB64);
  check(ls('Mod Sources').includes('rand.rigfx') && !ls('Effects').includes('rand.rigfx'), 'a source plugin goes in Mod Sources (by its manifest)');
  const wav = Buffer.alloc(44 + 960 * 2); wav.write('RIFF', 0); wav.writeUInt32LE(36 + 960 * 2, 4); wav.write('WAVEfmt ', 8); wav.writeUInt32LE(16, 16); wav.writeUInt16LE(1, 20); wav.writeUInt16LE(1, 22);
  wav.writeUInt32LE(48000, 24); wav.writeUInt32LE(96000, 28); wav.writeUInt16LE(2, 32); wav.writeUInt16LE(16, 34); wav.write('data', 36); wav.writeUInt32LE(960 * 2, 40); wav.writeInt16LE(30000, 44);
  const odd = 'Cab ünïcode – 4x12.wav';
  fs.writeFileSync(path.join(libDir, 'IR', odd), wav);
  await page.evaluate(() => window.dispatchEvent(new Event('focus'))); await sleep(1500);
  const irs = await V("(window.__rig.library.entries.ir || []).map((e) => e.name + ':' + (e.meta ? e.meta.sampleRate : '?'))");
  check(irs.includes(odd + ':48000'), `a file put in IR by hand shows up on coming back to the page, name intact (${irs.join(', ')})`);
  await page.fill('#presetName', 'Folder test'); await page.click('#presetSave'); await sleep(1500);
  const pres = ls('Presets');
  check(pres.includes('board.json') && pres.some((n) => n.endsWith('.rig.json')), `Save writes the pedalboard and its preset to Presets: ${pres.join(', ')}`);
  const irBack = await page.evaluate(async (n) => { const f = await window.__rig.library.core.file('ir', n); return new Uint8Array(await f.arrayBuffer()).length; }, odd);
  check(irBack === wav.length, `the page reads the file's bytes back (${irBack} of ${wav.length})`);
  await page.evaluate(async (n) => { await window.__rig.library.core.remove('ir', n); }, odd);
  check(!ls('IR').includes(odd), 'removing it from the library deletes the file');

  console.log('9. Only the rig page can use the library: other sites and other host names are refused');
  const http = require('http');
  const req = (headers, method = 'GET', p = '/lib/list') => new Promise((res) => { const r = http.request({ host: '127.0.0.1', port: PORT, path: p, method, headers }, (x) => { let b = ''; x.on('data', (d) => { b += d; }); x.on('end', () => res([x.statusCode, b])); }); r.on('error', (e) => res([0, e.message])); r.end(method === 'PUT' ? 'x' : undefined); });
  const ok = await req({ 'X-Rig': '1' }), noHdr = await req({}), evil = await req({ 'X-Rig': '1', Origin: 'http://evil.example' }), rebind = await req({ 'X-Rig': '1', Host: `evil.example:${PORT}` });
  const put = await req({ 'Content-Type': 'text/plain', Origin: 'http://evil.example' }, 'PUT', '/lib/f/plugins%2Fx.clap'), dots = await req({ 'X-Rig': '1' }, 'PUT', '/lib/f/nam%2F..%2F..%2Fx.nam');
  check(ok[0] === 200 && noHdr[0] === 403 && evil[0] === 403 && rebind[0] === 403 && put[0] === 403 && dots[0] === 400 && !fs.existsSync(path.join(libDir, 'Effects', 'x.clap')),
    `the page's own request ${ok[0]}; no header ${noHdr[0]}; another site ${evil[0]}; another host name ${rebind[0]}; another site writing a plugin ${put[0]}; a path with .. ${dots[0]}`);

  console.log('10. Anything this browser kept before is copied into the folder, once');
  await page.evaluate(async () => {
    const db = await new Promise((r) => { const q = indexedDB.open('rig01-library', 1); q.onsuccess = () => r(q.result); });
    const nam = JSON.stringify({ version: '0.5.4', architecture: 'Linear', config: { receptive_field: 2, bias: false }, weights: [1, 0], metadata: { name: 'Old Capture' } });
    await new Promise((r) => { const tx = db.transaction('files', 'readwrite'); tx.objectStore('files').put({ blob: new Blob([nam]), lastModified: Date.now() }, 'nam/old capture.nam'); tx.oncomplete = r; });
    localStorage.removeItem('rig01.lib.brought');
  });
  await page.reload(); await page.waitForFunction(() => window.__rig && window.__rig.library && window.__rig.library.backend === 'host', null, { timeout: 20000 }); await sleep(800);
  check(ls('NAM').includes('old capture.nam') && ls('Effects').includes('trem.rigfx'), `after a reload: the browser's old capture is in NAM (${ls('NAM').join(', ')}), and everything else is still there`);

  check(errors.length === 0, `no console errors${errors.length ? ': ' + errors.join(' | ') : ''}`);
  await browser.close();
  proc.kill('SIGINT'); await sleep(500);
  console.log(fails ? `\n${fails} FAILED` : '\nALL HOST CHECKS PASSED');
  process.exit(fails ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(2); });

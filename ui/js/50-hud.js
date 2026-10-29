// 50-hud.js - focus panel, mod matrix, hint line, view switch, bottom bar and settings drawer.
const panel = $('panel'), matrix = $('matrix'), hint = $('hint');
function renderPanel() {
  const m = MODS.find((x) => x.id === state.focus); if (!m) return;
  const byp = !!state.bypass[m.id];
  let h = `<div><div class="eyebrow">Module ${m.idx + 1}/${MODS.length} · instance ${m.inst}</div><h2>${m.name}</h2><div class="kind">${m.kind}</div></div>
  <div class="row"><button class="btn ${byp ? 'byp' : 'on'}" data-act="bypass" aria-pressed="${byp}">${byp ? 'Bypassed' : 'Active'}</button></div>`;
  if (NOTES[m.id]) h += `<p class="note">${NOTES[m.id]}</p>`;
  h += `<div class="params">`;
  m.params.forEach((p) => {
    const sid = state.conns[p.id], S = sid && SRCMAP[sid];
    h += `<div class="param"><div class="prow"><b><label for="r-${p.id}" style="color:inherit;font:inherit;display:inline">${p.label}</label></b><span class="num" data-disp="${p.id}"></span></div>
    <input type="range" id="r-${p.id}" data-pid="${p.id}" min="0" max="1" step="0.001" value="${p.v}">`;
    if (S) h += `<div class="mod"><span class="dot" style="background:${S.color}"></span><span class="grow">${S.name} <span class="muted mono">±${Math.round(DEPTH * 100)}%</span></span><button class="btn" data-act="unplug" data-pid="${p.id}">Unplug</button></div>`;
    else h += `<div class="mod"><label for="m-${p.id}" class="muted" style="flex-direction:row;align-items:center;gap:8px;font-size:13px">Patch <select id="m-${p.id}" data-patch="${p.id}" style="min-height:32px;width:auto"><option value="">—</option>${SRC.map((s) => `<option value="${s.id}">${s.name}</option>`).join('')}</select></label></div>`;
    h += `</div>`;
  });
  h += `</div><div class="hw"><span class="eyebrow">Text protocol</span><span class="mono" style="font-size:12px">param_set ${m.inst} ${m.params[0].sym} 0.5<br>bypass ${m.inst} 1</span></div>`;
  panel.innerHTML = h;
}
let panelPending = false;
function renderPanelSoon() { if (panelPending) return; panelPending = true; requestAnimationFrame(() => { panelPending = false; renderPanel(); }); }
function renderMatrix() {
  const cols = []; MODS.forEach((m) => m.params.forEach((p) => cols.push(p)));
  let h = `<div class="eyebrow" style="margin-bottom:12px">Source → target</div><div style="overflow-x:auto"><table><thead><tr><th></th>`;
  cols.forEach((p) => h += `<th scope="col"><small>${p.mod.name}</small>${p.label}</th>`);
  h += `</tr></thead><tbody>`;
  SRC.forEach((s) => {
    h += `<tr><th scope="row"><b style="color:${s.color}">${s.name}</b><small>${s.sub}</small></th>`;
    cols.forEach((p) => { const on = state.conns[p.id] === s.id;
      h += `<td><button class="cell" data-pid="${p.id}" data-sid="${s.id}" aria-pressed="${on}" aria-label="${s.name} to ${p.mod.name} ${p.label}${on ? ', patched' : ''}" style="${on ? `background:${s.color};border-color:${s.color};box-shadow:0 0 16px ${s.color}88` : ''}"></button></td>`; });
    h += `</tr>`;
  });
  h += `</tbody></table></div><p class="note" style="margin-top:14px">Each knob takes one source at ±${Math.round(DEPTH * 100)}%. Click a dot to patch or unpatch.</p>`;
  matrix.innerHTML = h;
}
function setHint() {
  if (state.storing) { hint.className = 'hint carry'; hint.textContent = 'Store mode: click a scene letter to save the current settings into it'; return; }
  if (state.carrying) { const s = SRCMAP[state.carrying]; hint.className = 'hint carry'; hint.innerHTML = `<span style="color:${s.color}">Carrying ${s.name}</span> · click a knob to patch it · Esc to drop`; return; }
  hint.className = 'hint'; hint.textContent = `Drag to orbit · scroll to zoom · drag a knob to turn it · click an orb, then a knob, to patch · keys 1–${Math.min(9, MODS.length)}`;
}
function update() { renderPanel(); if (state.view === 'matrix') renderMatrix(); setHint(); carryLine.visible = !!state.carrying; if (state.carrying) carryLine.material.color.set(SRCMAP[state.carrying].color); }
function setView(v) { state.view = v; state.carrying = null; $('vStage').setAttribute('aria-pressed', v === 'stage'); $('vMatrix').setAttribute('aria-pressed', v === 'matrix'); matrix.hidden = v !== 'matrix'; update(); }

function wireHud() {
  panel.addEventListener('click', (e) => {
    const b = e.target.closest('[data-act]'); if (!b) return; const m = MODS.find((x) => x.id === state.focus);
    if (b.dataset.act === 'bypass') { state.bypass[m.id] = !state.bypass[m.id]; rig.cmd(`bypass ${m.inst} ${state.bypass[m.id] ? 1 : 0}`); }
    if (b.dataset.act === 'unplug') connect(b.dataset.pid, null);
    update();
  });
  panel.addEventListener('input', (e) => { const pid = e.target.dataset && e.target.dataset.pid; if (pid) setParam(PARAMS[pid], Number(e.target.value)); });
  panel.addEventListener('change', (e) => { const pid = e.target.dataset && e.target.dataset.patch; if (pid && e.target.value) { connect(pid, e.target.value); update(); } });
  matrix.addEventListener('click', (e) => { const b = e.target.closest('.cell'); if (!b) return; const { pid, sid } = b.dataset; connect(pid, state.conns[pid] === sid ? null : sid); update(); });
  $('vStage').onclick = () => setView('stage'); $('vMatrix').onclick = () => setView('matrix'); $('overview').onclick = overview;
  $('settingsBtn').onclick = () => { const d = $('drawer'); d.hidden = !d.hidden; $('settingsBtn').setAttribute('aria-expanded', String(!d.hidden)); };

  // bottom bar
  document.querySelectorAll('.scene').forEach((b) => b.onclick = () => {
    const id = b.dataset.scene;
    if (state.storing) { state.scenes[id] = snapshot(); state.storing = false; $('storeBtn').setAttribute('aria-pressed', 'false'); b.classList.add('stored'); logLine(`scene ${id} stored`); }
    else if (state.scenes[id]) applySnapshot(state.scenes[id]);
    else logLine(`scene ${id} is empty: press Store, then ${id}`);
    state.scene = state.scenes[id] ? id : state.scene;
    document.querySelectorAll('.scene').forEach((x) => { x.setAttribute('aria-pressed', String(x.dataset.scene === state.scene)); x.classList.toggle('storing', state.storing); });
    setHint();
  });
  $('storeBtn').onclick = () => { state.storing = !state.storing; $('storeBtn').setAttribute('aria-pressed', String(state.storing)); document.querySelectorAll('.scene').forEach((x) => x.classList.toggle('storing', state.storing)); setHint(); };
  let taps = [];
  $('tap').onclick = () => { const n = performance.now(); taps = taps.filter((x) => n - x < 3000); taps.push(n);
    if (taps.length > 1) { const d = (taps[taps.length - 1] - taps[0]) / (taps.length - 1); state.bpm = clamp(Math.round(60000 / d), 30, 300); $('bpm').textContent = state.bpm; rig.cmd(`set_bpm ${state.bpm}`); } };
  $('master').oninput = (e) => { state.master = Number(e.target.value); $('masterVal').textContent = state.master + ' dB'; rig.cmd(`master ${state.master}`); };
  $('ctrlSlider').oninput = (e) => setCtrl(Number(e.target.value), false);

  // settings drawer: audio
  $('srcSel').onchange = async (e) => {
    try { if (e.target.value === 'file') { $('diFile').click(); return; } await setSource(e.target.value, e.target.value === 'device' ? $('inSel').value : undefined); }
    catch (err) { logLine('source error: ' + err.message, true); }
  };
  $('diFile').onchange = async (e) => { const f = e.target.files[0]; if (!f) return;
    try { const buf = await rig.ctx.decodeAudioData(await f.arrayBuffer()); await setSource(buf); $('srcSel').value = 'file'; logLine('DI file: ' + f.name); }
    catch (err) { logLine('could not decode ' + f.name + ': ' + err.message, true); } };
  $('inSel').onchange = async (e) => { try { await setSource('device', e.target.value); } catch (err) { logLine('input error: ' + err.message, true); } };
  $('chSel').onchange = (e) => rig.cmd(`input_channel ${e.target.value}`);
  $('outSel').onchange = async (e) => { try { await rig.ctx.setSinkId(e.target.value); logLine('output: ' + e.target.selectedOptions[0].textContent); } catch (err) { logLine('output error: ' + err.message, true); } };

  // settings drawer: cab IR (only when the chain has a cab module)
  const cab = MODS.find((m) => m.id === 'cab');
  if (!cab) $('irLoad').closest('section').hidden = true;
  $('irLoad').onclick = () => $('irFile').click();
  $('irFile').onchange = async (e) => { const f = e.target.files[0]; if (!f || !rig.ctx || !cab) return;
    try {
      const buf = await rig.ctx.decodeAudioData(await f.arrayBuffer());
      const data = buf.getChannelData(0).slice(0, 2048);
      rig.node.port.postMessage({ type: 'ir', data }, [data.buffer]);
      const sr = buf.sampleRate, d = buf.getChannelData(0), n = Math.min(d.length, 2048); let resp = [];
      for (let i = 0; i <= 63; i++) { const hz = 40 * Math.pow(400, i / 63); let re = 0, im = 0; for (let k = 0; k < n; k++) { const ph = -2 * Math.PI * hz * k / sr; re += d[k] * Math.cos(ph); im += d[k] * Math.sin(ph); } resp.push(10 * Math.log10(re * re + im * im + 1e-12)); }
      const ref = Math.max(...resp); (MODDATA.cab = MODDATA.cab || {}).irResp = resp.map((v) => v - ref);
      $('irName').textContent = `${f.name} · ${Math.min(buf.length, 2048)} taps${buf.length > 2048 ? ' (first 2048 used)' : ''}`;
      cab.kind = 'IR · ' + f.name.slice(0, 18);
    } catch (err) { logLine('could not load IR: ' + err.message, true); } };
  $('irReset').onclick = () => { if (!rig.node || !cab) return; rig.node.port.postMessage({ type: 'ir_default' }); if (MODDATA.cab) MODDATA.cab.irResp = null; $('irName').textContent = 'Built-in synthetic cab (21 ms)'; cab.kind = 'built-in cab'; };

  $('cmdSend').onclick = () => { const v = $('cmdIn').value.trim(); if (v) { rig.cmd(v); $('cmdIn').value = ''; } };
  $('cmdIn').onkeydown = (e) => { if (e.key === 'Enter') $('cmdSend').click(); };
}

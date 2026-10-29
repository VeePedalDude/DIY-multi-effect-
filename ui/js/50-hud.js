// 50-hud.js - focus panel, mod matrix, hint line, view switch, bottom bar and settings drawer.
const panel = $('panel'), matrix = $('matrix'), hint = $('hint');
function renderPanel() {
  const s = srcFocus(); if (s) { renderSourcePanel(s); return; }
  const m = MODS.find((x) => x.id === state.focus); if (!m) return;
  const byp = !!state.bypass[m.id];
  let h = `<div><div class="eyebrow">Module ${m.idx + 1}/${MODS.length} · instance ${m.inst}</div><h2>${m.name}</h2><div class="kind">${moduleSubtitle(m).replace(/</g, '&lt;')}</div></div>
  <div class="row"><button class="btn ${byp ? 'byp' : 'on'}" data-act="bypass" aria-pressed="${byp}">${byp ? 'Bypassed' : 'Active'}</button></div>`;
  if (NOTES[m.id]) h += `<p class="note">${NOTES[m.id]}</p>`;
  h += assetPanelHtml(m);
  h += `<div class="params">`;
  m.params.forEach((p) => {
    const sid = state.conns[p.id], S = sid && SRCMAP[sid];
    h += `<div class="param"><div class="prow"><b><label for="r-${p.id}" style="color:inherit;font:inherit;display:inline">${p.label}</label></b><span class="num" data-disp="${p.id}"></span></div>
    <input type="range" id="r-${p.id}" data-pid="${p.id}" min="0" max="1" step="0.001" value="${p.v}">`;
    if (S) h += `<div class="mod"><span class="dot" style="background:${S.color}"></span><button class="linkbtn grow" data-act="edit-src" data-sid="${S.id}" title="Edit ${S.name}">${S.name}</button><button class="btn mini" data-act="unplug" data-pid="${p.id}">Unplug</button></div>
      <label class="depth" for="dp-${p.id}"><span>Depth</span><input type="range" id="dp-${p.id}" data-depth="${p.id}" min="0" max="1" step="0.01" value="${depthOf(p.id)}" style="accent-color:${S.color}"><span class="mono" data-ddisp="${p.id}">±${Math.round(depthOf(p.id) * 100)}%</span></label>`;
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
    h += `<tr><th scope="row"><button class="linkbtn" data-edit-src="${s.id}" title="Edit ${s.name}"><b style="color:${s.color}">${s.name}</b></button><small>${s.sub}${s.params.length ? ` · ${s.params.length} knob${s.params.length > 1 ? 's' : ''}` : ''}</small></th>`;
    cols.forEach((p) => { const on = state.conns[p.id] === s.id;
      h += `<td><button class="cell" data-pid="${p.id}" data-sid="${s.id}" aria-pressed="${on}" aria-label="${s.name} to ${p.mod.name} ${p.label}${on ? ', patched' : ''}" style="${on ? `background:${s.color};border-color:${s.color};box-shadow:0 0 16px ${s.color}88` : ''}"></button></td>`; });
    h += `</tr>`;
  });
  h += `</tbody></table></div><p class="note" style="margin-top:14px">Each knob takes one source; new patches swing ±${Math.round(DEPTH * 100)}%. Click a dot to patch or unpatch, or a source's name to edit it and its depths.</p>`;
  matrix.innerHTML = h;
}
function setHint() {
  if (state.storing) { hint.className = 'hint carry'; hint.textContent = 'Store mode: click a scene letter to save the current settings into it'; return; }
  if (state.carrying) { const s = SRCMAP[state.carrying]; hint.className = 'hint carry'; hint.innerHTML = `<span style="color:${s.color}">Carrying ${s.name}</span> · click a knob to patch it · Esc to drop`; return; }
  hint.className = 'hint'; hint.textContent = `Drag to orbit · scroll to zoom · drag a knob to turn it · click an orb, then a knob, to patch · click a source's name to edit it · P hides the panel · keys 1–${Math.min(9, MODS.length)}`;
}
// The side panel can be hidden (button or P) and remembers that per browser.
let panelHidden = false;
try { panelHidden = localStorage.getItem('rig01.panelHidden') === '1'; } catch (e) { /* storage blocked: start open */ }
function setPanelHidden(h) {
  panelHidden = h; document.body.classList.toggle('panel-hidden', h);
  try { localStorage.setItem('rig01.panelHidden', h ? '1' : '0'); } catch (e) { /* not remembered */ }
  updatePanelToggle(); resize();
}
function updatePanelToggle() {
  const b = $('panelToggle'), s = srcFocus(), m = MODS.find((x) => x.id === state.focus), name = s ? s.name : m ? m.name : 'panel';
  b.setAttribute('aria-expanded', String(!panelHidden));
  b.innerHTML = panelHidden ? `<span aria-hidden="true">‹</span> ${name}` : `<span aria-hidden="true">›</span>`;
  b.title = panelHidden ? `Show the ${name} panel (P)` : 'Hide the panel (P)';
  b.setAttribute('aria-label', b.title);
}
function update() { renderPanel(); updatePanelToggle(); if (state.view === 'matrix') renderMatrix(); setHint(); carryLine.visible = !!state.carrying; if (state.carrying) carryLine.material.color.set(SRCMAP[state.carrying].color); }
function setView(v) { state.view = v; state.carrying = null; $('vStage').setAttribute('aria-pressed', v === 'stage'); $('vMatrix').setAttribute('aria-pressed', v === 'matrix'); matrix.hidden = v !== 'matrix'; update(); }

function wireHud() {
  panel.addEventListener('click', (e) => {
    const b = e.target.closest('[data-act]'); if (!b) return; const m = MODS.find((x) => x.id === state.focus);
    if (b.dataset.act === 'bypass' && m) { state.bypass[m.id] = !state.bypass[m.id]; rig.cmd(`bypass ${m.inst} ${state.bypass[m.id] ? 1 : 0}`); }
    if (b.dataset.act === 'unplug') connect(b.dataset.pid, null);
    if (b.dataset.act === 'bypass' || b.dataset.act === 'unplug') update();
  });
  panel.addEventListener('input', (e) => { const pid = e.target.dataset && e.target.dataset.pid; if (pid) setParam(PARAMS[pid], Number(e.target.value)); });
  panel.addEventListener('change', (e) => { const pid = e.target.dataset && e.target.dataset.patch; if (pid && e.target.value) { connect(pid, e.target.value); update(); } });
  matrix.addEventListener('click', (e) => { const b = e.target.closest('.cell'); if (!b) return; const { pid, sid } = b.dataset; connect(pid, state.conns[pid] === sid ? null : sid); update(); });
  $('panelToggle').onclick = () => setPanelHidden(!panelHidden);
  window.addEventListener('keydown', (e) => {
    if (e.target.closest && e.target.closest('input,textarea,select')) return;
    if ((e.key === 'p' || e.key === 'P') && !e.ctrlKey && !e.metaKey && !e.altKey) setPanelHidden(!panelHidden);
  });
  document.body.classList.toggle('panel-hidden', panelHidden);
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

  $('cmdSend').onclick = () => { const v = $('cmdIn').value.trim(); if (v) { rig.cmd(v); $('cmdIn').value = ''; } };
  $('cmdIn').onkeydown = (e) => { if (e.key === 'Enter') $('cmdSend').click(); };
}

// 51-source-panel.js - the side panel for a modulation source: its knobs (src_set), a live trace,
// and every knob it is patched to, each with its own depth. Opened by clicking a source's orb or
// name on the stage, its row in the matrix, or its name in a module's panel.
// state.focus is 'src:<id>' while a source is open (a module id otherwise).
const srcFocus = () => (typeof state.focus === 'string' && state.focus.startsWith('src:') ? SRCMAP[state.focus.slice(4)] : null);
function focusSource(s) { state.focus = 'src:' + s.id; update(); }
const srcEsc = (t) => String(t).replace(/&/g, '&amp;').replace(/</g, '&lt;');

function renderSourcePanel(s) {
  const targets = Object.keys(state.conns).filter((pid) => state.conns[pid] === s.id && PARAMS[pid]);
  const free = Object.values(PARAMS).filter((p) => !state.conns[p.id]);
  const carrying = state.carrying === s.id;
  let h = `<div><div class="eyebrow">Modulation source ${s.idx + 1}/${SRC.length}</div><h2 style="color:${s.color}">${srcEsc(s.name)}</h2><div class="kind">${srcEsc(s.sub)}</div></div>
  <canvas id="srcScope" class="scope" width="580" height="150" aria-label="${srcEsc(s.name)} output over the last 4 seconds" role="img"></canvas>
  <div class="row"><button class="btn ${carrying ? 'on' : ''}" data-act="carry-src" aria-pressed="${carrying}">${carrying ? 'Carrying · click a knob' : 'Carry a cable'}</button>
    ${s.params.length ? '<button class="btn" data-act="reset-src">Defaults</button>' : ''}</div>`;
  if (s.ext) h += `<p class="note">Takes outside input: ${srcEsc(s.ext)} (<span class="mono">ext ${s.id} &lt;value&gt;</span>).</p>`;
  if (s.params.length) {
    h += `<div class="params">`;
    s.params.forEach((q) => {
      h += `<div class="param"><div class="prow"><b><label for="sp-${q.id}" style="color:inherit;font:inherit;display:inline">${q.label}</label></b><span class="num" data-sdisp="${q.id}"></span></div>
      <input type="range" id="sp-${q.id}" data-spid="${q.id}" min="0" max="1" step="0.001" value="${q.v}" style="accent-color:${s.color}"></div>`;
    });
    h += `</div>`;
  } else h += `<p class="note">This source has no settings.</p>`;
  h += `<div class="patches"><span class="eyebrow">Patched to · ${targets.length}</span>`;
  if (!targets.length) h += `<p class="note">Nothing yet. Carry a cable to a knob on the stage, or pick one below.</p>`;
  targets.forEach((pid) => {
    const p = PARAMS[pid], d = depthOf(pid);
    h += `<div class="patch"><div class="prow"><span><b>${p.mod.name}</b> · ${p.label}</span><button class="btn mini" data-act="unplug" data-pid="${pid}">Unplug</button></div>
      <label class="depth" for="dp-${pid}"><span>Depth</span><input type="range" id="dp-${pid}" data-depth="${pid}" min="0" max="1" step="0.01" value="${d}" style="accent-color:${s.color}"><span class="mono" data-ddisp="${pid}">±${Math.round(d * 100)}%</span></label></div>`;
  });
  if (free.length) h += `<label class="muted" style="flex-direction:row;align-items:center;gap:8px;font-size:13px">Patch to <select data-srcpatch="${s.id}" style="min-height:32px;width:auto"><option value="">—</option>${free.map((p) => `<option value="${p.id}">${p.mod.name} · ${p.label}</option>`).join('')}</select></label>`;
  h += `</div>`;
  if (s.params.length) h += `<div class="hw"><span class="eyebrow">Text protocol</span><span class="mono" style="font-size:12px">src_set ${s.id} ${s.params[0].sym} 0.5</span></div>`;
  panel.innerHTML = h;
}
// Live trace of the open source (the last ~4 s of its output, -1..1), drawn from the main loop.
function drawSourceScope() {
  const s = srcFocus(), cv = s && document.getElementById('srcScope'); if (!cv) return;
  const c = cv.getContext('2d'), W = cv.width, H = cv.height;
  c.clearRect(0, 0, W, H); c.fillStyle = '#070B14'; c.fillRect(0, 0, W, H);
  c.strokeStyle = '#1F2A40'; c.lineWidth = 1; c.beginPath(); c.moveTo(0, H / 2); c.lineTo(W, H / 2); c.stroke();
  c.strokeStyle = s.color; c.lineWidth = 3; c.beginPath();
  s.hist.forEach((v, i) => { const x = W * i / 119, y = H / 2 - v * (H / 2 - 10); i ? c.lineTo(x, y) : c.moveTo(x, y); });
  c.stroke();
}
function refreshSourceReadouts() {
  document.querySelectorAll('[data-sdisp]').forEach((el) => { const [sid, sym] = el.dataset.sdisp.split('.'), q = SRCMAP[sid] && SRCMAP[sid].params.find((x) => x.sym === sym); if (q) el.textContent = q.fmt(q.v); });
}
function wireSourcePanel() {
  panel.addEventListener('input', (e) => {
    const t = e.target, ds = t.dataset || {};
    if (ds.spid) { const [sid, sym] = ds.spid.split('.'); const q = SRCMAP[sid].params.find((x) => x.sym === sym); setSrcParam(q, Number(t.value)); refreshSourceReadouts(); }
    if (ds.depth) { setDepth(ds.depth, Number(t.value)); document.querySelectorAll(`[data-ddisp="${ds.depth}"]`).forEach((el) => { el.textContent = `±${Math.round(Number(t.value) * 100)}%`; }); }
  });
  panel.addEventListener('change', (e) => {
    const sid = e.target.dataset && e.target.dataset.srcpatch;
    if (sid && e.target.value) { connect(e.target.value, sid); update(); }
  });
  panel.addEventListener('click', (e) => {
    const b = e.target.closest('[data-act]'); if (!b) return;
    const s = srcFocus();
    if (b.dataset.act === 'edit-src') focusSource(SRCMAP[b.dataset.sid]);
    if (!s) return;
    if (b.dataset.act === 'carry-src') { state.carrying = state.carrying === s.id ? null : s.id; update(); }
    if (b.dataset.act === 'reset-src') { s.params.forEach((q) => setSrcParam(q, q.def)); update(); }
  });
  matrix.addEventListener('click', (e) => { const b = e.target.closest('[data-edit-src]'); if (b) { setView('stage'); focusSource(SRCMAP[b.dataset.editSrc]); } });
}

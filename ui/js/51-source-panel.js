// 51-source-panel.js - the side panel for the modulation rack.
//  - A source's panel (state.focus 'src:<id>'): its knobs (src_set), a live trace, every knob it
//    moves with its own depth, and on each of its own knobs which source moves it (cross-
//    modulation). Duplicate and Remove act on the rack. Opened by clicking a source's orb or name on
//    the stage, its row in the matrix, or its name in a module's panel.
//  - The add panel (state.focus 'add'): every kind of source the engine has, to add another.
//    Opened from the "+" orb at the end of the source row, or the matrix.
const srcFocus = () => (typeof state.focus === 'string' && state.focus.startsWith('src:') ? SRCMAP[state.focus.slice(4)] : null);
function focusSource(s) { state.focus = 'src:' + s.id; update(); }
const srcEsc = (t) => String(t).replace(/&/g, '&amp;').replace(/</g, '&lt;');
let srcRemoveArmed = null;                    // the source whose Remove was clicked once (click again to confirm)

function patchRowHtml(pid, S) {            // one patched knob: who moves it, unplug, depth
  const d = depthOf(pid);
  return `<div class="mod"><span class="dot" style="background:${S.color}"></span><button class="linkbtn grow" data-act="edit-src" data-sid="${S.id}" title="Edit ${srcEsc(S.name)}">${srcEsc(S.name)}</button><button class="btn mini" data-act="unplug" data-pid="${pid}">Unplug</button></div>
    <label class="depth" for="dp-${pid}"><span>Depth</span><input type="range" id="dp-${pid}" data-depth="${pid}" min="0" max="1" step="0.01" value="${d}" style="accent-color:${S.color}"><span class="mono" data-ddisp="${pid}">±${Math.round(d * 100)}%</span></label>`;
}
function renderSourcePanel(s) {
  const targets = Object.keys(state.conns).filter((pid) => state.conns[pid] === s.id && knobOf(pid));
  const free = Object.values(PARAMS).concat(Object.values(SPARAMS)).filter((k) => !state.conns[k.id]);
  const carrying = state.carrying === s.id, other = state.carrying && state.carrying !== s.id ? SRCMAP[state.carrying] : null;
  const armed = srcRemoveArmed === s.id;
  let h = `<div><div class="eyebrow">Modulation source ${s.idx + 1}/${SRC.length} · ${srcEsc(TYPES[s.type].name)} · id ${srcEsc(s.id)}</div><h2 style="color:${s.color}">${srcEsc(s.name)}</h2><div class="kind">${srcEsc(s.sub)}</div></div>
  <canvas id="srcScope" class="scope" width="580" height="150" aria-label="${srcEsc(s.name)} output over the last 4 seconds" role="img"></canvas>
  <div class="row"><button class="btn ${carrying ? 'on' : ''}" data-act="carry-src" aria-pressed="${carrying}">${carrying ? 'Carrying · click a knob' : 'Carry a cable'}</button>
    ${s.params.length ? '<button class="btn" data-act="reset-src">Defaults</button>' : ''}
    <button class="btn" data-act="dup-src" ${rackFull() ? 'disabled title="The rack is full"' : 'title="Another one of these, same settings"'}>Duplicate</button>
    <button class="btn ${armed ? 'byp' : ''}" data-act="remove-src" title="Take it out of the rack, with its patches">${armed ? 'Confirm remove' : 'Remove'}</button></div>`;
  if (other) h += `<p class="note">Carrying <b style="color:${other.color}">${srcEsc(other.name)}</b>: pick one of ${srcEsc(s.name)}'s knobs below for it to move.</p>`;
  if (s.ext) h += `<p class="note">Takes outside input: ${srcEsc(s.ext)} (<span class="mono">ext ${s.id} &lt;value&gt;</span>).</p>`;
  if (s.params.length) {
    h += `<div class="params">`;
    s.params.forEach((q) => {
      const by = state.conns[q.id] && SRCMAP[state.conns[q.id]];
      h += `<div class="param"><div class="prow"><b><label for="sp-${q.id}" style="color:inherit;font:inherit;display:inline">${q.label}</label></b><span class="num" data-sdisp="${q.id}"></span></div>
      <input type="range" id="sp-${q.id}" data-spid="${q.id}" min="0" max="1" step="0.001" value="${q.v}" style="accent-color:${s.color}">`;
      if (by) h += patchRowHtml(q.id, by);
      else if (state.carrying) h += `<div class="mod"><button class="btn mini" data-act="patch-here" data-pid="${q.id}">Patch ${srcEsc(SRCMAP[state.carrying].name)} here</button></div>`;
      else h += `<div class="mod"><label for="sm-${q.id}" class="muted" style="flex-direction:row;align-items:center;gap:8px;font-size:13px">Moved by <select id="sm-${q.id}" data-spatch="${q.id}" style="min-height:32px;width:auto"><option value="">—</option>${SRC.map((x) => `<option value="${x.id}">${srcEsc(x.name)}${x === s ? ' (itself)' : ''}</option>`).join('')}</select></label></div>`;
      h += `</div>`;
    });
    h += `</div>`;
  } else h += `<p class="note">This source has no settings.</p>`;
  h += `<div class="patches"><span class="eyebrow">Moves · ${targets.length}</span>`;
  if (!targets.length) h += `<p class="note">Nothing yet. Carry a cable to a knob on the stage or to another source, or pick one below.</p>`;
  targets.forEach((pid) => {
    const d = depthOf(pid);
    h += `<div class="patch"><div class="prow"><span>${srcEsc(targetLabel(pid))}</span><button class="btn mini" data-act="unplug" data-pid="${pid}">Unplug</button></div>
      <label class="depth" for="dt-${pid}"><span>Depth</span><input type="range" id="dt-${pid}" data-depth="${pid}" min="0" max="1" step="0.01" value="${d}" style="accent-color:${s.color}"><span class="mono" data-ddisp="${pid}">±${Math.round(d * 100)}%</span></label></div>`;
  });
  if (free.length) h += `<label class="muted" style="flex-direction:row;align-items:center;gap:8px;font-size:13px">Patch to <select data-srcpatch="${s.id}" style="min-height:32px;width:auto"><option value="">—</option>`
    + `<optgroup label="Module knobs">${free.filter((k) => k.mod).map((k) => `<option value="${k.id}">${srcEsc(k.mod.name)} · ${k.label}</option>`).join('')}</optgroup>`
    + `<optgroup label="Source knobs (cross-modulation)">${free.filter((k) => k.src).map((k) => `<option value="${k.id}">${srcEsc(k.src.name)} · ${k.label}</option>`).join('')}</optgroup></select></label>`;
  h += `</div>`;
  h += `<div class="hw"><span class="eyebrow">Text protocol</span><span class="mono" style="font-size:12px">${s.params.length ? `src_set ${s.id} ${s.params[0].sym} 0.5<br>src_mod ${s.id} ${s.params[0].sym} &lt;source&gt; 0.3<br>` : ''}src_remove ${s.id}</span></div>`;
  panel.innerHTML = h;
}
// The add panel for sources: a tab per category (engine/sources/<category>/), search across them all.
PICKERS.src = {
  cats: () => DESC.sourceCategories,
  items: () => TYPE_ORDER.map((t) => { const d = TYPES[t], have = SRC.filter((s) => s.type === t).length; return { type: t, name: d.name, sub: d.sub, category: d.category, have,
    color: instanceColor(SKIN, t, have + 1, d.index), detail: (d.params.length ? d.params.map((q) => q.label.toLowerCase()).join(' · ') : 'no settings') + (d.ext ? ' · takes outside input' : '') }; }),
  full: () => rackFull(), attr: 'data-add-type', noun: 'source', folder: 'engine/sources', doc: 'docs/ADDING_A_SOURCE.md',
};
function renderAddPanel() {
  panel.innerHTML = `<div><div class="eyebrow">Modulation rack · ${SRC.length} of ${DESC.maxSources}</div><h2>Add a source</h2><div class="kind">${rackFull() ? 'The rack is full: remove one first.' : 'Any kind, as many as you like (several LFOs are fine).'}</div></div>
  ${pickerHtml('src')}
  <p class="note">Sources can move each other: open one and use <b>Moved by</b> on any of its knobs, or carry a cable from one orb to another.</p>
  <div class="hw"><span class="eyebrow">Text protocol</span><span class="mono" style="font-size:12px">src_add lfo lfo2<br>src_remove lfo2</span></div>`;
}
// Live trace of the open source (the last ~4 s of its output, -1..1), drawn from the main loop.
function drawSourceScope() {
  const s = srcFocus(), cv = s && document.getElementById('srcScope'); if (!cv) return;
  const c = cv.getContext('2d'), W = cv.width, H = cv.height;
  c.clearRect(0, 0, W, H); c.fillStyle = SKIN.screen.glass; c.fillRect(0, 0, W, H);
  c.strokeStyle = SKIN.screen.grid; c.lineWidth = 1; c.beginPath(); c.moveTo(0, H / 2); c.lineTo(W, H / 2); c.stroke();
  c.strokeStyle = s.color; c.lineWidth = 3; c.beginPath();
  s.hist.forEach((v, i) => { const x = W * i / 119, y = H / 2 - v * (H / 2 - 10); i ? c.lineTo(x, y) : c.moveTo(x, y); });
  c.stroke();
}
// Knob readouts: the setting, and where cross-modulation has it right now when it's patched.
function refreshSourceReadouts() {
  document.querySelectorAll('[data-sdisp]').forEach((el) => {
    const q = SPARAMS[el.dataset.sdisp]; if (!q) return;
    el.textContent = state.conns[q.id] ? `${q.fmt(q.v)} → ${q.fmt(srcEff(q))}` : q.fmt(q.v);
  });
}
function wireSourcePanel() {
  panel.addEventListener('input', (e) => {
    const t = e.target, ds = t.dataset || {};
    if (ds.spid) { setSrcParam(SPARAMS[ds.spid], Number(t.value)); refreshSourceReadouts(); }
    if (ds.depth) { setDepth(ds.depth, Number(t.value)); document.querySelectorAll(`[data-ddisp="${ds.depth}"]`).forEach((el) => { el.textContent = `±${Math.round(Number(t.value) * 100)}%`; }); }
  });
  panel.addEventListener('change', (e) => {
    const ds = e.target.dataset || {};
    if (ds.srcpatch && e.target.value) { connect(e.target.value, ds.srcpatch); update(); }
    if (ds.spatch && e.target.value) { connect(ds.spatch, e.target.value); update(); }
  });
  panel.addEventListener('click', (e) => {
    const add = e.target.closest('[data-add-type]');
    if (add) { const s = addSource(add.dataset.addType); if (s) focusSource(s); return; }
    const b = e.target.closest('[data-act]'); if (!b) return;
    const s = srcFocus();
    if (b.dataset.act === 'edit-src') { focusSource(SRCMAP[b.dataset.sid]); return; }
    if (!s) return;
    if (b.dataset.act !== 'remove-src') srcRemoveArmed = null;
    if (b.dataset.act === 'carry-src') { state.carrying = state.carrying === s.id ? null : s.id; update(); }
    if (b.dataset.act === 'reset-src') { s.params.forEach((q) => setSrcParam(q, q.def)); update(); }
    if (b.dataset.act === 'patch-here') { connect(b.dataset.pid, state.carrying); state.carrying = null; update(); }
    if (b.dataset.act === 'dup-src') { const d = duplicateSource(s); if (d) focusSource(d); }
    if (b.dataset.act === 'remove-src') {
      if (srcRemoveArmed !== s.id) { srcRemoveArmed = s.id; update(); setTimeout(() => { if (srcRemoveArmed === s.id) { srcRemoveArmed = null; update(); } }, 4000); return; }
      srcRemoveArmed = null; removeSource(s); update();
    }
  });
  matrix.addEventListener('click', (e) => {
    const b = e.target.closest('[data-edit-src]'); if (b) { focusSource(SRCMAP[b.dataset.editSrc]); return; }
    if (e.target.closest('[data-matrix-add]')) { state.focus = 'add'; update(); }
  });
}

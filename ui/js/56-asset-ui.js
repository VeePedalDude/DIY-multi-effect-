// 56-asset-ui.js - each module's file slot: the library browser in the focus panel, drag and drop
// onto modules (stage or panel), and loading the chosen file into the engine.
const assetQuery = {};                      // module id -> search text
const assetStatus = {};                     // module id -> short status line
const removeArmed = {};                     // module id -> timer while 'Confirm remove' is showing

function assetListHtml(m) {
  const all = library.entries[m.asset.kind] || [], q = assetQuery[m.id] || '', cur = state.assets[m.id];
  const hits = all.filter((e) => matchesQuery(e, q)).slice(0, 200);
  if (!all.length) return `<li class="meta">No ${kindLabel(m.asset.kind)} in the library yet.</li>`;
  if (!hits.length) return `<li class="meta">Nothing matches “${q.replace(/</g, '&lt;')}”.</li>`;
  return hits.map((e) => `<li><button data-asset-pick="${encodeURIComponent(e.name)}" aria-current="${cur && cur.name === e.name}"><span>${e.name.replace(/</g, '&lt;')}</span><span class="meta">${metaLine(e)}</span></button></li>`).join('');
}
function assetPanelHtml(m) {
  if (!m.asset) return '';
  const a = m.asset, cur = state.assets[m.id], total = (library.entries[a.kind] || []).length;
  return `<div class="asset" data-asset-mod="${m.id}">
    <span class="eyebrow">${a.label} · ${total} in library</span>
    <div class="cur">${cur ? `<b>${cur.name.replace(/</g, '&lt;')}</b>` : '<b>Built-in</b>'}<span class="muted">${assetStatus[m.id] || ''}</span></div>
    ${a.ready ? '' : `<p class="note warnline">This build can’t load these files yet. They are stored, searchable and saved in presets.</p>`}
    <input type="search" id="as-${m.id}" data-asset-search="${m.id}" placeholder="Search ${kindLabel(a.kind)}…" aria-label="Search ${kindLabel(a.kind)}" value="${(assetQuery[m.id] || '').replace(/"/g, '&quot;')}">
    <ul id="al-${m.id}">${assetListHtml(m)}</ul>
    <div class="drop" data-drop="${m.id}">Drop ${a.exts} files here, or
      <button class="btn" data-act="asset-import" data-mod="${m.id}" style="min-height:32px;font-size:11px;margin-left:6px">Import…</button>
      ${cur ? `<button class="btn" data-act="asset-clear" data-mod="${m.id}" style="min-height:32px;font-size:11px;margin-left:6px">Use built-in</button>` : ''}
      ${cur && !cur.missing ? `<button class="btn" data-act="asset-remove" data-mod="${m.id}" style="min-height:32px;font-size:11px;margin-left:6px">${removeArmed[m.id] ? 'Confirm remove' : 'Remove file'}</button>` : ''}
    </div>
  </div>`;
}
async function selectAsset(m, name) {
  const e = (library.entries[m.asset.kind] || []).find((x) => x.name === name); if (!e) return;
  state.assets[m.id] = { kind: m.asset.kind, name: e.name, size: e.size, hash: await library.core.hash(m.asset.kind, e.name) };
  (MODDATA[m.id] = MODDATA[m.id] || {}).assetName = e.name;
  assetStatus[m.id] = m.asset.ready ? (rig.node ? ' · loading…' : ' · loads when audio starts') : ' · saved with presets';
  logLine(`asset_load ${m.inst} ${m.asset.kind}/${e.name}`);
  renderPanel();
  if (m.asset.ready) await pushAsset(m);
}
function clearAsset(m) {
  delete state.assets[m.id];
  const d = MODDATA[m.id] || (MODDATA[m.id] = {}); d.assetName = null; d.irResp = null; d.irDecay = null; d.namMeta = null; d.engineNote = '';
  assetStatus[m.id] = '';
  if (rig.node) rig.node.port.postMessage({ type: 'asset_clear', inst: m.inst });
  logLine(`asset_clear ${m.inst}`); renderPanel();
}
// Read the chosen file and hand it to the engine. A .nam goes as its text; an IR is decoded by the
// browser (any WAV it can read, resampled to the engine's rate) and sent as RIR1 header + planar floats.
const IR_MAX_SECONDS = 20;
async function pushAsset(m) {
  const ref = state.assets[m.id]; if (!ref || ref.missing || !m.asset.ready || !rig.node) return;
  const d = MODDATA[m.id] || (MODDATA[m.id] = {});
  try {
    const file = await library.core.file(ref.kind, ref.name), raw = await file.arrayBuffer();
    let data = raw;
    if (ref.kind === 'ir') {
      data = await irPayload(raw, d);
      if (d.irTrimmed) assetStatus[m.id] = ` · first ${IR_MAX_SECONDS} s used`;
    } else if (ref.kind === 'nam') {
      d.namMeta = parseNamMeta(new TextDecoder().decode(raw));
    }
    rig.node.port.postMessage({ type: 'asset', inst: m.inst, data, ref: `${ref.kind}/${ref.name}` }, [data]);
  } catch (e) { assetStatus[m.id] = ' · couldn’t load: ' + e.message; logLine(`asset ${ref.name}: ${e.message}`, true); renderPanel(); }
}
async function irPayload(raw, d) {
  const buf = await rig.ctx.decodeAudioData(raw.slice(0)), sr = buf.sampleRate;
  const ch = Math.min(2, buf.numberOfChannels), frames = Math.min(buf.length, Math.floor(IR_MAX_SECONDS * sr));
  d.irTrimmed = buf.length > frames;
  const out = new ArrayBuffer(16 + ch * frames * 4), h = new DataView(out);
  h.setInt32(0, 0x31524952, true); h.setInt32(4, ch, true); h.setInt32(8, frames, true); h.setFloat32(12, sr, true);
  for (let c = 0; c < ch; c++) new Float32Array(out, 16 + c * frames * 4, frames).set(buf.getChannelData(c).subarray(0, frames));
  const first = buf.getChannelData(0).subarray(0, frames);
  d.irResp = irResponse(first, sr); d.irDecay = irDecay(first, sr); d.irSecs = frames / sr; d.irCh = ch;
  return out;
}
// Delete the module's current file from the library (two clicks), then fall back to built-in.
async function removeAssetFile(m) {
  const cur = state.assets[m.id]; if (!cur) return;
  if (!removeArmed[m.id]) { removeArmed[m.id] = setTimeout(() => { delete removeArmed[m.id]; renderPanel(); }, 4000); renderPanel(); return; }
  clearTimeout(removeArmed[m.id]); delete removeArmed[m.id];
  try { await library.core.remove(cur.kind, cur.name); logLine(`removed ${cur.kind}/${cur.name} from the library`); }
  catch (e) { logLine(`couldn't remove ${cur.name}: ${e.message}`, true); }
  clearAsset(m); await refreshLibrary();
}
function pushAllAssets() { MODS.forEach((m) => { if (m.asset && m.asset.ready && state.assets[m.id] && !state.assets[m.id].missing) pushAsset(m); }); }
function onAssetReply(d) {
  const m = MODS[d.inst]; if (!m) return;
  const md = MODDATA[m.id] || (MODDATA[m.id] = {});
  if (d.ref) {
    md.engineNote = d.status === 0 ? d.note : '';
    assetStatus[m.id] = d.status === 0 ? ` · ${d.note}${md.irTrimmed ? ` · first ${IR_MAX_SECONDS} s used` : ''}` : ` · couldn’t load: ${d.note || 'resp ' + d.status}`;
  } else md.engineNote = '';
  logLine(d.ref ? `${d.ref} -> resp ${d.status}${d.note ? ' (' + d.note + ')' : ''}${d.ms ? ` · ${d.ms} ms` : ''}` : `asset_clear ${d.inst} -> resp ${d.status}`, d.status !== 0);
  renderPanelSoon();
}
// Energy decay (dB, 48 points over the IR's length): the reverb-style view of a long IR
function irDecay(d, sr) {
  const n = d.length, tail = new Float64Array(n + 1);
  for (let i = n - 1; i >= 0; i--) tail[i] = tail[i + 1] + d[i] * d[i];
  const out = []; for (let k = 0; k < 48; k++) out.push(10 * Math.log10(tail[Math.floor(k * n / 48)] / (tail[0] || 1) + 1e-12));
  return out;
}
// 64 log-spaced magnitudes (dB, 40 Hz..16 kHz) for the cab screen
function irResponse(d, sr) {
  const n = Math.min(d.length, 2048), out = [];
  for (let i = 0; i <= 63; i++) { const hz = 40 * Math.pow(400, i / 63); let re = 0, im = 0; for (let k = 0; k < n; k++) { const ph = -2 * Math.PI * hz * k / sr; re += d[k] * Math.cos(ph); im += d[k] * Math.sin(ph); } out.push(10 * Math.log10(re * re + im * im + 1e-12)); }
  const ref = Math.max(...out); return out.map((v) => v - ref);
}

function wireAssetUi() {
  panel.addEventListener('input', (e) => {
    const id = e.target.dataset && e.target.dataset.assetSearch; if (!id) return;
    assetQuery[id] = e.target.value; const m = MODS.find((x) => x.id === id); $('al-' + id).innerHTML = assetListHtml(m);
  });
  panel.addEventListener('click', (e) => {
    const pick = e.target.closest('[data-asset-pick]');
    if (pick) { const m = MODS.find((x) => x.id === pick.closest('[data-asset-mod]').dataset.assetMod); selectAsset(m, decodeURIComponent(pick.dataset.assetPick)); return; }
    const b = e.target.closest('[data-act="asset-import"],[data-act="asset-clear"],[data-act="asset-remove"]'); if (!b) return;
    const m = MODS.find((x) => x.id === b.dataset.mod);
    if (b.dataset.act === 'asset-clear') clearAsset(m);
    else if (b.dataset.act === 'asset-remove') removeAssetFile(m);
    else { $('libFiles').dataset.target = m.id; $('libFiles').accept = m.asset.exts; $('libFiles').click(); }
  });
  // drop onto a module's panel
  panel.addEventListener('dragover', (e) => { const z = e.target.closest('[data-drop]'); if (!z) return; e.preventDefault(); z.classList.add('over'); });
  panel.addEventListener('dragleave', (e) => { const z = e.target.closest('[data-drop]'); if (z) z.classList.remove('over'); });
  panel.addEventListener('drop', (e) => {
    const z = e.target.closest('[data-drop]'); if (!z) return; e.preventDefault(); e.stopPropagation(); z.classList.remove('over');
    importFiles([...e.dataTransfer.files], MODS.find((m) => m.id === z.dataset.drop));
  });
  // drop onto the stage: onto a module face = import + load there; elsewhere = sort into the library by type
  const target = (e) => { const h = pick(e); return h && h.type === 'face' && h.mod.asset ? h.mod : null; };
  canvas.addEventListener('dragover', (e) => { e.preventDefault(); const m = target(e); const el = $('dropHint'); el.hidden = false; el.textContent = m ? `Drop to import into the library and load into ${m.name}` : 'Drop to import into the library'; });
  canvas.addEventListener('dragleave', () => { $('dropHint').hidden = true; });
  canvas.addEventListener('drop', async (e) => {
    e.preventDefault(); $('dropHint').hidden = true;
    const files = [...e.dataTransfer.files]; if (!files.length) return;
    const m = target(e);
    if (m && files.some((f) => kindForName(f.name, library.kinds) === m.asset.kind)) {
      await importFiles(files.filter((f) => kindForName(f.name, library.kinds) === m.asset.kind), m);
      const rest = files.filter((f) => kindForName(f.name, library.kinds) !== m.asset.kind); if (rest.length) await importFiles(rest, null);
    } else await importFiles(files, null);
  });
  // keep the browser from opening a file dropped anywhere else on the page
  window.addEventListener('dragover', (e) => e.preventDefault());
  window.addEventListener('drop', (e) => e.preventDefault());
}

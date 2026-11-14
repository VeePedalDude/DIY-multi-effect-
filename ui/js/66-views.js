// 66-views.js - views: screens that come as .rigview files in the library's views/ folder (docs/VIEWS.md).
// Each is a tab in the top bar that swaps the stage for one card, like the Chord Chart's (cardFit and the
// card frame from 64-chart.js). The view's own code runs in a sandboxed frame over the card (scripts only,
// no network by CSP), with the runtime and theory kit (UI_VIEWKIT) before it, and talks to the page only
// by the messages below: ready, save (its settings, in this browser), key (shortcuts). The page sends it
// init, layout, skin, show / hide, and its feeds (notes, sources, levels) while it's showing.
// The library decides (syncViews, after every read of it): a new file adds a tab, a changed one is
// rebuilt, a removed one takes its tab away.
const views = { list: new Map(), sync: null };       // id -> { m (manifest), file, key, js, css, el, btn, frame, fit, ready }
rig.views = views;
const VIEW_RESERVED = 'vctkp0123456789';             // the rig's own shortcut keys
const viewOf = (v) => (v && v.startsWith('view:') ? views.list.get(v.slice(5)) : null);

// .rigview bytes -> { manifest, js, css } (throws if it isn't one)
async function readRigview(bytes) {
  if (bytes.length > 4 * 1024 * 1024) throw new Error('bigger than 4 MB');
  const files = await unzip(bytes), m = files.get('manifest.json');
  if (!m) throw new Error('no manifest.json inside');
  const j = JSON.parse(new TextDecoder().decode(m));
  if (j.format !== 'rigview') throw new Error('not a RIG view bundle');
  if (j.formatVersion !== 1) throw new Error(`made for a newer RIG/01 (view format ${j.formatVersion})`);
  if (!/^[a-z0-9-]{1,40}$/.test(j.id || '')) throw new Error('its id should be 1-40 lowercase letters, digits or -');
  if (typeof j.tab !== 'string' || !j.tab.trim() || j.tab.length > 24) throw new Error('its tab label should be 1-24 characters');
  if (!files.has('view.js')) throw new Error('no view.js inside');
  const dec = (n) => (files.has(n) ? new TextDecoder('utf-8', { fatal: true }).decode(files.get(n)) : '');
  return { manifest: j, js: dec('view.js'), css: dec('view.css') };
}

// ------------------------------------------------------------------ the library decides
function syncViews() { return (views.sync = (views.sync || Promise.resolve()).then(syncViewsNow, syncViewsNow)); }
async function syncViewsNow() {
  const files = (library.entries.views || []).filter((e) => e.meta && !e.meta.error), want = new Map();
  for (const e of files) if (!want.has(e.meta.id)) want.set(e.meta.id, e);          // one id twice: the first by name
  for (const [id, v] of views.list) if (!want.has(id) || want.get(id).key !== v.key) removeView(id);
  for (const [id, e] of want) {
    if (views.list.has(id)) continue;
    try {
      const b = await readRigview(new Uint8Array(await (await library.core.file('views', e.name)).arrayBuffer()));
      addView(b, e); logLine(`view ${e.name}: ${b.manifest.tab} tab added`);
    } catch (err) { logLine(`view ${e.name}: not loaded: ${err.message}`, true); }
  }
  for (const e of library.entries.views || []) if (e.meta && e.meta.error) logLine(`view ${e.name}: not usable: ${e.meta.error}`, true);
}
function addView(b, e) {
  const m = b.manifest, id = m.id, esc = (t) => String(t).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/"/g, '&quot;');
  const taken = new Set([...views.list.values()].map((v) => v.letter).filter(Boolean)), k = String(m.key || '').toLowerCase();
  const letter = /^[a-z]$/.test(k) && !VIEW_RESERVED.includes(k) && !taken.has(k) ? k : '';
  const btn = document.createElement('button');
  btn.id = 'v-' + id; btn.dataset.view = id; btn.setAttribute('aria-pressed', 'false'); btn.textContent = m.tab;
  btn.title = `${m.name || m.tab}${letter ? ` (${letter.toUpperCase()})` : ''}${m.description ? ': ' + m.description : ''} · a view from ${e.name}`;
  btn.onclick = () => setView('view:' + id);
  const seg = $('vChart').parentNode, after = [...seg.querySelectorAll('[data-view]')].find((x) => x.textContent.localeCompare(m.tab) > 0);
  seg.insertBefore(btn, after || null);
  const el = document.createElement('section');
  el.className = 'hud chart'; el.hidden = true; el.id = 'view-' + id; el.setAttribute('aria-label', m.name || m.tab);
  el.innerHTML = `<div class="cboard"><div class="ccard"><canvas aria-hidden="true"></canvas><iframe class="vframe" title="${esc(m.name || m.tab)}" sandbox="allow-scripts" referrerpolicy="no-referrer"></iframe></div></div>`;
  $('chart').after(el);
  const v = { m, file: e.name, key: e.key, js: b.js, css: b.css, letter, el, btn, frame: el.querySelector('iframe'), fit: null, ready: false };
  views.list.set(id, v);
  window.addEventListener('message', v.listener = (ev) => onViewMsg(v, ev));
  // made when first shown, so a view in the library costs nothing until it's opened
  if (state.view === 'view:' + id) viewShow(v, true);
}
function removeView(id) {
  const v = views.list.get(id); if (!v) return;
  if (state.view === 'view:' + id) setView('stage');
  window.removeEventListener('message', v.listener); v.el.remove(); v.btn.remove(); views.list.delete(id);
}

// ------------------------------------------------------------------ the frame
// The page's card styles, copied into every frame, so a view's controls look like the rig's.
function viewBaseCss() {
  const keep = /^(\.cchip|\.crow|\.cfoot|\.btn|\.mono|select|button)\b/, out = [];
  for (const sh of document.styleSheets) {
    let rules; try { rules = sh.cssRules; } catch (e) { continue; }
    for (const r of rules) if (r.selectorText && r.selectorText.split(',').every((x) => keep.test(x.trim()))) out.push(r.cssText);
  }
  return out.join('\n');
}
// The skin's CSS variables as they are now (the page's defaults, then the skin's).
function viewVars() {
  const out = {}, cs = getComputedStyle(document.documentElement);
  for (const sh of document.styleSheets) {
    let rules; try { rules = sh.cssRules; } catch (e) { continue; }
    for (const r of rules) if (r.selectorText === ':root') for (const p of r.style) if (p.startsWith('--')) out[p] = cs.getPropertyValue(p).trim();
  }
  for (const p of document.documentElement.style) if (p.startsWith('--')) out[p] = cs.getPropertyValue(p).trim();
  return out;
}
const viewSkin = () => { const t = tileStyle(SKIN, SKIN.chart.tile); return JSON.parse(JSON.stringify({ ui: SKIN.ui, card: t.card, tile: { accent: t.accent, rim: t.rim, glow: t.glow }, screen: SKIN.screen, chart: SKIN.chart })); };
const safeTag = (s, tag) => s.replace(new RegExp(`</(${tag})`, 'gi'), '<\\/$1');
function viewSrcdoc(v) {
  return `<!doctype html><html><head><meta charset="utf-8"><meta name="color-scheme" content="dark">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; img-src data: blob:; font-src data:">
<style>:root{color-scheme:dark}html,body{margin:0;height:100%;background:transparent;overflow:hidden;color:var(--fg);font-family:var(--display)}button,select,input{font:inherit;color:inherit}
${safeTag(viewBaseCss(), 'style')}</style><style>${safeTag(v.css, 'style')}</style></head><body>
<script>${safeTag(UI_VIEWKIT.runtime, 'script')}<\/script><script>${safeTag(UI_VIEWKIT.theory, 'script')}<\/script><script>${safeTag(v.js, 'script')}<\/script></body></html>`;
}
const viewCard = (v) => ({ L: v.fit.L, H: CH_H, RX: CH_RX, RW: chRW(v.fit.L), k: v.fit.k, dpr: v.fit.dpr });
const viewPost = (v, m) => { if (v.frame.contentWindow) v.frame.contentWindow.postMessage({ rigview: 1, ...m }, '*'); };
function viewSettings(v) { try { return JSON.parse(lsGet('rig01.view.' + v.m.id, '{}')) || {}; } catch (e) { return {}; } }
function onViewMsg(v, ev) {
  if (ev.source !== v.frame.contentWindow) return;
  const d = ev.data; if (!d || d.rigview !== 1) return;
  if (d.type === 'ready') { v.ready = true; v.frame.style.visibility = ''; }
  else if (d.type === 'save') { if (typeof d.data === 'string' && d.data.length <= 65536) lsSet('rig01.view.' + v.m.id, d.data); else logLine(`view ${v.m.id}: settings over 64 KB not saved`, true); }
  else if (d.type === 'key' && typeof d.key === 'string' && d.key.length === 1) window.dispatchEvent(new KeyboardEvent('keydown', { key: d.key }));
}

// ------------------------------------------------------------------ show, hide, lay out, feeds
function viewLayout(v) {
  v.fit = cardFit(v.el.querySelector('.cboard'), v.el.querySelector('.ccard'));
  v.el.style.background = SKIN.chart.bg;
  const c = chCanvas(v.el.querySelector('canvas'), v.fit); chCardFrame(c, tileStyle(SKIN, SKIN.chart.tile), v.fit.L);
  v.frame.style.width = v.fit.L + 'px'; v.frame.style.height = CH_H + 'px';
  if (v.started) viewPost(v, { type: 'layout', card: viewCard(v) });
}
function viewShow(v, on) {
  v.el.hidden = !on; v.btn.setAttribute('aria-pressed', String(on));
  const wantsNotes = (v.m.feeds || []).includes('notes');
  if (!on) { if (v.started) viewPost(v, { type: 'hide' }); if (wantsNotes && state.view !== 'viz') rig.cmd('listen 0'); return; }
  canvas.style.visibility = 'hidden'; $('overview').disabled = true;
  viewLayout(v);
  if (!v.started) {                                  // first time: build the frame, then hand it its card, skin and settings
    v.started = true; v.frame.style.visibility = 'hidden';
    v.frame.onload = () => { viewPost(v, { type: 'init', card: viewCard(v), skin: viewSkin(), vars: viewVars(), settings: viewSettings(v) }); viewPost(v, { type: 'show' }); };
    v.frame.srcdoc = viewSrcdoc(v);
  } else viewPost(v, { type: 'show' });
  if (wantsNotes) { v.listenT = T; rig.cmd('listen 1'); }
}
function viewsShow(view) { for (const v of views.list.values()) { const on = view === 'view:' + v.m.id; if (on || !v.el.hidden) viewShow(v, on); } }
// Every frame of the main loop while a view is up: its feeds, about 30 a second.
function viewTick() {
  const v = viewOf(state.view); if (!v || !v.ready || frame % 2) return;
  const feeds = v.m.feeds || [];
  if (feeds.includes('notes')) {
    if (meters && LAYOUT && LAYOUT.notes !== undefined && meters.m[LAYOUT.notes] === 0 && T - v.listenT > 1) { v.listenT = T; rig.cmd('listen 1'); }
    const h = heardNotes(), ch = h.length ? chordHeard(h.map((x) => x.n)) : null;
    viewPost(v, { type: 'feed', name: 'notes', data: { notes: h.map((x) => ({ midi: x.n, level: x.amp })), chord: ch ? ch.name || '' : '' } });
  }
  if (feeds.includes('sources')) viewPost(v, { type: 'feed', name: 'sources', data: SRC.filter((s) => s.slot !== null).map((s) => ({ id: s.id, name: s.name, color: s.color, value: srcVal(s) })) });
  if (feeds.includes('levels') && meters) viewPost(v, { type: 'feed', name: 'levels', data: { in: meters.inPk, out: meters.outPk } });
}
function wireViews() {
  window.addEventListener('keydown', (e) => {
    if (e.target.closest && e.target.closest('input,textarea,select')) return;
    if (e.ctrlKey || e.metaKey || e.altKey) return;
    const k = e.key.toLowerCase(), v = [...views.list.values()].find((x) => x.letter && x.letter === k);
    if (v) setView(state.view === 'view:' + v.m.id ? 'stage' : 'view:' + v.m.id);
  });
  rig.skinHooks.push(() => { for (const v of views.list.values()) if (v.started) { if (!v.el.hidden) viewLayout(v); viewPost(v, { type: 'skin', skin: viewSkin(), vars: viewVars() }); } });
}
// Test helpers
rig.viewsInfo = () => [...views.list.values()].map((v) => ({ id: v.m.id, tab: v.m.tab, file: v.file, letter: v.letter, ready: v.ready, shown: !v.el.hidden, L: v.fit && v.fit.L }));

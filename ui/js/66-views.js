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
// What a view may ask to do to the rig (its manifest's "permissions"); the player allows them once per view.
const VIEW_PERMS = { params: 'turn knobs', presets: 'switch presets and scenes', tempo: 'set the tempo', chain: 'add, remove and bypass effects' };
const VIEW_FEEDS = ['notes', 'sources', 'levels', 'chain', 'presets', 'tempo'];
const VIEW_DOCK = { w: [160, 420, 280], h: [80, 360, 160] };   // a docked view's size in CSS px: least, most, default
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
  for (const f of j.feeds || []) if (!VIEW_FEEDS.includes(f)) throw new Error(`no feed called "${f}"`);
  for (const q of j.permissions || []) if (!VIEW_PERMS[q]) throw new Error(`no permission called "${q}"`);
  if (j.place !== undefined && j.place !== 'tab' && j.place !== 'dock') throw new Error('its place should be "tab" or "dock"');
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
  const dock = m.place === 'dock';
  btn.onclick = () => (dock ? viewDock(views.list.get(id), !views.list.get(id).docked) : setView('view:' + id));
  const seg = $('vChart').parentNode, after = [...seg.querySelectorAll('[data-view]')].find((x) => x.textContent.localeCompare(m.tab) > 0);
  seg.insertBefore(btn, after || null);
  const el = document.createElement('section'), frameHtml = `<iframe class="vframe" title="${esc(m.name || m.tab)}" sandbox="allow-scripts" referrerpolicy="no-referrer"></iframe>`;
  el.hidden = true; el.id = 'view-' + id; el.setAttribute('aria-label', m.name || m.tab);
  const size = (k) => { const [lo, hi, def] = VIEW_DOCK[k], n = Number(m.dock && m.dock[k]); return isFinite(n) && n > 0 ? clamp(Math.round(n), lo, hi) : def; };
  if (dock) { el.className = 'vdock'; el.style.width = size('w') + 'px'; el.style.height = size('h') + 'px'; el.innerHTML = frameHtml; $('vdocks').appendChild(el); }
  else { el.className = 'hud chart'; el.innerHTML = `<div class="cboard"><div class="ccard"><canvas aria-hidden="true"></canvas>${frameHtml}</div></div>`; $('chart').after(el); }
  const v = { m, file: e.name, key: e.key, js: b.js, css: b.css, letter, el, btn, frame: el.querySelector('iframe'), fit: null, ready: false,
    dock: dock ? { w: size('w'), h: size('h') } : null, docked: dock && lsGet('rig01.view.dock.' + id, '') === '1', sent: {}, cmdT: 0, cmdN: 0 };
  views.list.set(id, v);
  window.addEventListener('message', v.listener = (ev) => onViewMsg(v, ev));
  // made when first shown, so a view in the library costs nothing until it's opened
  if (state.view === 'view:' + id || (v.docked && state.view === 'stage')) viewShow(v, true);
}
// A docked view sits on the stage (bottom left, above the pedalboard) instead of taking it over; its button switches it on and off.
function viewDocksPlace() { $('vdocks').style.bottom = Math.round(window.innerHeight - $('board').getBoundingClientRect().top + 10) + 'px'; }
function viewDock(v, on) { v.docked = on; lsSet('rig01.view.dock.' + v.m.id, on ? '1' : ''); viewShow(v, on && state.view === 'stage'); }
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
const viewCard = (v) => (v.dock ? { L: v.dock.w, H: v.dock.h, RX: 0, RW: v.dock.w, k: 1, dpr: window.devicePixelRatio || 1, dock: true }
  : { L: v.fit.L, H: CH_H, RX: CH_RX, RW: chRW(v.fit.L), k: v.fit.k, dpr: v.fit.dpr });
const viewPost = (v, m) => { if (v.frame.contentWindow) v.frame.contentWindow.postMessage({ rigview: 1, ...m }, '*'); };
function viewSettings(v) { try { return JSON.parse(lsGet('rig01.view.' + v.m.id, '{}')) || {}; } catch (e) { return {}; } }
function onViewMsg(v, ev) {
  if (ev.source !== v.frame.contentWindow) return;
  const d = ev.data; if (!d || d.rigview !== 1) return;
  if (d.type === 'ready') { v.ready = true; v.frame.style.visibility = ''; }
  else if (d.type === 'save') { if (typeof d.data === 'string' && d.data.length <= 65536) lsSet('rig01.view.' + v.m.id, d.data); else logLine(`view ${v.m.id}: settings over 64 KB not saved`, true); }
  else if (d.type === 'key' && typeof d.key === 'string' && d.key.length === 1) window.dispatchEvent(new KeyboardEvent('keydown', { key: d.key }));
  else if (d.type === 'cmd') viewCmd(v, d);
}

// ------------------------------------------------------------------ permissions and commands
// What the player has answered for this view: { asked: [...], allow: [...] }, in this browser.
function viewAnswer(v) { try { const a = JSON.parse(lsGet('rig01.view.allow.' + v.m.id, '{}')) || {}; return { asked: a.asked || [], allow: a.allow || [] }; } catch (e) { return { asked: [], allow: [] }; } }
const viewWants = (v) => (v.m.permissions || []).filter((q) => VIEW_PERMS[q]);
const viewGranted = (v) => { const a = viewAnswer(v).allow; return viewWants(v).filter((q) => a.includes(q)); };
// Asked once, over the view, when it's first shown (and again only if a newer file asks for more).
function viewAsk(v) {
  const a = viewAnswer(v), fresh = viewWants(v).filter((q) => !a.asked.includes(q)), old = v.el.querySelector('.vask');
  if (old) old.remove();
  if (!fresh.length) return;
  const box = document.createElement('div'); box.className = 'vask'; box.setAttribute('role', 'dialog');
  const p = document.createElement('p'); p.textContent = `${v.m.name || v.m.tab} (${v.file}) asks to ${fresh.map((q) => VIEW_PERMS[q]).join(', ')}.`;
  const answer = (yes) => {
    lsSet('rig01.view.allow.' + v.m.id, JSON.stringify({ asked: [...new Set([...a.asked, ...fresh])], allow: yes ? [...new Set([...a.allow, ...fresh])] : a.allow }));
    box.remove(); logLine(`view ${v.m.id}: ${yes ? 'allowed to ' : 'not allowed to '}${fresh.map((q) => VIEW_PERMS[q]).join(', ')}`);
    viewPost(v, { type: 'permissions', list: viewGranted(v) });
  };
  const yes = document.createElement('button'), no = document.createElement('button');
  yes.className = 'btn'; yes.textContent = 'Allow'; yes.dataset.ask = 'allow'; yes.onclick = () => answer(true);
  no.className = 'btn'; no.textContent = 'Not now'; no.dataset.ask = 'deny'; no.onclick = () => answer(false);
  box.append(p, yes, no); (v.dock ? v.el : v.el.querySelector('.ccard')).appendChild(box);
}
// A command from a view: only what its granted permissions cover, only on things that exist, values
// clamped, and no more than 120 a second. Each goes through the page's own functions, as a click would.
function viewCmd(v, d) {
  const now = performance.now(); if (now - v.cmdT > 1000) { v.cmdT = now; v.cmdN = 0; }
  if (++v.cmdN > 120) return;
  const can = (q) => viewGranted(v).includes(q), a = d.args || {}, mod = MODS.find((m) => m.id === a.module);
  if (d.name === 'param' && can('params')) { const q = PARAMS[`${a.module}.${a.symbol}`]; if (q && typeof a.value === 'number' && isFinite(a.value)) setParam(q, a.value); }
  else if (d.name === 'bypass' && can('chain') && mod) { state.bypass[mod.id] = !!a.on; rig.cmd(`bypass ${mod.inst} ${a.on ? 1 : 0}`); update(); }
  else if (d.name === 'add' && can('chain') && typeof a.effect === 'string') { addModule(a.effect); update(); }
  else if (d.name === 'remove' && can('chain') && mod) { removeModule(mod); update(); }
  else if (d.name === 'preset' && can('presets')) {
    const sc = board.scenes[a.scene]; if (board.ready && sc && sc.presets[a.preset] && (a.scene !== board.s || a.preset !== board.p)) selectSlot(a.scene, a.preset);
  }
  else if (d.name === 'tempo' && can('tempo') && typeof a.bpm === 'number' && isFinite(a.bpm)) { state.bpm = clamp(Math.round(a.bpm), 30, 300); $('bpm').textContent = state.bpm; rig.cmd(`set_bpm ${state.bpm}`); }
}

// ------------------------------------------------------------------ show, hide, lay out, feeds
function viewLayout(v) {
  if (v.dock) { v.frame.style.width = v.dock.w + 'px'; v.frame.style.height = v.dock.h + 'px'; viewDocksPlace(); return; }
  v.fit = cardFit(v.el.querySelector('.cboard'), v.el.querySelector('.ccard'));
  v.el.style.background = SKIN.chart.bg;
  const c = chCanvas(v.el.querySelector('canvas'), v.fit); chCardFrame(c, tileStyle(SKIN, SKIN.chart.tile), v.fit.L);
  v.frame.style.width = v.fit.L + 'px'; v.frame.style.height = CH_H + 'px';
  if (v.started) viewPost(v, { type: 'layout', card: viewCard(v) });
}
function viewShow(v, on) {
  v.el.hidden = !on; v.btn.setAttribute('aria-pressed', String(v.dock ? v.docked : on)); v.sent = {};
  const wantsNotes = (v.m.feeds || []).includes('notes');
  if (!on) { if (v.started) viewPost(v, { type: 'hide' }); if (wantsNotes && state.view !== 'viz') rig.cmd('listen 0'); return; }
  if (!v.dock) { canvas.style.visibility = 'hidden'; $('overview').disabled = true; }
  viewLayout(v); viewAsk(v);
  if (!v.started) {                                  // first time: build the frame, then hand it its card, skin and settings
    v.started = true; v.frame.style.visibility = 'hidden';
    v.frame.onload = () => { viewPost(v, { type: 'init', card: viewCard(v), skin: viewSkin(), vars: viewVars(), settings: viewSettings(v), permissions: viewGranted(v) }); viewPost(v, { type: 'show' }); };
    v.frame.srcdoc = viewSrcdoc(v);
  } else viewPost(v, { type: 'show' });
  if (wantsNotes) { v.listenT = T; rig.cmd('listen 1'); }
}
function viewsShow(view) { for (const v of views.list.values()) { const on = v.dock ? v.docked && view === 'stage' : view === 'view:' + v.m.id; if (on || !v.el.hidden) viewShow(v, on); } }
// Every frame of the main loop while a view is up: its feeds, about 30 a second.
function viewTick() {
  if (frame % 2) return;
  for (const v of views.list.values()) if (v.ready && !v.el.hidden) viewFeeds(v);
}
// The feeds that describe the rig go out when they change (and once when the view is shown).
function viewState(v, name, data) { const j = JSON.stringify(data); if (v.sent[name] !== j) { v.sent[name] = j; viewPost(v, { type: 'feed', name, data }); } }
function viewFeeds(v) {
  const feeds = v.m.feeds || [];
  if (feeds.includes('notes')) {
    if (meters && LAYOUT && LAYOUT.notes !== undefined && meters.m[LAYOUT.notes] === 0 && T - v.listenT > 1) { v.listenT = T; rig.cmd('listen 1'); }
    const h = heardNotes(), ch = h.length ? chordHeard(h.map((x) => x.n)) : null;
    viewPost(v, { type: 'feed', name: 'notes', data: { notes: h.map((x) => ({ midi: x.n, level: x.amp })), chord: ch ? ch.name || '' : '' } });
  }
  if (feeds.includes('sources')) viewPost(v, { type: 'feed', name: 'sources', data: SRC.filter((s) => s.slot !== null).map((s) => ({ id: s.id, name: s.name, color: s.color, value: srcVal(s) })) });
  if (feeds.includes('levels') && meters) viewPost(v, { type: 'feed', name: 'levels', data: { in: meters.inPk, out: meters.outPk } });
  if (feeds.includes('chain')) viewState(v, 'chain', MODS.map((m) => ({ id: m.id, type: m.type, name: m.name, bypass: !!state.bypass[m.id],
    params: m.params.map((q) => ({ symbol: q.sym, name: q.label, value: Math.round(q.v * 1e4) / 1e4, text: String(q.fmt(q.v)) })) })));
  if (feeds.includes('presets') && board.ready) viewState(v, 'presets', { scene: board.s, preset: board.p, name: $('presetName').value,
    scenes: board.scenes.map((sc) => ({ name: sc.name, presets: sc.presets.map((q) => q.name || '') })) });
  if (feeds.includes('tempo')) viewState(v, 'tempo', { bpm: state.bpm });
}
function wireViews() {
  window.addEventListener('keydown', (e) => {
    if (e.target.closest && e.target.closest('input,textarea,select')) return;
    if (e.ctrlKey || e.metaKey || e.altKey) return;
    const k = e.key.toLowerCase(), v = [...views.list.values()].find((x) => x.letter && x.letter === k);
    if (v && v.dock) viewDock(v, !v.docked);
    else if (v) setView(state.view === 'view:' + v.m.id ? 'stage' : 'view:' + v.m.id);
  });
  window.addEventListener('resize', viewDocksPlace);
  rig.skinHooks.push(() => { for (const v of views.list.values()) if (v.started) { if (!v.el.hidden) viewLayout(v); viewPost(v, { type: 'skin', skin: viewSkin(), vars: viewVars() }); } });
}
// Test helpers
rig.viewsInfo = () => [...views.list.values()].map((v) => ({ id: v.m.id, tab: v.m.tab, file: v.file, letter: v.letter, ready: v.ready, shown: !v.el.hidden, L: v.fit && v.fit.L, dock: !!v.dock, docked: v.docked, granted: viewGranted(v), asking: !!v.el.querySelector('.vask') }));

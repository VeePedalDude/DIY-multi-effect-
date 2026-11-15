// Quick Controls - a docked view (docs/VIEWS.md): the preset before and after this one, the tempo, and a
// bypass switch per effect. It only shows what the rig's feeds say, and asks the rig to change things
// through its permissions (presets, tempo, chain).
(function () {
  'use strict';
  const el = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; if (text !== undefined) e.textContent = text; return e; };
  const row = (id) => { const r = el('div', 'qrow'); r.id = id; document.body.appendChild(r); return r; };
  const pr = row('pr'), tp = row('tp'), fx = row('fx');
  const prev = el('button', 'cchip', '‹'), name = el('span', 'qname', '–'), next = el('button', 'cchip', '›');
  prev.id = 'prev'; next.id = 'next'; name.id = 'name'; pr.append(prev, name, next);
  const slower = el('button', 'cchip', '−'), bpm = el('span', 'qname', '–'), faster = el('button', 'cchip', '+');
  slower.id = 'slower'; faster.id = 'faster'; bpm.id = 'bpm'; tp.append(slower, bpm, faster);
  let P = null, T = null;
  const step = (d) => {                               // the next preset in the scene, then on into the next scene
    if (!P) return;
    let s = P.scene, p = P.preset + d;
    if (p < 0) { s = (s - 1 + P.scenes.length) % P.scenes.length; p = P.scenes[s].presets.length - 1; }
    else if (p >= P.scenes[s].presets.length) { s = (s + 1) % P.scenes.length; p = 0; }
    rig.preset(s, p);
  };
  prev.onclick = () => step(-1); next.onclick = () => step(1);
  slower.onclick = () => T && rig.setTempo(T.bpm - 1); faster.onclick = () => T && rig.setTempo(T.bpm + 1);
  const locks = () => { prev.disabled = next.disabled = !rig.can('presets'); slower.disabled = faster.disabled = !rig.can('tempo'); fx.querySelectorAll('button').forEach((b) => { b.disabled = !rig.can('chain'); }); };
  rig.on('presets', (d) => { P = d; const sc = d.scenes[d.scene]; name.textContent = (d.name || sc.presets[d.preset] || 'unsaved') + ' · ' + sc.name; });
  rig.on('tempo', (d) => { T = d; bpm.textContent = d.bpm + ' BPM'; });
  rig.on('chain', (mods) => {
    fx.textContent = '';
    mods.forEach((m) => { const b = el('button', 'cchip', m.name); b.dataset.id = m.id; b.setAttribute('aria-pressed', String(!m.bypass)); b.title = m.bypass ? 'bypassed' : 'on'; b.onclick = () => rig.bypass(m.id, !m.bypass); fx.appendChild(b); });
    locks();
  });
  rig.on('permissions', locks);
  rig.on('init', () => { locks(); rig.ready(); });
})();

// ui/views/runtime.js - runs first in every view's sandboxed frame (66-views.js builds the frame): the `rig`
// object a view talks to the page through (docs/VIEWS.md). Messages from anything but the page are ignored.
(function () {
  'use strict';
  const handlers = {}, send = (m) => parent.postMessage(Object.assign({ rigview: 1 }, m), '*');
  const emit = (ev, d) => (handlers[ev] || []).forEach((f) => { try { f(d); } catch (e) { console.error(e); } });
  const cmd = (name, args) => send({ type: 'cmd', name, args });
  const rig = { card: {}, skin: {}, settings: {}, permissions: [],
    can(p) { return rig.permissions.indexOf(p) >= 0; },
    // each needs its permission (the manifest asks, the player allows); without it the page ignores the call
    setParam(module, symbol, value) { cmd('param', { module, symbol, value }); },             // params: value 0..1
    bypass(module, on) { cmd('bypass', { module, on: !!on }); },                              // chain
    addEffect(effect) { cmd('add', { effect }); }, removeEffect(module) { cmd('remove', { module }); },   // chain
    preset(scene, preset) { cmd('preset', { scene, preset }); },                              // presets
    setTempo(bpm) { cmd('tempo', { bpm }); },                                                 // tempo
    on(ev, fn) { (handlers[ev] = handlers[ev] || []).push(fn); },
    save(obj) { rig.settings = obj; send({ type: 'save', data: JSON.stringify(obj) }); },
    ready() { send({ type: 'ready' }); } };
  window.rig = rig;
  const vars = (v) => { for (const k in v || {}) document.documentElement.style.setProperty(k, v[k]); };
  addEventListener('message', (e) => {
    if (e.source !== parent) return;
    const m = e.data; if (!m || m.rigview !== 1) return;
    if (m.type === 'init') { Object.assign(rig.card, m.card); rig.skin = m.skin; rig.settings = m.settings || {}; rig.permissions = m.permissions || []; vars(m.vars); emit('init'); }
    else if (m.type === 'permissions') { rig.permissions = m.list || []; emit('permissions'); }
    else if (m.type === 'layout') { Object.assign(rig.card, m.card); emit('layout'); }
    else if (m.type === 'skin') { rig.skin = m.skin; vars(m.vars); emit('skin'); }
    else if (m.type === 'show' || m.type === 'hide') emit(m.type);
    else if (m.type === 'feed') emit(m.name, m.data);
  });
  // keys reach the rig (tab shortcuts, P), except while typing in the view's own fields
  addEventListener('keydown', (e) => {
    if (e.target.closest && e.target.closest('input,textarea,select')) return;
    if (e.ctrlKey || e.metaKey || e.altKey || e.key.length !== 1) return;
    send({ type: 'key', key: e.key });
  });
})();

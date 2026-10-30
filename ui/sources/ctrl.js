// ui/sources/ctrl.js - CONTROLLER's Input section: which control on which USB gamepad / joystick / flight
// stick or MIDI device this instance follows. LEARN, then move the control (an axis, the hat, a button, a
// MIDI knob or fader, pitch bend, a key). INVERT, RANGE (-1..+1 or 0..+1) and, for buttons and keys,
// MOMENTARY or TOGGLE. A bar shows the control live (from the device) and the source's value (from the
// engine). Devices and LEARN live in 60-controllers.js.
SRCVIEW.ctrl = (() => {
  const esc = (t) => String(t).replace(/&/g, '&amp;').replace(/</g, '&lt;');
  function body(s) {
    const b = state.inputs[s.id], learning = INPUTS.learning === s.id, here = inputPresent(b);
    let h = `<div class="prow"><b style="letter-spacing:.08em">INPUT</b><span class="mono" id="inVal" style="font-size:12px"></span></div>`;
    if (learning) h += `<p class="note" style="color:var(--fg)">Listening: move a stick, the throttle or a hat, press a button, or turn a MIDI knob or fader…</p>`;
    else if (b) h += `<p class="note" id="inBound"><b style="color:var(--fg)">${esc(describeBinding(b))}</b>${here ? '' : ' · <span class="warnline">waiting for it to be plugged in</span>'}</p>`;
    else h += `<p class="note">Not bound. Press LEARN, then move the control you want on a gamepad, joystick, flight stick or MIDI controller.${s.id === 'ctrl' ? ' Until then the CONTROLLER slider at the bottom moves it.' : ''}</p>`;
    h += `<div class="row"><button class="btn mini${learning ? ' on' : ''}" data-in="learn" aria-pressed="${learning}">${learning ? 'CANCEL' : 'LEARN'}</button>`;
    if (b) {
      h += `<button class="btn mini${b.invert ? ' on' : ''}" data-in="invert" aria-pressed="${b.invert}">INVERT</button>
        <button class="btn mini" data-in="range" title="What the control's travel becomes">${b.range === 'pos' ? '0 … +1' : '−1 … +1'}</button>`;
      if (isPress(b)) h += `<button class="btn mini${b.mode === 'toggle' ? ' on' : ''}" data-in="mode" aria-pressed="${b.mode === 'toggle'}">${b.mode === 'toggle' ? 'TOGGLE' : 'MOMENTARY'}</button>`;
      h += `<button class="btn mini" data-in="unbind">UNBIND</button>`;
    }
    h += `</div><div class="inbar" aria-hidden="true"><i id="inRaw"></i><b id="inOut"></b></div>`;
    const pads = INPUTS.pads.map((p) => esc(p.id.replace(/\s*\(.*?Vendor.*?\)\s*/i, ' ').trim()));
    h += `<p class="note" style="font-size:12px">Gamepads and joysticks: ${pads.length ? pads.join(', ') : 'none yet (press a button on one to wake it)'}.<br>MIDI: `;
    if (INPUTS.midi) { const m = midiNames(); h += m.length ? m.map(esc).join(', ') : 'on, no inputs connected'; }
    else h += INPUTS.midiNote ? `<span class="warnline">${esc(INPUTS.midiNote)}</span>` : `off <button class="btn mini" data-in="midi" style="margin-left:6px">ENABLE MIDI</button>`;
    return h + `</p>`;
  }
  const sig = (s) => JSON.stringify([state.inputs[s.id], INPUTS.learning === s.id, INPUTS.pads.map((p) => p.id), INPUTS.midiState, INPUTS.midiNote, INPUTS.midi ? midiNames() : null]);
  let shown = '';
  return {
    html: () => `<div class="inview" id="inView"></div>`,
    mount(s) {
      const el = document.getElementById('inView'); if (!el) return;
      el.innerHTML = body(s); shown = sig(s);
      el.addEventListener('click', async (e) => {
        const t = e.target.closest('[data-in]'); if (!t) return; const a = t.dataset.in, b = state.inputs[s.id];
        if (a === 'learn') { if (INPUTS.learning === s.id) INPUTS.learning = null; else startLearn(s.id); }
        if (a === 'unbind') bindInput(s.id, null);
        if (a === 'midi') await enableMidi(false);
        if (b && a === 'invert') bindInput(s.id, { ...b, invert: !b.invert });
        if (b && a === 'range') bindInput(s.id, { ...b, range: b.range === 'pos' ? 'full' : 'pos' });
        if (b && a === 'mode') bindInput(s.id, { ...b, mode: b.mode === 'toggle' ? 'momentary' : 'toggle' });
        el.innerHTML = body(s); shown = sig(s);
      });
    },
    frame(s) {
      const el = document.getElementById('inView'); if (!el) return;
      if (sig(s) !== shown) { el.innerHTML = body(s); shown = sig(s); }
      const b = state.inputs[s.id], raw = INPUTS.raw[s.id], out = srcVal(s);
      const r = document.getElementById('inRaw'), o = document.getElementById('inOut'), v = document.getElementById('inVal');
      const u = b && raw !== null && raw !== undefined ? (CENTRED[b.type] ? (raw + 1) / 2 : raw) : null;
      if (r) { r.style.width = u === null ? '0' : `${(u * 100).toFixed(1)}%`; }
      if (o) o.style.left = `${((out + 1) / 2 * 100).toFixed(1)}%`;
      if (v) v.textContent = `${u === null ? '' : `control ${b && CENTRED[b.type] ? (raw >= 0 ? '+' : '') + raw.toFixed(2) : (raw * 100).toFixed(0) + '%'} → `}value ${out >= 0 ? '+' : ''}${out.toFixed(2)}`;
    },
  };
})();

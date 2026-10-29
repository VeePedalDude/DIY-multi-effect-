// 60-controllers.js - outside-world inputs feeding sources: gamepad stick -> `ctrl`, BLE heart strap -> `heart`.
// Each optional browser feature fails softly (an app preview can block gamepads, Bluetooth and the mic).
let lastCtrlSent = 0, padBlocked = false;
function setCtrl(v, fromPad) {
  state.ctrl = v; $('ctrlVal').textContent = v.toFixed(2); if (fromPad) $('ctrlSlider').value = v;
  const now = performance.now(); if (SRCMAP.ctrl && now - lastCtrlSent > 25) { rig.cmd(`ext ctrl ${v.toFixed(3)}`); lastCtrlSent = now; }
}
function readPad() {
  if (padBlocked || !navigator.getGamepads) return null;
  try { const pads = navigator.getGamepads(); return pads ? Array.from(pads).find((p) => p) || null : null; }
  catch (e) { padBlocked = true; $('padNote').textContent = 'Gamepads are blocked where this page is open. Open the file directly in Chrome to use one; the CONTROLLER slider still works.'; logLine('gamepad blocked: ' + e.message, true); return null; }
}
function pollPad(frame) {                     // left stick up = +1
  const pad = readPad(); if (!pad) return;
  const v = -(pad.axes[1] || 0);
  if (Math.abs(v - state.ctrl) > 0.01) setCtrl(clamp(Math.abs(v) < 0.06 ? 0 : v, -1, 1), true);
  if (frame % 60 === 0) $('padNote').textContent = 'Gamepad: ' + pad.id.slice(0, 48) + ' · left stick → CONTROLLER';
}
function wireControllers() {
  if (!SRCMAP.heart) { $('heartBtn').hidden = true; $('heartNote').hidden = true; }
  $('heartBtn').onclick = async () => {
    if (!navigator.bluetooth) { $('heartNote').textContent = 'This browser has no Web Bluetooth. Use Chrome or Edge on Windows, Mac or Android.'; return; }
    try {
      const dev = await navigator.bluetooth.requestDevice({ filters: [{ services: ['heart_rate'] }] });
      const ch = await (await (await dev.gatt.connect()).getPrimaryService('heart_rate')).getCharacteristic('heart_rate_measurement');
      await ch.startNotifications();
      ch.addEventListener('characteristicvaluechanged', (ev) => { const v = ev.target.value, bpm = (v.getUint8(0) & 1) ? v.getUint16(1, true) : v.getUint8(1); rig.cmd(`ext heart ${bpm}`); $('heartNote').textContent = `Heart source: ${dev.name || 'strap'} · ${bpm} bpm`; SRCMAP.heart.sub = `${bpm} bpm · live`; });
      $('heartNote').textContent = `Connected to ${dev.name || 'strap'}; waiting for the first beat…`;
    } catch (err) { $('heartNote').textContent = /permissions policy/i.test(err.message) ? 'Bluetooth is blocked where this page is open. Open the file directly in Chrome.' : 'Strap not connected: ' + err.message; }
  };
}

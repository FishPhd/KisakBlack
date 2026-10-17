// Browser Standard Gamepad -> PC XInput. Leave deadzones and response curves to GPad_*.
const digitalButtons = [0x1000, 0x2000, 0x4000, 0x8000, 0x100, 0x200,
  0, 0, 0x20, 0x10, 0x40, 0x80, 1, 2, 4, 8];
const neutral = [0, 0, 0, 0, 0, 0, 0, 0];
const clamp = (value, min, max) => Number.isFinite(value) ? Math.max(min, Math.min(max, value)) : 0;

export function gamepadSample(pad, focused = true) {
  if (!pad || pad.connected === false || pad.mapping !== 'standard') return [...neutral];
  if (!focused) return [1, ...neutral.slice(1)];
  let buttons = 0;
  for (let i = 0; i < digitalButtons.length; ++i)
    if (pad.buttons[i]?.pressed) buttons |= digitalButtons[i];
  const trigger = i => Math.round(clamp(pad.buttons[i]?.value, 0, 1) * 255);
  const stick = (i, sign) => Math.round(clamp(pad.axes[i], -1, 1) * sign * 32767) || 0;
  // Browser Y is positive down; XInput Y is positive up.
  return [1, buttons, trigger(6), trigger(7), stick(0, 1), stick(1, -1), stick(2, 1), stick(3, -1)];
}

export function gamepadPoller(send, read = () => navigator.getGamepads?.() || []) {
  let index = null;
  return (focused = true) => {
    let pads;
    try { pads = Array.from(read()); } catch { pads = []; }
    const supported = pad => pad && pad.connected !== false && pad.mapping === 'standard';
    const previous = pads.find(pad => supported(pad) && pad.index === index);
    if (index !== null && !previous) {
      // Release the old controller before assigning a replacement to the single PC slot.
      index = null;
      send(...neutral);
      return;
    }
    const pad = previous || pads.find(supported);
    index = pad?.index ?? null;
    send(...gamepadSample(pad, focused));
  };
}

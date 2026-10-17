// DOM MouseEvent.buttons bits match the engine mask from Win32 IN_MouseEvent
// (bit 0 left → key 200, bit 1 right → 201, bit 2 middle → 202). That is not the
// DOM button index (0 left, 1 middle, 2 right).

const DOM_BUTTON_BIT = [1, 4, 2, 8, 16];

export function mouseButtonChanges(previous, buttons) {
  const next = (Number(buttons) >>> 0) & 31;
  const prev = (Number(previous) >>> 0) & 31;
  const diff = prev ^ next;
  const changes = [];
  for (let i = 0; i < 5; i++) {
    const bit = 1 << i;
    if ((diff & bit) && (next & bit) === 0) changes.push({ key: 200 + i, down: false });
  }
  for (let i = 0; i < 5; i++) {
    const bit = 1 << i;
    if ((diff & bit) && (next & bit) !== 0) changes.push({ key: 200 + i, down: true });
  }
  return { mask: next, changes };
}

// Chorded pointerdown/mousedown often arrives with a stale `buttons` mask (still
// the previously held buttons, or 0). The button index on a down event is the
// button that went down; keep bits already held so that press cannot release aim.
// A locked mousemove that reports buttons 0 is the raw-input lie and must not
// drop a button that is still held. Real releases arrive as mouseup/pointerup.
export function nextMouseMask(previous, event) {
  const prev = (Number(previous) >>> 0) & 31;
  let reported = Number(event.buttons);
  if (!Number.isFinite(reported)) reported = prev;
  reported &= 31;
  const type = event.type || "";
  if (type === "mousedown" || type === "pointerdown") {
    const bit = DOM_BUTTON_BIT[event.button] || 0;
    return (prev | reported | bit) & 31;
  }
  if ((type === "mousemove" || type === "pointermove") && event.locked && reported === 0 && prev !== 0)
    return prev;
  return reported;
}

// Chorded mouse buttons. Chrome often reports the wrong MouseEvent.button when one
// button is released while another is held, and a right-click can swallow mouseup.
// CL_KeyEvent then treats the next mousedown as a repeat and drops it, so left click dies.
import assert from "node:assert/strict";
import { mouseButtonChanges, nextMouseMask } from "../../web/mouse-buttons.js";
// Release aim while fire stays down. buttons=1 is left held; the mouseup's button
// index is often the other button and must not be what we release.
{
  let mask = nextMouseMask(0, { type: "pointerdown", button: 2, buttons: 2, locked: false });
  assert.equal(mask, 2);
  mask = nextMouseMask(mask, { type: "mousedown", button: 0, buttons: 2, locked: false });
  assert.equal(mask, 3);
  const step = mouseButtonChanges(2, mask);
  assert.deepEqual(step.changes, [{ key: 200, down: true }]);
  const released = nextMouseMask(3, { type: "mouseup", button: 0, buttons: 1, locked: false });
  assert.equal(released, 1);
  assert.deepEqual(mouseButtonChanges(3, released).changes, [{ key: 201, down: false }]);
}

// A locked move that reports no buttons must not drop aim. An unlocked move may.
{
  assert.equal(nextMouseMask(2, { type: "mousemove", button: 0, buttons: 0, locked: true }), 2);
  assert.equal(nextMouseMask(2, { type: "mousemove", button: 0, buttons: 0, locked: false }), 0);
}

// A down whose buttons field is 0 still adds that button without clearing aim.
{
  assert.equal(nextMouseMask(2, { type: "pointerdown", button: 0, buttons: 0, locked: true }), 3);
}

function apply(mask, buttons) {
  return mouseButtonChanges(mask, buttons);
}

function keysDown(mask) {
  const held = [];
  for (let i = 0; i < 5; i++) if (mask & (1 << i)) held.push(200 + i);
  return held;
}

// Engine behavior that makes a stuck button fatal: a second down is not a new edge.
function Engine() {
  const keys = new Map();
  return {
    edge(key, down) {
      const s = keys.get(key) || { down: 0, repeats: 0, edges: 0 };
      if (down) {
        s.down = 1;
        s.repeats += 1;
        if (s.repeats <= 1) s.edges += 1;
      } else {
        s.down = 0;
        s.repeats = 0;
      }
      keys.set(key, s);
      return s.edges;
    },
    edges(key) {
      return keys.get(key)?.edges || 0;
    },
  };
}

function feed(engine, events) {
  let mask = 0;
  for (const buttons of events) {
    const step = apply(mask, buttons);
    for (const change of step.changes) engine.edge(change.key, change.down);
    mask = step.mask;
  }
  return mask;
}

// DOM buttons: 1 left, 2 right, 3 both. Right is key 201, not the button-index map.
{
  const step = apply(0, 2);
  assert.deepEqual(step.changes, [{ key: 201, down: true }]);
  assert.equal(step.mask, 2);
  const middle = apply(0, 4);
  assert.deepEqual(middle.changes, [{ key: 202, down: true }]);
}

// Hold ADS, then fire, then release fire, then release ADS.
{
  let mask = 0;
  let step = apply(mask, 2);
  mask = step.mask;
  step = apply(mask, 3);
  assert.deepEqual(step.changes, [{ key: 200, down: true }]);
  mask = step.mask;
  step = apply(mask, 2);
  assert.deepEqual(step.changes, [{ key: 200, down: false }]);
  assert.deepEqual(keysDown(step.mask), [201]);
  mask = step.mask;
  step = apply(mask, 0);
  assert.deepEqual(step.changes, [{ key: 201, down: false }]);
  assert.equal(step.mask, 0);
}

// Release RMB while LMB is held. The old code used e.button, which Chrome reports as 0
// (left) for this mouseup, so it released attack and left ADS latched.
{
  let mask = apply(0, 3).mask;
  const step = apply(mask, 1);
  assert.deepEqual(step.changes, [{ key: 201, down: false }]);
  assert.deepEqual(keysDown(step.mask), [200]);
}

// mouseup swallowed (context menu / chord). The next event's buttons mask releases both,
// so the following left press is a new edge instead of a dropped repeat.
{
  const engine = Engine();
  const mask = feed(engine, [2, 3, 0, 1, 0]);
  assert.equal(mask, 0);
  assert.equal(engine.edges(200), 2);
  assert.equal(engine.edges(201), 1);
}

// Same physical sequence if the page forgets the button and ignores mouseup: one edge, then dead.
{
  const engine = Engine();
  engine.edge(200, true);
  engine.edge(200, true);
  assert.equal(engine.edges(200), 1);
  engine.edge(200, false);
  engine.edge(200, true);
  assert.equal(engine.edges(200), 2);
}

// Lost left mousedown while ADS is held: mousemove buttons includes both.
{
  const step = apply(2, 3);
  assert.deepEqual(step.changes, [{ key: 200, down: true }]);
  assert.deepEqual(keysDown(step.mask), [200, 201]);
}

console.log("mouse button mask ok");

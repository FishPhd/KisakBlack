import assert from 'node:assert/strict';
import {gamepadSample, gamepadPoller} from '../../web/gamepad.js';

const pad = (index = 0) => ({index, mapping: 'standard', connected: true, axes: [0, 0, 0, 0],
  buttons: Array.from({length: 17}, () => ({pressed: false, value: 0}))});
const neutral = [0, 0, 0, 0, 0, 0, 0, 0];
const p = pad();

// Every Standard Gamepad button, including the intentionally unused guide button.
const expected = [0x1000, 0x2000, 0x4000, 0x8000, 0x100, 0x200, 0, 0, 0x20, 0x10,
  0x40, 0x80, 1, 2, 4, 8, 0];
for (let i = 0; i < expected.length; ++i) {
  p.buttons[i].pressed = true;
  assert.equal(gamepadSample(p)[1], expected[i], `button ${i}`);
  p.buttons[i].pressed = false;
}

// Independent analog triggers, correct Y polarity, range limiting, and no second deadzone.
p.buttons[6] = {pressed: true, value: 0.5};
p.buttons[7] = {pressed: true, value: 1};
p.axes = [-1, -1, 0.01, 1];
assert.deepEqual(gamepadSample(p), [1, 0, 128, 255, -32767, 32767, 328, -32767]);
p.axes = [-2, NaN, Infinity, 2];
assert.deepEqual(gamepadSample(p).slice(4), [-32767, 0, 0, -32767]);
assert.deepEqual(gamepadSample(p, false), [1, 0, 0, 0, 0, 0, 0, 0]);
assert.deepEqual(gamepadSample({...p, mapping: ''}), neutral);
assert.deepEqual(gamepadSample({...p, connected: false}), neutral);

// Sparse browser slots, stable assignment, disconnect releases, and reassignment.
let devices = [null, pad(1)];
const samples = [];
const poll = gamepadPoller((...sample) => samples.push(sample), () => devices);
poll();
devices[1].buttons[0] = {pressed: true, value: 1};
poll();
assert.equal(samples.at(-1)[1], 0x1000);
devices[0] = pad(0);
poll();
assert.equal(samples.at(-1)[1], 0x1000, 'do not switch away from an existing controller');
devices[1] = null;
poll();
assert.deepEqual(samples.at(-1), neutral, 'release before switching controllers');
poll();
assert.deepEqual(samples.at(-1), [1, 0, 0, 0, 0, 0, 0, 0]);
devices = [];
poll();
assert.deepEqual(samples.at(-1), neutral);
const denied = gamepadPoller((...sample) => assert.deepEqual(sample, neutral), () => {throw new Error('denied');});
denied();
console.log('Standard gamepad mapping, analog ranges, focus, disconnect and slot assignment passed.');

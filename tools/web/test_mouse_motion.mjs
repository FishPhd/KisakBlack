import assert from 'node:assert/strict';
import {MouseMotion} from '../../web/mouse-motion.js';
const motion = new MouseMotion();
const event = (movementX, movementY, clientX = 100, clientY = 100) => ({movementX, movementY, clientX, clientY});
assert.deepEqual(motion.sample(event(1920, -1080), true, true), [0, 0]);
assert.deepEqual(motion.sample(event(24, -12), true, true), [24, -12]);
assert.deepEqual(motion.sample(event(0.4, -0.4), true, true), [0, -0]);
assert.deepEqual(motion.sample(event(0.4, -0.4), true, true), [0, -0]);
assert.deepEqual(motion.sample(event(0.4, -0.4), true, true), [1, -1]);
// Fast legitimate sweeps keep their full sensitivity.
assert.deepEqual(motion.sample(event(2000, -1500), true, true), [2000, -1500]);
assert.deepEqual(motion.sample(event(Infinity, 1), true, true), [0, 0]);
// Unlock/lock and refocus discard the cursor warp, not the next real delta.
assert.deepEqual(motion.sample(event(-1800, 900, 400, 400), false, true), [0, 0]);
assert.deepEqual(motion.sample(event(9000, -9000, 402, 397), false, true), [2, -3]);
motion.reset();
assert.deepEqual(motion.sample(event(-1800, 900), true, true), [0, 0]);
assert.deepEqual(motion.sample(event(-5, 7), true, true), [-5, 7]);
console.log('PASS pointer-lock transitions, unlocked motion, subpixels and fast sweeps');

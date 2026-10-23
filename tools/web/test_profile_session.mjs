import assert from 'node:assert/strict';
import {isolateProfile} from '../../web/profile-session.js';

// Model a browser-origin lock manager. A held engine profile cannot be acquired
// by another tab, but becomes available again after that engine closes.
let held = false;
const locks = {request: async (name, options, callback) => {
  assert.equal(name, 'kisak-primary-profile');
  assert.equal(options.ifAvailable, true);
  if (held) return callback(null);
  held = true;
  try { await callback({name}); } finally { held = false; }
}};
let release;
const hold = new Promise(resolve => {release = resolve;});
assert.equal(await isolateProfile({guest:false,locks,hold}), false);
assert.equal(await isolateProfile({guest:false,locks,hold}), true);
assert.equal(await isolateProfile({guest:true,locks,hold}), true);
release();
await new Promise(resolve => setTimeout(resolve, 0));
assert.equal(await isolateProfile({guest:false,locks,hold:Promise.resolve()}), false);
assert.equal(await isolateProfile({guest:false,locks:null}), false);
console.log('PASS: ordinary concurrent tabs isolate writable profiles; invites remain isolated; closing releases the primary profile.');

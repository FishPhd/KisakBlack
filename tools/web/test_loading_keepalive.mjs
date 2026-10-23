import assert from 'node:assert/strict';
import {LoadingKeepalive} from '../../web/loading-keepalive.js';
const k = new LoadingKeepalive();
const state = {guest:true, joined:true, connection:6, now:0};
assert.deepEqual([...k.packet(state)], [127,0,0,2,12,2,255,255,255,255,88,88,0]);
assert.equal(k.packet({...state,now:999}), null);
assert(k.packet({...state,now:1000}));
// An 80-second engine stall must still have loading traffic for the host.
assert(k.packet({...state,now:81000}));
assert(k.packet({...state,connection:8,now:299000}));
assert.equal(k.packet({...state,now:300000}), null);
for (const connection of [undefined,0,4,5,9,10])
  assert.equal(k.packet({...state,connection,now:400000}),null);
assert.equal(k.packet({...state,guest:false}),null);
assert.equal(k.packet({...state,joined:false}),null);
assert(k.packet({...state,now:500000})); // A new connection gets a new loading window.
assert.equal(k.packet({...state,connection:10,now:501000}),null);
console.log('PASS: loading keepalives use the engine packet, survive an 80-second load, stop at gameplay, and expire after five minutes.');

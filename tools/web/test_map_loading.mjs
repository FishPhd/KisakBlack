import assert from 'node:assert/strict';
import {MapLoading} from '../../web/map-loading.js';

let view;
const loading = new MapLoading(v => { view = v; });
const a = {path:'zone/Map.ff', size:100}, b = {path:'main/map.iwd', size:300};
loading.transfer(a, 50); // Already-prefetching file is counted on Start Game.
loading.start('mp_test', [a, b, a]);
assert.equal(view.percent, 12); // Deduplicated and weighted by bytes.
assert.equal(view.done, 50);
assert.equal(view.total, 400);
assert.match(view.text, /MB/);
assert.match(view.text, /Downloading map/);
loading.transfer(b, 150);
assert.equal(view.percent, 50);
loading.transfer({path:'unrelated',size:999}, 999);
assert.equal(view.percent, 50);
loading.start('mp_test', [a,b]); // Server notification must not reset progress.
assert.equal(view.percent, 50);
loading.observe({frontendReady:true,connection:0}, 10);
loading.presented(11);
assert.ok(view); // Old lobby snapshots do not dismiss the loading screen.
loading.transfer(a, 100);
loading.transfer(b, 300);
assert.equal(view.percent, 100);
assert.equal(view.text, 'Loading map…');
loading.observe({connection:10,map:'mp_test'}, 20);
loading.presented(20);
assert.ok(view); // Wait for a frame after the active snapshot.
loading.presented(21);
assert.equal(view, null);
loading.start('mp_test', [a,b]);
assert.equal(view.text, 'Loading map…'); // Cached map.
loading.fail(b, new Error('HTTP 503'));
assert.match(view.text, /503/);
loading.clear();
loading.start('mp_other', []);
assert.equal(view.percent, null);
loading.observe({connection:8}, 30);
loading.observe({connection:0,frontendReady:true}, 31);
loading.presented(32);
assert.equal(view, null); // Aborted load returning to frontend.
console.log('Map loading progress and presentation lifecycle passed.');

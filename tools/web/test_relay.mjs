import assert from 'node:assert/strict';
const url = process.argv[2] || 'ws://127.0.0.1:8080/relay';
const discoveryUrl = new URL(url);
discoveryUrl.protocol = discoveryUrl.protocol === 'wss:' ? 'https:' : 'http:';
discoveryUrl.pathname = '/lobby/servers';
const list = async () => (await fetch(discoveryUrl, {cache:'no-store'})).json();
const open = () => new Promise(resolve => {
  const w = new WebSocket(url);
  w.binaryType = 'arraybuffer';
  const messages = [];
  w.onmessage = e => messages.push(e.data);
  w.onopen = () => resolve({ w, messages });
});
const wait = async f => {
  const end = Date.now() + 3000;
  while (!(await f())) {
    if (Date.now() > end) throw Error('timeout');
    await new Promise(r => setTimeout(r, 10));
  }
};
const texts = messages => messages.filter(m => typeof m === 'string');
const lastPhase = messages => texts(messages).filter(m => m.startsWith('PHASE ')).at(-1);

const h = await open();
h.w.send('HOST');
await wait(() => h.messages.some(m => typeof m === 'string' && m.startsWith('ROOM ')));
assert(h.messages.includes('PROTOCOL 3'));
assert.equal(lastPhase(h.messages), 'PHASE lobby');
const room = h.messages.find(m => m.startsWith('ROOM ')).slice(5);
h.w.send('NAME Host');
h.w.send('RANK 49 15');
h.w.send('MAP mp_nuked');
h.w.send('GAMETYPE tdm');
await wait(() => h.messages.includes('ROSTER 2 Host') && h.messages.includes('MAP mp_nuked') && h.messages.includes('GAMETYPE tdm'));
await wait(() => h.messages.includes('RANK 2 49 15'));
assert(!(await list()).some(r => r.room === room), 'unlisted host does not appear');
h.w.send('LISTED 1');
h.w.send('CAPACITY 18');
await wait(async () => (await list()).some(r => r.room === room));
let browserRow = (await list()).find(r => r.room === room);
assert.equal(browserRow.phase, 'lobby');
assert.equal(browserRow.clients, 1);
assert.equal(browserRow.name, 'Host - Lobby');

const a = await open();
a.w.send('JOIN ' + room);
await wait(() => a.messages.includes('PEER 2') && a.messages.includes('PHASE lobby') && a.messages.includes('MAP mp_nuked') && a.messages.includes('GAMETYPE tdm') && a.messages.includes('ROSTER 2 Host'));
assert(a.messages.includes('RANK 2 49 15'), 'new guest receives host rank and prestige');
const peerAt = a.messages.indexOf('PEER 2');
const phaseAt = a.messages.indexOf('PHASE lobby');
assert(phaseAt >= 0 && phaseAt < peerAt, 'lobby phase arrives before the host peer');
a.w.send('NAME Guest One');
a.w.send('RANK 12 2');
await wait(() => h.messages.includes('ROSTER 3 Guest One') && a.messages.includes('ROSTER 3 Guest One'));
await wait(() => h.messages.includes('RANK 3 12 2'));
a.w.send('RANK 50 0');
a.w.send('RANK 2 49 15');
await new Promise(r => setTimeout(r, 50));
assert(!h.messages.includes('RANK 3 50 0'), 'out-of-range rank is rejected');
assert.equal(texts(h.messages).filter(m => m.startsWith('RANK 2 ')).at(-1), 'RANK 2 49 15', 'guest cannot set another player rank');

const b = await open();
b.w.send('JOIN ' + room);
await wait(() => h.messages.includes('PEER 4') && b.messages.includes('PHASE lobby') && b.messages.includes('ROSTER 3 Guest One'));
b.w.send('PHASE match');
b.w.send('MAP mp_array');
b.w.send('LISTED 0');
b.w.send('CLIENTS 18');
b.w.send('CAPACITY 1');
await new Promise(r => setTimeout(r, 50));
assert.equal(lastPhase(h.messages), 'PHASE lobby');
assert(!h.messages.includes('MAP mp_array'));
browserRow = (await list()).find(r => r.room === room);
assert.equal(browserRow.clients, 3, 'lobby occupancy follows room members');
assert.equal(browserRow.maxClients, 18, 'guest cannot change listing or capacity');

h.w.send('PHASE loading');
await wait(() => lastPhase(a.messages) === 'PHASE loading' && lastPhase(b.messages) === 'PHASE loading');
await wait(async () => (await list()).find(r => r.room === room)?.phase === 'loading');
// A guest arriving while the host loads must receive the loading state and map.
const loadingGuest = await open();
loadingGuest.w.send('JOIN ' + room);
await wait(() => loadingGuest.messages.includes('PHASE loading') && loadingGuest.messages.includes('MAP mp_nuked'));
loadingGuest.w.close();
h.w.send('PHASE match');
h.w.send('CLIENTS 7');
await wait(() => lastPhase(a.messages) === 'PHASE match' && lastPhase(b.messages) === 'PHASE match');
await wait(async () => (await list()).find(r => r.room === room)?.clients === 7);
assert.equal((await list()).find(r => r.room === room).phase, 'match');
h.w.send(new Uint8Array([127, 0, 0, 4, 12, 2, 42]));
await wait(() => b.messages.some(m => m instanceof ArrayBuffer));
assert.deepEqual([...new Uint8Array(b.messages.find(m => m instanceof ArrayBuffer))], [127, 0, 0, 2, 12, 2, 42]);
assert(!a.messages.some(m => m instanceof ArrayBuffer));
a.w.send(new Uint8Array([127, 0, 0, 2, 12, 2, 43]));
await wait(() => h.messages.some(m => m instanceof ArrayBuffer));
assert.deepEqual([...new Uint8Array(h.messages.find(m => m instanceof ArrayBuffer))], [127, 0, 0, 3, 12, 2, 43]);
a.w.send('SIGNAL ' + JSON.stringify({ to: 2, data: { candidate: 'test' } }));
await wait(() => h.messages.some(m => typeof m === 'string' && m.startsWith('SIGNAL ')));
assert.equal(JSON.parse(h.messages.find(m => typeof m === 'string' && m.startsWith('SIGNAL ')).slice(7)).from, 3);

h.w.send('PHASE lobby');
h.w.send('MAP mp_cairo');
h.w.send('GAMETYPE dm');
await wait(() => lastPhase(a.messages) === 'PHASE lobby' && a.messages.includes('MAP mp_cairo') && a.messages.includes('GAMETYPE dm'));
await wait(() => lastPhase(b.messages) === 'PHASE lobby' && b.messages.includes('GAMETYPE dm'));

a.w.close();
await wait(() => h.messages.includes('LEAVE 3'));
await wait(() => b.messages.includes('LEAVE 3'));
h.w.send('LISTED 0');
await wait(async () => !(await list()).some(r => r.room === room));
h.w.send('LISTED 1');
await wait(async () => (await list()).some(r => r.room === room));
b.w.close();
h.w.close();
await wait(async () => !(await list()).some(r => r.room === room));
console.log('PASS relay: lobby/match discovery, occupancy, listing authority, ranks, loading, routing, departures');

import assert from 'node:assert/strict';
import {evictionOrder, evictRecipeSets, TOTAL_LIMIT, MAP_LIMIT, startGpuRecipeCache, demonstrablyBadRecipe, parseRecipeIndex} from '../../web/gpu-cache.js';
import {assetIdentity, sha256} from '../../web/gpu-preparation.js';
const mb = 1024 * 1024;
for (const reason of ['Device lost during validation','Out of memory','Validation failed: insufficient memory','allocation failed','pipeline compilation failed','callback cancelled','internal compiler validation error'])
  assert.equal(demonstrablyBadRecipe(reason),false,reason);
for (const reason of ['native recipe validation failed','bad declaration','Validation error: vertex offset exceeds stride','error while parsing shader'])
  assert.equal(demonstrablyBadRecipe(reason),true,reason);
const entries = [{name:'old.bin',size:16*mb,at:1},{name:'new.bin',size:16*mb,at:3},{name:'middle.bin',size:16*mb,at:2},{name:'last.bin',size:16*mb,at:4}];
assert.deepEqual(evictionOrder(entries,'incoming.bin',16*mb),['old.bin']);
assert.deepEqual(evictionOrder(entries,'old.bin',16*mb),[]);
assert.deepEqual(evictionOrder([], 'incoming.bin', TOTAL_LIMIT),[]);
function memoryDirectory(hooks = {}) {
  const files = new Map();
  const children = new Map();
  const dir = {
    files,
    async getDirectoryHandle(name, options) {
      if (!children.has(name)) {
        if (!options?.create) throw Object.assign(new Error('missing'), {name:'NotFoundError'});
        children.set(name, memoryDirectory(hooks));
      }
      return children.get(name);
    },
    async getFileHandle(name, options) {
      if (!files.has(name)) {
        if (!options?.create) throw Object.assign(new Error('missing'), {name:'NotFoundError'});
        files.set(name, {bytes:new Uint8Array(), at:Date.now()});
      }
      return {
        async getFile() {
          if (hooks.beforeRead) await hooks.beforeRead(name);
          const f = files.get(name);
          return {size:f.size ?? f.bytes.length, lastModified:f.at,
            async arrayBuffer() { return f.bytes.slice().buffer; },
            async text() { return new TextDecoder().decode(f.bytes); }};
        },
        async createWritable() {
          if (hooks.beforeWrite) await hooks.beforeWrite(name);
          let bytes;
          return {async write(data) { bytes = typeof data === 'string' ? new TextEncoder().encode(data) : new Uint8Array(data); },
            async close() { files.set(name, {bytes, at:Date.now()}); }};
        }
      };
    },
    async *entries() { for (const name of files.keys()) yield [name, await this.getFileHandle(name)]; },
    async removeEntry(name) { if(hooks.beforeRemove)await hooks.beforeRemove(name);files.delete(name); children.delete(name); }
  };
  return dir;
}
async function waitFinal(channel, id) {
  const deadline = Date.now() + 3000;
  while (Date.now() < deadline) {
    const result = channel.sent.find(r => r.token === id && r.final);
    if (result) return result;
    await new Promise(r => setTimeout(r, 2));
  }
  throw new Error('cache channel timeout');
}
const OriginalChannel = globalThis.BroadcastChannel;
globalThis.BroadcastChannel = class { sent = []; postMessage(data) { this.sent.push(data); } };
try {
  const root = memoryDirectory();
  const channel = startGpuRecipeCache('test', root, {version:1});
  let token = 0;
  async function load(key) {
    const id = ++token;
    channel.onmessage({data:{kind:'load', key, token:id}});
    const deadline = Date.now() + 3000;
    while (Date.now() < deadline) {
      const result = channel.sent.find(r => r.token === id);
      if (result) return result.bytes;
      await new Promise(r => setTimeout(r, 2));
    }
    throw new Error('cache channel timeout');
  }
  const recipes = () => root.getDirectoryHandle('gpu-recipes-v2');
  assert.equal((await load('missing')).length, 0);
  channel.onmessage({data:{kind:'save', key:'map/a', bytes:new Uint8Array([1,2,3])}});
  assert.deepEqual([...await load('map/a')], [1,2,3]);
  assert.equal((await load('map/b')).length, 0);
  channel.onmessage({data:{kind:'save', key:'chunked', chunks:[new Uint8Array([4,5]), new Uint8Array([6,7])]}});
  const chunkToken = ++token;
  channel.onmessage({data:{kind:'load', key:'chunked', token:chunkToken}});
  while (!channel.sent.some(r => r.token === chunkToken && r.final)) await new Promise(r => setTimeout(r, 2));
  const streamed = channel.sent.filter(r => r.token === chunkToken);
  assert.deepEqual(streamed.map(r => [...r.bytes]), [[4,5], [6,7], []]);
  assert.equal(streamed.at(-1).ok, true);
  const recipeFiles = (await recipes()).files;
  const part = [...recipeFiles.keys()].find(n => /\.[a-f0-9]+\.1\.bin$/.test(n));
  recipeFiles.delete(part);
  const brokenToken = ++token;
  channel.onmessage({data:{kind:'load', key:'chunked', token:brokenToken}});
  while (!channel.sent.some(r => r.token === brokenToken && r.final)) await new Promise(r => setTimeout(r, 2));
  const broken = channel.sent.filter(r => r.token === brokenToken);
  assert.deepEqual(broken.filter(r => !r.final).map(r => [...r.bytes]), []);
  assert.equal(broken.at(-1).ok, false);
  // The renderer's background writer sends one acknowledged chunk at a time.
  // A failed replacement must leave the previous published set readable.
  let failPart = false;
  const streamRoot = memoryDirectory({beforeWrite(name) { if (failPart && /\.1\.bin$/.test(name)) throw new Error('disk failure'); }});
  const acknowledgements = [], captures = [];
  const streamChannel = startGpuRecipeCache('stream-test', streamRoot, {version:1}, {
    onAck:(ticket,ok)=>acknowledgements.push({ticket,ok}),
    onSave:(key,chunks,metadata)=>captures.push({key,chunks,metadata}),
  });
  let ticket = 0;
  async function savePart(stream, part, bytes, final) {
    const id = ++ticket;
    streamChannel.onmessage({data:{kind:'saveChunk',key:'stream/map',stream,part,bytes:new Uint8Array(bytes),final,ticket:id,
      observed:part===0?['123','18446744073709551615']:undefined}});
    const deadline = Date.now()+3000;
    while(Date.now()<deadline) {
      const ack = acknowledgements.find(a=>a.ticket===id);
      if(ack) return ack.ok;
      await new Promise(r=>setTimeout(r,2));
    }
    throw new Error('stream acknowledgement timeout');
  }
  assert.equal(await savePart(1,0,[10,11],false),true);
  const streamFiles = (await streamRoot.getDirectoryHandle('gpu-recipes-v2')).files;
  assert(![...streamFiles.keys()].some(n=>n.endsWith('.index.json')));
  assert.equal(await savePart(1,1,[12,13],true),true);
  assert.equal(captures.length,1);
  assert.deepEqual(captures[0].chunks.map(b=>[...b]),[[10,11],[12,13]]);
  assert.deepEqual(captures[0].metadata.observed,['123','18446744073709551615']);
  const learned=await streamChannel.readLearning('stream/map');
  assert.equal(learned.complete,true);
  assert.deepEqual(learned.observed,captures[0].metadata.observed);
  assert.deepEqual(learned.chunks.map(b=>[...b]),[[10,11],[12,13]]);
  assert.equal(await savePart(2,0,[20,21],false),true);
  failPart = true;
  assert.equal(await savePart(2,1,[22,23],true),false);
  streamChannel.onmessage({data:{kind:'load',key:'stream/map',token:999}});
  assert.equal((await waitFinal(streamChannel,999)).ok,true);
  assert.deepEqual(streamChannel.sent.filter(r=>r.token===999 && !r.final).map(r=>[...r.bytes]),[[10,11],[12,13]]);
  const sameAssets = startGpuRecipeCache('same-assets', streamRoot, {version:1,installation:{sha256:'new-inventory'},gpuPreparation:{version:2}});
  sameAssets.onmessage({data:{kind:'load',key:'stream/map',token:998}});
  await waitFinal(sameAssets,998);
  assert.deepEqual(sameAssets.sent.filter(r=>r.token===998 && !r.final).map(r=>[...r.bytes]),[[10,11],[12,13]]);
  assert.equal(await savePart(3,1,[1],true),false);
  assert.equal(captures.length,1);
  const stale = startGpuRecipeCache('test', root, {version:2});
  stale.onmessage({data:{kind:'load', key:'map/a', token:100}});
  while (!stale.sent.length) await new Promise(r => setTimeout(r, 2));
  assert.equal(stale.sent[0].bytes.length, 0);
  const stored = (await recipes()).files;
  const name = [...stored.keys()].find(n => /^[a-f0-9]{64}\.bin$/.test(n));
  stored.get(name).size = MAP_LIMIT + 1;
  assert.equal((await load('map/a')).length, 0);
  channel.onmessage({data:{kind:'save', key:'map/a', bytes:new Uint8Array(MAP_LIMIT + 1)}});
  assert.equal((await load('map/a')).length, 0);
  const unavailable = startGpuRecipeCache('test', {async getDirectoryHandle() { throw new Error('unavailable'); }}, {});
  unavailable.onmessage({data:{kind:'load', key:'map', token:200}});
  while (!unavailable.sent.length) await new Promise(r => setTimeout(r, 2));
  assert.equal(unavailable.sent[0].bytes.length, 0);

  const partialRoot = memoryDirectory({async beforeWrite(fileName) { if (/\.1\.bin$/.test(fileName)) throw new Error('disk'); }});
  const partial = startGpuRecipeCache('partial', partialRoot, {version:1});
  partial.onmessage({data:{kind:'save', key:'chunked', chunks:[new Uint8Array([4,5]), new Uint8Array([6,7])]}});
  partial.onmessage({data:{kind:'load', key:'chunked', token:1}});
  const partialFinal = await waitFinal(partial, 1);
  assert.equal(partialFinal.ok, true);
  assert.deepEqual(partial.sent.filter(r => r.token === 1 && !r.final).map(r => [...r.bytes]), []);
  assert.equal([...(await partialRoot.getDirectoryHandle('gpu-recipes-v2')).files.keys()].filter(n=>n.endsWith('.bin')).length,0);

  const eviction = memoryDirectory(), firstHash='a'.repeat(64), secondHash='b'.repeat(64);
  for (const [hash,at] of [[firstHash,1],[secondHash,2]]) {
    for (let i=0;i<2;i++) eviction.files.set(`${hash}.generation.${i}.bin`,{bytes:new Uint8Array(),size:16*mb,at});
    eviction.files.set(hash+'.index.json',{bytes:new Uint8Array(),at});
  }
  await evictRecipeSets(eviction,'c'.repeat(64),16*mb);
  assert(![...eviction.files.keys()].some(name=>name.startsWith(firstHash)));
  assert.equal([...eviction.files.keys()].filter(name=>name.startsWith(secondHash)).length,3);

  let binOnes = 0;
  const protectedRoot = memoryDirectory({async beforeWrite(fileName) {
    if (/\.1\.bin$/.test(fileName) && ++binOnes > 1) throw new Error('disk');
  }});
  const kept = startGpuRecipeCache('kept', protectedRoot, {version:1});
  kept.onmessage({data:{kind:'save', key:'chunked', chunks:[new Uint8Array([4,5]), new Uint8Array([6,7])]}});
  kept.onmessage({data:{kind:'save', key:'chunked', chunks:[new Uint8Array([8,9]), new Uint8Array([10,11])]}});
  kept.onmessage({data:{kind:'load', key:'chunked', token:1}});
  const keptFinal = await waitFinal(kept, 1);
  assert.equal(keptFinal.ok, true);
  assert.deepEqual(kept.sent.filter(r => r.token === 1 && !r.final).map(r => [...r.bytes]), [[4,5], [6,7]]);

  const bytesA = new Uint8Array(24).fill(7);
  const bytesB = new Uint8Array(24).fill(8);
  const hashA = await sha256(bytesA);
  const hashB = await sha256(bytesB);
  const manifest = {version:3, generated:'persist', boot:[], client:{maps:{mp_a:[]}}};
  manifest.gpuPreparation = {version:2, assetIdentity:await assetIdentity(manifest), profiles:[{
    key:'renderer/config', maps:['@frontend', 'mp_a'],coverage:{kind:'recorded-families-v1'},
    chunks:[{path:`gpu-catalog/${hashA}.bin`, size:24, sha256:hashA}, {path:`gpu-catalog/${hashB}.bin`, size:24, sha256:hashB}]
  }]};
  const catalogDir = await root.getDirectoryHandle('gpu-catalog-v1', {create:true});
  const staleName = 'ab'.repeat(32) + '.bin';
  catalogDir.files.set(staleName, {bytes:new Uint8Array([1]), at:1});
  const seen = [];
  const fetchFile = async url => {
    seen.push(url.pathname);
    const sha = url.pathname.split('/').pop().slice(0, -4);
    if (sha === hashA) return new Response(bytesA);
    if (sha === hashB) return new Response(bytesB);
    return new Response('', {status:404});
  };
  const base = 'https://game.test/gen/';
  const cat = startGpuRecipeCache('catalog', root, manifest, {base, fetchFile});
  cat.onmessage({data:{kind:'catalog', key:'renderer/config', token:1}});
  assert.equal((await waitFinal(cat, 1)).ok, true);
  assert.equal(seen.length, 2);
  assert.equal(catalogDir.files.has(staleName), false);
  assert.equal(catalogDir.files.has(hashA + '.bin'), true);
  assert.equal(catalogDir.files.has(hashB + '.bin'), true);
  const again = [];
  const cat2 = startGpuRecipeCache('catalog-2', root, manifest, {base, fetchFile: async url => { again.push(url.pathname); return fetchFile(url); }});
  cat2.onmessage({data:{kind:'catalog', key:'renderer/config', token:1}});
  assert.equal((await waitFinal(cat2, 1)).ok, true);
  assert.equal(again.length, 0);
  assert.deepEqual(cat2.sent.filter(r => r.token === 1 && !r.final).map(r => [...r.bytes]), [[...bytesA], [...bytesB]]);
  catalogDir.files.delete(hashB + '.bin');
  const missed = [];
  const cat3 = startGpuRecipeCache('catalog-3', root, manifest, {base, fetchFile: async url => {
    missed.push(url.pathname);
    if (url.pathname.includes(hashB)) return new Response('', {status:503});
    return new Response(bytesA);
  }});
  cat3.onmessage({data:{kind:'catalog', key:'renderer/config', token:1}});
  assert.equal((await waitFinal(cat3, 1)).ok, false);
  assert.equal(missed.length, 1);
  assert.equal(missed[0].includes(hashB), true);
  assert.equal(catalogDir.files.has(hashA + '.bin'), true);
  assert.deepEqual(cat3.sent.filter(r => r.token === 1 && !r.final).map(r => [...r.bytes]), [[...bytesA]]);
  // A native semantic rejection withdraws only the optional recipe chunk.
  cat3.onmessage({data:{kind:'quarantine',key:'renderer/config',sha256:hashA,reason:'Device lost during validation'}});
  await cat3.drain();
  assert.equal(catalogDir.files.has(hashA+'.bad.json'),false);
  cat3.onmessage({data:{kind:'quarantine',key:'renderer/config',sha256:hashA,reason:'bad declaration'}});
  await cat3.drain();
  assert.equal(catalogDir.files.has(hashA+'.bad.json'),true);
  const rejected=[];
  const cat4=startGpuRecipeCache('catalog-4',root,manifest,{base,fetchFile:async url=>{rejected.push(url.pathname);return fetchFile(url);}});
  cat4.onmessage({data:{kind:'catalog',key:'renderer/config',token:1}});
  assert.equal((await waitFinal(cat4,1)).ok,false);
  assert.equal(rejected.some(p=>p.includes(hashA)),false);
  assert.equal(catalogDir.files.has(hashA+'.bin'),true);

  // Temporary OPFS read errors invalidate this attempt, not the committed set.
  let readFailure=false;
  const retryRoot=memoryDirectory({beforeRead(name){if(readFailure && name.endsWith('.bin'))
    throw Object.assign(new Error('temporarily unavailable'),{name:'NotReadableError'});}});
  const retryReports=[];
  const retryChannel=startGpuRecipeCache('read-retry',retryRoot,manifest,{base,fetchFile,onCatalog:r=>retryReports.push(r)});
  retryChannel.onmessage({data:{kind:'save',key:'learned/map',chunks:[new Uint8Array([1,2]),new Uint8Array([3,4])]}});
  await retryChannel.drain();
  const retryDir=await retryRoot.getDirectoryHandle('gpu-recipes-v2');
  const retryIndex=[...retryDir.files.keys()].find(n=>n.endsWith('.index.json'));
  const retryHash=retryIndex.slice(0,64);
  const originalNames=[...retryDir.files.keys()];
  readFailure=true;
  retryChannel.onmessage({data:{kind:'load',key:'learned/map',token:1}});
  assert.equal((await waitFinal(retryChannel,1)).ok,false);
  assert.deepEqual([...retryDir.files.keys()],originalNames);
  assert.equal(retryDir.files.has(retryHash+'.bad.json'),false);
  readFailure=false;
  retryChannel.onmessage({data:{kind:'load',key:'learned/map',token:2}});
  assert.equal((await waitFinal(retryChannel,2)).ok,true);
  assert.deepEqual(retryChannel.sent.filter(r=>r.token===2&&!r.final).map(r=>[...r.bytes]),[[1,2],[3,4]]);

  // A successful portable download cannot mask a broken committed learned set.
  const missingPart=JSON.parse(new TextDecoder().decode(retryDir.files.get(retryIndex).bytes))[1];
  retryDir.files.delete(missingPart);
  retryChannel.onmessage({data:{kind:'catalog',key:'renderer/config',learnedKey:'learned/map',token:3}});
  assert.equal((await waitFinal(retryChannel,3)).ok,false);
  await retryChannel.drain();
  assert.equal(retryReports.at(-1).ok,false);
  assert.equal(retryReports.at(-1).downloaded,true);
  assert.equal(retryReports.at(-1).learned.committed,true);
  assert.equal(retryReports.at(-1).learned.ok,false);
  assert.match(retryReports.at(-1).reason,/local learned recipes/);
  assert.equal(retryDir.files.has(retryIndex),false);

  // Reject oversized indexed sets and duplicate references before reading data.
  const budgetRoot=memoryDirectory(),budgetChannel=startGpuRecipeCache('read-budget',budgetRoot,{version:1});
  budgetChannel.onmessage({data:{kind:'save',key:'budget/map',chunks:[new Uint8Array([1])]}});
  await budgetChannel.drain();
  const budgetDir=await budgetRoot.getDirectoryHandle('gpu-recipes-v2');
  const indexName=[...budgetDir.files.keys()].find(n=>n.endsWith('.index.json')),budgetHash=indexName.slice(0,64);
  const names=Array.from({length:5},(_,i)=>`${budgetHash}.abcdef.${i}.bin`);
  for(const name of names)budgetDir.files.set(name,{bytes:new Uint8Array([1]),size:MAP_LIMIT,at:1});
  budgetDir.files.set(indexName,{bytes:new TextEncoder().encode(JSON.stringify(names)),at:1});
  budgetChannel.onmessage({data:{kind:'load',key:'budget/map',token:1}});
  assert.equal((await waitFinal(budgetChannel,1)).ok,false);
  assert.equal(budgetChannel.sent.filter(r=>r.token===1&&!r.final).length,0);
  assert.equal(budgetDir.files.has(indexName),false);
  assert.throws(()=>parseRecipeIndex([names[0],names[0]],budgetHash),/invalid cache index/);

  retryChannel.onmessage({data:{kind:'save',key:'legacy/retry',bytes:new Uint8Array([8,9])}});
  await retryChannel.drain();
  readFailure=true;
  retryChannel.onmessage({data:{kind:'load',key:'legacy/retry',token:4}});
  assert.equal((await waitFinal(retryChannel,4)).ok,false);
  readFailure=false;
  retryChannel.onmessage({data:{kind:'load',key:'legacy/retry',token:5}});
  const retryLegacy=await waitFinal(retryChannel,5);
  assert.equal(retryLegacy.ok,true);assert.deepEqual([...retryLegacy.bytes],[8,9]);

  const lockedIndex='d'.repeat(64),lockedRoot=memoryDirectory({beforeRemove(name){
    if(name===lockedIndex+'.index.json')throw Object.assign(new Error('locked'),{name:'NoModificationAllowedError'});
  }});
  lockedRoot.files.set(lockedIndex+'.index.json',{bytes:new Uint8Array([1]),at:1});
  const lockedPart=lockedIndex+'.abc.0.bin';
  lockedRoot.files.set(lockedPart,{bytes:new Uint8Array([1]),size:TOTAL_LIMIT,at:1});
  await assert.rejects(evictRecipeSets(lockedRoot,'e'.repeat(64),MAP_LIMIT),/locked/);
  assert.equal(lockedRoot.files.has(lockedIndex+'.index.json'),true);
  assert.equal(lockedRoot.files.has(lockedPart),true);
} finally { globalThis.BroadcastChannel = OriginalChannel; }
console.log('GPU recipe cache channel, persistence, partial failure and retry checks passed');

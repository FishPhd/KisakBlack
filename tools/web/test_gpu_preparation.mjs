import assert from 'node:assert/strict';
import {assetIdentity, loadCatalog, catalogChunks, preparationMode, residentStaticImages, pipelineNostall, runtimePipelineJobs, settleFinished, persistWarmRecipe, skipMapLoadPreparation, preparationReadiness, preparationMessage, sha256, CHUNK_LIMIT, CATALOG_LIMIT, shouldRetryDownloads, MAX_DOWNLOAD_REATTEMPTS} from '../../web/gpu-preparation.js';
const manifest = {version:1, generated:'first', boot:[], client:{maps:{mp_a:[],mp_b:[]}}};
const finishedButIncomplete={status:'incomplete',active:false,done:198,total:198,failures:0,rejected:0};
const mismatchMessage=preparationMessage(finishedButIncomplete,{ok:false,reason:'no catalog matches this renderer build and graphics profile'});
assert.doesNotMatch(mismatchMessage.title,/incomplete/);
assert.equal(mismatchMessage.reason,'');
assert.match(mismatchMessage.hint,/gameplay coverage/);
assert.equal(skipMapLoadPreparation('full', false), true);
assert.equal(skipMapLoadPreparation('full', true), false);
assert.equal(skipMapLoadPreparation('normal', true), true);
assert.equal(skipMapLoadPreparation('off', false), true);
assert.match(preparationMessage({...finishedButIncomplete,failures:2}).reason,/2 graphics compilations failed/);
assert.match(preparationMessage({...finishedButIncomplete,rejected:1}).reason,/1 shader recipe was rejected/);
assert.match(preparationMessage(finishedButIncomplete,{ok:false,learned:{ok:false}}).reason,/Saved shader recipes/);
assert.equal(preparationMessage({active:true}).reason,'');
assert.equal(preparationMode(new URLSearchParams()), 'full');
assert.equal(preparationMode(new URLSearchParams(), true), 'full');
assert.equal(preparationMode(new URLSearchParams('preparation=off')), 'off');
assert.equal(preparationMode(new URLSearchParams('preparation=normal'), true), 'normal');
assert.throws(() => preparationMode(new URLSearchParams('preparation=invalid')));
assert.equal(residentStaticImages('full','webgpu'),true);
assert.equal(residentStaticImages('normal','webgpu'),false);
assert.equal(residentStaticImages('off','webgpu'),false);
assert.equal(residentStaticImages('full','webgl2'),false);
assert.equal(pipelineNostall(new URLSearchParams()), true);
assert.equal(pipelineNostall(new URLSearchParams('pipelineNostall=0')), false);
assert.equal(pipelineNostall(new URLSearchParams('pipelineNostall=1')), true);
assert.match(preparationMessage({active:true,stage:'settle'}).hint,/instead of stalling/);
assert.equal(runtimePipelineJobs(0), 8);
assert.equal(runtimePipelineJobs(8), 8);
assert.equal(runtimePipelineJobs(4), 4);
assert.equal(runtimePipelineJobs(16), 16);
assert.equal(settleFinished(1, 250), false);
assert.equal(settleFinished(0, 0), false);
assert.equal(settleFinished(0, 250), true);
assert.equal(settleFinished(3, 45000), true);
assert.equal(settleFinished(1, 10, true), true);
assert.equal(persistWarmRecipe(false, false), false);
assert.equal(persistWarmRecipe(false, true), true);
assert.equal(persistWarmRecipe(true, false), true);
const readyInput={installation:{downloaded:true,validated:true},catalog:{ok:true,scope:'mp_a',downloaded:true,validated:true},preparation:{map:'mp_a',status:'complete',active:false},renderer:'webgpu'};
assert.deepEqual(preparationReadiness(readyInput),{downloaded:true,validated:true,compiledOnDevice:true,coverageVerified:false});
assert.equal(preparationReadiness({...readyInput,renderer:'webgl2'}).compiledOnDevice,false);
assert.equal(preparationReadiness({...readyInput,catalog:{}}).compiledOnDevice,false);
assert.equal(preparationReadiness({...readyInput,preparation:{status:'captured'}}).compiledOnDevice,false);
assert.equal(preparationReadiness({...readyInput,preparation:{map:'@frontend',status:'complete'}}).compiledOnDevice,false);
assert.equal(preparationReadiness({...readyInput,catalog:{ok:true,downloaded:true,validated:true,scope:'*'},preparation:{map:'@frontend',status:'complete'}}).compiledOnDevice,true);
assert.equal(preparationReadiness({...readyInput,catalog:{...readyInput.catalog,ok:false}}).compiledOnDevice,false);
assert.equal(preparationReadiness({...readyInput,catalog:{...readyInput.catalog,scope:'mp_b'}}).compiledOnDevice,false);
const verifiedInput={...readyInput,catalog:{...readyInput.catalog,profile:'profile',catalogIdentity:'catalog'},
  installation:{...readyInput.installation,coverageVerified:true,coverageReceipt:{map:'mp_a',profile:'profile',catalogIdentity:'catalog',scope:'map route only'}}};
assert.equal(preparationReadiness(verifiedInput).coverageVerified,true);
assert.equal(preparationReadiness({...verifiedInput,preparation:{...readyInput.preparation,map:'mp_b'}}).coverageVerified,false);
assert.equal(preparationReadiness({...verifiedInput,catalog:{...verifiedInput.catalog,profile:'other'}}).coverageVerified,false);
assert.equal(preparationReadiness({...verifiedInput,catalog:{...verifiedInput.catalog,catalogIdentity:'changed'}}).coverageVerified,false);
assert.equal(preparationReadiness({...verifiedInput,installation:{...verifiedInput.installation,coverageReceipt:null}}).coverageVerified,false);
const identity = await assetIdentity(manifest);
assert.equal(await assetIdentity({...manifest,gpuPreparation:{}}), identity);
assert.notEqual(await assetIdentity({...manifest,generated:'later'}), identity);
assert.notEqual(await assetIdentity({...manifest,contentIdentity:'changed bytes, identical file sizes'}), identity);
assert.notEqual(await assetIdentity({...manifest,boot:[{path:'changed',size:1}]}), identity);
const bytes = new Uint8Array(24).fill(5), hash = await sha256(bytes);
const chunk = {path:`gpu-catalog/${hash}.bin`,size:bytes.length,sha256:hash};
const profile = {key:'renderer/config',maps:['@frontend','mp_a','mp_b'],coverage:{kind:'recorded-families-v1'},chunks:[chunk,chunk]};
const catalog = {version:2,assetIdentity:identity,profiles:[profile]};
const withCatalog = {...manifest,gpuPreparation:catalog};
let requests = 0, emitted = [];
const fetchFile = async url => { ++requests; assert.equal(url.origin,'https://game.test'); return new Response(bytes); };
const load = (m=withCatalog, k=profile.key, fetcher=fetchFile) => loadCatalog(m,k,'https://game.test/gen/',b=>emitted.push(b),fetcher);
const loaded = await load();
assert.equal(loaded.ok, true);
assert.equal(loaded.chunks, 1);
assert.equal(loaded.failed, 0);
assert.equal(loaded.reattempts, 0);
assert.equal(emitted.length,1);
assert.equal((await load(withCatalog,'other-config')).ok,false);
assert.equal((await load(manifest)).ok,false);
assert.equal((await load({...manifest,gpuPreparation:{...catalog,version:1}})).ok,false);
assert.equal((await load({...manifest,gpuPreparation:{...catalog,profiles:[{...profile,coverage:null}]}})).ok,false);
assert.equal((await load({...withCatalog,boot:[{path:'new',size:2}]})).ok,false);
assert.equal((await load({...manifest,gpuPreparation:{...catalog,profiles:[{...profile,maps:['mp_a']}]}})).ok,false);
for (const bad of [{...chunk,path:'../escape.bin'},{...chunk,size:CHUNK_LIMIT+1},{...chunk,sha256:'bad'}]) {
  assert.equal((await load({...manifest,gpuPreparation:{...catalog,profiles:[{...profile,chunks:[bad]}]}})).ok,false);
}
const mismatch = await load(withCatalog, profile.key, async () => new Response(new Uint8Array(24)));
assert.equal(mismatch.ok, false);
assert.equal(mismatch.chunks, 0);
assert.equal(mismatch.reattempts, 0);
assert.match(mismatch.reason, /checksum/);
const missing = await load(withCatalog, profile.key, async () => new Response('', {status:404}));
assert.equal(missing.ok, false);
assert.equal(missing.chunks, 0);
assert.equal(missing.reattempts, 0);
assert.match(missing.reason, /404/);
assert.equal(requests,1);
assert.equal(MAX_DOWNLOAD_REATTEMPTS, 2);
assert.equal(shouldRetryDownloads(1, 4), true);
assert.equal(shouldRetryDownloads(2, 8), true);
assert.equal(shouldRetryDownloads(1, 3), false);
assert.equal(shouldRetryDownloads(3, 8), false);
assert.equal(shouldRetryDownloads(0, 4), false);

async function makeChunks(count) {
  const chunks = [];
  for (let i = 0; i < count; ++i) {
    const bytes = new Uint8Array(24);
    bytes.fill(i + 1);
    const hash = await sha256(bytes);
    chunks.push({path:`gpu-catalog/${hash}.bin`, size:bytes.length, sha256:hash, bytes});
  }
  return chunks;
}
const describe = chunks => chunks.map(({path, size, sha256: hash}) => ({path, size, sha256: hash}));
const catalogWith = chunks => ({...withCatalog, gpuPreparation:{...catalog, profiles:[{...profile, chunks:describe(chunks)}]}});
const diskStore = disk => ({
  async get(sha) { const hit = disk.get(sha); return hit ? hit.slice() : null; },
  async put(sha, bytes) { disk.set(sha, bytes.slice()); },
  async delete(sha) { disk.delete(sha); },
});
const shaOf = url => url.pathname.split('/').pop().replace(/\.bin$/, '');
const fetcherFor = (chunks, fail, calls) => async url => {
  const sha = shaOf(url);
  calls.push(sha);
  if (fail.has(sha)) return new Response('', {status:503});
  return new Response(chunks.find(chunk => chunk.sha256 === sha).bytes);
};
const four = await makeChunks(4);
const bad = four[1].sha256;
const disk = new Map();
const calls = [];
const firstEmit = [];
const first = await loadCatalog(catalogWith(four), profile.key, 'https://game.test/gen/', bytes => firstEmit.push(bytes), fetcherFor(four, new Set([bad]), calls), diskStore(disk));
assert.equal(first.ok, false);
assert.equal(first.chunks, 3);
assert.equal(first.failed, 1);
assert.equal(first.reattempts, 2);
assert.equal(calls.length, 6);
assert.equal(calls.filter(sha => sha === bad).length, 3);
assert.equal(disk.size, 3);
assert.equal(firstEmit.length, 3);
assert.equal(disk.has(bad), false);
const again = [];
const secondEmit = [];
const second = await loadCatalog(catalogWith(four), profile.key, 'https://game.test/gen/', bytes => secondEmit.push(bytes), fetcherFor(four, new Set([bad]), again), diskStore(disk));
assert.equal(second.chunks, 3);
assert.equal(second.reattempts, 2);
assert.equal(again.length, 3);
assert.equal(again.every(sha => sha === bad), true);
const healed = new Set();
const thirdCalls = [];
const third = await loadCatalog(catalogWith(four), profile.key, 'https://game.test/gen/', () => {}, fetcherFor(four, healed, thirdCalls), diskStore(disk));
assert.equal(third.ok, true);
assert.equal(third.chunks, 4);
assert.equal(third.reattempts, 0);
assert.deepEqual(thirdCalls, [bad]);
const fourthCalls = [];
const fourth = await loadCatalog(catalogWith(four), profile.key, 'https://game.test/gen/', () => {}, fetcherFor(four, healed, fourthCalls), diskStore(new Map(disk)));
assert.equal(fourth.ok, true);
assert.equal(fourthCalls.length, 0);
const halfFail = new Set([four[0].sha256, four[2].sha256]);
const halfCalls = [];
const halfDisk = new Map();
const half = await loadCatalog(catalogWith(four), profile.key, 'https://game.test/gen/', () => {}, fetcherFor(four, halfFail, halfCalls), diskStore(halfDisk));
assert.equal(half.reattempts, 0);
assert.equal(half.chunks, 2);
assert.equal(half.failed, 2);
assert.equal(halfCalls.length, 4);
const halfAgain = [];
await loadCatalog(catalogWith(four), profile.key, 'https://game.test/gen/', () => {}, fetcherFor(four, halfFail, halfAgain), diskStore(halfDisk));
assert.equal(halfAgain.length, 2);
assert.equal(halfAgain.every(sha => halfFail.has(sha)), true);
const three = four.slice(0, 3);
const threeCalls = [];
const threeResult = await loadCatalog(catalogWith(three), profile.key, 'https://game.test/gen/', () => {}, fetcherFor(three, new Set([three[0].sha256]), threeCalls), diskStore(new Map()));
assert.equal(threeResult.reattempts, 0);
assert.equal(threeCalls.length, 3);
assert.equal(threeResult.chunks, 2);
let flakyTries = 0;
const flakySha = four[0].sha256;
const flakyCalls = [];
const flakyDisk = new Map();
const flaky = await loadCatalog(catalogWith(four), profile.key, 'https://game.test/gen/', () => {}, async url => {
  const sha = shaOf(url);
  flakyCalls.push(sha);
  if (sha === flakySha && flakyTries++ < 2) return new Response('', {status:503});
  return new Response(four.find(chunk => chunk.sha256 === sha).bytes);
}, diskStore(flakyDisk));
assert.equal(flaky.ok, true);
assert.equal(flaky.reattempts, 2);
assert.equal(flaky.chunks, 4);
assert.equal(flakyCalls.filter(sha => sha === flakySha).length, 3);
const after = [];
await loadCatalog(catalogWith(four), profile.key, 'https://game.test/gen/', () => {}, fetcherFor(four, new Set(), after), diskStore(flakyDisk));
assert.equal(after.length, 0);
disk.set(four[3].sha256, new Uint8Array(24).fill(9));
const repairCalls = [];
const repaired = await loadCatalog(catalogWith([four[3]]), profile.key, 'https://game.test/gen/', () => {}, fetcherFor(four, new Set(), repairCalls), diskStore(disk));
assert.equal(repaired.ok, true);
assert.deepEqual(repairCalls, [four[3].sha256]);
assert.equal(disk.get(four[3].sha256)[0], 4);
const quotaCalls = [];
const quota = await loadCatalog(catalogWith(four), profile.key, 'https://game.test/gen/', () => {}, fetcherFor(four, new Set(), quotaCalls), {async get(){return null;}, async put(){throw new Error('quota');}});
assert.equal(quota.ok, true);
assert.equal(quota.chunks, 4);
assert.equal(quotaCalls.length, 4);
assert.equal(quota.persistence.status,'incomplete');
assert.equal(quota.persistence.writeFailures,4);
const networkOnly = await loadCatalog(catalogWith(four), profile.key, 'https://game.test/gen/', () => {}, fetcherFor(four, new Set(), []));
assert.equal(networkOnly.ok,true);
assert.equal(networkOnly.persistence.status,'unavailable');
console.log('Preparation modes, asset identity, catalog persistence, partial failure and retry checks passed');

// Scoped v3 catalogs load only shared/weapon recipes and the requested map.
const [shared,mapA,mapB,weapons] = await makeChunks(4);
const grouped = {...withCatalog,gpuPreparation:{...catalog,version:3,profiles:[{...profile,
  coverage:{kind:'exact-draws-v1'},chunks:describe([shared,mapA,mapB,weapons]),groups:[
    {scope:'@shared',chunks:describe([shared])},{scope:'@weapons',chunks:describe([weapons])},
    {scope:'@frontend',chunks:[]},{scope:'mp_a',chunks:describe([mapA])},{scope:'mp_b',chunks:describe([mapB])}]}]}};
const scopedCalls=[];
const scoped=await loadCatalog(grouped,profile.key,'https://game.test/gen/',()=>{},fetcherFor([shared,mapA,mapB,weapons],new Set(),scopedCalls),null,{scope:'mp_a'});
assert.equal(scoped.ok,true);
assert.deepEqual(new Set(scopedCalls),new Set([shared.sha256,weapons.sha256,mapA.sha256]));
assert.equal(scopedCalls.includes(mapB.sha256),false);
assert.equal((await loadCatalog(grouped,profile.key,'https://game.test/gen/',()=>{},fetchFile,null,{scope:'mp_unknown'})).ok,false);
let quarantineRequests=0;
const quarantined=await loadCatalog(withCatalog,profile.key,'https://game.test/gen/',()=>{},async()=>{++quarantineRequests;return new Response(bytes);},
  {quarantined:async()=>true});
assert.equal(quarantined.ok,false);
assert.equal(quarantineRequests,0);
assert.match(quarantined.reason,/quarantined/);
console.log('Scoped exact catalog selection and native quarantine checks PASS');

// Renderer delivery failures must remain failures until an emit succeeds.
let deliveryAttempts=0;
const retriedDelivery=await loadCatalog(catalogWith(four),profile.key,'https://game.test/gen/',async(_bytes,chunk)=>{
  if(chunk.sha256===four[0].sha256 && ++deliveryAttempts===1)throw new Error('renderer unavailable');
},fetcherFor(four,new Set(),[]),diskStore(new Map()));
assert.equal(deliveryAttempts,2);
assert.equal(retriedDelivery.ok,true);
assert.equal(retriedDelivery.chunks,4);
assert.equal(retriedDelivery.reattempts,1);
const rejectedDelivery=await loadCatalog(catalogWith(four),profile.key,'https://game.test/gen/',(_bytes,chunk)=>{
  if(chunk.sha256===four[0].sha256)throw new Error('renderer unavailable');
},fetcherFor(four,new Set(),[]));
assert.equal(rejectedDelivery.ok,false);
assert.equal(rejectedDelivery.chunks,3);
assert.equal(rejectedDelivery.failed,1);
assert.equal(rejectedDelivery.downloaded,true);
assert.equal(rejectedDelivery.validated,true);
assert.match(rejectedDelivery.reason,/renderer unavailable/);

// Reject malformed optional metadata as a cache miss, without throwing or fetching.
for(const profiles of [{},[null], [{...profile,maps:'@frontend mp_a mp_b'}],
  [{...profile,chunks:[null]}],[{...profile,groups:[{scope:'@frontend',chunks:[null]}]}],
  [{...profile,chunks:[chunk,{...chunk,size:25}]}],
  [{...profile,chunks:[{...chunk,path:`gpu-catalog/${'f'.repeat(64)}.bin`}]}]]) {
  const result=await load({...withCatalog,gpuPreparation:{...catalog,profiles}},profile.key,()=>{throw new Error('unexpected fetch');});
  assert.equal(result.ok,false);
}
const excessive=Array.from({length:CATALOG_LIMIT/CHUNK_LIMIT+1},(_,i)=>{
  const sha=i.toString(16).padStart(64,'0');return {sha256:sha,path:`gpu-catalog/${sha}.bin`,size:CHUNK_LIMIT};
});
assert.equal(catalogChunks({chunks:excessive}),null);

// Decode directly into the advertised bounded allocation and cancel on overflow.
let cancelled=false, reads=0;
const overflow=await load(catalogWith([four[0]]),profile.key,async()=>({ok:true,body:{getReader:()=>({
  async read(){++reads;return {value:new Uint8Array(25),done:false};},
  async cancel(){cancelled=true;},releaseLock(){},
})}}));
assert.equal(overflow.ok,false);assert.match(overflow.reason,/oversized/);
assert.equal(cancelled,true);assert.equal(reads,1);
const truncated=await load(catalogWith([four[0]]),profile.key,async()=>new Response(new Uint8Array(23)));
assert.equal(truncated.ok,false);assert.match(truncated.reason,/truncated/);
const split=await load(catalogWith([four[0]]),profile.key,async()=>new Response(new ReadableStream({start(c){
  c.enqueue(four[0].bytes.subarray(0,7));c.enqueue(four[0].bytes.subarray(7));c.close();
}})));
assert.equal(split.ok,true);

const allScope=await loadCatalog(grouped,profile.key,'https://game.test/gen/',()=>{},fetcherFor([shared,mapA,mapB,weapons],new Set(),[]));
assert.equal(allScope.catalogIdentity,scoped.catalogIdentity);
assert.equal(allScope.assetIdentity,identity);
const replacement=await load(catalogWith([four[0]]));
const otherReplacement=await load(catalogWith([four[1]]));
assert.notEqual(replacement.catalogIdentity,otherReplacement.catalogIdentity);
assert.notEqual(replacement.catalogIdentity,identity);
let prunes=0;
await loadCatalog(catalogWith(four),profile.key,'https://game.test/gen/',()=>{},fetcherFor(four,new Set([bad]),[]),
  {...diskStore(new Map()),async prune(){++prunes;}});
assert.equal(prunes,0);
await loadCatalog(grouped,profile.key,'https://game.test/gen/',()=>{},fetcherFor([shared,mapA,mapB,weapons],new Set(),[]),
  {...diskStore(new Map()),async prune(){++prunes;}},{scope:'mp_a'});
assert.equal(prunes,0);
await loadCatalog(catalogWith(four),profile.key,'https://game.test/gen/',()=>{},fetcherFor(four,new Set(),[]),
  {...diskStore(new Map()),async prune(){++prunes;}});
assert.equal(prunes,1);
console.log('Bounded catalog reads, exact generation identities and failed-delivery retries PASS');

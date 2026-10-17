import assert from 'node:assert/strict';
import {HASH_BLOCK, digest, installationEntries, verifyStream, receiptMatches, verificationReceipt, assetNetworkGuard} from '../../web/asset-install.js';
import {HitchDiagnostics} from '../../web/hitch-diagnostics.js';
const bytes = new Uint8Array(HASH_BLOCK + 31);
for (let i = 0; i < bytes.length; ++i) bytes[i] = i % 251;
const entry = {path:'main/iw_00.iwd',size:bytes.length,blockSize:HASH_BLOCK,
  hashes:[await digest(bytes.subarray(0,HASH_BLOCK)),await digest(bytes.subarray(HASH_BLOCK))]};
const stream = data => new ReadableStream({start(controller) {
  // Deliberately cross hash boundaries and use different final chunk sizes.
  for (let at=0; at<data.length; at+=65539) controller.enqueue(data.subarray(at,at+65539));
  controller.close();
}});
let written=0, largest=0;
assert.equal(await verifyStream(stream(bytes),entry,async data=>{written+=data.length;largest=Math.max(largest,data.length);}),bytes.length);
assert.equal(written,bytes.length); assert.equal(largest,HASH_BLOCK);
const corrupt=bytes.slice();corrupt[1]^=1;
await assert.rejects(verifyStream(stream(corrupt),entry),/checksum/);
await assert.rejects(verifyStream(stream(bytes.subarray(0,-1)),entry),/truncated/);
await assert.rejects(verifyStream(stream(new Uint8Array(bytes.length+1)),entry),/oversized|checksum/);
await assert.rejects(verifyStream(stream(bytes),entry,async()=>{throw new Error('quota exceeded');}),/quota/);
assert.equal(installationEntries({version:1,files:[entry]}).length,1);
assert.throws(()=>installationEntries({version:1,files:[entry,{...entry,path:'MAIN/iw_00.iwd'}]}),/Duplicate/);
for(const path of ['../escape','main/../escape','main/a\\b','main/http:x','main/a?b','main//a'])
  assert.throws(()=>installationEntries({version:1,files:[{...entry,path}]}));
const file={size:bytes.length,lastModified:123}, receipt=verificationReceipt(entry,file);
assert(receiptMatches(receipt,entry,file));
assert(!receiptMatches(receipt,entry,{...file,lastModified:124}));
assert(!receiptMatches(receipt,{...entry,hashes:[entry.hashes[1],entry.hashes[0]]},file));
let active=false;const events=[];
const guard=assetNetworkGuard({enabled:true,gameplay:()=>active,record:e=>events.push(e),now:()=>100});
guard('map.ff','asset');active=true;
assert.throws(()=>guard('missing.iwi','loose'),/blocked/);assert.equal(events.at(-1).blocked,true);
assert.deepEqual(guard.counters,{requests:2,gameplayRequests:1,blockedRequests:1});
events.length=0;
assert.equal(guard.counters.blockedRequests,1); // Rolling traces may be cleared or trimmed.
const report=new HitchDiagnostics(3);
report.event({kind:'file-wait',path:'x',at:10,end:70});report.frame({at:80,intervalMs:60});
assert.equal(report.report().hitches[0].overlaps[0].path,'x');
assert.equal(report.report().hitches[0].unexplained,false);
report.frame({at:200,intervalMs:80});assert(report.report().hitches[1].unexplained);
report.event({kind:'pipeline-use',at:250,recipeHash:'cached'});
report.frame({at:260,intervalMs:55});
assert.equal(report.report().hitches[2].overlaps.length,1);
assert.equal(report.report().hitches[2].workOverlaps.length,0);
assert.equal(report.report().hitches[2].unexplained,true);
report.event({kind:'pipeline-miss',at:275,requiredPipeline:true});
report.frame({at:290,intervalMs:55});
assert.equal(report.report().hitches.at(-1).unexplained,true);
report.frame({at:360,intervalMs:55,pipelineWaitMs:20});
assert.equal(report.report().hitches.at(-1).unexplained,false);
for(let i=0;i<4;i++)report.event({at:i});assert.equal(report.events.length,3);assert(report.dropped>0);
console.log('Full-install integrity, bounded streams, receipts, offline guard and hitch correlation passed');

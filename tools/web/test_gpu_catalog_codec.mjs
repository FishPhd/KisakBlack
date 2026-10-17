import assert from 'node:assert/strict';
import {CatalogPacker} from './gpu_catalog_codec.mjs';
import {compactCatalog} from './compact_gpu_catalog.mjs';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import {createHash} from 'node:crypto';
const u32 = n => { const b=Buffer.alloc(4); b.writeUInt32LE(n); return b; };
const blob = b => Buffer.concat([u32(b.length),b]);
// Independent reference checksum for fixtures and the repacked output.
function checked(parts) {
  const bytes=Buffer.concat(parts); let hash=0xcbf29ce484222325n;
  for(const byte of bytes) hash=BigInt.asUintN(64,(hash^BigInt(byte))*0x100000001b3n);
  const tail=Buffer.alloc(8); tail.writeBigUInt64LE(hash); return Buffer.concat([bytes,tail]);
}
function fixture(ids, source='vertex source', reversed=false, keyFor=u32) {
  const sources=[Buffer.from(source),Buffer.from('pixel source')]; if(reversed) sources.reverse();
  return checked([u32(0x4b475243),u32(2),u32(2),...sources.map(blob),u32(ids.length),...ids.map(id=>Buffer.concat([
    u32(reversed?1:0),u32(reversed?0:1),blob(keyFor(id)),Buffer.alloc(1024),blob(Buffer.alloc(0)),Buffer.alloc(16),
    u32(4),u32(0),u32(7),u32(1),u32(3),u32(2),u32(1),u32(0),u32(1),u32(0),u32(0),u32(4),u32(0),
  ]))]);
}
const p=new CatalogPacker(); p.add(fixture([1,2])); p.add(fixture([2,3],'vertex source',true));
assert.equal(p.recipes.size,3); assert.equal(p.sources.size,2);
const packed=[...p.chunks()]; assert.equal(packed.length,1);
assert(checked([packed[0].subarray(0,-8)]).equals(packed[0]));
const restored=new CatalogPacker(); restored.add(packed[0]); assert.equal(restored.recipes.size,3);
assert.throws(()=>p.add(fixture([1],'changed source')),/conflicting/);
const corrupted=fixture([5]); corrupted[20]^=1; assert.throws(()=>p.add(corrupted),/checksum/);
assert.throws(()=>p.add(checked([u32(0x4b475243),u32(99),u32(0),u32(0)])),/unsupported/);
assert.throws(()=>p.add(checked([u32(0x4b475243),u32(2),u32(0),u32(1),u32(0),u32(0)])),/shader reference/);
const large=new CatalogPacker();
large.add(fixture(Array.from({length:3000},(_,i)=>i)));
large.add(fixture(Array.from({length:3000},(_,i)=>i+3000)));
const bounded=[...large.chunks()]; assert.equal(bounded.length,2);
const again=new CatalogPacker(); for(const chunk of bounded) again.add(chunk); assert.equal(again.recipes.size,6000);
const keyFor=id=>{const key=Buffer.alloc(160);key.writeUInt32LE(11,0);key.writeUInt32LE(id,20);key.writeUInt32LE(id===3?2:1,144);return key;};
const family=new CatalogPacker(); family.add(fixture([1,2,3],'vertex source',false,keyFor));
const observedHash=checked([keyFor(1)]).subarray(-8).readBigUInt64LE().toString();
assert.deepEqual(family.selectRecordedFamilies([observedHash]),{
  kind:'recorded-families-v1',enumeratedRecipes:3,observedRecipes:1,families:1,catalogRecipes:2});
const chosen=new CatalogPacker(); for(const chunk of family.chunks())chosen.add(chunk);
assert.equal(chosen.recipes.size,2);
assert.throws(()=>family.selectRecordedFamilies(['999999']),/missing/);
const exact = new CatalogPacker(); exact.add(fixture([1,2,3],'vertex source',false,keyFor));
assert.equal(exact.selectExactDraws([observedHash]).catalogRecipes,1);
assert.throws(()=>exact.selectExactDraws(['999999']),/missing/);
const temporaryRoot=fs.realpathSync(os.tmpdir());
const directory=fs.mkdtempSync(path.join(temporaryRoot,'kisak-catalog-test-'));
try {
  fs.mkdirSync(path.join(directory,'gpu-catalog'));
  const chunks=[fixture([1,2],'used source',false,keyFor),fixture([3],'unused source',false,keyFor)].map(bytes=>{
    const sha256=createHash('sha256').update(bytes).digest('hex'), relative=`gpu-catalog/${sha256}.bin`;
    fs.writeFileSync(path.join(directory,relative),bytes);
    return {path:relative,size:bytes.length,sha256};
  });
  const [first]=compactCatalog(directory,[{chunks,observedHashes:[observedHash]}]);
  assert.equal(first.packing.sources,2); assert.equal(first.packing.inputSources,3);
  const [repacked]=compactCatalog(directory,[first]);
  assert.deepEqual(repacked.coverage,first.coverage);
  assert.equal(repacked.coverage.enumeratedRecipes,3);
  assert.equal(repacked.packing.sources,2);
  assert.deepEqual(repacked.chunks,first.chunks);
  const grouped=compactCatalog(directory,[{key:'p',maps:['@frontend','mp_a','mp_b'],chunks,
    observedHashes:[observedHash],scopes:{'@frontend':[],'mp_a':[observedHash],'mp_b':[observedHash]}}])[0];
  assert.equal(grouped.coverage.kind,'exact-draws-v1');
  assert.equal(grouped.groups.find(g=>g.scope==='@shared').recipes,1);
  assert.equal(grouped.groups.find(g=>g.scope==='mp_a').recipes,0);
} finally {
  assert.equal(path.dirname(directory),temporaryRoot);
  assert(path.basename(directory).startsWith('kisak-catalog-test-'));
  fs.rmSync(directory,{recursive:true,force:true});
}
console.log('Catalog packing preserves recipes, deduplicates sources, bounds chunks and rejects corruption');

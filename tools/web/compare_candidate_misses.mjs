// Check whether previously observed misses are covered by a capture's initial
// enumeration, rather than merely appended after actual rendering learned them.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {CatalogPacker} from './gpu_catalog_codec.mjs';
const args=process.argv.slice(2),option=(name,fallback)=>{const at=args.indexOf(name);return at<0?fallback:args[at+1];};
const directory=path.resolve(option('--directory','build-web/assets-gen'));
const capture=JSON.parse(fs.readFileSync(path.join(directory,'gpu-catalog/capture.json'),'utf8'));
const misses=JSON.parse(fs.readFileSync(option('--misses','build-web/first-play-probe-before-catalog/misses.json'),'utf8'));
const map=option('--map','mp_nuked'),entry=capture.maps.find(m=>m.map===map),profile=capture.profiles.find(p=>entry.key.startsWith(p.key+'/'));
assert(entry?.preparation?.candidates && profile,'Capture has not collected this map');
assert.equal(capture.maps.at(-1).map,map,'Run against the latest collected map before another map appends chunks');
const fnv=key=>{let hash=0xcbf29ce484222325n;for(const byte of key)hash=BigInt.asUintN(64,(hash^BigInt(byte))*0x100000001b3n);return hash.toString();};
const wanted=new Set(misses.map(m=>m.recipeHash)),found=new Set();
const read=chunk=>{assert(/^gpu-catalog\/[a-f0-9]{64}\.bin$/.test(chunk.path));const packer=new CatalogPacker();packer.add(fs.readFileSync(path.join(directory,chunk.path)));return [...packer.recipes.values()];};
for(const chunk of profile.chunks)for(const recipe of read(chunk)){const hash=fnv(recipe.key);if(wanted.has(hash))found.add(hash);}
const learned=entry.gpu.cacheQueuedRecipes-entry.preparation.candidates;
const tail=read(profile.chunks.at(-1));
assert(learned>=0 && learned<=tail.length,'Learned tail spans multiple chunks; preserve per-map chunk order to inspect it');
const added=new Set((learned?tail.slice(-learned):[]).map(r=>fnv(r.key)));
const result={map,candidates:entry.preparation.candidates,learnedAfterEnumeration:learned,
  previousMisses:[...wanted].map(hash=>({hash,present:found.has(hash),coveredByEnumeration:found.has(hash)&&!added.has(hash)}))};
const output=path.resolve(option('--out','build-web/first-play-probe-before-catalog/candidate-coverage.json'));
fs.writeFileSync(output,JSON.stringify(result,null,2));console.log(JSON.stringify(result));

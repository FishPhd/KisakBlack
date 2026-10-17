// Resolve runtime miss identities to the exact portable descriptors learned by
// the engine. Reads only this owned session's application recipe cache.
import fs from 'node:fs';
import path from 'node:path';
import {CDP} from './cdp-client.mjs';
import {CatalogPacker} from './gpu_catalog_codec.mjs';
const args=process.argv.slice(2),option=(name,fallback)=>{const at=args.indexOf(name);return at<0?fallback:args[at+1];};
const route=JSON.parse(fs.readFileSync(option('--route','build-web/first-play-probe/route.json'),'utf8'));
const output=path.resolve(option('--out','build-web/first-play-misses.json'));
const session=JSON.parse(fs.readFileSync(option('--session','build-web/agent-session/session.json'),'utf8'));
const cdp=await CDP.connect(session.debugPort,session.browserUrl),sid=await cdp.attach(session.targets.host);
const evaluate=expression=>cdp.evaluate(expression,sid);
const names=['cull','colorWrite','blend','srcBlend','dstBlend','blendOp','separateAlpha','srcAlpha','dstAlpha','alphaOp',
  'zEnable','zWrite','zFunc','depthBias','slopeBias','stencil','stencilFunc','stencilFail','stencilZFail','stencilPass',
  'stencilMask','stencilWriteMask','twoSidedStencil','ccwFunc','ccwFail','ccwZFail','ccwPass'];
const fnv=key=>{let hash=0xcbf29ce484222325n;for(const byte of key)hash=BigInt.asUintN(64,(hash^BigInt(byte))*0x100000001b3n);return hash.toString();};
try {
  const files=await evaluate(`(async()=>{const dir=await (await navigator.storage.getDirectory()).getDirectoryHandle('gpu-recipes-v2');
    const result=[];for await(const [name,handle] of dir.entries())if(name.endsWith('.index.json')){
      const index=JSON.parse(await (await handle.getFile()).text());result.push(...(Array.isArray(index)?index:index.chunks));}
    return [...new Set(result)];})()`);
  const packer=new CatalogPacker();
  for(const name of files) {
    if(!/^[a-f0-9]{64}\.[a-f0-9]+\.\d+\.bin$/.test(name))throw new Error('Invalid recipe chunk');
    const encoded=await evaluate(`(async()=>{const dir=await (await navigator.storage.getDirectory()).getDirectoryHandle('gpu-recipes-v2');
      const bytes=new Uint8Array(await(await(await dir.getFileHandle(${JSON.stringify(name)})).getFile()).arrayBuffer());
      let binary='';for(let i=0;i<bytes.length;i+=8192)binary+=String.fromCharCode(...bytes.subarray(i,i+8192));return btoa(binary);})()`);
    packer.add(Buffer.from(encoded,'base64'));
  }
  const recipes=new Map([...packer.recipes.values()].map(r=>[fnv(r.key),r]));
  const misses=route.visits.flatMap(visit=>visit.events.filter(e=>e.requiredPipeline).map(e=>({...e,phase:visit.phase,pointIndex:visit.pointIndex,yaw:visit.yaw})));
  const report=misses.map(miss=>{
    const recipe=recipes.get(miss.recipeHash);
    if(!recipe)return {...miss,resolved:false};
    const key=recipe.key,end=key.length-32;
    const states=Object.fromEntries(names.map((name,i)=>[name,key.readUInt32LE(20+i*4)]));
    return {...miss,resolved:true,descriptor:{vertexHash:key.readBigUInt64LE(0).toString(),pixelHash:key.readBigUInt64LE(8).toString(),
      depthMask:key.readUInt32LE(16),states,declaration:[...key.subarray(128,end)],strides:[0,1,2,3].map(i=>key.readUInt32LE(end+i*4)),
      color:key.readUInt32LE(end+16),depth:key.readUInt32LE(end+20),topology:key.readUInt32LE(end+24),strip:key.readUInt32LE(end+28)}};
  });
  fs.writeFileSync(output,JSON.stringify(report,null,2));
  console.log(JSON.stringify({file:output,misses:report.length,resolved:report.filter(r=>r.resolved).length,descriptors:report.map(r=>r.descriptor)}));
} finally {cdp.close();}

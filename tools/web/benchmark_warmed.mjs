// Compare worker scheduling on one primed GPU device. No reloads, shader
// preparation, builds or CPU profilers belong inside this timing experiment.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {CDP,sleep} from './cdp-client.mjs';
const args=process.argv.slice(2), option=(name,fallback)=>{const at=args.indexOf(name);return at<0?fallback:args[at+1];};
const session=JSON.parse(fs.readFileSync(option('--session','build-web/agent-session/session.json'),'utf8'));
assert(session.headed,'Use the owned visible game');
const output=path.resolve(option('--out','build-web/preparation-validation/warmed-submit'));
fs.mkdirSync(output,{recursive:true});
const dwell=Number(option('--dwell','2')); assert(dwell>=1 && dwell<=10);
const cdp=await CDP.connect(session.debugPort,session.browserUrl), sid=await cdp.attach(session.targets.host);
const evaluate=expression=>cdp.evaluate(expression,sid), state=()=>evaluate('window.kisak.agent.status()');
const metrics=()=>evaluate('window.kisak.agent.metrics()');
const send=command=>evaluate(`window.kisak.state.send(${JSON.stringify(command)})`);
const percentile=(values,p)=>{if(!values.length)return null;const a=values.slice().sort((x,y)=>x-y);return a[Math.min(a.length-1,Math.ceil(a.length*p)-1)];};
const summary=values=>({median:percentile(values,.5),p95:percentile(values,.95),p99:percentile(values,.99),over50:values.filter(v=>v>50).length});
let original,originalFlags;
const results=[];
function competingBuilds() {
  if(process.platform!=='win32')return [];
  return execFileSync('tasklist',['/FO','CSV','/NH'],{encoding:'utf8',windowsHide:true})
    .split(/\r?\n/).filter(line=>/^"(?:ninja|cmake|clang\+\+|wasm-ld|emcc|em\+\+)\.exe"/i.test(line));
}
const buildActivity=[];
const monitorBuilds=setInterval(()=>{
  try{const active=competingBuilds();if(active.length)buildActivity.push({at:Date.now(),active});}
  catch{buildActivity.push({at:Date.now(),unavailable:true});}
},5000);
monitorBuilds.unref();
async function visit(origin,yaw) {
  const before=await metrics(), last=before.gpuFrames.at(-1)?.frameId || 0;
  await send(`setviewpos ${origin.join(' ')} ${yaw} 0`); await sleep(dwell*1000+250);
  const after=await metrics(), current=await state();
  assert(!current.crash && current.engine.inputReady && !current.engine.uiActive);
  assert.equal(current.engine.map,original.engine.map); assert.equal(current.engine.bots,original.engine.bots);
  assert.equal(after.resolution,'960x540'); assert.equal(after.visibilityChanges,before.visibilityChanges);
  assert(Math.abs(current.engine.origin[0]-origin[0])<.5 && Math.abs(current.engine.origin[1]-origin[1])<.5,'Camera moved');
  assert(Math.abs(((current.engine.cameraAngles[1]-yaw+540)%360)-180)<1 && Math.abs(current.engine.cameraAngles[0])<1,'Camera turned');
  const frames=after.gpuFrames.filter(f=>f.frameId>last), ids=new Set(frames.map(f=>f.frameId));
  const events=after.gpuEvents.filter(e=>ids.has(e.frameId));
  return {yaw,camera:current.engine.origin,counters:after.gpu,
    intervals:after.renderIntervals.slice(before.renderIntervals.length),frames,events,
    requiredPipelineCompilations:events.filter(e=>e.requiredPipeline).length,
    firstTextureUploads:frames.reduce((n,f)=>n+(f.firstTextureUploads||0),0),
    firstStaticBufferUploads:frames.reduce((n,f)=>n+(f.firstStaticBufferUploads||0),0)};
}
try {
  assert.equal(competingBuilds().length,0,'Finish competing builds before measuring frame times');
  await cdp.send('Page.bringToFront',{},sid);
  original=await state(); const initial=await metrics();
  assert(original.engine?.inputReady && !original.engine.uiActive,'Enter gameplay before running the warmed comparison');
  assert.equal(initial.rendererBackend,'webgpu'); assert.equal(initial.resolution,'960x540');
  const fixtures={mp_nuked:[-1634,1192,0],mp_mountain:[2535,-1983,520]}, origin=fixtures[original.engine.map];
  assert(origin,'Use mp_nuked or mp_mountain'); assert([0,11].includes(original.engine.bots));
  const params=new URL(await evaluate('location.href')).searchParams;
  assert.notEqual(params.get('gpuValidation'),'1','Measure release timing on a validation-disabled device');
  assert.equal(params.get('gpuProfile'),'1','Enable GPU profiling when entering this owned test match');
  originalFlags=1 | (params.get('gpuWake')==='0'?0:2) | (params.get('gpuDepthMerge')==='0'?0:4) |
    (params.get('gpuUploadBatch')==='0'?0:8) | (params.get('gpuTextureBindings')==='0'?0:16) |
    (params.get('gpuCapture')==='1'?32:0) | (params.get('gpuSubmitWake')==='1'?64:0) |
    (params.get('bestCase')==='1'?128:0) | (params.get('gpuEnumerate')==='1'?256:0);
  assert(!(originalFlags&32),'Do not measure catalog capture');
  const base=originalFlags&~64;
  const cheats=[]; if(!original.engine.cheats.god)cheats.push('god'); if(!original.engine.cheats.noclip)cheats.push('noclip');
  if(cheats.length)await send(cheats.join('; ')); await sleep(500);
  const priming=[]; let quiet=0;
  for(let pass=0;pass<8 && quiet<2;++pass) {
    const visits=[]; for(const yaw of [0,90,180,270])visits.push(await visit(origin,yaw));
    const misses=visits.reduce((n,v)=>n+v.requiredPipelineCompilations+v.firstTextureUploads+v.firstStaticBufferUploads,0);
    quiet=misses?0:quiet+1; priming.push({pass,misses,visits});
    console.log(JSON.stringify({stage:'priming',pass,misses,quiet}));
  }
  assert.equal(quiet,2,'The route did not finish warming within eight passes');
  for(let rep=0;rep<3;++rep)for(const policy of rep%2?['notify','display']:['display','notify']) {
    const buildMonitorStart=buildActivity.length;
    await evaluate(`window.kisak.state.mod._Kisak_SetGpuOptions(${base|(policy==='notify'?64:0)})`); await sleep(500);
    const run={rep,policy,map:original.engine.map,bots:original.engine.bots,build:session.build,startedAt:new Date().toISOString(),
      browserIdentity:await evaluate('({userAgent:navigator.userAgent,cores:navigator.hardwareConcurrency,devicePixelRatio})'),
      resolution:initial.resolution,cacheCondition:'same GPU device, all camera views primed; application/browser caches unchanged',visits:[]};
    for(const yaw of [0,90,180,270])run.visits.push(await visit(origin,yaw));
    const frames=run.visits.flatMap(v=>v.frames),events=run.visits.flatMap(v=>v.events);
    run.summary={intervalMs:summary(run.visits.flatMap(v=>v.intervals)),executorMs:summary(frames.map(f=>f.executorMs)),
      ownerQueueWaitMs:summary(frames.map(f=>f.ownerQueueWaitMs)),gpuMs:summary(events.filter(e=>e.gpuSpanMs!=null).map(e=>e.gpuSpanMs)),
      requiredPipelineCompilations:run.visits.reduce((n,v)=>n+v.requiredPipelineCompilations,0),
      firstTextureUploads:run.visits.reduce((n,v)=>n+v.firstTextureUploads,0),
      firstStaticBufferUploads:run.visits.reduce((n,v)=>n+v.firstStaticBufferUploads,0)};
    if(run.summary.requiredPipelineCompilations || run.summary.firstTextureUploads || run.summary.firstStaticBufferUploads)
      fs.writeFileSync(path.join(output,`${run.map}-${run.bots}-${rep}-${policy}-incomplete.json`),JSON.stringify(run,null,2));
    assert.equal(run.summary.requiredPipelineCompilations,0,'Unwarmed pipelines invalidate this comparison');
    assert.equal(run.summary.firstTextureUploads,0,'Unwarmed texture uploads invalidate this comparison');
    assert.equal(run.summary.firstStaticBufferUploads,0,'Unwarmed static meshes invalidate this comparison');
    run.competingBuilds=buildActivity.slice(buildMonitorStart);
    run.timingQualified=!run.competingBuilds.length && competingBuilds().length===0;
    if(!run.timingQualified) {
      fs.writeFileSync(path.join(output,`${run.map}-${run.bots}-${rep}-${policy}-incomplete.json`),JSON.stringify(run,null,2));
      throw new Error('Competing build or unavailable build monitoring invalidated the timing sample');
    }
    results.push(run); fs.writeFileSync(path.join(output,`${run.map}-${run.bots}-${rep}-${policy}.json`),JSON.stringify(run,null,2));
    fs.writeFileSync(path.join(output,'results.json'),JSON.stringify({priming,results},null,2));
    console.log(JSON.stringify({rep,policy,summary:run.summary}));
  }
} catch(error) {
  fs.writeFileSync(path.join(output,'failure.json'),JSON.stringify({error:String(error),state:await state().catch(()=>null)},null,2)); throw error;
} finally {
  clearInterval(monitorBuilds);
  if(originalFlags!=null)await evaluate(`window.kisak.state.mod._Kisak_SetGpuOptions(${originalFlags})`).catch(()=>{});
  if(original?.engine?.cheats) {
    const current=await state().catch(()=>null);
    if(current?.documentId===original.documentId && current.engine?.map===original.engine.map) {
      const commands=[], e=original.engine;
      if(current.engine.cheats.noclip!==e.cheats.noclip)commands.push('noclip');
      if(current.engine.cheats.god!==e.cheats.god)commands.push('god');
      commands.push(`setviewpos ${e.origin[0]} ${e.origin[1]} ${e.origin[2]+e.cheats.viewHeight} ${e.cameraAngles[1]} ${e.cameraAngles[0]}`);
      await send(commands.join('; ')).catch(()=>{});
    }
  }
  cdp.close();
}

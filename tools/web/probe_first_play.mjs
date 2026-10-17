// Diagnose the first traversal/combat route on an already prepared device.
// This deliberately does not prime the route before measuring it.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {CDP,sleep} from './cdp-client.mjs';
import {routePoints,combatActions} from './gameplay_route.mjs';
const args=process.argv.slice(2), option=(name,fallback)=>{const at=args.indexOf(name);return at<0?fallback:args[at+1];};
const session=JSON.parse(fs.readFileSync(option('--session','build-web/agent-session/session.json'),'utf8'));
assert(session.headed,'Use an owned visible browser');
const output=path.resolve(option('--out','build-web/first-play-probe'));
fs.mkdirSync(output,{recursive:true});
const cdp=await CDP.connect(session.debugPort,session.browserUrl),sid=await cdp.attach(session.targets.host);
const evaluate=expression=>cdp.evaluate(expression,sid),state=()=>evaluate('window.kisak.agent.status()');
const send=command=>evaluate(`window.kisak.state.send(${JSON.stringify(command)})`);
const percentile=(values,p)=>{const sorted=values.slice().sort((a,b)=>a-b);return sorted.length?sorted[Math.min(sorted.length-1,Math.ceil(sorted.length*p)-1)]:null;};
const summarize=values=>({count:values.length,median:percentile(values,.5),p95:percentile(values,.95),p99:percentile(values,.99),over50:values.filter(v=>v>50).length});
const result={build:session.build,profile:session.profile,startedAt:new Date().toISOString(),kind:'same-device first/repeat diagnosis',visits:[]};
let original;
try {
  await cdp.send('Page.bringToFront',{},sid);
  original=await state();
  assert(original.engine?.inputReady && !original.engine.uiActive,'Enter gameplay before probing');
  assert.equal(original.preparation?.status,'complete','Full preparation must finish before this probe');
  result.initial=original;
  const flags=[];
  if(!original.engine.cheats.god)flags.push('god');
  if(!original.engine.cheats.noclip)flags.push('noclip');
  if(flags.length)await send(flags.join('; '));
  await sleep(300);
  const fixture={mp_nuked:[-1634,1192,0],mp_mountain:[2535,-1983,520]}[original.engine.map] || original.engine.origin.map((v,i)=>v+(i===2?original.engine.cheats.viewHeight:0));
  for(const phase of ['first','repeat'])for(const [pointIndex,point] of routePoints(fixture).entries())for(const yaw of [0,90,180,270]) {
    const cursor=await evaluate('window.kisak.state.perf.gpuFrames?.at(-1)?.frameId || 0');
    await send(`setviewpos ${point.join(' ')} ${yaw} 0`);
    await combatActions(cdp,sid,evaluate,pointIndex===0 && yaw===0);
    await sleep(600); // Allow the last telemetry batch to arrive.
    const metrics=await evaluate(`window.kisak.agent.metrics(${cursor})`),current=await state();
    assert(!metrics.historyTruncated,'Telemetry cursor expired');
    assert(!current.crash && current.engine.inputReady && !current.engine.uiActive,'Route lost gameplay');
    assert.equal(current.engine.map,original.engine.map);
    const frames=metrics.gpuFrames.filter(f=>f.frameId>cursor),ids=new Set(frames.map(f=>f.frameId));
    const events=metrics.gpuEvents.filter(e=>ids.has(e.frameId));
    result.visits.push({phase,pointIndex,yaw,camera:current.engine.origin,angles:current.engine.cameraAngles,
      intervals:summarize(frames.map(f=>f.intervalMs)),requiredPipelineCompilations:events.filter(e=>e.requiredPipeline).length,
      firstTextureUploads:frames.reduce((n,f)=>n+(f.firstTextureUploads||0),0),
      firstStaticBufferUploads:frames.reduce((n,f)=>n+(f.firstStaticBufferUploads||0),0),frames,events});
    fs.writeFileSync(path.join(output,'route.json'),JSON.stringify(result,null,2));
    console.log(JSON.stringify({phase,pointIndex,yaw,...result.visits.at(-1).intervals,misses:result.visits.at(-1).requiredPipelineCompilations}));
  }
  result.hitchReport=await evaluate('window.kisak.state.hitchReport()');
  result.blockedAssetRequests=result.hitchReport.events.filter(e=>e.blocked);
  for(const phase of ['first','repeat']) {
    const visits=result.visits.filter(v=>v.phase===phase);
    (result.summary ||= {})[phase]={intervals:summarize(visits.flatMap(v=>v.frames.map(f=>f.intervalMs))),
      requiredPipelineCompilations:visits.reduce((n,v)=>n+v.requiredPipelineCompilations,0),
      firstTextureUploads:visits.reduce((n,v)=>n+v.firstTextureUploads,0),
      firstStaticBufferUploads:visits.reduce((n,v)=>n+v.firstStaticBufferUploads,0)};
  }
  fs.writeFileSync(path.join(output,'route.json'),JSON.stringify(result,null,2));
  const shot=await cdp.send('Page.captureScreenshot',{format:'png',captureBeyondViewport:false},sid);
  fs.writeFileSync(path.join(output,'gameplay.png'),Buffer.from(shot.data,'base64'));
  console.log(JSON.stringify({summary:result.summary,blockedAssets:result.blockedAssetRequests.length}));
} catch(error) {
  fs.writeFileSync(path.join(output,'failure.json'),JSON.stringify({error:String(error),state:await state().catch(()=>null)},null,2));
  throw error;
} finally {
  try {
    const current=await state();
    if(original && current.documentId===original.documentId && current.engine?.inputReady) {
      const commands=[];
      for(const flag of ['god','noclip'])if(current.engine.cheats[flag]!==original.engine.cheats[flag])commands.push(flag);
      const e=original.engine,a=e.cameraAngles;
      commands.push(`setviewpos ${e.origin[0]} ${e.origin[1]} ${e.origin[2]+e.cheats.viewHeight} ${a[1]} ${a[0]}`);
      await send(commands.join('; '));
    }
  } finally { cdp.close(); }
}

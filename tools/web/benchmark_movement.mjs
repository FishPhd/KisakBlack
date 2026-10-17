// Continuous player input, with collision and path evidence. Unlike the camera
// sweep, this exercises movement, visibility changes and first-use work together.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {execFileSync,spawn} from 'node:child_process';
import {CDP,sleep,click,key} from './cdp-client.mjs';
import {movementRoute as route,movementControls as controls,movementInput,movementAction} from './movement_route.mjs';

const args=process.argv.slice(2),option=(name,fallback)=>{const i=args.indexOf(name);return i<0?fallback:args[i+1];};
const session=JSON.parse(fs.readFileSync(option('--session','build-web/agent-session/session.json'),'utf8'));
assert(session.headed,'Use the owned visible browser');
const map=option('--map','mp_nuked'),preparation=option('--preparation','full'),workers=Number(option('--workers','4'));
const submit=option('--submit-wake','display'),bestCase=args.includes('--best-case'),profiling=args.includes('--profile');
assert(/^[a-z0-9_]+$/.test(map));assert(['off','normal','full'].includes(preparation));assert([1,2,4].includes(workers));
assert(['display','notify'].includes(submit));assert(!bestCase||preparation==='full');
const output=path.resolve(option('--out',`build-web/movement-${preparation}-${workers}-${submit}`));fs.mkdirSync(output,{recursive:true});
const cdp=await CDP.connect(session.debugPort,session.browserUrl),sid=await cdp.attach(session.targets.host);
const evaluate=expression=>cdp.evaluate(expression,sid),state=()=>evaluate('window.kisak?.agent?.status() || null');
const send=command=>evaluate(`window.kisak.state.send(${JSON.stringify(command)})`);
const metrics=()=>evaluate('window.kisak.agent.metrics()');
const result={build:session.build,profile:session.profile,startedAt:new Date().toISOString(),map,preparation,workers,submit,bestCase,
  profiling,validation:false,route:'collision-enabled continuous input v1',phases:[],competingBuilds:[]};
const save=()=>fs.writeFileSync(path.join(output,'movement.json'),JSON.stringify(result,null,2));
const percentile=(a,p)=>{const s=a.filter(Number.isFinite).sort((a,b)=>a-b);return s.length?s[Math.min(s.length-1,Math.ceil(s.length*p)-1)]:null;};
const summary=a=>({count:a.length,median:percentile(a,.5),p95:percentile(a,.95),p99:percentile(a,.99),max:percentile(a,1),over50:a.filter(v=>v>50).length,over100:a.filter(v=>v>100).length});
const builds=()=>process.platform!=='win32'?[]:execFileSync('tasklist',['/FO','CSV','/NH'],{encoding:'utf8',windowsHide:true}).split(/\r?\n/).filter(l=>/^"(?:ninja|cmake|clang\+\+|wasm-ld|emcc|em\+\+)\.exe"/i.test(l));
let monitor,original,profileProcess;
async function wait(predicate,label,timeout=180000){
  const until=Date.now()+timeout;
  while(Date.now()<until){const s=await state();if(s?.crash||s?.phase==='failed')throw new Error(`${label}: ${s.crash||s.phase}`);if(s&&predicate(s))return s;await sleep(200);}
  throw new Error(`Timed out: ${label}`);
}
async function select(pattern){
  let item,signature,stableAt=0;
  await wait(s=>{
    item=s.engine?.menus.flatMap(m=>m.items.map(i=>({...i,menu:m.name}))).find(i=>new RegExp(pattern,'i').test(i.text||'')&&i.rect[2]>0&&i.rect[3]>0);
    const next=item&&JSON.stringify([item.menu,item.index,item.rect]);if(next!==signature){signature=next;stableAt=Date.now();}
    return item&&Date.now()-stableAt>700&&s.engineAgeMs<1000;
  },pattern);
  const point=await evaluate(`(()=>{const c=document.getElementById('game'),r=c.getBoundingClientRect(),i=${JSON.stringify(item.rect)},k=Math.min(r.width/c.width,r.height/c.height);return [r.x+(r.width-c.width*k)/2+(i[0]+i[2]/2)*k,r.y+(r.height-c.height*k)/2+(i[1]+i[3]/2)*k];})()`);
  await click(cdp,sid,...point);await sleep(300);
  if((await state()).engine.menus.some(m=>m.name===item.menu))await key(cdp,sid,'Enter',80);
}
try{
  assert.equal(builds().length,0,'Finish competing builds before testing');
  monitor=setInterval(()=>{try{const active=builds();if(active.length)result.competingBuilds.push({at:Date.now(),active});}catch{result.competingBuilds.push({at:Date.now(),unavailable:true});}},5000);monitor.unref();
  await cdp.send('Page.bringToFront',{},sid);
  const url=new URL(session.url);url.searchParams.set('renderer','webgpu');url.searchParams.set('mode','all');url.searchParams.set('res','960x540');
  url.searchParams.set('gpuProfile','1');url.searchParams.set('gpuValidation','0');url.searchParams.set('gpuFallback','0');url.searchParams.set('preparation',preparation);
  url.searchParams.delete('gpuCapture');url.searchParams.delete('gpuEnumerate');url.searchParams.delete('join');
  url.searchParams.set('gpuSubmitWake',submit==='notify'?'1':'0');
  if(bestCase){url.searchParams.set('bestCase','1');url.searchParams.set('assetNetwork','block');}else{url.searchParams.delete('bestCase');url.searchParams.delete('assetNetwork');}
  url.searchParams.set('args',`+set r_smp_worker_threads ${workers} +set web_disableWeaponFiringEffects 0 +set kisak_pm_bots 0 +set com_maxfps 60`);
  if(args.includes('--fresh-recipes'))await evaluate(`(async()=>{const c=window.kisak?.state?.gpuRecipeCache;c?.close();await c?.drain?.();const r=await navigator.storage.getDirectory();await r.removeEntry('gpu-recipes-v2',{recursive:true}).catch(e=>{if(e.name!=='NotFoundError')throw e;});})()`);
  result.applicationRecipeCache=args.includes('--fresh-recipes')?'cleared before launch':'retained';
  await cdp.send('Page.navigate',{url:'about:blank'},sid);await sleep(1500);
  const bootAt=Date.now();await cdp.send('Page.navigate',{url:url.href},sid);await wait(s=>s.menuReady,'frontend');const menuAt=Date.now();
  await send(`set onlinegame 1; set xblive_privatematch 1; set kisak_pm_bots 0; set scr_bots_managed_all 0; set scr_bots_managed_axis 0; set scr_bots_managed_allies 0; set scr_tdm_timelimit 60; devmap ${map}`);
  let s=await wait(s=>s.engine?.map===map&&s.engine.connection===10&&s.engine.menus.some(m=>/team|class/.test(m.name)),'map');
  if(s.engine.menus.some(m=>/team/.test(m.name)))await select('AUTO.?ASSIGN');
  await wait(s=>s.engine?.menus.some(m=>/class/.test(m.name)),'class');const classAt=Date.now();await select('^SMG$');
  original=await wait(s=>s.engine?.inputReady&&!s.engine.uiActive,'player input');
  const initial=await metrics();assert.equal(initial.rendererBackend,'webgpu');
  result.url=url.href;result.initial=original;result.catalog=initial.catalog;result.loading={bootToMenuMs:menuAt-bootAt,mapToInputMs:Date.now()-menuAt,classToInputMs:Date.now()-classAt};
  if(bestCase)assert.equal(original.preparation?.status,'complete','Best-case catalog is not compiled; do not claim full preparation');
  const flags=[];if(!original.engine.cheats.god)flags.push('god');if(original.engine.cheats.noclip)flags.push('noclip');if(flags.length)await send(flags.join('; '));
  await wait(s=>s.engine.cheats.god&&!s.engine.cheats.noclip,'collision-enabled player');
  await evaluate(`(()=>{const m=kisak.state.mod,f=m._Kisak_Key;kisak.state.movementKeys=[];m._Kisak_Key=(k,d)=>{kisak.state.movementKeys.push({key:k,down:d,at:Date.now()});return f(k,d);};document.getElementById('game').focus();})()`);
  const start=(await state()).engine;
  for(const phase of ['first','repeat','repeat2']){
    if(phase!=='first'){
      await send(`setviewpos ${start.origin[0]} ${start.origin[1]} ${start.origin[2]+start.cheats.viewHeight} ${start.cameraAngles[1]} ${start.cameraAngles[0]}`);
      await sleep(1000);
    }
    const before=await metrics(),run={phase,at:Date.now(),actions:[]};
    if(profiling&&phase==='first'){
      profileProcess=spawn(process.execPath,['tools/web/profile_renderer.mjs','30',option('--session','build-web/agent-session/session.json'),path.join(output,'cpu-profile')],{stdio:['ignore',fs.openSync(path.join(output,'profile.log'),'w'),'inherit'],windowsHide:true});
      profileProcess.completion=new Promise(resolve=>{profileProcess.on('error',e=>resolve({error:String(e)}));profileProcess.on('exit',code=>resolve({code}));});
    }
    console.log(JSON.stringify({stage:'moving',phase,preparation,workers,submit,catalogReady:initial.catalog?.ok}));
    for(const action of route)run.actions.push(await movementAction(cdp,sid,state,map,action));
    run.end=Date.now();await sleep(1200);const after=await metrics();
    run.frames=(after.gpuFrames||[]).filter(f=>f.at>=run.at&&f.at<=run.end);const ids=new Set(run.frames.map(f=>f.frameId));
    run.events=(after.gpuEvents||[]).filter(e=>ids.has(e.frameId));
    run.assetWork=(await evaluate('window.kisak.state.hitchReport()')).events.filter(e=>e.at<=run.end&&(e.end??e.at)>=run.at&&e.kind!=='pipeline-use');
    run.summary={intervalMs:summary(run.frames.map(f=>f.intervalMs)),executorMs:summary(run.frames.map(f=>f.executorMs)),producerQueueWaitMs:summary(run.frames.map(f=>f.producerQueueWaitMs)),
      ownerQueueWaitMs:summary(run.frames.map(f=>f.ownerQueueWaitMs)),translationMs:summary(run.frames.map(f=>f.translationMs)),gpuMs:summary(run.events.filter(e=>e.gpuSpanMs!=null).map(e=>e.gpuSpanMs)),
      requiredPipelineCompilations:run.events.filter(e=>e.requiredPipeline).length,firstTextureUploads:run.frames.reduce((n,f)=>n+(f.firstTextureUploads||0),0),
      firstStaticBufferUploads:run.frames.reduce((n,f)=>n+(f.firstStaticBufferUploads||0),0),pathLength:run.actions.reduce((n,a)=>n+a.pathLength,0),
      transferredBytes:after.transferred-before.transferred};
    run.hitches=(await evaluate('window.kisak.state.hitchReport()')).hitches.filter(f=>ids.has(f.frameId));
    assert(run.frames.length>100,'No meaningful GPU frame sample');assert(run.summary.pathLength>128,'Player did not actually move; route is invalid');
    const changing=run.actions.some(a=>Math.abs(a.after.angles[1]-a.before.angles[1])>20);assert(changing,'Camera did not turn');
    result.phases.push(run);result.keys=await evaluate('window.kisak.state.movementKeys');save();console.log(JSON.stringify({phase,summary:run.summary}));
  }
  result.hitchReport=await evaluate('window.kisak.state.hitchReport()');result.final=await state();
  result.timingQualified=!profiling&&!result.competingBuilds.length&&!builds().length;
  if(profileProcess)result.cpuProfile=await profileProcess.completion;
  save();const shot=await cdp.send('Page.captureScreenshot',{format:'png'},sid);fs.writeFileSync(path.join(output,'gameplay.png'),Buffer.from(shot.data,'base64'));
}catch(error){result.error=String(error);result.final=await state().catch(()=>null);save();throw error;}
finally{clearInterval(monitor);for(const code of Object.keys(controls))await movementInput(cdp,sid,code,false).catch(()=>{});cdp.close();}

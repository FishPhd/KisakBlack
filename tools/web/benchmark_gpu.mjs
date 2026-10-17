// Owned visible browser benchmark. Camera fixtures, cache state, bot count and
// build hashes are retained; first and repeated visits are separate samples.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {CDP, sleep, click, key} from './cdp-client.mjs';
import {routePoints, combatActions, weaponCoverage} from './gameplay_route.mjs';
const args = process.argv.slice(2);
const option = (name, fallback) => { const at=args.indexOf(name); return at<0 ? fallback : args[at+1]; };
const sessionPath = path.resolve(option('--session','build-web/agent-session/session.json'));
const session = JSON.parse(fs.readFileSync(sessionPath,'utf8'));
const manifest = JSON.parse(fs.readFileSync(path.join(session.gen,'manifest.json'),'utf8'));
const availableMaps = Object.keys(manifest.client?.maps || {});
const repetitions = Number(option('--repetitions','3')), dwell = Number(option('--dwell','2'));
const maxFps = Number(option('--maxfps','60'));
assert(Number.isInteger(maxFps) && maxFps >= 0 && maxFps <= 1000);
const bestCase = args.includes('--best-case');
const mapOnly = args.includes('--map-only');
assert(!(mapOnly && args.includes('--weapons')),'--map-only excludes the weapon fixture');
const combat = bestCase || args.includes('--combat');
function competingBuilds() {
  if (process.platform !== 'win32') return [];
  const listing = execFileSync('tasklist',['/FO','CSV','/NH'],{encoding:'utf8',windowsHide:true});
  return listing.split(/\r?\n/).filter(line=>/^"(?:ninja|cmake|clang\+\+|wasm-ld|emcc|em\+\+)\.exe"/i.test(line));
}
const buildActivity = [];
const monitorBuilds = setInterval(()=>{try {const active=competingBuilds();if(active.length)buildActivity.push({at:Date.now(),active});}catch{buildActivity.push({at:Date.now(),unavailable:true});}},5000);
monitorBuilds.unref();
assert.equal(competingBuilds().length,0,'Finish competing builds before measuring frame times');
const requestedMaps = option('--maps','mp_nuked,mp_mountain');
const maps = requestedMaps === 'all' ? availableMaps : requestedMaps.split(','), botsList = option('--bots','0,11').split(',').map(Number);
const assetModes = option('--asset-modes',bestCase?'all':'lazy').split(',');
assert(assetModes.every(mode=>['lazy','bulk','all'].includes(mode)));
const traversal = bestCase || args.includes('--traversal');
const preparations = option('--preparation',bestCase?'full':'off,normal,full').split(',');
assert(preparations.every(p => ['full','normal','off'].includes(p)));
if(bestCase)assert(assetModes.every(m=>m==='all') && preparations.every(p=>p==='full'),'Best-case requires all assets and full preparation');
const jobBudgets = option('--preparation-jobs','8').split(',').map(Number);
assert(jobBudgets.every(n => [4,8,16].includes(n)));
const bindingPolicies = option('--texture-bindings','stable').split(',');
assert(bindingPolicies.every(p => ['legacy','stable'].includes(p)));
const submitPolicies = option('--submit-wake','display').split(',');
assert(submitPolicies.every(p => ['display','notify'].includes(p)));
const cacheSelection = option('--cache',args.includes('--ab') ? 'cached' : 'both');
assert(['fresh','cached','both'].includes(cacheSelection));
assert(repetitions>=1 && repetitions<=3 && dwell>=1 && dwell<=10);
assert(maps.length && maps.every(m=>availableMaps.includes(m)) && botsList.every(n=>[0,11].includes(n)));
const directory = path.resolve(option('--out',path.join(path.dirname(sessionPath),'gpu-benchmark')));
fs.mkdirSync(directory,{recursive:true});
const cdp = await CDP.connect(session.debugPort,session.browserUrl), sid = await cdp.attach(session.targets.host);
const fixtures = {mp_nuked:[-1634,1192,0],mp_mountain:[2535,-1983,520]};
const results = [];
const state = () => cdp.evaluate('window.kisak?.agent?.status() || null',sid);
const evaluate = expression => cdp.evaluate(expression,sid);
const send = command => evaluate(`window.kisak.state.send(${JSON.stringify(command)})`);
let restore;
async function restoreGameplay() {
  if (!restore) return;
  const saved=restore; restore=null;
  const current=await state();
  if (current.documentId!==saved.documentId || current.engine?.connection!==10 || current.engine.map!==saved.engine.map) return;
  const commands=[];
  if (current.engine.cheats?.noclip!==saved.engine.cheats.noclip) commands.push('noclip');
  if (current.engine.cheats?.god!==saved.engine.cheats.god) commands.push('god');
  const e=saved.engine, a=e.cameraAngles;
  commands.push(`setviewpos ${e.origin[0]} ${e.origin[1]} ${e.origin[2]+e.cheats.viewHeight} ${a[1]} ${a[0]}`);
  await send(commands.join('; '));
  await wait(s=>s.engine?.cheats?.noclip===e.cheats.noclip && s.engine.cheats.god===e.cheats.god,'restore gameplay',5000);
}
async function wait(predicate, label, timeout=900000) {
  const deadline=Date.now()+timeout;
  while(Date.now()<deadline) {
    if(cdp.ws.readyState!==WebSocket.OPEN) throw new Error(`${label}: test browser closed`);
    const s=await state().catch(()=>null);
    if(s?.crash || s?.phase==='failed') throw new Error(`${label}: ${s.crash || s.phase}`);
    if(s && predicate(s)) return s;
    await sleep(200);
  }
  throw new Error(`Timeout: ${label}`);
}
async function select(pattern) {
  let item, previous, stableAt=0;
  const expression=new RegExp(pattern,'i');
  await wait(s=>{
    item=s.engine?.menus.flatMap(m=>m.items.map(i=>({...i,menu:m.name}))).find(i=>expression.test(i.text||'') && i.rect[2]>0 && i.rect[3]>0);
    const signature=item?JSON.stringify([item.menu,item.index,item.rect]):null;
    if(signature!==previous) stableAt=Date.now();
    previous=signature;
    return signature && Date.now()-stableAt>=700 && s.engineAgeMs<1000;
  },pattern);
  const point=await evaluate(`(() => {const c=document.getElementById('game'),r=c.getBoundingClientRect(),i=${JSON.stringify(item.rect)};
    const k=Math.min(r.width/c.width,r.height/c.height);return [r.x+(r.width-c.width*k)/2+(i[0]+i[2]/2)*k,r.y+(r.height-c.height*k)/2+(i[1]+i[3]/2)*k];})()`);
  await click(cdp,sid,...point);
  await sleep(300);
  if((await state()).engine.menus.some(m=>m.name===item.menu)) await key(cdp,sid,'Enter',100);
}
async function clearRecipes() {
  await evaluate(`(async()=>{const root=await navigator.storage.getDirectory();
    const cache=window.kisak?.state?.gpuRecipeCache; cache?.close(); await cache?.drain?.();
    try {await root.removeEntry('gpu-recipes-v2',{recursive:true});
    } catch(error) {if(error.name!=='NotFoundError') throw error;}})()`);
}
const percentile = (values,p) => { if(!values.length) return null; const a=[...values].sort((x,y)=>x-y);return a[Math.min(a.length-1,Math.ceil(a.length*p)-1)]; };
const summary = values => ({median:percentile(values,.5),p95:percentile(values,.95),p99:percentile(values,.99),over50:values.filter(v=>v>50).length});
async function screenshot(name) {
  const shot=await cdp.send('Page.captureScreenshot',{format:'png'},sid);
  fs.writeFileSync(path.join(directory,name),Buffer.from(shot.data,'base64'));
}
try {
  await cdp.send('Page.bringToFront',{},sid);
  const cores = await evaluate('navigator.hardwareConcurrency || 2');
  const workerBudget = Math.max(1, Math.min(4, cores - 2));
  let variants=args.includes('--ab') ? [
    {name:'default',workers:4,wake:1,depth:1,uploads:1},
    {name:'workers1',workers:1,wake:1,depth:1,uploads:1},
    {name:'workers2',workers:2,wake:1,depth:1,uploads:1},
    {name:'display-tick',workers:4,wake:0,depth:1,uploads:1},
    {name:'previous-depth',workers:4,wake:1,depth:0,uploads:1},
    {name:'ordered-uploads',workers:4,wake:1,depth:1,uploads:0},
  ] : [{name:'default',workers:4,wake:1,depth:1,uploads:1}];
  if (args.includes('--workers')) variants = option('--workers','1,2,4').split(',').map(Number).map(workers => {
    assert([1,2,4].includes(workers) && workers <= workerBudget,'Worker request exceeds available browser core budget');
    return {name:`workers${workers}`,workers,wake:1,depth:1,uploads:1};
  });
  variants = variants.map(v => ({...v,workers:Math.min(v.workers,workerBudget)}));
  for(const assetMode of assetModes) for(const jobBudget of jobBudgets) for(const preparation of preparations) for(const map of maps) for(const bots of botsList) for(let rep=0;rep<repetitions;rep++) for(const bindingPolicy of bindingPolicies) for(const submitPolicy of submitPolicies) for(const variant of variants)
  for(const cache of cacheSelection === 'both' ? ['fresh','cached'] : [cacheSelection]) {
    const buildMonitorStart=buildActivity.length;
    console.log(JSON.stringify({stage:'loading',variant:variant.name,map,bots,rep,cache}));
    if(cache==='fresh') await clearRecipes();
    // Release the previous Wasm heap before booting another match. A second
    // engine page used solely to clear OPFS could briefly retain two heaps.
    await cdp.send('Page.navigate',{url:'about:blank'},sid);await sleep(1500);
    const url=new URL(session.url);url.searchParams.set('renderer','webgpu');url.searchParams.set('gpuProfile','1');
    url.searchParams.delete('gpuCapture'); url.searchParams.delete('gpuEnumerate');
    if(bestCase)url.searchParams.set('bestCase','1'); else url.searchParams.delete('bestCase');
    url.searchParams.set('res','960x540');url.searchParams.set('preparation',preparation);
    url.searchParams.set('mode',assetMode);
    if(bestCase || args.includes('--block-assets'))url.searchParams.set('assetNetwork','block');
    url.searchParams.set('preparationJobs',String(jobBudget));
    url.searchParams.set('gpuTextureBindings',bindingPolicy === 'stable' ? '1' : '0');
    url.searchParams.set('gpuSubmitWake',submitPolicy === 'notify' ? '1' : '0');
    // Validation runs are separate from release-performance measurements.
    url.searchParams.set('gpuValidation',args.includes('--validation')?'1':'0');url.searchParams.set('gpuFallback','0');
    url.searchParams.set('gpuWake',String(variant.wake));url.searchParams.set('gpuDepthMerge',String(variant.depth));url.searchParams.set('gpuUploadBatch',String(variant.uploads));
    url.searchParams.set('args',`+set r_smp_worker_threads ${variant.workers} +set web_warmShaders 1 +set web_disableWeaponFiringEffects 0 +set cg_drawFPS 1 +set kisak_pm_bots 0 +set com_maxfps ${maxFps}`);
    const bootAt=performance.now();
    await cdp.send('Page.navigate',{url:url.href},sid); await wait(s=>s.menuReady,'menu');
    const menuAt=performance.now();
    assert.equal(await evaluate('window.kisak.state.rendererBackend'),'webgpu');
    const mapAt=performance.now();
    await send(`set onlinegame 1; set xblive_privatematch 1; set kisak_pm_bots 0; set scr_bots_managed_all 0; set scr_bots_managed_axis 0; set scr_bots_managed_allies 0; set scr_tdm_timelimit 60; devmap ${map}`);
    await wait(s=>s.engine?.connection===10 && s.engine.map===map,'map');
    let s=await wait(s=>s.engine.menus.some(m=>/team|class/.test(m.name)),'join menus');
    const gate=await evaluate('window.kisak.agent.metrics()');
    if(bestCase)assert.equal(gate.preparation?.status,'complete','Best-case preparation did not complete');
    if(s.engine.menus.some(m=>/team/.test(m.name))) await select('AUTO.?ASSIGN');
    const classState=await wait(s=>s.engine.menus.some(m=>/class/.test(m.name)),'class');
    const classAt=performance.now();
    const classMenuName=classState.engine.menus.find(m=>/class/.test(m.name)).name;
    await select('^SMG$');
    await wait(s=>s.engine?.connection===10 && !s.engine.uiActive,'class menu closed');
    const menuClosedAt=performance.now();
    restore=await wait(s=>s.engine?.inputReady && !s.engine.uiActive,'input');
    const inputAt=performance.now();
    assert(restore.engine.cheats,'Rebuild the agent snapshot with cheat state');
    const enable=[];
    if (!restore.engine.cheats.god) enable.push('god');
    if (!restore.engine.cheats.noclip) enable.push('noclip');
    if (enable.length) await send(enable.join('; '));
    await wait(s=>s.engine?.cheats?.noclip && s.engine.cheats.god,'benchmark camera');
    if(bots) {await send(`addbots ${bots}`);await wait(s=>s.engine?.bots===bots,'bots');}
    if (combat) await sleep(600); // Let the first weapon finish raising.
    const initial=await evaluate('window.kisak.agent.metrics()');
    assert.equal(initial.resolution,'960x540');
    const base = fixtures[map] || initial.engine.origin.map((v,i)=>v+(i===2?initial.engine.cheats.viewHeight:0));
    const name=`${assetMode}-${traversal?'traversal-':''}${combat?'combat':'camera'}-${submitPolicy}-${bindingPolicy}-${preparation}-jobs${jobBudget}-fps${maxFps}-${variant.name}-${map}-${bots}-${rep}-${cache}`;
    const run={name,map,bots,rep,cache,assetMode,traversal,preparation,jobBudget,maxFps,combat,bindingPolicy,submitPolicy,cores,cacheMeaning:'application recipes only; browser and driver caches uncontrolled; asset mode does not clear existing assets',variant,build:session.build,browser:session.browser,profile:session.profile,resolution:initial.resolution,
      origin:base,weapon:initial.engine.weapon,warmup:initial.warmupEvents || [],logs:await evaluate('window.kisak.state.records.filter(r=>/WEB_GPU_(WARMUP|RECIPE_CACHE|BUILTIN)/.test(r.text)).map(r=>r.text)'),visits:[]};
    run.startedAt=new Date().toISOString(); run.url=url.href; run.bestCase=bestCase;
    run.loading={bootToMenuMs:menuAt-bootAt,mapToClassMs:classAt-mapAt,classToInputMs:inputAt-classAt,
      classSelectionMs:menuClosedAt-classAt,postMenuInputWaitMs:inputAt-menuClosedAt,
      mapToInputMs:inputAt-mapAt,bootToInputMs:inputAt-bootAt,
      controlPolicy:'CDP menu selections include the same 700 ms stable-item wait in each run',bootPhases:initial.timings};
    run.fastfilePhases=initial.fastfiles;
    const gateFrame=gate.gpuFrames.at(-1)?.frameId || 0;
    run.entryFrames=initial.gpuFrames.filter(f=>f.frameId>gateFrame);
    run.entryEvents=initial.gpuEvents.filter(e=>e.frameId>gateFrame);
    run.buildMonitorStart=buildMonitorStart;
    run.browserIdentity=await evaluate('({userAgent:navigator.userAgent,devicePixelRatio})');
    run.catalog=await evaluate('window.kisak.state.catalog || null');
    run.preparationProgressSpanMs=run.warmup.length>1?run.warmup.at(-1).at-run.warmup[0].at:null;
    console.log(JSON.stringify({stage:'sweep',name,preparationProgressSpanMs:run.preparationProgressSpanMs}));
    for(const phase of ['first','repeat']) for(const [pointIndex,point] of (traversal?routePoints(base):[base]).entries()) for(const yaw of [0,90,180,270]) {
      const before=await evaluate('window.kisak.agent.metrics()');const lastId=before.gpuFrames.at(-1)?.frameId||0;
      await send(`setviewpos ${point.join(' ')} ${yaw} 0`);
      if (combat) {
        await combatActions(cdp,sid,evaluate,traversal && pointIndex===0 && yaw===0,250,pointIndex===0 && yaw===0);
      }
      await sleep(dwell*1000+250);
      const after=await evaluate('window.kisak.agent.metrics()');
      const current=await state();assert.equal(current.engine.map,map);assert.equal(current.engine.bots,bots);
      assert(Math.abs(current.engine.origin[0]-point[0])<.5 && Math.abs(current.engine.origin[1]-point[1])<.5,
        'Camera moved during measurement; discard this sample');
      const angles=current.engine.cameraAngles;
      assert(angles,'Rebuild the agent snapshot with effective camera angles');
      const deltaYaw=((angles[1]-yaw+540)%360)-180;
      assert(Math.abs(deltaYaw)<1 && Math.abs(angles[0])<1,'Camera look changed during measurement');
      assert.equal(after.visibilityChanges,before.visibilityChanges);
      const frames=after.gpuFrames.filter(f=>f.frameId>lastId);
      const ids=new Set(frames.map(f=>f.frameId)); const events=after.gpuEvents.filter(e=>ids.has(e.frameId));
      const sample={phase,yaw,pointIndex,camera:{origin:current.engine.origin,angles,inputAngles:current.engine.angles},gpuCounters:after.gpu,
        intervalMs:summary(after.renderIntervals.slice(before.renderIntervals.length)),executorMs:summary(frames.map(f=>f.executorMs)),
        gpuMs:summary(events.filter(e=>e.gpuSpanMs!=null).map(e=>e.gpuSpanMs)),
        pipelineWaitMs:summary(frames.filter(f=>f.pipelineMisses).map(f=>f.pipelineWaitMs)),
        requiredPipelineCompilations:events.filter(e=>e.requiredPipeline).length,
        firstTextureUploads:frames.reduce((n,f)=>n+(f.firstTextureUploads||0),0),firstStaticBufferUploads:frames.reduce((n,f)=>n+(f.firstStaticBufferUploads||0),0),frames,events};
      run.visits.push(sample);if(args.includes('--screenshots')){await screenshot(`${name}-${phase}-${pointIndex}-${yaw}.png`);await sleep(500);}
    }
    if (((bestCase && !mapOnly) || args.includes('--weapons')) && map===maps[0] && bots===botsList[0] && rep===0 && cache===(cacheSelection==='both'?'fresh':cacheSelection)) {
      let last=(await evaluate('window.kisak.agent.metrics()')).gpuFrames.at(-1)?.frameId || 0;
      run.weaponVisits=[];
      run.weapons=await weaponCoverage(cdp,sid,evaluate,send,async weapon=>{
        const metrics=await evaluate(`window.kisak.agent.metrics(${last})`);
        assert(!metrics.historyTruncated,'Weapon coverage history expired');
        last=metrics.gpuFrames.at(-1)?.frameId || last;
        run.weaponVisits.push({weapon,frames:metrics.gpuFrames,events:metrics.gpuEvents});
      });
    }
    if(args.includes('--check-loading')) {
      const beforeClass=await evaluate('window.kisak.agent.metrics()');
      const classFrame=beforeClass.gpuFrames.at(-1)?.frameId || 0;
      assert(/^[a-zA-Z0-9_]+$/.test(classMenuName));
      await send(`openmenu ${classMenuName}`);
      await wait(s=>s.engine.menus.some(m=>m.name===classMenuName),'reopened class menu');
      await select('^SMG$');
      await wait(s=>s.engine?.inputReady && !s.engine.uiActive,'same class selected');
      const expectedWarm=preparation==='off'?'0':'1';
      await send(`set web_warmShaders ${expectedWarm==='1'?0:1}`);
      await wait(s=>s.engine?.graphics?.settings?.web_warmShaders===expectedWarm,'managed preparation overrides legacy toggle');
      await sleep(3000);
      const afterClass=await evaluate(`window.kisak.agent.metrics(${classFrame})`);
      assert(!afterClass.historyTruncated,'Class selection history expired');
      assert(!afterClass.preparation?.active,'Class selection restarted map preparation');
      assert.equal(afterClass.warmupEvents.length,beforeClass.warmupEvents.length,'Class selection submitted another preparation gate');
      run.classCheck={frames:afterClass.gpuFrames,events:afterClass.gpuEvents};
    }
    const gates=await evaluate(`window.kisak.state.perf.warmupEvents.filter(e=>e.map===${JSON.stringify(map)} && !e.active)`);
    run.completedMapGates=gates.length;
    assert.equal(gates.length,preparation==='off'?0:1,'Map preparation restarted after loading');
    run.measurementPolicy={screenshots:args.includes('--screenshots'),reloadAndEquipment:'first pose of each traversal',dwellSeconds:dwell};
    run.cacheWrites=await evaluate('window.kisak.state.perf.cacheWrites || []');
    run.hitchReport=await evaluate('window.kisak.state.hitchReport()');
    run.memory=run.hitchReport.memory;
    run.validationEnabled=args.includes('--validation');
    run.timingQualified=buildActivity.length===run.buildMonitorStart && competingBuilds().length===0;
    run.performanceQualified=run.timingQualified && !run.validationEnabled;
    run.competingBuilds=buildActivity.slice(run.buildMonitorStart);
    const weaponEvents=[...(run.weaponVisits || []).flatMap(v=>v.events),...(run.classCheck?.events || [])],
      weaponFrames=[...(run.weaponVisits || []).flatMap(v=>v.frames),...(run.classCheck?.frames || [])];
    const pipelineMisses=weaponEvents.filter(e=>e.requiredPipeline).length+run.entryEvents.filter(e=>e.requiredPipeline).length+run.visits.reduce((n,v)=>n+v.requiredPipelineCompilations,0);
    const textureUploads=weaponFrames.reduce((n,f)=>n+(f.firstTextureUploads||0),0)+run.entryFrames.reduce((n,f)=>n+(f.firstTextureUploads||0),0)+run.visits.reduce((n,v)=>n+v.firstTextureUploads,0);
    const meshUploads=weaponFrames.reduce((n,f)=>n+(f.firstStaticBufferUploads||0),0)+run.entryFrames.reduce((n,f)=>n+(f.firstStaticBufferUploads||0),0)+run.visits.reduce((n,v)=>n+v.firstStaticBufferUploads,0);
    run.assetNetworkCounters=run.hitchReport.assetNetworkCounters;
    const assetNetworkMeasured=Number.isSafeInteger(run.assetNetworkCounters?.blockedRequests);
    const blocked=assetNetworkMeasured?run.assetNetworkCounters.blockedRequests:run.hitchReport.events.filter(e=>e.blocked).length;
    const uncoveredWeapons=(run.weapons || []).filter(w=>w.uncovered).map(w=>({name:w.name,reason:w.uncovered}));
    run.coverage={scope:run.weapons?'map route and registered weapon fixture':'map route only',weaponsExercised:!!run.weapons,
      pipelineMisses,textureUploads,meshUploads,blockedRequests:blocked,assetNetworkMeasured,uncoveredWeapons,
      verified:bestCase && assetNetworkMeasured && run.catalog?.ok===true && !pipelineMisses && !textureUploads && !meshUploads && !blocked && !uncoveredWeapons.length};
    await evaluate(`(() => {const s=window.kisak.state;if(s.installation){
      s.installation.coverageVerified=${run.coverage.verified};
      s.installation.coverageReceipt={map:${JSON.stringify(map)},profile:s.catalog?.profile,
        catalogIdentity:s.catalog?.catalogIdentity,scope:${JSON.stringify(run.coverage.scope)}};}})()`);
    fs.writeFileSync(path.join(directory,name+'.json'),JSON.stringify(run,null,2));
    const {entryFrames,entryEvents,weaponVisits,classCheck,hitchReport,visits,...metadata}=run;
    results.push({...metadata,artifact:name+'.json',visits:visits.map(({frames,events,...v})=>v)});
    fs.writeFileSync(path.join(directory,'results.json'),JSON.stringify(results.map(({visits,...r})=>({...r,visits:visits.map(({frames,events,...s})=>s)})),null,2));
    console.log(JSON.stringify({name,visits:run.visits.map(({phase,yaw,intervalMs,executorMs,gpuMs})=>({phase,yaw,intervalMs,executorMs,gpuMs}))}));
    // Allow the idle executor and OPFS writer to persist learned recipes.
    await sleep(5500);
    await restoreGameplay();
  }
} catch(error) {
  fs.writeFileSync(path.join(directory,'failure.json'),JSON.stringify({error:String(error),state:await state().catch(()=>null)},null,2));
  if(cdp.ws.readyState===WebSocket.OPEN) await screenshot('failure.png').catch(()=>{});
  throw error;
} finally {
  clearInterval(monitorBuilds);
  try { if(cdp.ws.readyState===WebSocket.OPEN) await restoreGameplay(); }
  finally { cdp.close(); }
}

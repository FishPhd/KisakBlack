// Capture portable recipes on the owned visible browser. Regenerate after a
// renderer/translator or asset change; the catalog is specific to its profile.
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {CDP, sleep, click, key} from './cdp-client.mjs';
import {assetIdentity, CATALOG_VERSION} from '../../web/gpu-preparation.js';
import {compactCatalog} from './compact_gpu_catalog.mjs';
import {CatalogPacker} from './gpu_catalog_codec.mjs';
import {routePoints, combatActions, weaponCoverage} from './gameplay_route.mjs';
import {movementRoute,movementAction} from './movement_route.mjs';

const args = process.argv.slice(2);
const option = (name, fallback) => { const at = args.indexOf(name); return at < 0 ? fallback : args[at + 1]; };
const sessionFile = path.resolve(option('--session', 'build-web/agent-session/session.json'));
const session = JSON.parse(fs.readFileSync(sessionFile, 'utf8'));
if (!session.headed) throw new Error('Catalog capture requires an owned visible browser (launch --headed).');
const directory = path.resolve(option('--out', session.gen));
const manifestPath = path.join(directory, 'manifest.json');
const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
const availableMaps = Object.keys(manifest.client?.maps || {}).sort();
const priority = ['mp_nuked','mp_mountain','mp_cairo'];
const maps = [...priority.filter(m=>availableMaps.includes(m)),...availableMaps.filter(m=>!priority.includes(m))];
if (!maps.length) throw new Error('Manifest contains no multiplayer client maps');
const cdp = await CDP.connect(session.debugPort, session.browserUrl);
const sid = await cdp.attach(session.targets.host);
const evaluate = expression => cdp.evaluate(expression, sid);
const send = command => evaluate(`window.kisak.state.send(${JSON.stringify(command)})`);
const capturePath = path.join(directory,'gpu-catalog','capture.json');
const identity = await assetIdentity(manifest);
const evidence = args.includes('--resume') ? JSON.parse(fs.readFileSync(capturePath,'utf8')) :
  {build:session.build,assetIdentity:identity,routeVersion:'exact-weapons-effects-walking-v6',maps:[],profiles:[],startedAt:new Date().toISOString()};
if (JSON.stringify(evidence.build) !== JSON.stringify(session.build) || evidence.assetIdentity !== identity || evidence.routeVersion !== 'exact-weapons-effects-walking-v6')
  throw new Error('Resume requires the identical build, page shell and asset generation');
fs.mkdirSync(path.join(directory, 'gpu-catalog'), {recursive: true});
async function wait(test, label) {
  const until = Date.now() + 900000;
  let reportAt = 0;
  while (Date.now() < until) {
    const state = await evaluate('window.kisak?.agent?.status() || null').catch(() => null);
    if (state?.crash || state?.phase === 'failed') throw new Error(`${label}: ${state.crash || state.phase}`);
    if (state && test(state)) return state;
    if (Date.now() - reportAt >= 15000) {
      reportAt = Date.now();
      console.log(JSON.stringify({stage:'waiting',label,preparation:state?.preparation,engine:state?.engine && {
        map:state.engine.map,connection:state.engine.connection,inputReady:state.engine.inputReady,menus:state.engine.menus?.map(m=>m.name)}}));
    }
    await sleep(250);
  }
  throw new Error(`Timed out: ${label}`);
}
async function select(pattern) {
  let item, previous, stableAt = 0;
  await wait(s => {
    item = s.engine?.menus.flatMap(m => m.items.map(i => ({...i,menu:m.name}))).find(i => new RegExp(pattern, 'i').test(i.text || '') && i.rect[2] > 0);
    const signature = item ? JSON.stringify([item.menu,item.index,item.rect]) : null;
    if (signature !== previous) stableAt = Date.now();
    previous = signature;
    return signature && Date.now()-stableAt >= 700 && s.engineAgeMs < 1000;
  }, pattern);
  const point = await evaluate(`(() => {const c=document.getElementById('game'), r=c.getBoundingClientRect(), i=${JSON.stringify(item.rect)};
    const k=Math.min(r.width/c.width,r.height/c.height); return [r.x+(r.width-c.width*k)/2+(i[0]+i[2]/2)*k,r.y+(r.height-c.height*k)/2+(i[1]+i[3]/2)*k];})()`);
  await click(cdp, sid, ...point); await sleep(300);
  if ((await evaluate('window.kisak.agent.status()')).engine.menus.some(m => m.name === item.menu)) await key(cdp, sid, 'Enter', 80);
}
async function collect(map) {
  let name;
  const until = Date.now()+120000;
  while (Date.now()<until) {
    name = await evaluate(`(() => { const s=window.kisak.state, name=Object.keys(s.gpuRecipeExports).find(k=>k.endsWith(${JSON.stringify('/'+map)}));
      return name && (s.gpuRecipeExportCounts[name]||0)>=(s.perf.gpu?.cacheQueuedRecipes||0) &&
        (s.gpuRecipeExportObserved[name]?.length||0)>0 &&
        (s.gpuRecipeExportObserved[name]?.length||0)>=(s.perf.gpu?.cacheQueuedObserved||0) ? name : null; })()`);
    if (name) break;
    await sleep(250);
  }
  if (!name) throw new Error(`No exported recipe chunks for ${map}`);
  const observed=await evaluate(`window.kisak.state.gpuRecipeExportObserved[${JSON.stringify(name)}]`);
  const learning=await evaluate(`(async()=>{const s=window.kisak.state;
    s.gpuCapturedLearning=await s.gpuRecipeCache.readLearning(${JSON.stringify(name)});
    return {count:s.gpuCapturedLearning.chunks.length,observed:s.gpuCapturedLearning.observed,complete:s.gpuCapturedLearning.complete};})()`);
  const count = await evaluate(`window.kisak.state.gpuRecipeExports[${JSON.stringify(name)}].length`);
  const chunks = [], historicalChunks = [], historical = new CatalogPacker();
  let validLearning=learning.complete;
  for (let i = 0; i < count+learning.count; ++i) {
    const encoded = await evaluate(`(() => {const s=window.kisak.state,bytes=${i}<${count}?
      s.gpuRecipeExports[${JSON.stringify(name)}][${i}]:s.gpuCapturedLearning.chunks[${i-count}];
      let binary=''; for(let i=0;i<bytes.length;i+=8192) binary+=String.fromCharCode(...bytes.subarray(i,i+8192)); return btoa(binary);})()`);
    const bytes = Buffer.from(encoded, 'base64');
    const hash = createHash('sha256').update(bytes).digest('hex');
    const relative = `gpu-catalog/${hash}.bin`;
    fs.writeFileSync(path.join(directory, relative), bytes);
    const chunk={path: relative, size: bytes.length, sha256: hash};
    if(i<count)chunks.push(chunk);
    else {
      try { historical.add(bytes); } catch { validLearning=false; }
      historicalChunks.push(chunk);
    }
  }
  if(validLearning && learning.observed.length) {
    try { historical.selectRecordedFamilies(learning.observed); } catch { validLearning=false; }
  }
  if(validLearning)chunks.push(...historicalChunks);
  const profileKey = name.slice(0, -map.length - 1);
  await evaluate(`(() => { const s=window.kisak.state; (s.gpuRecipeExportClosed ??= new Set()).add(${JSON.stringify(name)});
    delete s.gpuRecipeExports[${JSON.stringify(name)}]; delete s.gpuCapturedLearning; })()`);
  let profile = evidence.profiles.find(p => p.key === profileKey);
  if (!profile) evidence.profiles.push(profile = {key: profileKey, maps: [], chunks: [], observedHashes:[], scopes:{}});
  profile.maps.push(map);
  profile.scopes[map] = [...new Set([...observed,...(validLearning?learning.observed:[])])];
  profile.observedHashes=[...new Set([...profile.observedHashes,...observed,...(validLearning?learning.observed:[])])];
  for (const chunk of chunks) if (!profile.chunks.some(c => c.sha256 === chunk.sha256)) profile.chunks.push(chunk);
  return {map, key: name, chunks: chunks.length,observed:observed.length,learningComplete:validLearning};
}
async function recordRoute(origin, height, bots) {
  const visits=[];
  for (const phase of ['first','repeat']) for (const [pointIndex,point] of routePoints(origin,height).entries()) for (const yaw of [0,90,180,270]) {
    const last=await evaluate('window.kisak.state.perf.gpuFrames?.at(-1)?.frameId || 0');
    await send(`setviewpos ${point.join(' ')} ${yaw} 0`);
    const firstPose=pointIndex===0 && yaw===0;
    await combatActions(cdp,sid,evaluate,firstPose,250,firstPose);
    const after=await evaluate(`window.kisak.agent.metrics(${last})`), current=await evaluate('window.kisak.agent.status()');
    if(after.historyTruncated)throw new Error('Capture route exceeded telemetry history');
    if (current.crash || !current.engine?.inputReady || current.engine.bots!==bots) throw new Error('Capture route lost gameplay readiness');
    const frames=after.gpuFrames.filter(f=>f.frameId>last), ids=new Set(frames.map(f=>f.frameId));
    const events=after.gpuEvents.filter(e=>ids.has(e.frameId));
    visits.push({phase,yaw,pointIndex,bots,origin:current.engine.origin,angles:current.engine.cameraAngles,
      intervals:after.renderIntervals,frames,events,counters:after.gpu,
      requiredPipelineCompilations:events.filter(e=>e.requiredPipeline).length,
      firstTextureUploads:frames.reduce((n,f)=>n+(f.firstTextureUploads||0),0),firstStaticBufferUploads:frames.reduce((n,f)=>n+(f.firstStaticBufferUploads||0),0)});
  }
  return visits;
}
try {
  await cdp.send('Page.bringToFront', {}, sid);
  const url = new URL(session.url);
  url.searchParams.delete('join'); url.searchParams.set('renderer', 'webgpu');
  url.searchParams.set('preparation', 'full'); url.searchParams.delete('bestCase'); url.searchParams.delete('gpuEnumerate'); url.searchParams.set('gpuCapture', '1');
  url.searchParams.set('res', option('--resolution', '960x540'));
  url.searchParams.set('gpuFallback', '0'); url.searchParams.set('gpuProfile', '1');
  url.searchParams.set('args', '+set kisak_pm_bots 0 +set web_warmShaders 1 +set web_disableWeaponFiringEffects 0');
  await cdp.send('Page.navigate', {url: 'about:blank'}, sid); await sleep(1500);
  await cdp.send('Page.navigate', {url: url.href}, sid);
  await wait(s => s.menuReady, 'frontend'); await sleep(6000);
  if (!evidence.maps.some(m=>m.map==='@frontend')) evidence.maps.push(await collect('@frontend'));
  fs.writeFileSync(capturePath, JSON.stringify(evidence,null,2));
  for (const map of maps.filter(map=>!evidence.maps.some(m=>m.map===map))) {
    console.log(JSON.stringify({stage: 'capture', map}));
    await send(`set onlinegame 1; set xblive_privatematch 1; set kisak_pm_bots 0; set scr_bots_managed_all 0; set scr_bots_managed_axis 0; set scr_bots_managed_allies 0; set scr_tdm_timelimit 60; devmap ${map}`);
    let current = await wait(s => s.engine?.connection === 10 && s.engine.map === map && s.engine.menus.some(m=>/team|class/.test(m.name)), map);
    if (current.preparation?.map !== map || current.preparation.status !== 'captured')
      throw new Error(`Incomplete map preparation: ${map}: ${JSON.stringify(current.preparation)}`);
    if (current.engine.menus.some(m => /team/.test(m.name))) await select('AUTO.?ASSIGN');
    await wait(s => s.engine?.menus.some(m=>/class/.test(m.name)), 'class menu');
    await select('^SMG$');
    current = await wait(s => s.engine?.inputReady && !s.engine.uiActive, 'gameplay');
    await sleep(500);
    const cheats = [];
    if (!current.engine.cheats?.god) cheats.push('god');
    if (!current.engine.cheats?.noclip) cheats.push('noclip');
    if (cheats.length) await send(cheats.join('; '));
    const fixtures = {mp_nuked:[-1634,1192,0],mp_mountain:[2535,-1983,520]};
    const origin = fixtures[map] || current.engine.origin, height = fixtures[map] ? 0 : current.engine.cheats?.viewHeight || 60;
    const walking=[];
    if(fixtures[map]) {
      await send('noclip');await wait(s=>!s.engine.cheats.noclip,'collision-enabled capture');
      await evaluate("document.getElementById('game').focus()");
      for(const action of movementRoute)walking.push(await movementAction(cdp,sid,()=>evaluate('window.kisak.agent.status()'),map,action));
      if(walking.reduce((n,a)=>n+a.pathLength,0)<128)throw new Error(`Player did not move in capture: ${map}`);
      await send('noclip');await wait(s=>s.engine.cheats.noclip,'restore capture camera');
    }
    let weapons, observedBeforeWeapons = [];
    if (map === maps[0]) {
      await sleep(6000);
      observedBeforeWeapons = await evaluate(`Object.entries(window.kisak.state.gpuRecipeExportObserved).find(([key])=>key.endsWith('/${map}'))?.[1] || []`);
      weapons = await weaponCoverage(cdp,sid,evaluate,send);
    }
    if (weapons) {
      await send(`kisak_coverage_weapon ${current.engine.weapon} fast`);
      await wait(s=>s.engine?.weapon===current.engine.weapon && s.engine.inputReady,'restore primary weapon');
    }
    const routes=await recordRoute(origin,height,0);
    await key(cdp, sid, 'KeyG', 80); await sleep(2000);
    await key(cdp, sid, 'KeyQ', 80); await sleep(2000);
    if (fixtures[map]) {
      await send('addbots 11'); await wait(s=>s.engine?.bots===11 && s.engine.inputReady,'eleven bots');
      routes.push(...await recordRoute(origin,height,11));
    }
    await sleep(6000);
    const result = await collect(map);
    if (weapons) {
      result.weapons = weapons;
      evidence.profiles[0].scopes['@weapons'] = evidence.profiles[0].scopes[map].filter(hash=>!observedBeforeWeapons.includes(hash));
    }
    result.preparation = (await evaluate('window.kisak.agent.status()')).preparation;
    result.gpu = await evaluate('window.kisak.state.perf.gpu');
    const routeFile=`gpu-catalog/route-${map}.json`;
    fs.writeFileSync(path.join(directory,routeFile),JSON.stringify({map,build:session.build,walking,routes},null,2));
    result.route=routeFile; result.routeVersion='exact-weapons-effects-walking-v6';
    result.origin = origin; evidence.maps.push(result);
    fs.writeFileSync(capturePath, JSON.stringify(evidence, null, 2));
  }
  if (evidence.profiles.length !== 1 || evidence.profiles[0].maps.length !== maps.length + 1)
    throw new Error('Capture did not produce one complete configuration; catalog not published');
  // Refuse to attach evidence to assets changed during the long capture.
  const latest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
  if (await assetIdentity(latest) !== await assetIdentity(manifest)) throw new Error('Assets changed during capture');
  // Preserve material candidates separately from the published selection so
  // later investigation can distinguish enumeration from observed coverage.
  fs.writeFileSync(path.join(directory,'gpu-catalog','capture-inputs.json'),JSON.stringify(evidence,null,2));
  evidence.profiles = compactCatalog(directory,evidence.profiles);
  latest.gpuPreparation = {version: CATALOG_VERSION, assetIdentity: await assetIdentity(latest), profiles: evidence.profiles};
  const temporary = manifestPath + '.tmp';
  fs.writeFileSync(temporary, JSON.stringify(latest, null, 1)); fs.renameSync(temporary, manifestPath);
  fs.writeFileSync(path.join(directory, 'gpu-catalog', 'capture.json'), JSON.stringify(evidence, null, 2));
  console.log(JSON.stringify({stage: 'published', maps: maps.length, chunks: evidence.profiles[0].chunks.length}));
} catch (error) {
  fs.writeFileSync(path.join(directory, 'gpu-catalog', 'failure.json'), JSON.stringify({error: String(error), evidence}, null, 2));
  throw error;
} finally { cdp.close(); }

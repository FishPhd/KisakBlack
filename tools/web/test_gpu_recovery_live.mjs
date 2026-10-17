// Destroy only this owned page's WebGPU device, then verify one-use fallback
// and an explicit retry. Run separately from capture/performance measurement.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {CDP,sleep} from './cdp-client.mjs';
const file=path.resolve(process.argv[2] || 'build-web/agent-session/session.json');
const session=JSON.parse(fs.readFileSync(file,'utf8'));
assert(session.headed,'Recovery test requires an owned visible browser');
const cdp=await CDP.connect(session.debugPort,session.browserUrl),sid=await cdp.attach(session.targets.host);
const evaluate=expression=>cdp.evaluate(expression,sid);
const report={build:session.build,checks:[],injection:'agent-only native GPUDevice.Destroy() on the renderer owner thread'};
async function snapshot(){return evaluate(`({status:window.kisak?.agent?.status(),backend:window.kisak?.state?.rendererBackend,
  failure:sessionStorage.getItem('kisak-webgpu-failure'),override:sessionStorage.getItem('kisak-renderer'),
  retryButtons:document.querySelectorAll('#retry-webgpu').length,
  retryVisible:!!document.querySelector('#retry-webgpu')?.getClientRects().length})`);}
async function quarantines(){return evaluate(`(async()=>{
  const root=await navigator.storage.getDirectory(),result=[];
  for(const name of ['gpu-recipes-v2','gpu-catalog-v1']) {
    try {const dir=await root.getDirectoryHandle(name);for await(const [file] of dir.entries())
      if(file.endsWith('.bad.json'))result.push(name+'/'+file);
    } catch(error){if(error.name!=='NotFoundError')throw error;}
  }
  return result.sort();
})()`);}
async function wait(predicate,label){
  const deadline=Date.now()+180000;
  while(Date.now()<deadline){
    const s=await snapshot().catch(()=>null);
    if(s && predicate(s))return s;
    if(s?.status?.crash || s?.status?.phase==='failed')throw new Error(`${label}: ${s.status.crash || s.status.phase}`);
    await sleep(250);
  }
  throw new Error(`Recovery test timed out: ${label}`);
}
async function managedUi(backend){
  const count=await evaluate('window.kisak.state.perf.warmupEvents?.length || 0');
  await evaluate("window.kisak.state.send('con_restricted_access 0; openmenu options_graphics')");
  await wait(s=>s.status?.engine?.menus.some(m=>m.name==='options_graphics' && m.items.some(i=>i.dvar==='ui_r_mode')),'graphics menu');
  await evaluate("window.kisak.state.send('set r_shaderWarming 0; openmenu kisak_browser_graphics')");
  const s=await wait(s=>s.status?.engine?.menus.some(m=>m.name==='kisak_browser_graphics') &&
    s.status.engine.graphics.settings.r_shaderWarming==='0','shader warming toggle');
  const graphics=s.status.engine.menus.find(m=>m.name==='options_graphics');
  assert(graphics?.items.some(i=>i.dvar==='r_shaderWarming'),'Shader Warming toggle is missing');
  assert(!s.status.engine.menus.some(m=>m.items?.some(i=>i.dvar==='web_warmShaders')),'Internal warming alias is visible');
  await sleep(1200);
  assert.equal(await evaluate('window.kisak.state.perf.warmupEvents?.length || 0'),count,'Frontend update restarted preparation');
  await evaluate("window.kisak.state.send('closemenu kisak_browser_graphics; closemenu options_graphics')");
  report.checks.push(`${backend}: Shader Warming toggle stays off without restarting the frontend gate`);
}
try {
  const url=new URL(session.url);
  for(const key of ['gpuCapture','gpuEnumerate','gpuFallback','bestCase','gpuRecovery'])url.searchParams.delete(key);
  url.searchParams.set('renderer','webgpu');url.searchParams.set('gpuValidation','1');url.searchParams.set('mode','all');url.searchParams.set('preparation','full');
  await evaluate(`for(const key of ['kisak-renderer','kisak-webgpu-recovery','kisak-webgpu-recovery-at','kisak-webgpu-failure'])sessionStorage.removeItem(key)`);
  await cdp.send('Page.navigate',{url:'about:blank'},sid);await sleep(1500);
  await cdp.send('Page.navigate',{url:url.href},sid);
  report.before=await wait(s=>s.backend==='webgpu' && s.status?.menuReady,'WebGPU menu');
  console.log(JSON.stringify({stage:'recovery',backend:'webgpu'}));
  assert.equal(report.before.status.installation?.validated,true,'A validated installation is required');
  report.quarantinesBefore=await quarantines();
  await managedUi('webgpu');
  const destroyed=await evaluate('window.kisak.state.mod._Kisak_TestGpuDeviceLoss?.() || 0');
  assert.equal(destroyed,1,'Must destroy exactly the owned renderer device');
  report.fallback=await wait(s=>s.backend==='webgl2' && s.status?.menuReady,'automatic WebGL recovery');
  assert.match(report.fallback.failure,/destroy|lost/i);
  assert.equal(report.fallback.override,null);
  assert.equal(report.fallback.retryButtons,1);
  assert.equal(report.fallback.retryVisible,true,'Retry is hidden behind debug controls');
  assert.equal(report.fallback.status.installation?.files,report.before.status.installation?.files);
  assert.equal(report.fallback.status.transferred,0,'Recovery redownloaded installed content');
  assert.deepEqual(await quarantines(),report.quarantinesBefore,'Device loss quarantined valid portable data');
  report.checks.push('actual device loss, one-use WebGL recovery, retained installation and one Retry action');
  await managedUi('webgl2');
  await cdp.send('Page.reload',{},sid);
  report.relaunch=await wait(s=>s.status?.documentId!==report.fallback.status.documentId && s.backend==='webgpu' && s.status?.menuReady,'subsequent launch retries WebGPU');
  assert.equal(report.relaunch.status.transferred,0);
  assert.equal(report.relaunch.retryVisible,false,'Historical failure covers a healthy WebGPU launch');
  report.checks.push('ordinary relaunch retries WebGPU without requiring the Retry button');
  const previous=report.relaunch.status.documentId;
  await evaluate('window.kisak.state.retryWebGPU()');
  report.retry=await wait(s=>s.status?.documentId!==previous && s.backend==='webgpu' && s.status?.menuReady,'explicit WebGPU retry');
  assert.equal(report.retry.failure,null);
  assert.equal(report.retry.status.transferred,0);
  assert.equal(report.retry.retryButtons,0);
  report.checks.push('explicit Retry WebGPU succeeds without a sticky WebGL override or asset downloads');
  report.passed=true;
  console.log(JSON.stringify({passed:true,checks:report.checks}));
} catch(error){report.error=String(error);throw error;}
finally {
  fs.writeFileSync(path.join(path.dirname(file),'gpu-recovery-results.json'),JSON.stringify(report,null,2));
  cdp.close();
}

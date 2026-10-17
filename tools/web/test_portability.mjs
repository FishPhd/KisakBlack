// Exercise engine startup with restricted browser capabilities in an owned CDP
// session. This models API availability/core reporting, not slower hardware.
// Usage: node tools/web/test_portability.mjs [session.json]
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import { CDP, sleep } from './cdp-client.mjs';

const sessionPath = path.resolve(process.argv[2] || 'build-web/agent-session/session.json');
const session = JSON.parse(fs.readFileSync(sessionPath, 'utf8'));
const cdp = await CDP.connect(session.debugPort, session.browserUrl);
const sid = await cdp.attach(session.targets.host);
const results = [];
let patch = '', scriptId, instrumentationError;
const listener = message => {
  if (message.method !== 'Target.attachedToTarget' || !message.params.waitingForDebugger) return;
  const workerSid = message.params.sessionId;
  (async () => {
    try {
      await cdp.evaluate(patch, workerSid);
      await cdp.send('Target.setAutoAttach', {autoAttach:true,waitForDebuggerOnStart:true,flatten:true}, workerSid);
    } catch (error) { instrumentationError = error; }
    finally { await cdp.send('Runtime.runIfWaitingForDebugger', {}, workerSid).catch(() => {}); }
  })();
};
cdp.listeners.add(listener);

async function ready(previous) {
  const deadline = Date.now() + 180000;
  while (Date.now() < deadline) {
    if (instrumentationError) throw instrumentationError;
    const state = await cdp.evaluate(`(() => {
      const s = window.kisak?.state;
      const status = window.kisak?.agent?.status();
      return s && {id:status.documentId,crash:status.crash,ready:s.menuReady,
        phase:s.phase,backend:s.rendererBackend,cores:navigator.hardwareConcurrency,
        events:s.records.filter(r=>/job worker|WEB_GPU_(FEATURES|DEVICE_READY|FALLBACK)|adapter request failed/.test(r.text)).map(r=>r.text)};
    })()`, sid).catch(() => null);
    if (state?.phase === 'failed' || state?.crash) throw new Error(JSON.stringify(state));
    if (state?.id !== previous && state?.ready) return state;
    await sleep(250);
  }
  throw new Error('Portability startup timed out');
}

try {
  await cdp.send('Page.enable', {}, sid);
  await cdp.send('Target.setAutoAttach', {autoAttach:true,waitForDebuggerOnStart:true,flatten:true}, sid);
  for (const test of [{cores:2,workers:1}, {cores:8,workers:4,noFloats:true}, {cores:4,workers:2,adapterFailure:true}]) {
    instrumentationError = null;
    patch = `Object.defineProperty(navigator,'hardwareConcurrency',{get:()=>${test.cores},configurable:true});`;
    if (test.noFloats) patch += `if(navigator.gpu) {
      const request = navigator.gpu.requestAdapter.bind(navigator.gpu);
      navigator.gpu.requestAdapter = async options => {
        const adapter = await request(options);
        if(adapter) Object.defineProperty(adapter,'features',{
          value:new Set([...adapter.features].filter(f=>!f.startsWith('float32-')))
        });
        return adapter;
      };
    }`;
    if (test.adapterFailure) patch += `if(navigator.gpu) navigator.gpu.requestAdapter=async()=>{throw new Error('portability test adapter rejection');};`;
    if (scriptId) await cdp.send('Page.removeScriptToEvaluateOnNewDocument', {identifier:scriptId}, sid);
    scriptId = (await cdp.send('Page.addScriptToEvaluateOnNewDocument', {source:patch}, sid)).identifier;
    const previous = await cdp.evaluate('window.kisak.agent.status().documentId', sid);
    await cdp.evaluate(`sessionStorage.removeItem('kisak-renderer');`, sid);
    const url = new URL(session.url); url.searchParams.set('renderer','webgpu');
    await cdp.send('Page.navigate', {url:url.href}, sid);
    const state = await ready(previous);
    results.push({test,state});
    console.log(JSON.stringify({test,state}));
    assert.equal(state.cores, test.cores);
    assert(state.events.includes(`Started ${test.workers} job worker threads`), JSON.stringify(state));
    if (test.adapterFailure) {
      assert.equal(state.backend,'webgl2');
      assert(state.events.some(line=>line.includes('portability test adapter rejection')));
    } else {
      assert.equal(state.backend,'webgpu',JSON.stringify(state));
      if (test.noFloats) assert(state.events.includes('WEB_GPU_FEATURES float32-filterable=0 float32-blendable=0'));
    }
    console.log(JSON.stringify({passed:true,test}));
  }
} finally {
  if (scriptId) await cdp.send('Page.removeScriptToEvaluateOnNewDocument', {identifier:scriptId}, sid).catch(() => {});
  await cdp.send('Target.setAutoAttach', {autoAttach:false,waitForDebuggerOnStart:false,flatten:true}, sid).catch(() => {});
  cdp.listeners.delete(listener);
  fs.writeFileSync(path.join(path.dirname(sessionPath),'portability-results.json'),JSON.stringify(results,null,2));
  // Restore normal browser capabilities for the user after testing.
  await cdp.send('Page.navigate',{url:session.url},sid).catch(() => {});
  cdp.close();
}

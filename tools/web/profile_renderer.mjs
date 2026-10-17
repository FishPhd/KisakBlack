// Sample every engine/executor worker without pausing the game. Uses the owned
// CDP session, so unrelated browser windows are never attached.
import fs from 'node:fs';
import path from 'node:path';
import { CDP, sleep } from './cdp-client.mjs';
const seconds = Number(process.argv[2] || 10);
if (!Number.isFinite(seconds) || seconds < 1 || seconds > 30) throw new Error('Sample duration must be 1..30 seconds');
const sessionPath = path.resolve(process.argv[3] || 'build-web/agent-session/session.json');
const session = JSON.parse(fs.readFileSync(sessionPath, 'utf8'));
const output = path.resolve(process.argv[4] || path.join(path.dirname(sessionPath), 'cpu-profile'));
fs.mkdirSync(output, {recursive:true});
const cdp = await CDP.connect(session.debugPort, session.browserUrl);
try {
  const pageSid=await cdp.attach(session.targets.host);
  const snapshot=()=>cdp.evaluate(`({engine:window.kisak.state.engine,gpu:window.kisak.state.perf.gpu,at:performance.now()})`,pageSid);
  const before=await snapshot();
  const origin = new URL(session.url).origin;
  const targets = (await cdp.send('Target.getTargets')).targetInfos.filter(t => t.targetId === session.targets.host || /worker/.test(t.type) && t.url.startsWith(origin));
  const attempts = await Promise.allSettled(targets.map(async t => {
    const sid = await cdp.attach(t.targetId);
    await cdp.send('Profiler.enable', {}, sid, 5000);
    await cdp.send('Profiler.stop', {}, sid, 5000).catch(()=>{});
    await cdp.send('Profiler.setSamplingInterval', {interval:1000}, sid, 5000);
    await cdp.send('Profiler.start', {}, sid, 5000);
    return {t,sid};
  }));
  const attached = attempts.filter(r=>r.status==='fulfilled').map(r=>r.value);
  const skipped = attempts.flatMap((r,i)=>r.status==='rejected'?[{target:targets[i].targetId,error:String(r.reason)}]:[]);
  await sleep(seconds * 1000);
  const summaries = await Promise.all(attached.map(async ({t,sid}) => {
    const {profile} = await cdp.send('Profiler.stop', {}, sid);
    const file = path.join(output, `${t.targetId}.cpuprofile`);
    fs.writeFileSync(file, JSON.stringify(profile));
    const nodes = new Map(profile.nodes.map(n => [n.id,n]));
    const parents=new Map(); for(const node of profile.nodes)for(const child of node.children||[])parents.set(child,node.id);
    const times = new Map();
    let skinUs=0;
    for (let i = 0; i < (profile.samples || []).length; ++i) {
      const node = nodes.get(profile.samples[i]);
      const name = node.callFrame.functionName || `(anonymous ${node.callFrame.url}:${node.callFrame.lineNumber})`;
      times.set(name, (times.get(name) || 0) + (profile.timeDeltas[i] || 0));
      if(!/futex|wait|emscripten_thread_sleep|Sys_Sleep/i.test(name)) {
        let ancestor=node;
        while(ancestor) {
          if(/\bR_Skin|\bSkin(?:Weight|Vertex|Transform|Apply|Pack|Unpack|Bone)/.test(ancestor.callFrame.functionName)) {
            skinUs+=profile.timeDeltas[i]||0;break;
          }
          ancestor=nodes.get(parents.get(ancestor.id));
        }
      }
    }
    return {target:t.targetId,type:t.type,url:t.url,file,skinInclusiveMs:skinUs/1000,
      topSelfMs:[...times].sort((a,b)=>b[1]-a[1]).slice(0,18).map(([name,us])=>({name,ms:us/1000}))};
  }));
  const after=await snapshot(), renderedFrames=after.gpu.submitted-before.gpu.submitted;
  const metadata={build:session.build,before,after,renderedFrames,skinInclusiveMs:summaries.reduce((n,s)=>n+s.skinInclusiveMs,0),
    timingCaveat:'CPU sampling perturbs execution; use separate matched runs for frame-time comparisons'};
  fs.writeFileSync(path.join(output,'summary.json'),JSON.stringify({metadata,summaries,skipped},null,2));
  console.log(JSON.stringify({metadata,summaries,skipped},null,2));
} finally { cdp.close(); }

import fs from 'node:fs';
import assert from 'node:assert/strict';
import path from 'node:path';
import {CDP,sleep} from './cdp-client.mjs';
// Start a local devmap with developer_script 1 before loading the map. The
// shipped developer script drives normal supply-drop scripts at an allied bot.
const file=process.argv[2]||'build-web/issue-session/session.json';
const runsToDo=Number(process.argv[3]||5);
assert.ok(Number.isInteger(runsToDo)&&runsToDo>0&&runsToDo<=10);
const session=JSON.parse(fs.readFileSync(file));assert.equal(session.headed,true);
const c=await CDP.connect(session.debugPort,session.browserUrl),sid=await c.attach(session.targets.host);
const ev=x=>c.evaluate(x,sid),status=()=>ev('window.kisak.agent.status()');
const report={build:session.build,runs:[]};
const output=path.join(path.dirname(file),'supplydrop-results.json');
try {
 const initial=await status();assert.equal(initial.engine?.connection,10);assert.equal(initial.engine.cheats.god,true,'Enable god for the test host');
 for(let run=0;run<runsToDo;run++){
  const before=await status();
  const existing=new Set(before.engine.gameplayEntities.filter(e=>/mp_supplydrop_/.test(e.model)).map(e=>e.entity));
  await ev('window.kisak.state.send("set scr_givetestsupplydrop 1\\n")');
  const start=Date.now(),samples=[];let sawHeli=false,sawCrate=false,departed=false;
  while(Date.now()-start<65000){
   const state=await status();assert.ok(!state.crash,state.crash);
   const ents=state.engine.gameplayEntities.filter(e=>e.vehicle||(/mp_supplydrop_/.test(e.model)&&!existing.has(e.entity)));
   const players=state.engine.gameplayEntities.filter(e=>e.type===1);
   samples.push({at:Date.now()-start,serverTime:state.engine.serverTime,age:state.engineAgeMs,ents,players});
   sawHeli ||= ents.some(e=>e.vehicle);sawCrate ||= ents.some(e=>!e.vehicle);
   if(sawHeli&&sawCrate&&!ents.some(e=>e.vehicle)){departed=true;break;}
   await sleep(350);
  }
  const result={run:run+1,map:before.engine.map,sawHeli,sawCrate,departed,samples};report.runs.push(result);
  fs.writeFileSync(output,JSON.stringify(report,null,2));
  console.log(JSON.stringify({...result,samples:undefined,durationMs:samples.at(-1)?.at}));
  assert.ok(sawHeli&&sawCrate&&departed,'Crate must spawn and helicopter must leave within 65 seconds');
  assert.ok(samples.at(-1).serverTime>samples[0].serverTime+5000,'Server must keep advancing');
  await sleep(2000);
 }
} finally {c.close()}

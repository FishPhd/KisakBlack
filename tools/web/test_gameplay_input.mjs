import fs from 'node:fs';
import assert from 'node:assert/strict';
import {CDP,sleep,click} from './cdp-client.mjs';
const file=process.argv[2]||'build-web/issue-session/session.json';
const session=JSON.parse(fs.readFileSync(file));
assert.equal(session.headed,true,'Use a visible, owned test session');
const c=await CDP.connect(session.debugPort,session.browserUrl),sid=await c.attach(session.targets.host);
const ev=x=>c.evaluate(x,sid);
try {
 const state=await ev('window.kisak.agent.status()');
 assert.equal(state.engine?.connection,10);assert.equal(state.engine.uiActive,false,'Close menus before checking input');
 assert.equal(state.engine.mouse2[0],'+speed_throw');assert.equal(state.engine.mouse2[1],'');
 const [x,y]=await ev('(()=>{const r=game.getBoundingClientRect();return[r.left+r.width/2,r.top+r.height/2]})()');
 if (!state.capture) await click(c,sid,x,y);await sleep(1800);
 await ev('window.__gameplayKeys=[];window.__gameplayKeyOriginal=window.kisak.state.mod._Kisak_Key;window.kisak.state.mod._Kisak_Key=(key,down)=>{window.__gameplayKeys.push({key,down});return window.__gameplayKeyOriginal(key,down)}');
 const mouse=(type,button,buttons)=>c.send('Input.dispatchMouseEvent',{type,x,y,button,buttons,clickCount:1},sid);
 await mouse('mousePressed','right',2);await sleep(1400);
 const aimed=await ev('window.kisak.agent.status().engine.adsFraction');
 await mouse('mousePressed','left',3);await sleep(150);await mouse('mouseReleased','left',2);await sleep(150);
 const held=await ev('window.kisak.agent.status().engine.adsFraction');
 await mouse('mouseReleased','right',0);await sleep(1400);
 const released=await ev('window.kisak.agent.status().engine.adsFraction');
 const keys=await ev('window.__gameplayKeys');let right=false,chord=false;
 for(const e of keys){if(e.key===201)right=!!e.down;if(e.key===200&&e.down&&right)chord=true;}
 console.log(JSON.stringify({aimed,held,released,keys}));
 assert.ok(chord,'Engine must receive attack while ADS remains held');assert.ok(aimed>.9);assert.ok(held>.9);assert.ok(released<.1);
} finally {
 await c.send('Input.dispatchMouseEvent',{type:'mouseReleased',x:632,y:312,button:'left',buttons:0,clickCount:1},sid).catch(()=>{});
 await c.send('Input.dispatchMouseEvent',{type:'mouseReleased',x:632,y:312,button:'right',buttons:0,clickCount:1},sid).catch(()=>{});
 await ev('if(window.__gameplayKeyOriginal){window.kisak.state.mod._Kisak_Key=window.__gameplayKeyOriginal;delete window.__gameplayKeyOriginal;delete window.__gameplayKeys}').catch(()=>{});
 c.close();
}

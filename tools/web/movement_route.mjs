import assert from 'node:assert/strict';
import {sleep} from './cdp-client.mjs';
export const movementControls={KeyW:['w',87],KeyA:['a',65],KeyS:['s',83],KeyD:['d',68],ShiftLeft:['Shift',16],ArrowRight:['ArrowRight',39],ArrowLeft:['ArrowLeft',37],KeyR:['r',82]};
export const movementRoute=[
  {name:'walk-forward',keys:['KeyW'],ms:3500},
  {name:'walk-and-look-right',keys:['KeyW','ArrowRight'],ms:4500},
  {name:'strafe-and-look-left',keys:['KeyD','ArrowLeft'],ms:4000},
  {name:'walk-back',keys:['KeyS'],ms:2500},
  {name:'strafe-left',keys:['KeyA'],ms:3500},
  {name:'sprint-and-look',keys:['KeyW','ShiftLeft','ArrowRight'],ms:4500},
  {name:'look-around',keys:['ArrowLeft'],ms:5000},
  {name:'walk-forward-again',keys:['KeyW'],ms:3500},
];
const distance=(a,b)=>Math.hypot(...a.map((v,i)=>v-b[i]));
export async function movementInput(cdp,sid,code,down){
  const [name,vk]=movementControls[code];
  await cdp.send('Input.dispatchKeyEvent',{type:down?'rawKeyDown':'keyUp',code,key:name,windowsVirtualKeyCode:vk},sid);
}
export async function movementAction(cdp,sid,state,map,action){
  const before=await state(),entry={...action,at:Date.now(),trajectory:[]};
  try{
    for(const code of action.keys)await movementInput(cdp,sid,code,true);
    const end=performance.now()+action.ms;
    while(performance.now()<end){
      const s=await state();assert(!s.crash&&s.engine.inputReady&&!s.engine.uiActive,'Lost active gameplay');
      assert.equal(s.engine.map,map);assert(!s.engine.cheats.noclip,'Movement must have collision enabled');
      entry.trajectory.push({at:Date.now(),origin:s.engine.origin,angles:s.engine.cameraAngles,engineAgeMs:s.engineAgeMs});
      await sleep(Math.min(250,Math.max(0,end-performance.now())));
    }
  }finally{for(const code of action.keys.slice().reverse())await movementInput(cdp,sid,code,false).catch(()=>{});}
  const after=await state();entry.end=Date.now();entry.displacement=distance(before.engine.origin,after.engine.origin);
  const samples=[before.engine.origin,...entry.trajectory.map(p=>p.origin),after.engine.origin];
  entry.pathLength=samples.reduce((n,p,i)=>n+(i?distance(p,samples[i-1]):0),0);
  entry.before={origin:before.engine.origin,angles:before.engine.cameraAngles};entry.after={origin:after.engine.origin,angles:after.engine.cameraAngles};
  return entry;
}

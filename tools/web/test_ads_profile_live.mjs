import fs from 'node:fs';
import assert from 'node:assert/strict';
import {CDP,sleep} from './cdp-client.mjs';
// This changes and reloads saved controls. Use an isolated gameplay test profile.
const file=process.argv[2]||'build-web/issue-session/session.json';
const session=JSON.parse(fs.readFileSync(file));assert.equal(session.headed,true);
assert.match(session.profile.replaceAll('\\','/'),/\/(?:issue-session|gameplay-[^/]+)\/profile$/);
const c=await CDP.connect(session.debugPort,session.browserUrl),sid=await c.attach(session.targets.host);
const ev=x=>c.evaluate(x,sid),status=()=>ev('window.kisak.agent.status()');
const command=x=>ev(`window.kisak.state.send(${JSON.stringify(x+'\n')})`);
async function wait(fn,label,ms=60000){const end=Date.now()+ms;while(Date.now()<end){try{const s=await status();if(fn(s))return s;}catch{}await sleep(200);}throw Error(label);}
async function save(){await command('writeconfig config_mp.cfg');await sleep(2500);}
async function reload(){const old=(await status()).documentId;await c.send('Page.reload',{},sid);return wait(s=>s.documentId!==old&&s.menuReady&&s.engine,'Reloaded controls');}
try {
 await wait(s=>s.menuReady&&s.engine,'Initial menu');
 await command('con_restricted_access 0; bind MOUSE2 +toggleads_throw; web_adsBindingVersion 0');await save();
 let s=await reload();assert.deepEqual(s.engine.mouse2,['+speed_throw','']);assert.equal(s.engine.graphics.settings.web_adsBindingVersion,'1');
 console.log('Old toggle-ADS profile migrated to hold-ADS');
 await command('con_restricted_access 0; bind MOUSE2 +toggleads_throw');await save();
 s=await reload();assert.deepEqual(s.engine.mouse2,['+toggleads_throw','']);assert.equal(s.engine.graphics.settings.web_adsBindingVersion,'1');
 console.log('Post-migration customization preserved on reload');
 await command('con_restricted_access 0; exec reset_bindings.cfg');
 s=await wait(s=>s.engine?.mouse2[0]==='+speed_throw'&&s.engine.mouse2[1]==='','Reset controls applies hold-ADS');await save();
 console.log('Reset controls restored hold-ADS and cleared secondary mouse binding');
}finally{c.close()}

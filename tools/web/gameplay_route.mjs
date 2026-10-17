// Deterministic camera/combat fixtures shared by capture and qualification.
// These sample several areas; they do not claim exhaustive interaction coverage.
import {sleep, key} from './cdp-client.mjs';
export function routePoints(origin, height = 0) {
  const [x,y,z] = origin;
  return [[x,y,z+height],[x+300,y,z+height],[x+300,y+300,z+height+80],[x,y+300,z+height]];
}
export async function combatActions(cdp, sid, evaluate, extended = false, fireMs = 400, reload = true) {
  const press = (code, down) => evaluate(`window.kisak.state.mod._Kisak_Key(${code},${down ? 1 : 0})`);
  await press(201, true);
  try { await sleep(250); await press(200, true); await sleep(fireMs); }
  finally { await press(200, false); await press(201, false); }
  if(reload){await key(cdp,sid,'KeyR',80); await sleep(2600);}
  if (extended) {
    await key(cdp,sid,'Digit2',80); await sleep(800);
    await press(200,true); try { await sleep(350); } finally { await press(200,false); }
    await key(cdp,sid,'KeyR',80); await sleep(2600);
    await key(cdp,sid,'Digit2',80); await sleep(800);
    for (const code of ['KeyG','KeyQ','KeyV']) { await key(cdp,sid,code,80); await sleep(1800); }
  }
}

// Every registered usable variant, with an acknowledgement before input. The
// resulting list is evidence of coverage, rather than an assumption about keys.
export async function weaponCoverage(cdp, sid, evaluate, send, sample = null) {
  const start = await evaluate('window.kisak.state.logSequence || 0');
  await send('kisak_coverage_weapon list');
  let records;
  for (let n=0; n<100; ++n) {
    records = await evaluate(`window.kisak.state.records.filter(r=>r.seq>${start})`);
    if (records.some(r=>r.text==='WEB_COVERAGE_WEAPONS_END')) break;
    await sleep(100);
  }
  if (!records.some(r=>r.text==='WEB_COVERAGE_WEAPONS_END')) throw new Error('Weapon coverage enumeration did not finish');
  const weapons = records.map(r=>/^WEB_COVERAGE_WEAPON (\d+) (\d+) (\d+) ([01]) ([a-f0-9_]+) (.+)$/.exec(r.text)).filter(Boolean)
    .map(m=>({index:Number(m[1]),offhand:Number(m[2]),slot:Number(m[3]),selectable:m[4]==='1',effects:m[5],name:m[6]}));
  if (!weapons.length) throw new Error('No usable registered weapon variants');
  const effects=new Set(); let completed=0;
  for (const weapon of weapons) {
    if (!weapon.selectable) {
      weapon.uncovered='requires scripted world or vehicle fixture; engine rejects player loadout';
      continue;
    }
    if (/^defaultweapon(?:_mp)?$/.test(weapon.name) || /lh_mp$/.test(weapon.name)) {
      weapon.excluded=/lh_mp$/.test(weapon.name)?'left-hand component, exercised by dual-wield variant':'engine fallback placeholder';
      continue;
    }
    const before = await evaluate('window.kisak.state.logSequence || 0');
    await send(`kisak_coverage_weapon ${weapon.index} fast`);
    let ready = false;
    for (let n=0; n<100; ++n) {
      ready = await evaluate(`window.kisak.state.records.some(r=>r.seq>${before} && r.text==='WEB_COVERAGE_SELECTED ${weapon.index}')`);
      if (ready) break;
      await sleep(100);
    }
    if (!ready) throw new Error(`Weapon fixture failed: ${weapon.name}`);
    await sleep(80);
    if (weapon.offhand) {
      if(weapon.slot!==1 && weapon.slot!==2) {
        const beforeUse=await evaluate('window.kisak.state.logSequence || 0');
        await send(`kisak_coverage_weapon ${weapon.index} use`);
        await sleep(300);
        const used=await evaluate(`window.kisak.state.records.some(r=>r.seq>${beforeUse} && r.text==='WEB_COVERAGE_USED ${weapon.index}')`);
        if(!used)throw new Error(`Equipment fixture could not deploy: ${weapon.name}`);
        weapon.exercised='server equipment deployment';
      } else {
        await key(cdp,sid,weapon.slot===1?'KeyG':'KeyQ',150);
        weapon.exercised='offhand throw';
      }
      await sleep(4200);
    } else {
      let selected=false;
      for(let n=0;n<600;++n){
        if((await evaluate('window.kisak.agent.status()')).engine?.weapon===weapon.index){selected=true;break;}
        await sleep(50);
      }
      if(!selected)throw new Error(`Weapon did not reach client: ${weapon.name}`);
      const full=!effects.has(weapon.effects);
      if(full){await combatActions(cdp,sid,evaluate,false,1200);effects.add(weapon.effects);}
      else {
        await evaluate('window.kisak.state.mod._Kisak_Key(201,1)');
        try{await sleep(100);}finally{await evaluate('window.kisak.state.mod._Kisak_Key(201,0)');}
      }
      weapon.exercised=full?'ADS/fire/reload':'draw/ADS (shared firing-effect family)';
    }
    const status = await evaluate('window.kisak.agent.status()');
    if (status.crash || !status.engine?.inputReady) throw new Error(`Gameplay lost during weapon coverage: ${weapon.name}`);
    if (sample) await sample(weapon);
    if(++completed%50===0)console.log(JSON.stringify({stage:'weapons',completed,total:weapons.length,name:weapon.name}));
  }
  return weapons;
}

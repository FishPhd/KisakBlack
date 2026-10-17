// Integration checks against a headed cdp.mjs session and the shipped assets.
// Use an isolated --profile build-web/cac-profile. Never seeds a regular profile.
//   node tools/web/test_cac.mjs [verify|ui] [--role guest]
import fs from 'node:fs';
import assert from 'node:assert/strict';
import { CDP, sleep, click, key } from './cdp-client.mjs';

const args = process.argv.slice(2);
const role = args.includes('--role') ? args[args.indexOf('--role') + 1] : 'host';
const session = JSON.parse(fs.readFileSync('build-web/agent-session/session.json'));
assert.match(session.profile.replaceAll('\\', '/'), /\/cac-profile$/, 'Use the isolated CAC test profile');
assert.equal(session.headed, true, 'Run visible browser checks');
const cdp = await CDP.connect(session.debugPort, session.browserUrl);
const sid = await cdp.attach(session.targets[role]);
const evaluate = expression => cdp.evaluate(expression, sid);
const expectedFile = `build-web/cac-expected${role === 'host' ? '' : '-' + role}.json`;

// Read the live schema and tables; offsets, item prices and enum ordering are
// discovered from this build's assets, not copied from a second implementation.
function installProbe() {
  const h = window.kisak.state.mod.HEAPU8, w = new Uint32Array(h.buffer);
  const str = p => new TextDecoder().decode(h.slice(p, h.indexOf(0, p)));
  function asset(name, accept) {
    const n = new TextEncoder().encode(name), names = [];
    for (let p = h.indexOf(n[0]); p >= 0; p = h.indexOf(n[0], p + 1))
      if (h[p + n.length] === 0 && n.every((x, j) => h[p + j] === x)) names.push(p);
    for (let i = 0; i < Math.min(w.length, 33554432); ++i)
      if (names.includes(w[i]) && accept(i * 4)) return i * 4;
    throw Error('Loaded asset not found: ' + name);
  }
  const root = asset('ddl_mp/stats.ddl', p => w[p / 4 + 1] > 10000000);
  const d = w[root / 4 + 1], structs = [], enums = [];
  for (let i = 0; i < w[d / 4 + 3]; ++i) {
    const p = w[d / 4 + 2] + i * 16, members = [];
    for (let j = 0; j < w[p / 4 + 2]; ++j) {
      const m = w[p / 4 + 3] + 48 * j;
      members.push({ name: str(w[m / 4]), size: w[m / 4 + 1], offset: w[m / 4 + 2],
        type: w[m / 4 + 3], externalIndex: w[m / 4 + 4], min: w[m / 4 + 5], max: w[m / 4 + 6],
        arraySize: w[m / 4 + 9], enumIndex: w[m / 4 + 10] | 0 });
    }
    structs.push(members);
  }
  for (let i = 0; i < w[d / 4 + 5]; ++i) {
    const p = w[d / 4 + 4] + i * 12;
    enums.push(Array.from({ length: w[p / 4 + 1] }, (_, j) => str(w[w[p / 4 + 2] / 4 + j])));
  }
  const candidates = [], stride = 40552;
  for (let i = 0; i < Math.min(w.length, 33554432); ++i) {
    const p = i * 4;
    if (w[i + 1] === w[d / 4] && w[i + stride * 4 / 4 + 1] === w[d / 4]
        && w[i + stride * 3 / 4 + 1] === w[d / 4]
        && h[p + 40548] === 1 && h[p + 40550] === 1 && h[p + 40551] === 1) candidates.push(p);
  }
  if (candidates.length !== 1) throw Error('Expected one loaded controller stats buffer: ' + candidates);
  function field(path) {
    let list = structs[0], offset = 320, m;
    for (let i = 0; i < path.length; ++i) {
      m = list.find(x => x.name === String(path[i]).toLowerCase());
      if (!m) throw Error('Unknown DDL member ' + path[i]);
      offset += m.offset;
      if (m.arraySize > 1) {
        const value = path[++i], index = m.enumIndex < 0 ? Number(value) : enums[m.enumIndex].indexOf(String(value).toLowerCase());
        if (index < 0 || index >= m.arraySize) throw Error('Invalid DDL index ' + value);
        offset += index * m.size / m.arraySize;
      }
      if (m.type === 6) list = structs[m.externalIndex];
    }
    if (m.type > 2) throw Error('Expected an integer leaf');
    return { offset, size: m.size / m.arraySize, min: m.min, max: m.max };
  }
  function readBytes(bytes, path) {
    const { offset, size, min } = field(path);
    let value = 0;
    for (let i = 0; i < size; ++i) value += ((bytes[(offset + i) >> 3] >> ((offset + i) & 7)) & 1) * 2 ** i;
    return value + min;
  }
  const base = candidates[0];
  function read(path, buffer = 0) { return readBytes(h.subarray(base + buffer * stride), path); }
  // Fixture setup only. Purchases and equipment below always use native commands/UI.
  function seed(path, value, buffer = 0) {
    const { offset, size, min, max } = field(path), p = base + buffer * stride;
    if (value < min || value > max) throw Error('Invalid fixture value');
    for (let i = 0; i < size; ++i) {
      const byte = p + ((offset + i) >> 3), mask = 1 << ((offset + i) & 7);
      h[byte] = (h[byte] & ~mask) | (((value - min) >>> i & 1) * mask);
    }
    h[p + 8] |= 1;
  }
  function table(name) {
    const p = asset(name, p => w[p / 4 + 1] > 5 && w[p / 4 + 1] < 40 && w[p / 4 + 2] > 30 && w[p / 4 + 2] < 1000);
    const cols = w[p / 4 + 1], rows = w[p / 4 + 2], values = w[p / 4 + 3];
    return Array.from({ length: rows }, (_, r) => Array.from({ length: cols }, (_, c) => str(w[(values + (r * cols + c) * 8) / 4])));
  }
  window.cacTest = { read, seed, readBytes, base, table };
  return { version: w[d / 4], base };
}

async function command(line) {
  const result = await evaluate(`(async()=>{const n=window.kisak.agent.logs().latest;window.kisak.state.send(${JSON.stringify(line)});await new Promise(r=>setTimeout(r,120));return window.kisak.agent.logs(n).records.map(x=>x.text);})()`);
  assert.equal(result.some(x => /^ASSERT|RuntimeError|non-leaf|could not find item version/i.test(x)), false, result.join('\n'));
  return result;
}
const read = (path, buffer = 0) => evaluate(`window.cacTest.read(${JSON.stringify(path)},${buffer})`);
const seed = (path, value, buffer = 0) => evaluate(`window.cacTest.seed(${JSON.stringify(path)},${value},${buffer})`);
const points = () => read(['PlayerStatsList', 'CODPOINTS']);
const owned = id => read(['ItemStats', id, 'purchased']);
const equipped = (slot, cls = 'customclass1') => read(['CacLoadouts', cls, slot]);
async function saved() {
  return evaluate(`(async()=>{const root=await navigator.storage.getDirectory();let d=await root.getDirectoryHandle('profile');${role === 'guest' ? "const p=new URL(location.href).searchParams.get('player');const id=p?localStorage.getItem('kisakGuestProfile:'+p):sessionStorage.getItem('kisakGuestProfile');d=await d.getDirectoryHandle('guests');d=await d.getDirectoryHandle(id);" : ''}d=await d.getDirectoryHandle('players');const h=await d.getFileHandle('kisak_mpstats.dat');const bytes=new Uint8Array(await(await h.getFile()).arrayBuffer());return {points:window.cacTest.readBytes(bytes.subarray(16),['PlayerStatsList','CODPOINTS']),globalPoints:window.cacTest.readBytes(bytes.subarray(16+40548),['PlayerStatsList','CODPOINTS']),primary:window.cacTest.readBytes(bytes.subarray(16),['CacLoadouts','customclass1','primary'])};})()`);
}

async function clickItem(pattern) {
  let selected, previous, stableSince = 0;
  for (let i = 0; i < 100; ++i) {
    const menus = await evaluate('window.kisak.agent.status().engine.menus');
    selected = menus.at(-1)?.items.find(i => pattern.test(`${i.name} ${i.text}`) && i.rect[2] > 0 && i.rect[3] > 0);
    const signature = selected && JSON.stringify([menus.at(-1).name, selected.index, selected.rect]);
    if (signature !== previous) stableSince = Date.now();
    if (signature && signature === previous && Date.now() - stableSince >= 700) break;
    previous = signature;
    await sleep(100);
  }
  assert.ok(selected, `Missing focused menu item ${pattern}`);
  await clickRect(selected.rect);
  await sleep(700);
}
async function clickRect(rect) {
  const point = await evaluate(`(()=>{const c=document.getElementById('game'),r=c.getBoundingClientRect(),s=Math.min(r.width/c.width,r.height/c.height),p=${JSON.stringify(rect)};return [r.left+(r.width-c.width*s)/2+(p[0]+p[2]/2)*s,r.top+(r.height-c.height*s)/2+(p[1]+p[3]/2)*s];})()`);
  await click(cdp, sid, ...point);
}
async function clickRow(index) {
  let item, previous, stableSince = 0;
  for (let i = 0; i < 100; ++i) {
    item = await evaluate("window.kisak.agent.status().engine.menus.at(-1).items.find(i=>/list/.test(i.name)&&i.rect[3]>30)");
    const signature = item && JSON.stringify(item.rect);
    if (signature !== previous) stableSince = Date.now();
    if (signature && signature === previous && Date.now() - stableSince >= 700) break;
    previous = signature;
    await sleep(100);
  }
  assert.ok(item, 'Visible native list');
  await clickRect([item.rect[0], item.rect[1] + index * 19.6875, item.rect[2], 19.6875]);
  await sleep(700);
}
async function back() { await key(cdp, sid, 'Escape'); await sleep(750); }

try {
  for (let i = 0; i < 120 && !await evaluate('window.kisak?.agent?.status().menuReady'); ++i) await sleep(500);
  console.log('Loaded production schema', await evaluate(`(${installProbe})()`));
  if (args.includes('verify')) {
    const expected = JSON.parse(fs.readFileSync(expectedFile));
    assert.equal(await points(), expected.points);
    assert.equal(await equipped('primary'), expected.primary);
    assert.equal(await equipped('specialty1'), 153);
    assert.equal(await owned(29), 1);
    assert.equal(await read(['WeaponStats', 29, 'purchasedOptions', 1]), 1);
    assert.deepEqual(await saved(), expected);
    console.log('PASS browser restart: purchases, classes, cosmetics and balance');
  } else if (args.includes('ui')) {
    await command('con_restricted_access 0; ddl_verbose 0');
    if (!args.includes('resume')) {
      await clickItem(/\bPLAY$/);
      await clickItem(/CREATE.?A.?GAME/);
      await clickItem(/CREATE.?A.?CLASS/);
      await clickItem(/CUSTOM 1$/);
      if (await evaluate("window.kisak.agent.status().engine.menus.at(-1).name==='cac_main'"))
        await clickItem(/CUSTOM 1$/);
    }
    await clickItem(/PRIMARY$/);
    await command('clearAllAttachmentsForItem customclass1 29');
    await seed(['WeaponStats', 29, 'purchasedAttachments', 1], 0);
    await seed(['WeaponStats', 29, 'purchasedAttachments', 5], 0);
    let balance = await points();
    await clickItem(/ATTACHMENTS$/);
    await clickRow(0);
    await clickItem(/CANCEL PURCHASE$/);
    assert.equal(await points(), balance);
    assert.equal(await read(['WeaponStats', 29, 'purchasedAttachments', 1]), 0);
    await clickRow(0);
    await sleep(1500); // Let the background list repaint while confirmation is open.
    await clickItem(/BUY AND EQUIP$/);
    balance -= 1000;
    assert.equal(await points(), balance);
    assert.equal(await read(['WeaponStats', 29, 'purchasedAttachments', 1]), 1);
    assert.equal(await equipped('primaryattachmenttrigger'), 1);
    // Swap to a sight to exercise reticle/lens customization with real compatibility.
    await command('clearAttachmentForItemAtPoint customclass1 29 3');
    await clickRow(4);
    await clickItem(/BUY AND EQUIP$/);
    balance -= 1000;
    assert.equal(await points(), balance);
    assert.equal(await read(['WeaponStats', 29, 'purchasedAttachments', 5]), 1);
    assert.equal(await equipped('primaryattachmenttop'), 5);
    await back();
    for (const [label, option, slot, price] of [
      ['RETICLE', 23, 'primaryreticle', 500],
      ['RETICLE COLOR', 66, 'primaryreticlecolor', 250],
      ['LENS', 17, 'primarylens', 500]
    ]) {
      await seed(['WeaponStats', 29, 'purchasedOptions', option], 0);
      await clickItem(new RegExp(` ${label}$`));
      await clickRow(1);
      await clickItem(/BUY AND EQUIP$/);
      balance -= price;
      assert.equal(await points(), balance, label);
      assert.equal(await read(['WeaponStats', 29, 'purchasedOptions', option]), 1, label);
      assert.equal(await equipped(slot), 1, label + ' equipped');
      console.log('PASS stock UI option', label);
      // Some stock purchase dialogs return directly to weapon customization.
      if (await evaluate("window.kisak.agent.status().engine.menus.at(-1).name!=='cac_weapon'"))
        await back();
    }
    for (const [name, option, group] of [['tag', 62, 3], ['emblem', 63, 4]]) {
      await seed(['WeaponStats', 29, 'purchasedOptions', option], 0);
      await command(`setCurrentItemIndex 29; purchaseWeaponOption ${name}; ui_currentWeaponOptionGroup ${group}`);
      balance -= 1000;
      assert.equal(await points(), balance);
      assert.equal(await read(['WeaponStats', 29, 'purchasedOptions', option]), 1);
      const prior = await equipped('primary' + name);
      await command(`toggleWeaponOption customclass1 ${name}`);
      assert.equal(await equipped('primary' + name), prior ^ 1);
      await command(`toggleWeaponOption customclass1 ${name}`);
      assert.equal(await equipped('primary' + name), prior);
    }
    await sleep(2500);
    const expected = { points: balance, globalPoints: balance, primary: 29 };
    assert.deepEqual(await saved(), expected);
    fs.writeFileSync(expectedFile, JSON.stringify(expected, null, 2));
    console.log('PASS stock UI: cancel, buy and equip, attachment selection across repaint, reticle/color/lens and autosave', expected);
  } else {
    await command('con_restricted_access 0; ddl_verbose 1; allItemsPurchased 0; allItemsUnlocked 0; ui_items_no_cost 0; ui_useCustomClassInfo 0');
    const items = await evaluate("window.cacTest.table('mp/statstable.csv')");
    const tested = [29, 5, 64, 68, 74, 152, 153, 154, 160, 170, 89, 107];
    for (const id of tested) await seed(['ItemStats', id, 'purchased'], 0);
    for (const buffer of [0, 4]) {
      await seed(['PlayerStatsList', 'CODPOINTS'], 100000, buffer);
      await seed(['PlayerStatsList', 'CURRENCYSPENT'], 0, buffer);
      await seed(['PlayerStatsList', 'RANKXP'], 0, buffer);
      await seed(['PlayerStatsList', 'PLEVEL'], 0, buffer);
    }
    await command('purchaseItem 29');
    assert.equal(await owned(29), 0, 'rank-locked purchase');
    assert.equal(await points(), 100000);
    const ranks = await evaluate("window.cacTest.table('mp/ranktable.csv')");
    const xp = Number(ranks.find(r => r[0] === '49')[2]);
    for (const buffer of [0, 4]) {
      await seed(['PlayerStatsList', 'RANKXP'], xp, buffer);
      await seed(['PlayerStatsList', 'RANK'], 49, buffer);
    }
    await command('equipDefaultClass customclass1 class_custom_smg; equipDefaultClass customclass2 class_custom_smg');
    let balance = 100000;
    for (const id of tested.filter(x => ![153, 154].includes(x))) {
      const row = items.find(r => Number(r[0]) === id), cost = Number(row[12]);
      await command(`purchaseItem ${id}; setCurrentItemIndex ${id}; equipClassCurrentItem customclass1`);
      balance -= cost;
      assert.equal(await owned(id), 1, row[3]);
      assert.equal(await points(), balance, row[3] + ' price');
      if (!row[13].startsWith('facepaint')) assert.equal(await equipped(row[13]), id, row[13]);
      await command(`purchaseItem ${id}; clearItemNew ${row[3]}`);
      assert.equal(await points(), balance, 'duplicate purchase');
      assert.equal(await owned(id), 1, 'badge does not clear ownership');
    }
    console.log('PASS weapons, secondary, lethal, tactical, equipment, all perk slots, face paint/colors, duplicate and rank checks');
    for (const buffer of [0, 4]) await seed(['PlayerStatsList', 'CODPOINTS'], 0, buffer);
    await command('purchaseItem 154');
    assert.equal(await owned(154), 0, 'insufficient funds');
    for (const buffer of [0, 4]) await seed(['PlayerStatsList', 'CODPOINTS'], balance, buffer);
    for (let i = 0; i < 3; ++i) await seed(['ItemStats', 152, 'isProVersionUnlocked', i], 0, 4);
    await command('setCurrentItemIndex 152; purchaseCurrentProItem');
    assert.equal(await owned(153), 0, 'pro challenges required');
    for (let i = 0; i < 3; ++i) await seed(['ItemStats', 152, 'isProVersionUnlocked', i], 1, 4);
    await command('setCurrentItemIndex 152; purchaseCurrentProItem');
    balance -= 3000;
    assert.equal(await owned(153), 1);
    assert.equal(await equipped('specialty1'), 153);
    await seed(['WeaponStats', 29, 'purchasedOptions', 1], 0);
    await seed(['WeaponStats', 29, 'purchasedOptions', 15], 0);
    await command('setCurrentItemIndex 29; purchaseWeaponOption camo_dusty; purchaseWeaponOption camo_gold');
    balance -= 250;
    assert.equal(await read(['WeaponStats', 29, 'purchasedOptions', 1]), 1);
    assert.equal(await read(['WeaponStats', 29, 'purchasedOptions', 15]), 0, 'gold prestige gate');
    assert.equal(await points(), balance);
    assert.equal(await read(['PlayerStatsList', 'CODPOINTS'], 4), balance, 'reward balance mirrors purchases');
    assert.equal(await read(['PlayerStatsList', 'CURRENCYSPENT']), 100000 - balance);
    await command('copyclass 0 0');
    assert.equal(await equipped('primary', 'customclass2'), 29);
    await command('equipDefaultClass customclass2 class_custom_smg');
    assert.equal(await owned(29), 1, 'reset class preserves purchases');
    assert.equal(await equipped('primary'), 29, 'reset affects selected class only');
    await command('ddl_verbose 0');
    await sleep(2500); // No uploadStats: the frontend must autosave native DDL changes.
    const expected = { points: balance, globalPoints: balance, primary: 29 };
    assert.deepEqual(await saved(), expected);
    fs.writeFileSync(expectedFile, JSON.stringify(expected, null, 2));
    console.log('PASS insufficient funds, pro prerequisites/upgrade, cosmetics, prestige gate, class copy/reset and frontend autosave', expected);
  }
} finally { cdp.close(); }

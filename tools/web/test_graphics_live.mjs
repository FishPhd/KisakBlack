// Run against an isolated, headed cdp.mjs graphics test session:
// node tools/web/cdp.mjs launch --headed --session build-web/graphics-check/session.json --profile build-web/graphics-check/profile
// node tools/web/test_graphics_live.mjs [build-web/graphics-check/session.json]
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import { CDP, sleep, click, key } from './cdp-client.mjs';

const sessionFile = process.argv[2] || 'build-web/graphics-check/session.json';
const session = JSON.parse(fs.readFileSync(sessionFile));
assert.equal(session.headed, true, 'Graphics checks require a visible browser');
assert.match(session.profile.replaceAll('\\', '/'), /\/graphics-[^/]+\/profile$/, 'Use an isolated graphics test profile');
const cdp = await CDP.connect(session.debugPort, session.browserUrl);
const sid = await cdp.attach(session.targets.host);
const evaluate = expression => cdp.evaluate(expression, sid);
const snapshot = () => evaluate('window.kisak?.agent?.status()');
const command = text => evaluate(`window.kisak.state.send(${JSON.stringify(text)})`);
const report = { checks: [] };

async function wait(predicate, label, timeout = 60000) {
  const end = Date.now() + timeout;
  let state;
  while (Date.now() < end) {
    state = await snapshot();
    if (state?.crash) throw Error(state.crash);
    if (state && predicate(state)) return state;
    await sleep(200);
  }
  throw Error(`${label}: ${JSON.stringify(state?.engine?.graphics)}`);
}
const graphicsMenu = state => state.engine?.menus.find(m => m.name === 'options_graphics');
async function openGraphics() {
  await command('con_restricted_access 0; closemenu kisak_browser_graphics; closemenu vid_restart_popmenu; openmenu options_graphics');
  await wait(s => graphicsMenu(s)?.items.some(i => i.dvar === 'ui_r_mode'), 'graphics menu');
  await sleep(700); // stock menu animation / mouse readiness
}
async function clickRow(menuName, predicate) {
  const state = await snapshot();
  const item = state.engine.menus.find(m => m.name === menuName)?.items.find(predicate);
  assert.ok(item, `${menuName}: missing control`);
  const point = await evaluate(`(() => {
    const c = document.getElementById('game'), r = c.getBoundingClientRect(), p = ${JSON.stringify(item.rect)};
    const scale = Math.min(r.width / c.width, r.height / c.height);
    return [r.left + (r.width - c.width * scale) / 2 + (p[0] + p[2] / 2) * scale,
      r.top + (r.height - c.height * scale) / 2 + (p[1] + p[3] / 2) * scale];
  })()`);
  await click(cdp, sid, ...point);
  await sleep(300);
}
async function apply() {
  await clickRow('options_graphics', i => i.text === 'Apply');
  await wait(s => s.engine.menus.some(m => m.name === 'vid_restart_popmenu'), 'apply confirmation');
  await sleep(700);
  await clickRow('vid_restart_popmenu', i => /^yes$/i.test(i.text));
}
async function presented(width, height) {
  await wait(s => s.engine?.graphics?.width === width && s.engine.graphics.height === height, 'engine dimensions');
  await wait(s => s.engineAgeMs < 1000, 'fresh engine snapshot');
  const end = Date.now() + 10000;
  while (Date.now() < end) {
    const result = await evaluate(`({canvas:[game.width,game.height],resolution:window.kisak.state.res,selected:document.getElementById('res').value,status:document.getElementById('status').textContent})`);
    if (result.canvas[0] === width && result.canvas[1] === height) {
      assert.equal(result.resolution, `${width}x${height}`);
      assert.equal(result.selected, result.resolution);
      if (result.status.startsWith('Playing (')) assert.ok(result.status.includes(result.resolution));
      return result;
    }
    await sleep(200);
  }
  throw Error(`Canvas did not present ${width}x${height}`);
}
async function screenshot(name) {
  const shot = await cdp.send('Page.captureScreenshot', { format: 'png' }, sid);
  fs.writeFileSync(path.join(path.dirname(sessionFile), name), Buffer.from(shot.data, 'base64'));
}

try {
  await wait(s => s.menuReady && s.engine?.graphics, 'updated engine');
  report.renderer = await evaluate('window.kisak.state.rendererBackend');
  report.initial = (await snapshot()).engine.graphics;
  await openGraphics();
  const menu = graphicsMenu(await snapshot());
  assert.equal(menu.items.find(i => i.dvar === 'ui_r_aasamples').text, 'OFF (UNAVAILABLE IN BROWSER)');
  const warming = menu.items.find(i => i.dvar === 'r_shaderWarming');
  assert.ok(warming, 'Shader Warming toggle is missing');
  assert.equal(warming.value, '1', 'Shader Warming defaults on');
  assert.ok(!menu.items.some(i => i.dvar === 'web_warmShaders'), 'Internal warming alias is on the graphics menu');
  assert.ok(menu.items.some(i => i.name === 'kisak_browser_graphics_link'));
  report.checks.push('MSAA availability and Shader Warming defaulting on');

  // Select a different mode using the stock control; it remains staged until Apply.
  const before = (await snapshot()).engine.graphics;
  await clickRow('options_graphics', i => i.dvar === 'ui_r_mode');
  // Mouse clicks choose a direction according to the value's text position.
  // Two Left presses ensure a different mode regardless of that direction.
  await key(cdp, sid, 'ArrowLeft', 80);
  await key(cdp, sid, 'ArrowLeft', 80);
  const staged = await wait(s => s.engine.graphics.settings.ui_r_mode !== before.settings.r_mode, 'staged resolution', 10000);
  assert.equal(staged.engine.graphics.width, before.width);
  await apply();
  const applied = await wait(s => s.engine.graphics.settings.r_mode !== before.settings.r_mode, 'applied resolution');
  const mode = applied.engine.graphics.settings.r_mode;
  const aspectRatio = applied.engine.graphics.aspectRatio;
  const [width, height] = mode.split('x').map(Number);
  assert.ok(width && height, `Applied resolution: ${mode}`);
  await presented(width, height);
  report.checks.push(`Apply changes engine, canvas and launcher to ${mode} while windowed`);

  // Declining the restart must leave the applied resolution intact. Reopening
  // the menu initializes the staged controls from that applied selection.
  await openGraphics();
  await clickRow('options_graphics', i => i.dvar === 'ui_r_mode');
  await key(cdp, sid, 'ArrowLeft', 80);
  await key(cdp, sid, 'ArrowLeft', 80);
  await clickRow('options_graphics', i => i.text === 'Apply');
  await wait(s => s.engine.menus.some(m => m.name === 'vid_restart_popmenu'), 'cancel confirmation');
  await sleep(700);
  await clickRow('vid_restart_popmenu', i => /^no$/i.test(i.text));
  await openGraphics();
  await wait(s => s.engine.graphics.settings.ui_r_mode === mode, 'cancelled UI value');
  await presented(width, height);
  report.checks.push('Cancel preserves applied resolution and reopening restores its UI value');

  // The in-game fullscreen option must enter real browser fullscreen. F11 must
  // exit and update both the engine and the staged control without changing pixels.
  await clickRow('options_graphics', i => i.dvar === 'ui_r_fullscreen');
  await apply();
  await wait(s => s.engine.graphics.fullscreen, 'in-game fullscreen');
  assert.equal(await evaluate('!!document.fullscreenElement'), true);
  assert.equal((await snapshot()).engine.graphics.aspectRatio, aspectRatio);
  await presented(width, height);
  await key(cdp, sid, 'F11', 80);
  await wait(s => !s.engine.graphics.fullscreen && s.engine.graphics.settings.ui_r_fullscreen === '0', 'fullscreen exit synchronization');
  assert.equal(await evaluate('!!document.fullscreenElement'), false);
  await presented(width, height);
  report.checks.push('In-game fullscreen and F11 agree with the browser and preserve resolution');

  await evaluate("window.graphicsRequestFullscreen = stage.requestFullscreen; stage.requestFullscreen = () => Promise.reject(new Error('Graphics test rejection'))");
  try {
    await openGraphics();
    await clickRow('options_graphics', i => i.dvar === 'ui_r_fullscreen');
    await apply();
    await wait(s => !s.engine.graphics.fullscreen && s.engine.graphics.settings.ui_r_fullscreen === '0', 'rejected fullscreen rollback');
    assert.equal(await evaluate('!!document.fullscreenElement'), false);
    report.checks.push('Rejected fullscreen request rolls both engine and menu back to windowed');
  } finally {
    await evaluate('stage.requestFullscreen = window.graphicsRequestFullscreen; delete window.graphicsRequestFullscreen');
  }

  // Browser Graphics uses the existing controls, including an operable Back button.
  await openGraphics();
  await clickRow('options_graphics', i => i.name === 'kisak_browser_graphics_link');
  await wait(s => s.engine.menus.some(m => m.name === 'kisak_browser_graphics'), 'Browser Graphics');
  await sleep(700);
  const browserMenu = (await snapshot()).engine.menus.find(m => m.name === 'kisak_browser_graphics');
  for (const item of browserMenu.items) {
    assert.ok(item.rect[0] >= 0 && item.rect[1] >= 0, 'Browser Graphics control starts on screen');
    assert.ok(item.rect[0] + item.rect[2] <= width && item.rect[1] + item.rect[3] <= height, 'Browser Graphics control fits on screen');
  }
  await screenshot(`browser-graphics-${report.renderer}.png`);
  const distortion = (await snapshot()).engine.graphics.settings.web_disableDistortion === '1' ? '0' : '1';
  await clickRow('kisak_browser_graphics', i => i.dvar === 'web_disableDistortion');
  await wait(s => s.engine.graphics.settings.web_disableDistortion === distortion, 'distortion control');
  await clickRow('kisak_browser_graphics', i => i.text === 'BACK');
  await wait(s => !s.engine.menus.some(m => m.name === 'kisak_browser_graphics'), 'Browser Graphics Back');
  report.checks.push('Browser Graphics toggles and Back button work');

  await openGraphics();
  await clickRow('options_graphics', i => i.dvar === 'ui_r_picmip');
  await key(cdp, sid, 'ArrowLeft', 80);
  // Pick HIGH through the staged UI values, then use the real Apply confirmation.
  await command('set ui_r_picmip 1; set ui_r_picmip_bump 1; set ui_r_picmip_spec 1; set ui_r_picmip_manual 1');
  await wait(s => graphicsMenu(s)?.items.find(i => i.dvar === 'ui_r_picmip')?.value === '1', 'staged texture detail');
  await apply();
  await wait(s => s.engine.graphics.settings.r_picmip === '1' && s.engine.graphics.settings.r_picmip_bump === '1', 'applied texture detail');
  report.checks.push('Texture-detail Apply changes the engine values from the staged controls');

  await command('set sm_enable 1; set r_texFilterAnisoMax 1; set r_texFilterAnisoMin 8');
  await wait(s => s.engine.graphics.settings.sm_enable === '1' && s.engine.graphics.settings.r_texFilterAnisoMax === '8', 'shadow persistence and legacy filtering cap');
  await sleep(2500); // archived configuration + profile mirror
  await screenshot(`graphics-${report.renderer}.png`);
  await cdp.send('Page.reload', {}, sid);
  await wait(s => s.documentId !== staged.documentId && s.menuReady && s.engine?.graphics, 'saved graphics reload');
  const restored = (await snapshot()).engine.graphics;
  assert.equal(restored.settings.r_mode, mode);
  assert.equal(restored.settings.sm_enable, '1');
  assert.equal(restored.settings.web_disableDistortion, distortion);
  assert.equal(restored.settings.r_texFilterAnisoMin, '8');
  assert.equal(restored.settings.r_picmip, '1');
  assert.equal(restored.settings.r_picmip_bump, '1');
  assert.equal(restored.settings.r_picmip_spec, '1');
  await presented(width, height);
  report.checks.push('Resolution, texture detail, shadows, filtering and Browser Graphics persist across reload');
  report.restored = restored;
  report.passed = true;
  console.log(JSON.stringify(report, null, 2));
} finally {
  fs.writeFileSync(path.join(path.dirname(sessionFile), `graphics-${report.renderer || 'unknown'}-report.json`), JSON.stringify(report, null, 2));
  cdp.close();
}

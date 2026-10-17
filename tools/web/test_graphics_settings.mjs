import assert from 'node:assert/strict';
import { test } from 'node:test';
import { fitResolution, savedSettings, graphicsBootSettings, fullscreenSync } from '../../web/graphics-settings.js';

test('fresh defaults fit physical pixels up to 1080p and use the visible filtering control', () => {
  assert.equal(fitResolution(1920, 1080), '1920x1080');
  assert.equal(fitResolution(1280, 720, 1.5), '1920x1080');
  assert.equal(fitResolution(3840, 2160, 2), '1920x1080');
  assert.equal(fitResolution(1280, 680), '960x540');
  const args = graphicsBootSettings(new Map(), '1600x900', false).join(' ');
  assert.match(args, /r_mode 1600x900/);
  assert.match(args, /r_picmip 2/);
  assert.match(args, /web_disableWeaponFiringEffects 0/);
  assert.match(args, /r_texFilterAnisoMin 4/);
  assert.doesNotMatch(args, /r_customMode/);
});

test('weapon firing visuals preserve an explicit saved choice', () => {
  const saved = savedSettings('seta web_disableWeaponFiringEffects "1"');
  const args = graphicsBootSettings(saved, '1280x720', false).join(' ');
  assert.doesNotMatch(args, /web_disableWeaponFiringEffects /);
});

test('shader warming defaults on and a saved choice is kept', () => {
  const fresh = graphicsBootSettings(new Map(), '1280x720', false).join(' ');
  assert.match(fresh, /r_shaderWarming 1/);
  const saved = savedSettings('seta r_shaderWarming "0"');
  const args = graphicsBootSettings(saved, '1280x720', false).join(' ');
  assert.doesNotMatch(args, /r_shaderWarming /);
});

test('saved graphics choices survive reload instead of being replaced by boot defaults', () => {
  const saved = savedSettings('// generated\nseta r_picmip "0"\nseta r_texFilterAnisoMin "8"\nseta web_disableDistortion "0"\nseta web_warmShaders "0"\nseta r_picmip "3"');
  assert.equal(saved.get('r_picmip'), '3');
  assert.equal(saved.get('web_warmshaders'), '0');
  const args = graphicsBootSettings(saved, '1920x1080', true).join(' ');
  assert.doesNotMatch(args, /r_picmip /);
  assert.doesNotMatch(args, /r_texFilterAnisoMin /);
  assert.doesNotMatch(args, /web_disableDistortion /);
  assert.match(args, /r_picmip_bump 2/);
  assert.match(args, /r_fullscreen 1/);
});

function browser() {
  let change, denied = false;
  const notified = [], errors = [];
  let requests = 0;
  const document = {
    fullscreenElement: null,
    addEventListener(name, fn) { assert.equal(name, 'fullscreenchange'); change = fn; },
    async exitFullscreen() { this.fullscreenElement = null; change(); },
  };
  const stage = {
    async requestFullscreen() {
      ++requests;
      if (denied) throw new Error('User gesture required');
      document.fullscreenElement = stage;
      change();
    },
  };
  const sync = fullscreenSync({ document, stage, notify: v => notified.push(v), onError: e => errors.push(e) });
  return { document, stage, sync, notified, errors,
    deny() { denied = true; }, requests: () => requests };
}

test('menu fullscreen, F11 and browser exits converge despite stale engine snapshots', async () => {
  const b = browser();
  await b.sync.observe(false);
  await b.sync.observe(true);
  assert.equal(b.document.fullscreenElement, b.stage);
  assert.deepEqual(b.notified, [true]);
  b.sync.observe(true); // engine acknowledgement
  await b.document.exitFullscreen(); // Escape / browser chrome
  b.sync.observe(true); // stale snapshot must not reopen fullscreen
  assert.equal(b.document.fullscreenElement, null);
  assert.equal(b.requests(), 1);
  b.sync.observe(false);
  await b.sync.toggle();
  assert.equal(b.document.fullscreenElement, b.stage);
  assert.deepEqual(b.notified, [true, false, true]);
});

test('a rejected fullscreen request rolls the menu back without a retry loop', async () => {
  const b = browser();
  b.deny();
  b.sync.observe(false);
  await b.sync.observe(true);
  assert.deepEqual(b.notified, [false]);
  b.sync.observe(true);
  b.sync.observe(true);
  assert.equal(b.requests(), 1);
  b.sync.observe(false);
  assert.equal(b.errors.length, 1);
});

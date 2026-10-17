// Defaults are used only for settings absent from the engine's saved profile.
// Explicit launcher/URL choices use the same r_mode as the in-game menu.
export const graphicsDefaults = Object.freeze({
  r_aaSamples: 1,
  web_disableWeaponFiringEffects: 0,
  web_disableDistortion: 1,
  web_disableMotionBlur: 1,
  web_reduceDynamicLights: 1,
  r_dof_enable: 0,
  sm_enable: 0,
  r_picmip_manual: 1,
  r_picmip: 2, // NORMAL in the shipped texture-detail controls.
  r_picmip_bump: 2,
  r_picmip_spec: 2,
  // The existing slider controls the minimum. Keep the ceiling high enough
  // that increasing the slider can actually improve the rendered filtering.
  r_texFilterAnisoMax: 16,
  r_texFilterAnisoMin: 4,
});

export function savedSettings(text = '') {
  const settings = new Map();
  for (const match of text.matchAll(/^\s*set[au]?\s+(\w+)\s+(?:"([^"\r\n]*)"|([^\s;]+))/gim))
    settings.set(match[1].toLowerCase(), match[2] ?? match[3]);
  return settings;
}

export function fitResolution(width, height, pixelRatio = 1) {
  const w = width * pixelRatio, h = height * pixelRatio;
  return ['1920x1080', '1600x900', '1280x720', '960x540'].find(mode => {
    const [x, y] = mode.split('x').map(Number);
    return x <= w && y <= h;
  }) || '960x540';
}

export function graphicsBootSettings(saved, resolution, fullscreen) {
  return [
    `+set r_mode ${resolution}`,
    // Browsers cannot restore fullscreen without a fresh user gesture.
    `+set r_fullscreen ${fullscreen ? 1 : 0}`,
    ...Object.entries(graphicsDefaults)
      .filter(([name]) => !saved.has(name.toLowerCase()))
      .map(([name, value]) => `+set ${name} ${value}`),
  ];
}

// Browser events can overtake engine snapshots. Wait for the engine's acknowledgement
// of a browser exit/rejection before accepting another engine-originated request.
export function fullscreenSync({ document, stage, notify, onError }) {
  let lastEngine;
  let browserUpdate = null;
  const active = () => document.fullscreenElement === stage;
  const changed = () => {
    browserUpdate = lastEngine = active();
    notify(active());
  };
  document.addEventListener('fullscreenchange', changed);
  const request = async enabled => {
    if (active() === enabled) return;
    try {
      if (enabled) await stage.requestFullscreen();
      else await document.exitFullscreen();
    } catch (error) {
      onError?.(error);
      changed();
    }
  };
  return {
    toggle: () => request(!active()),
    observe(enabled) {
      if (browserUpdate !== null) {
        if (enabled !== browserUpdate) return;
        browserUpdate = null;
      }
      if (enabled === lastEngine) return;
      lastEngine = enabled;
      return request(enabled);
    },
  };
}

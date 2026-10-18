// Run the actual driver's JS graph with OfflineAudioContext in the owned Chrome
// session. This exercises audio samples, including browser compressor makeup.
import fs from 'node:fs';
import assert from 'node:assert/strict';
import {CDP} from './cdp-client.mjs';
const session = JSON.parse(fs.readFileSync(process.argv[2] || 'build-web/agent-session/session.json'));
const c = await CDP.connect(session.debugPort, session.browserUrl);
const sid = await c.attach(session.targets.host);
const source = fs.readFileSync('src/web/web_audio.cpp', 'utf8');
const graph = source.split('MAIN_THREAD_ASYNC_EM_ASM({')[1].split('    });')[0];
try {
  const results = await c.evaluate(`(async () => {
    const run = async (amplitude, voices, enabled = 1) => {
      const ctx = new OfflineAudioContext(2, 48000, 48000);
      const Module = {kisakAudio: ctx};
      const window = {kisakAudio: ctx};
      const HEAP16 = new Int16Array(2);
      (new Function('Module', 'window', 'HEAP16', ${JSON.stringify(graph)}))(Module, window, HEAP16);
      Module.kisakSetBuses(1,enabled,1,1,0.2,4,0.02,0.1,0,1,1,1,1,0,0.1);
      const bus = Module.kisakBuses.world.pre;
      for (let voice=0; voice<voices; ++voice) {
        const buffer = ctx.createBuffer(1, 48000, 48000), data = buffer.getChannelData(0);
        for (let i=0; i<data.length; ++i) {
          const t=i/48000;
          data[i] = amplitude*Math.sin(2*Math.PI*60*t)*(t<0.4 ? 1 : Math.exp(-15*(t-0.4)));
        }
        const node=ctx.createBufferSource(); node.buffer=buffer; node.connect(bus); node.start();
      }
      const rendered=await ctx.startRendering(); const samples=rendered.getChannelData(0);
      let peak=0, clipped=0, energy=0;
      for (let i=0; i<samples.length; ++i) {
        peak=Math.max(peak,Math.abs(samples[i])); if(Math.abs(samples[i])>=1)++clipped;
        if(i>=4800&&i<14400) energy+=samples[i]*samples[i];
      }
      return {amplitude,voices,enabled,peak,clipped,rms:Math.sqrt(energy/9600)};
    };
    return [await run(0.01,1),await run(1,1),await run(1,8),await run(1,64),await run(1,64,0)];
  })()`, sid);
  console.log(JSON.stringify(results));
  for (const result of results) assert.equal(result.clipped, 0, 'bass/transient mix must stay below full scale');
  // Below threshold, the only gains should be the engine gain and final headroom.
  assert.ok(results[0].rms < 0.01, 'browser automatic makeup must not amplify quiet sounds');
} finally {c.close();}

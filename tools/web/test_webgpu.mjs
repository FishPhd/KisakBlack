// Validate emitted WGSL using the browser's actual Dawn/Tint implementation.
// First build/run test_sm3 and capture stdout in build-web/wgsl-fixtures.txt.
import fs from 'node:fs';
import path from 'node:path';
import { CDP } from './cdp-client.mjs';
const fixturePath = path.resolve(process.argv[2] || 'build-web/wgsl-fixtures.txt');
const session = JSON.parse(fs.readFileSync(process.argv[3] || 'build-web/agent-session/session.json', 'utf8'));
const fixtures = fs.readFileSync(fixturePath, 'utf8').split(/\r?\n/)
  .filter(s => s.startsWith('WGSL_FIXTURE ')).map(s => JSON.parse(s.slice(13)));
if (fixtures.length < 2) throw new Error('Expected vertex and pixel WGSL fixtures');
const cdp = await CDP.connect(session.debugPort, session.browserUrl);
try {
  const sid = await cdp.attach(session.targets.host);
  const result = await cdp.evaluate(`(async () => {
    const adapter = await navigator.gpu?.requestAdapter();
    if (!adapter) throw new Error('No WebGPU adapter');
    const device = await adapter.requestDevice();
    const sources = ${JSON.stringify(fixtures)};
    const messages = [];
    const shaders = [];
    for (const code of sources) {
      const module = device.createShaderModule({code});
      const info = await module.getCompilationInfo();
      messages.push(...info.messages.map(m => ({type:m.type, message:m.message, line:m.lineNum})));
      shaders.push(module);
    }
    if (!messages.some(m => m.type === 'error')) {
      device.pushErrorScope('validation');
      await device.createRenderPipelineAsync({layout:'auto',
        vertex:{module:shaders[0],entryPoint:'main',buffers:[{arrayStride:16,attributes:[{shaderLocation:0,offset:0,format:'float32x4'}]}]},
        fragment:{module:shaders.at(-1),entryPoint:'main',targets:[{format:'rgba8unorm'}]}});
      const error = await device.popErrorScope();
      if (error) messages.push({type:'error',message:error.message});
    }
    device.destroy();
    return {passed:!messages.some(m => m.type === 'error'), fixtures:sources.length, messages};
  })()`, sid);
  console.log(JSON.stringify(result, null, 2));
  if (!result.passed) process.exitCode = 1;
} finally { cdp.close(); }

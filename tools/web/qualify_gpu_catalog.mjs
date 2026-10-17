// Qualification is a separate playback run: capture never proves compile hits.
import fs from 'node:fs';
import path from 'node:path';
import {spawn} from 'node:child_process';
const args=process.argv.slice(2);
const option=(name,fallback)=>{const at=args.indexOf(name);return at<0?fallback:args[at+1];};
const sessionFile=path.resolve(option('--session','build-web/agent-session/session.json'));
const session=JSON.parse(fs.readFileSync(sessionFile,'utf8'));
const manifestPath=path.join(session.gen,'manifest.json');
const manifest=JSON.parse(fs.readFileSync(manifestPath,'utf8'));
const maps=Object.keys(manifest.client?.maps || {}).sort();
const out=path.resolve(option('--out',path.join(path.dirname(sessionFile),'catalog-qualification')));
const command=['tools/web/benchmark_gpu.mjs','--session',sessionFile,'--best-case','--maps','all',
  '--bots','0,11','--repetitions','3','--cache','fresh','--out',out];
if (!args.includes('--timing')) command.push('--validation');
await new Promise((resolve,reject)=>{
  const child=spawn(process.execPath,command,{stdio:'inherit',windowsHide:true});
  child.on('error',reject);child.on('exit',code=>code===0?resolve():reject(new Error(`Qualification exited ${code}`)));
});
const results=JSON.parse(fs.readFileSync(path.join(out,'results.json'),'utf8'));
const missing=maps.filter(map=>![0,11].every(bots=>[0,1,2].every(rep=>results.some(r=>r.map===map && r.bots===bots && r.rep===rep && r.coverage?.verified && r.timingQualified))));
const qualification={status:missing.length?'incomplete':'verified',missingMaps:missing,build:session.build,
  maps,at:new Date().toISOString(),results:path.join(out,'results.json'),validation:!args.includes('--timing')};
fs.writeFileSync(path.join(out,'qualification.json'),JSON.stringify(qualification,null,2));
if (missing.length) throw new Error(`Catalog misses or unqualified measurements on: ${missing.join(', ')}`);
const latest=JSON.parse(fs.readFileSync(manifestPath,'utf8'));
if (JSON.stringify(latest.gpuPreparation)!==JSON.stringify(manifest.gpuPreparation)) throw new Error('Catalog changed during qualification');
latest.gpuPreparation.qualification=qualification;
fs.writeFileSync(manifestPath+'.qualification.tmp',JSON.stringify(latest,null,1));
fs.renameSync(manifestPath+'.qualification.tmp',manifestPath);
console.log('Complete catalog playback qualification PASS');

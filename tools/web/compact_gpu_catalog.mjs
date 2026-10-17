import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {brotliCompressSync, constants} from 'node:zlib';
import {CatalogPacker} from './gpu_catalog_codec.mjs';

export function compactCatalog(directory, profiles) {
  const result = [];
  for (const profile of profiles) {
    const packer = new CatalogPacker();
    let inputBytes = 0;
    for (const chunk of profile.chunks) {
      if (!/^gpu-catalog\/[a-f0-9]{64}\.bin$/.test(chunk.path)) throw new Error('invalid catalog path');
      const bytes = fs.readFileSync(path.join(directory,chunk.path));
      if (bytes.length !== chunk.size || createHash('sha256').update(bytes).digest('hex') !== chunk.sha256)
        throw new Error('catalog checksum mismatch');
      packer.add(bytes); inputBytes += bytes.length;
    }
    const selected=profile.scopes ? packer.selectExactDraws(profile.observedHashes || []) : packer.selectRecordedFamilies(profile.observedHashes || []);
    // Repacking a published selection must retain its original enumeration
    // coverage; its inputs no longer contain the rejected candidate families.
    const previous=profile.coverage;
    const coverage=previous?.kind===selected.kind && previous.catalogRecipes===selected.catalogRecipes
      && previous.observedRecipes===selected.observedRecipes ? previous : selected;
    const selectedSources=new Set(packer.selectedRecipes.flatMap(recipe=>[recipe.vs,recipe.ps]));
    const chunks = []; let encodedBytes = 0, transferBytes = 0;
    for (const bytes of packer.chunks()) {
      const hash = createHash('sha256').update(bytes).digest('hex'), relative = `gpu-catalog/${hash}.bin`;
      const compressed = brotliCompressSync(bytes,{params:{[constants.BROTLI_PARAM_QUALITY]:6}});
      fs.writeFileSync(path.join(directory,relative),bytes);
      fs.writeFileSync(path.join(directory,relative)+'.br',compressed);
      encodedBytes += bytes.length; transferBytes += compressed.length;
      chunks.push({path:relative,size:bytes.length,sha256:hash});
    }
    let groups;
    if (profile.scopes) {
      const selectedRecipes = packer.selectedRecipes;
      const scopes = new Map(Object.entries(profile.scopes).map(([scope,hashes])=>[scope,new Set(hashes)]));
      const bins = new Map([...scopes.keys(),'@shared','@weapons'].map(scope=>[scope,[]]));
      for (const recipe of selectedRecipes) {
        const hash = packer.recipeHash(recipe), owners = [...scopes].filter(([,set])=>set.has(hash)).map(([scope])=>scope);
        const scope = owners.includes('@frontend') ? '@frontend' : owners.includes('@weapons') ? '@weapons' : owners.length > 1 ? '@shared' : owners[0];
        if (!scope) throw new Error('Exact recipe has no capture scope');
        bins.get(scope).push(recipe);
      }
      groups = [];
      for (const [scope,recipes] of bins) {
        packer.selectedRecipes = recipes;
        const scopedChunks = [];
        for (const bytes of packer.chunks()) {
          const hash = createHash('sha256').update(bytes).digest('hex'), relative = `gpu-catalog/${hash}.bin`;
          fs.writeFileSync(path.join(directory,relative),bytes);
          fs.writeFileSync(path.join(directory,relative)+'.br',brotliCompressSync(bytes));
          scopedChunks.push({path:relative,size:bytes.length,sha256:hash});
        }
        groups.push({scope,recipes:recipes.length,chunks:scopedChunks});
      }
      packer.selectedRecipes = selectedRecipes;
    }
    result.push({...profile,chunks:groups ? [...new Map(groups.flatMap(g=>g.chunks).map(c=>[c.sha256,c])).values()] : chunks,
      ...(groups ? {groups} : {}),coverage,packing:{recipes:coverage.catalogRecipes,sources:selectedSources.size,inputSources:packer.sources.size,inputBytes,encodedBytes,transferBytes}});
  }
  return result;
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const directory = path.resolve(process.argv[2] || 'build-web/assets-gen');
  const manifestPath = path.join(directory,'manifest.json'), manifest = JSON.parse(fs.readFileSync(manifestPath,'utf8'));
  if (!manifest.gpuPreparation?.profiles?.length) throw new Error('No published catalog to compact');
  manifest.gpuPreparation.profiles = compactCatalog(directory,manifest.gpuPreparation.profiles);
  const temporary = manifestPath+'.tmp'; fs.writeFileSync(temporary,JSON.stringify(manifest,null,1)); fs.renameSync(temporary,manifestPath);
  console.log(JSON.stringify(manifest.gpuPreparation.profiles.map(p=>p.packing)));
}

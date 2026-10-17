// Offline repacking of the portable v2 recipe codec. Keep shader sources shared
// across captures and group variants together before making bounded chunks.
import {createHash} from 'node:crypto';
const LIMIT = 16 << 20, EXPANDED = 24 << 20;
function checksum(bytes) {
  let low = 0x84222325, high = 0xcbf29ce4;
  for (const byte of bytes) {
    low = (low ^ byte) >>> 0;
    const product = low * 435;
    high = (high * 435 + Math.floor(product / 4294967296) + (low << 8)) >>> 0;
    low = product >>> 0;
  }
  const result = Buffer.alloc(8); result.writeUInt32LE(low); result.writeUInt32LE(high, 4); return result;
}
const word = value => { const b = Buffer.alloc(4); b.writeUInt32LE(value); return b; };
const blob = value => [word(value.length), value];
export class CatalogPacker {
  sources = new Map();
  recipes = new Map();
  sourceBytes = 0;
  add(input) {
    const bytes = Buffer.from(input);
    if (bytes.length < 24 || bytes.length > LIMIT || !checksum(bytes.subarray(0, -8)).equals(bytes.subarray(-8)))
      throw new Error('invalid recipe checksum or size');
    let at = 0;
    const take = n => { if (n < 0 || at + n > bytes.length - 8) throw new Error('truncated recipe'); const b = bytes.subarray(at, at + n); at += n; return b; };
    const value = () => take(4).readUInt32LE();
    const readBlob = cap => { const n = value(); if (n > cap) throw new Error('oversize recipe field'); return take(n); };
    if (value() !== 0x4b475243 || value() !== 2) throw new Error('unsupported recipe codec');
    const count = value(); if (count > 8192) throw new Error('too many shader sources');
    const sources = [];
    for (let i = 0; i < count; ++i) {
      const b = readBlob(1 << 20); if (!b.length) throw new Error('empty shader');
      const hash = createHash('sha256').update(b).digest('hex');
      if (!this.sources.has(hash)) { this.sources.set(hash, {id:this.sources.size, bytes:Buffer.from(b)}); this.sourceBytes += b.length; }
      sources.push(this.sources.get(hash));
    }
    const total = value(); if (total > 4096) throw new Error('too many recipes');
    for (let i = 0; i < total; ++i) {
      const vs = sources[value()], ps = sources[value()];
      if (!vs || !ps) throw new Error('invalid shader reference');
      const start = at, key = readBlob(2048); if (!key.length) throw new Error('empty recipe identity');
      take(1024); readBlob(256); take(16);
      for (let stage = 0; stage < 2; ++stage) {
        if (value() > 256) throw new Error('invalid constants');
        const samplers = value(); if (samplers > 16) throw new Error('invalid samplers'); take(samplers * 16);
      }
      take(20);
      const id = key.toString('base64'), body = bytes.subarray(start, at), old = this.recipes.get(id);
      if (old) {
        if (old.vs !== vs || old.ps !== ps || !old.body.equals(body)) throw new Error('conflicting recipe identity');
      } else this.recipes.set(id, {vs, ps, key:Buffer.from(key), body:Buffer.from(body)});
    }
    if (at !== bytes.length - 8) throw new Error('trailing recipe bytes');
  }
  selectRecordedFamilies(observedHashes) {
    const observed=new Set(observedHashes), found=new Set(), families=new Set();
    const family=recipe=>{
      if(recipe.key.length<160)throw new Error('family selection requires normalized engine recipe identities');
      // The key starts with shader hashes + depth sampling mask, then 27
      // canonical render states. Keep the layout, strides, targets and topology
      // while joining material state variants to actual rendering families.
      return recipe.key.subarray(0,20).toString('hex')+'/'+recipe.key.subarray(128).toString('hex');
    };
    for(const recipe of this.recipes.values()) {
      const hash=checksum(recipe.key).readBigUInt64LE().toString();
      if(observed.has(hash)){found.add(hash);families.add(family(recipe));}
    }
    if(!found.size || found.size!==observed.size)throw new Error('Observed recipes are missing from capture inputs');
    this.selectedRecipes=[...this.recipes.values()].filter(recipe=>families.has(family(recipe)));
    return {kind:'recorded-families-v1',enumeratedRecipes:this.recipes.size,observedRecipes:found.size,
      families:families.size,catalogRecipes:this.selectedRecipes.length};
  }
  selectExactDraws(observedHashes) {
    const observed = new Set(observedHashes), found = new Set();
    this.selectedRecipes = [...this.recipes.values()].filter(recipe => {
      const hash = checksum(recipe.key).readBigUInt64LE().toString();
      if (!observed.has(hash)) return false;
      found.add(hash); return true;
    });
    if (!found.size || found.size !== observed.size) throw new Error('Observed recipes are missing from capture inputs');
    return {kind:'exact-draws-v1',enumeratedRecipes:this.recipes.size,observedRecipes:found.size,catalogRecipes:this.selectedRecipes.length};
  }
  recipeHash(recipe) { return checksum(recipe.key).readBigUInt64LE().toString(); }
  *chunks() {
    const sorted = [...(this.selectedRecipes || this.recipes.values())].sort((a,b) => a.vs.id-b.vs.id || a.ps.id-b.ps.id || Buffer.compare(a.body,b.body));
    let recipes = [], sources = new Map(), size = 24, expanded = 0;
    const encode = () => {
      const parts = [word(0x4b475243), word(2), word(sources.size)];
      for (const source of sources.keys()) parts.push(...blob(source.bytes));
      parts.push(word(recipes.length));
      for (const recipe of recipes) parts.push(word(sources.get(recipe.vs)), word(sources.get(recipe.ps)), recipe.body);
      const body = Buffer.concat(parts); return Buffer.concat([body, checksum(body)]);
    };
    for (const recipe of sorted) {
      const growth = () => 8 + recipe.body.length + [...new Set([recipe.vs,recipe.ps])].filter(s=>!sources.has(s)).reduce((n,s)=>n+4+s.bytes.length,0);
      const memory = recipe.vs.bytes.length + recipe.ps.bytes.length + 4096;
      if (recipes.length && (recipes.length >= 4096 || expanded + memory > EXPANDED || size + growth() > LIMIT)) {
        yield encode(); recipes = []; sources = new Map(); size = 24; expanded = 0;
      }
      if (size + growth() > LIMIT || memory > EXPANDED) throw new Error('recipe exceeds chunk budget');
      size += growth(); expanded += memory;
      for (const source of [recipe.vs,recipe.ps]) if (!sources.has(source)) sources.set(source,sources.size);
      recipes.push(recipe);
    }
    if (recipes.length) yield encode();
  }
}

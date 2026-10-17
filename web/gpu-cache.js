// Application pipeline recipes only. GPU objects and browser compiler caches
// never cross this interface. OPFS failures are recoverable cache misses.
// Catalog chunks are content-addressed in gpu-catalog-v1 and survive reload.
import {loadCatalog, openCatalogStore, assetIdentity} from './gpu-preparation.js';
export const MAP_LIMIT = 16 * 1024 * 1024, TOTAL_LIMIT = 64 * 1024 * 1024;
class RecipeDataError extends Error {}
function corruptRecipeRead(error, committed) {
  return error instanceof RecipeDataError || error instanceof SyntaxError || (committed && error.name === 'NotFoundError');
}
async function recipeFiles(dir, names) {
  const files = []; let bytes = 0;
  for (const name of names) {
    const file = await (await dir.getFileHandle(name)).getFile(); bytes += file.size;
    if (!Number.isSafeInteger(file.size) || file.size <= 0 || file.size > MAP_LIMIT || bytes > TOTAL_LIMIT)
      throw new RecipeDataError('invalid recipe set size');
    files.push(file);
  }
  return files;
}
export function demonstrablyBadRecipe(reason) {
  const text=String(reason || '');
  if (/device|memory|allocation|internal|cancel|destroyed|unknown|quota/i.test(text)) return false;
  return /native recipe validation failed|bad declaration|validation|error while parsing|invalid.*(?:shader|vertex|binding|pipeline)|WGSL.*error/i.test(text);
}
export function parseRecipeIndex(value, hash) {
  const chunks = Array.isArray(value) ? value : value?.version === 1 ? value.chunks : null;
  const observed = Array.isArray(value) ? [] : value?.observed || [];
  if (!Array.isArray(chunks) || !chunks.length || chunks.length > 4096 ||
      chunks.some(part => !new RegExp('^'+hash+'\\.[a-f0-9]+\\.\\d+\\.bin$').test(part)) ||
      new Set(chunks).size !== chunks.length ||
      !Array.isArray(observed) || observed.length > 100000 || observed.some(h=>typeof h!=='string' || !/^\d{1,20}$/.test(h)))
    throw new RecipeDataError('invalid cache index');
  return {chunks,observed};
}
export function evictionOrder(entries, incomingName, incomingBytes) {
  const others = entries.filter(e => e.name !== incomingName).sort((a,b) => a.at - b.at);
  let total = others.reduce((n,e) => n + e.size, incomingBytes);
  const remove = [];
  for (const e of others) { if (total <= TOTAL_LIMIT) break; remove.push(e.name); total -= e.size; }
  return remove;
}
export async function evictRecipeSets(dir, incomingHash, incomingBytes) {
  const groups = new Map();
  for await (const [name, handle] of dir.entries()) {
    const hash = /^([a-f0-9]{64})(?:\.|$)/.exec(name)?.[1];
    if (!hash || hash === incomingHash || !name.endsWith('.bin')) continue;
    const file = await handle.getFile();
    let group = groups.get(hash);
    if (!group) {
      let at = file.lastModified;
      try { at = Number(await (await (await dir.getFileHandle(hash+'.at')).getFile()).text()) || at; } catch {}
      groups.set(hash, group = {name:hash,size:0,at,files:[]});
    }
    group.size += file.size; group.files.push(name);
  }
  for (const hash of evictionOrder([...groups.values()], incomingHash, incomingBytes)) {
    // Invalidate the index first. A reader must never see a committed set with
    // one LRU-evicted chunk missing from its middle.
    try { await dir.removeEntry(hash+'.index.json'); }
    catch(error) { if(error.name!=='NotFoundError')throw error; }
    for (const name of groups.get(hash).files) await dir.removeEntry(name);
    await dir.removeEntry(hash+'.at').catch(()=>{});
  }
}
async function touchRecipeSet(dir, hash) {
  const writer = await (await dir.getFileHandle(hash+'.at',{create:true})).createWritable();
  try { await writer.write(String(Date.now())); await writer.close(); }
  catch(error) { await writer.abort?.().catch(()=>{}); throw error; }
}
async function withdrawRecipeSet(dir, hash, reason) {
  try {
    const writer=await (await dir.getFileHandle(hash+'.bad.json',{create:true})).createWritable();
    try {await writer.write(JSON.stringify({reason,at:Date.now()}));await writer.close();}
    catch(error){await writer.abort?.().catch(()=>{});throw error;}
  } catch { /* A full disk must not prevent withdrawing a broken optional set. */ }
  await dir.removeEntry(hash+'.index.json').catch(()=>{});
  await dir.removeEntry(hash+'.bin').catch(()=>{});
}
export function startGpuRecipeCache(session, opfs, manifest, options = {}) {
  const channel = new BroadcastChannel(`kisak-gpu-cache-${session}`);
  const identity = assetIdentity(manifest);
  const catalogStore = openCatalogStore(opfs).catch(() => null);
  const streams = new Map();
  let serial = Promise.resolve();
  const cacheHash = async key => [...new Uint8Array(await crypto.subtle.digest('SHA-256', new TextEncoder().encode((await identity) + key)))].map(v=>v.toString(16).padStart(2,'0')).join('');
  // Asset preparation may incorporate actual variants learned on prior runs.
  // Share the serialization queue with writes so a committed index is stable.
  channel.readLearning = key => {
    const result = serial.then(async () => {
      try {
        const hash=await cacheHash(key), dir=await opfs.getDirectoryHandle('gpu-recipes-v2');
        const file=await (await dir.getFileHandle(hash+'.index.json')).getFile();
        if(file.size>8*1024*1024)throw new RecipeDataError('oversize cache index');
        const index=parseRecipeIndex(JSON.parse(await file.text()),hash), chunks=[];
        if(!index.observed.length)return {chunks,observed:[],complete:true};
        for(const part of await recipeFiles(dir,index.chunks)) {
          chunks.push(new Uint8Array(await part.arrayBuffer()));
        }
        return {chunks,observed:index.observed,complete:true};
      } catch { return {chunks:[],observed:[],complete:false}; }
    });
    serial=result.catch(()=>{}); return result;
  };
  // The owned benchmark closes the channel and drains writes before clearing
  // recipes; otherwise an in-flight save can refill a supposedly fresh cache.
  channel.drain = () => serial;
  channel.onmessage = ({data}) => {
    serial = serial.then(async () => {
      if (!data || !['load','save','saveChunk','catalog','quarantine'].includes(data.kind) || typeof data.key !== 'string') return;
      try {
        if (data.kind === 'quarantine') {
          const quarantine=demonstrablyBadRecipe(data.reason);
          options.onCacheRejection?.({key:data.key,sha256:data.sha256,reason:data.reason,quarantined:quarantine});
          // Compilation cancellation, device loss and memory pressure do not
          // establish corrupt portable data. Keep it available for retry.
          if (!quarantine) return;
          if (/^[a-f0-9]{64}$/.test(data.sha256 || '')) {
            await (await catalogStore)?.quarantine(data.sha256, data.reason);
          } else if (data.key) {
            const hash = await cacheHash(data.key), dir = await opfs.getDirectoryHandle('gpu-recipes-v2',{create:true});
            await withdrawRecipeSet(dir,hash,data.reason);
          }
          return;
        }
        if (data.kind === 'saveChunk') {
          let ok = false, stream = streams.get(data.stream);
          try {
            if (!Number.isSafeInteger(data.stream) || !Number.isSafeInteger(data.part) || data.part < 0 ||
                !(data.bytes instanceof Uint8Array) || !data.bytes.length || data.bytes.length > MAP_LIMIT)
              throw new Error('invalid recipe stream');
            if (data.part === 0) {
              const hash = await cacheHash(data.key), observed=data.observed || [];
              if(!Array.isArray(observed) || observed.length>100000 || observed.some(h=>typeof h!=='string' || !/^\d{1,20}$/.test(h))) throw new Error('invalid observed identities');
              stream = {key:data.key, hash, observed, recipes:data.recipes || 0, generation:crypto.randomUUID().replaceAll('-',''), names:[], bytes:0,
                captured:options.onSave ? [] : null, dir:await opfs.getDirectoryHandle('gpu-recipes-v2',{create:true}), disk:true};
              streams.set(data.stream, stream);
            }
            if (!stream || stream.key !== data.key || data.part !== stream.names.length || data.part >= 4096)
              throw new Error('out-of-order recipe stream');
            stream.bytes += data.bytes.length;
            stream.captured?.push(data.bytes);
            if (stream.bytes > TOTAL_LIMIT) {
              if (!stream.captured) throw new Error('recipe set exceeds persistent-cache budget');
              // Capture tools can export a larger catalog; gameplay cache writes
              // stay within their disk budget and never buffer a whole set.
              if (stream.disk) for (const name of stream.names) await stream.dir.removeEntry(name).catch(()=>{});
              stream.disk = false;
            }
            const name = `${stream.hash}.${stream.generation}.${data.part}.bin`;
            stream.names.push(name); // Include a failed/empty staging file in cleanup.
            if (stream.disk) {
              const writer = await (await stream.dir.getFileHandle(name,{create:true})).createWritable();
              try { await writer.write(data.bytes); await writer.close(); }
              catch (error) { await writer.abort?.().catch(()=>{}); throw error; }
            }
            if (data.final) {
              if (stream.disk) {
                const writer = await (await stream.dir.getFileHandle(stream.hash+'.index.json',{create:true})).createWritable();
                try { await writer.write(JSON.stringify({version:1,chunks:stream.names,observed:stream.observed})); await writer.close(); }
                catch (error) { await writer.abort?.().catch(()=>{}); throw error; }
                // Publication succeeded. Cleanup failure must not delete the
                // replacement or leave the committed index pointing at holes.
                stream.published = true;
                try {
                  for await (const [old, handle] of stream.dir.entries()) if (old.endsWith('.bin') && !stream.names.includes(old)) {
                    if (old.startsWith(stream.hash+'.')) await stream.dir.removeEntry(old);
                  }
                  await touchRecipeSet(stream.dir, stream.hash);
                  await evictRecipeSets(stream.dir, stream.hash, stream.bytes);
                } catch { /* Retry garbage collection on the next save. */ }
              }
              if (stream.captured) options.onSave(data.key, stream.captured, {recipes:stream.recipes,observed:stream.observed});
              streams.delete(data.stream);
            }
            ok = true;
          } catch {
            if (stream && !stream.published) for (const name of stream.names) await stream.dir.removeEntry(name).catch(()=>{});
            streams.delete(data.stream);
          } finally {
            options.onAck?.(data.ticket, ok);
          }
          return;
        }
        if (data.kind === 'catalog') {
          const result = await loadCatalog(manifest, data.key, options.base || location.href,
            (bytes,chunk) => channel.postMessage({kind:'loaded',token:data.token,bytes,final:false,sha256:chunk.sha256,sourceKey:data.learnedKey}),
            options.fetchFile || fetch, await catalogStore, {scope:data.scope || '*'});
          // Exact local misses supplement the downloaded set in the same gate.
          // An absent learned cache is normal; a broken committed set is not.
          let learnedOk = true, learnedCommitted = false, learnedReason = '';
          if (typeof data.learnedKey === 'string') {
            try {
              const dir = await opfs.getDirectoryHandle('gpu-recipes-v2');
              const hash = await cacheHash(data.learnedKey);
              const file = await (await dir.getFileHandle(hash+'.index.json')).getFile();
              learnedCommitted = true;
              if (file.size > 8*1024*1024) throw new RecipeDataError('oversize learned index');
              const index = parseRecipeIndex(JSON.parse(await file.text()), hash);
              for (const file of await recipeFiles(dir,index.chunks)) {
                channel.postMessage({kind:'loaded',token:data.token,bytes:new Uint8Array(await file.arrayBuffer()),final:false,sourceKey:data.learnedKey || data.key});
              }
            } catch (error) {
              learnedOk = !learnedCommitted && error.name === 'NotFoundError';
              learnedReason = String(error);
              if (!learnedOk && corruptRecipeRead(error,learnedCommitted)) {
                const dir=await opfs.getDirectoryHandle('gpu-recipes-v2',{create:true});
                await withdrawRecipeSet(dir,await cacheHash(data.learnedKey),String(error));
              }
            }
          }
          channel.postMessage({kind:'loaded',token:data.token,bytes:new Uint8Array(),final:true,ok:result.ok && learnedOk});
          options.onCatalog?.({...result,ok:result.ok && learnedOk,learned:{ok:learnedOk,committed:learnedCommitted},
            ...(!learnedOk ? {reason:`local learned recipes unavailable: ${learnedReason}`} : {})});
          return;
        }
        const hash = await cacheHash(data.key);
        const dir = await opfs.getDirectoryHandle('gpu-recipes-v2', {create:true});
        const name = hash + '.bin';
        if (data.kind === 'load') {
          let indexed = false;
          try {
            const file=await (await dir.getFileHandle(hash+'.index.json')).getFile();
            indexed = true;
            if(file.size>8*1024*1024)throw new RecipeDataError('oversize cache index');
            const index = parseRecipeIndex(JSON.parse(await file.text()),hash).chunks;
            // Validate all file metadata before importing any part. This also
            // prevents a corrupt index from scheduling gigabytes of reads.
            for (const file of await recipeFiles(dir,index)) {
              channel.postMessage({kind:'loaded',token:data.token,bytes:new Uint8Array(await file.arrayBuffer()),final:false,sourceKey:data.learnedKey || data.key});
            }
            channel.postMessage({kind:'loaded',token:data.token,bytes:new Uint8Array(),final:true,ok:true});
            await touchRecipeSet(dir, hash).catch(()=>{});
            return;
          } catch (error) {
            if (indexed || error.name !== 'NotFoundError') {
              if (corruptRecipeRead(error,indexed)) await withdrawRecipeSet(dir,hash,String(error));
              channel.postMessage({kind:'loaded',token:data.token,bytes:new Uint8Array(),final:true,ok:false});
              return;
            }
          }
          let bytes = new Uint8Array(), ok = true;
          try {
            const file = await (await dir.getFileHandle(name)).getFile();
            if (!file.size || file.size > MAP_LIMIT) throw new RecipeDataError('invalid legacy recipe size');
            bytes = new Uint8Array(await file.arrayBuffer());
            // Refresh LRU without rewriting the recipe file.
            await touchRecipeSet(dir,hash).catch(()=>{});
          } catch(error) {
            ok = error.name === 'NotFoundError';
            if (corruptRecipeRead(error,false)) await withdrawRecipeSet(dir,hash,String(error));
          }
          channel.postMessage({kind:'loaded',token:data.token,bytes,final:true,ok,sourceKey:data.key});
        } else {
          if (Array.isArray(data.chunks)) {
            if (!data.chunks.length || data.chunks.length > 4096 || data.chunks.some(b => !(b instanceof Uint8Array) || !b.length || b.length > MAP_LIMIT)) return;
            options.onSave?.(data.key, data.chunks);
            const incoming = data.chunks.reduce((n, b) => n + b.length, 0);
            if (incoming > TOTAL_LIMIT) throw new Error('recipe set exceeds total persistent-cache budget');
            // A finished replacement publishes its index only after every new chunk
            // has closed. Partial first saves must not masquerade as complete.
            const generation = crypto.randomUUID().replaceAll('-', '');
            const names = data.chunks.map((_, i) => `${hash}.${generation}.${i}.bin`);
            const committed = [];
            let published = false;
            try {
              for (let i = 0; i < names.length; ++i) {
                let writer;
                try {
                  writer = await (await dir.getFileHandle(names[i],{create:true})).createWritable();
                  await writer.write(data.chunks[i]);
                  await writer.close();
                  committed.push(names[i]);
                } catch (error) {
                  if (writer) await writer.abort?.().catch(() => {});
                  throw error;
                }
              }
              let writer;
              try {
                writer = await (await dir.getFileHandle(hash+'.index.json',{create:true})).createWritable();
                await writer.write(JSON.stringify(names));
                await writer.close();
                published = true;
              } catch (error) {
                if (writer) await writer.abort?.().catch(() => {});
                throw error;
              }
              for await (const [entryName] of dir.entries()) if (entryName.startsWith(hash+'.') && entryName.endsWith('.bin') && !names.includes(entryName)) await dir.removeEntry(entryName);
              await touchRecipeSet(dir, hash);
              await evictRecipeSets(dir, hash, incoming);
            } catch {
              if (!published) for (const part of names) await dir.removeEntry(part).catch(()=>{});
            }
            return;
          }
          const bytes = data.bytes;
          if (!(bytes instanceof Uint8Array) || !bytes.length || bytes.length > MAP_LIMIT) return;
          await evictRecipeSets(dir, hash, bytes.length);
          const writer = await (await dir.getFileHandle(name,{create:true})).createWritable();
          try { await writer.write(bytes); await writer.close(); }
          catch (error) { await writer.abort?.().catch(() => {}); throw error; }
          // A successful legacy replacement must supersede an older chunk index.
          await dir.removeEntry(hash+'.index.json').catch(()=>{});
          for await (const [old] of dir.entries()) if (old.startsWith(hash+'.') && old.endsWith('.bin') && old !== name) await dir.removeEntry(old).catch(()=>{});
          await touchRecipeSet(dir, hash);
        }
      } catch {
        if (data.kind === 'load' || data.kind === 'catalog') channel.postMessage({kind:'loaded',token:data.token,bytes:new Uint8Array(),final:true,ok:false});
      }
    }).catch(() => {});
  };
  return channel;
}

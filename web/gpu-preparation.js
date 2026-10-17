// Catalogs contain portable recipes, never device handles or driver binaries.
// Downloaded catalog chunks are stored by content hash. A later session reads
// those files and fetches only hashes that are missing or failed.
export const CATALOG_VERSION = 3;
export const CHUNK_LIMIT = 16 * 1024 * 1024;
export const CATALOG_LIMIT = 128 * 1024 * 1024;
// Up to two additional passes after the first attempt.
export const MAX_DOWNLOAD_REATTEMPTS = 2;
const canonical = value => Array.isArray(value) ? value.map(canonical) : value && typeof value === 'object'
  ? Object.fromEntries(Object.keys(value).sort().map(key => [key, canonical(value[key])])) : value;
export async function sha256(bytes) {
  return [...new Uint8Array(await crypto.subtle.digest('SHA-256', bytes))].map(v => v.toString(16).padStart(2, '0')).join('');
}
export async function assetIdentity(manifest) {
  // Include the generation stamp: the asset packer does not hash every source
  // fastfile, so a same-size replacement must still invalidate old catalogs.
  // The optional disk-install inventory does not change rendered assets or
  // invalidate an otherwise identical portable pipeline catalog.
  const {gpuPreparation, installation, ...assets} = manifest;
  return sha256(new TextEncoder().encode(JSON.stringify(canonical(assets))));
}
export function preparationMode(params, zombies = false) {
  void zombies;
  const mode = params.get('preparation') || 'full';
  if (!['full', 'normal', 'off'].includes(mode)) throw new Error('preparation must be full, normal, or off');
  return mode;
}
export function residentStaticImages(mode, backend, zombies = false) {
  void zombies;
  return mode === 'full' && backend === 'webgpu';
}
export function pipelineNostall(params) {
  return params.get('pipelineNostall') !== '0';
}
export function runtimePipelineJobs(jobs) {
  return jobs === 4 || jobs === 16 ? jobs : 8;
}
export function settleFinished(pipelineJobs, elapsedMs, cancelled = false) {
  return cancelled || elapsedMs >= 45000 || (elapsedMs >= 250 && pipelineJobs === 0);
}
export function persistWarmRecipe(capture, learned) {
  return !!(capture || learned);
}
// Temporary: gameplay map loads skip the graphics gate. Catalog capture
// (preparation=full with capture) still prepares maps. Frontend warmup is separate.
export function skipMapLoadPreparation(mode, capture = false) {
  return !(mode === 'full' && capture);
}
export function preparationMessage(w, catalog) {
  if (w?.active && w.stage === 'settle')
    return {title:'Preparing graphics',hint:'Finishing shaders from the first view before play. Surfaces can appear when ready instead of stalling the frame.',reason:''};
  if (w?.active) return {title:'Preparing graphics',hint:'Preparing shaders and static resources before play. This is not a coverage guarantee.',reason:''};
  const catalogOnly = w?.status === 'incomplete' && !(w.failures > 0) && !(w.rejected > 0) && !(w.done < w.total)
    && catalog?.ok === false && catalog?.learned?.ok !== false;
  if (w?.status !== 'incomplete' || catalogOnly) return {title:'Graphics preparation',hint:'Preparation finished; gameplay coverage is checked separately.',reason:''};
  let reason='Some required graphics work could not be verified.';
  if (w.failures > 0) reason=`${w.failures} graphics compilation${w.failures===1?'':'s'} failed.`;
  else if (w.rejected > 0) reason=`${w.rejected} shader recipe${w.rejected===1?' was':'s were'} rejected or exceeded the preparation budget.`;
  else if (catalog?.ok === false) {
    const why=catalog.reason || '';
    if (catalog.learned?.ok === false) reason='Saved shader recipes could not be read completely. This attempt is incomplete.';
    else if (/configuration|renderer|profile/.test(why)) reason='No shader catalog matches this renderer build and graphics settings.';
    else if (/missing or stale/.test(why)) reason='The shader catalog is missing or out of date for these game assets.';
    else if (/quarantined/.test(why)) reason='A shader recipe set failed validation and has been set aside.';
    else reason=`Shader catalog unavailable: ${why || 'unknown loading failure'}.`;
  } else if (w.done < w.total) reason='Preparation stopped before all graphics work finished.';
  return {title:'Graphics preparation incomplete',hint:'Some shaders may still compile during play. The progress count is not a coverage check.',reason};
}
export function preparationReadiness({installation, catalog, preparation, renderer}) {
  const downloaded=!!installation?.downloaded && !!catalog?.downloaded;
  const validated=!!installation?.validated && !!catalog?.validated;
  const mapPrepared=preparation?.map?.startsWith('mp_') || catalog?.scope==='*';
  const scopeMatches=catalog?.scope==='*' || catalog?.scope===preparation?.map;
  const compiledOnDevice=validated && catalog?.ok===true && mapPrepared && scopeMatches && renderer==='webgpu' &&
    !preparation?.active && preparation?.status==='complete';
  const receipt=installation?.coverageReceipt;
  const coverageVerified=compiledOnDevice && installation?.coverageVerified===true &&
    receipt?.map===preparation?.map && receipt?.profile===catalog?.profile &&
    !!receipt?.catalogIdentity && receipt.catalogIdentity===catalog?.catalogIdentity;
  return {downloaded,validated,compiledOnDevice,coverageVerified};
}
// Retry only when the failed count is 25% or less of the batch. 1 of 4 retries;
// 1 of 3 does not. Zero failures do not retry.
export function shouldRetryDownloads(failed, total) {
  return failed > 0 && total > 0 && failed * 4 <= total;
}
function validChunk(chunk) {
  return !!chunk && Number.isSafeInteger(chunk.size) && chunk.size >= 24 && chunk.size <= CHUNK_LIMIT &&
    typeof chunk.sha256 === 'string' && /^[a-f0-9]{64}$/.test(chunk.sha256) && chunk.path === `gpu-catalog/${chunk.sha256}.bin`;
}
function uniqueChunks(chunks) {
  if (!Array.isArray(chunks) || chunks.length > 4096) return null;
  const unique = new Map();
  let bytes = 0;
  for (const chunk of chunks) {
    if (!validChunk(chunk)) return null;
    const previous = unique.get(chunk.sha256);
    if (previous && previous.size !== chunk.size) return null;
    if (previous) continue;
    unique.set(chunk.sha256, chunk); bytes += chunk.size;
    if (bytes > CATALOG_LIMIT) return null;
  }
  return [...unique.values()];
}
async function readChunk(response, size) {
  if (!response.body?.getReader) throw new Error('catalog truncated download');
  const reader = response.body.getReader(), bytes = new Uint8Array(size);
  let offset = 0;
  try {
    for (;;) {
      const {value, done} = await reader.read();
      if (done) break;
      if (!(value instanceof Uint8Array) || value.length > size - offset) throw new Error('catalog oversized download');
      bytes.set(value, offset); offset += value.length;
    }
    if (offset !== size) throw new Error('catalog truncated download');
    return bytes;
  } catch (error) {
    await reader.cancel(error).catch(() => {}); throw error;
  } finally { reader.releaseLock(); }
}
async function storedChunk(store, chunk, storage) {
  if (!store?.get) return null;
  let bytes = null;
  try { bytes = await store.get(chunk.sha256); } catch { ++storage.readFailures; return null; }
  if (bytes instanceof Uint8Array && bytes.length === chunk.size && await sha256(bytes) === chunk.sha256) return bytes;
  if (bytes != null) {
    ++storage.corruptChunks;
    if (store.delete) { try { await store.delete(chunk.sha256); } catch { ++storage.writeFailures; } }
  }
  return null;
}
async function downloadChunk(chunk, base, fetchFile, store, storage) {
  if (await store?.quarantined?.(chunk.sha256)) throw new Error('catalog chunk quarantined after native validation failure');
  const cached = await storedChunk(store, chunk, storage);
  if (cached) return cached;
  const response = await fetchFile(new URL(chunk.path, base), {signal: AbortSignal.timeout(60000)});
  if (!response?.ok) throw new Error(`catalog fetch: ${response?.status ?? 'failed'}`);
  const bytes = await readChunk(response, chunk.size);
  if (await sha256(bytes) !== chunk.sha256) throw new Error('catalog checksum mismatch');
  if (store?.put) {
    try { await store.put(chunk.sha256, bytes); }
    catch { ++storage.writeFailures; }
  }
  return bytes;
}
export async function openCatalogStore(root) {
  if (!root || typeof root.getDirectoryHandle !== 'function') return null;
  const dir = await root.getDirectoryHandle('gpu-catalog-v1', {create: true});
  const fileName = sha => `${sha}.bin`;
  return {
    async get(sha256) {
      if (!/^[a-f0-9]{64}$/.test(sha256)) return null;
      try {
        const file = await (await dir.getFileHandle(fileName(sha256))).getFile();
        if (file.size > CHUNK_LIMIT) return null;
        return new Uint8Array(await file.arrayBuffer());
      } catch (error) {
        if (error.name === 'NotFoundError') return null;
        throw error;
      }
    },
    async put(sha256, bytes) {
      if (!/^[a-f0-9]{64}$/.test(sha256) || !(bytes instanceof Uint8Array) || !bytes.length || bytes.length > CHUNK_LIMIT) return;
      const handle = await dir.getFileHandle(fileName(sha256), {create: true});
      const writer = await handle.createWritable();
      try {
        await writer.write(bytes);
        await writer.close();
      } catch (error) {
        await writer.abort?.().catch(() => {});
        throw error;
      }
    },
    async quarantined(sha256) {
      try { await dir.getFileHandle(`${sha256}.bad.json`); return true; } catch { return false; }
    },
    async quarantine(sha256, reason) {
      if (!/^[a-f0-9]{64}$/.test(sha256)) return;
      const writer = await (await dir.getFileHandle(`${sha256}.bad.json`,{create:true})).createWritable();
      try { await writer.write(JSON.stringify({sha256,reason,at:Date.now()})); await writer.close(); }
      catch (error) { await writer.abort?.().catch(()=>{}); throw error; }
    },
    async delete(sha256) {
      if (!/^[a-f0-9]{64}$/.test(sha256)) return;
      try { await dir.removeEntry(fileName(sha256)); } catch { /* already absent */ }
    },
    async prune(keep) {
      const retain = new Set(keep);
      const names = [];
      for await (const [name] of dir.entries()) names.push(name);
      for (const name of names) {
        if (!/^[a-f0-9]{64}\.bin$/.test(name) || retain.has(name.slice(0, -4))) continue;
        try { await dir.removeEntry(name); } catch { /* ignore */ }
      }
    },
  };
}
export function catalogChunks(profile, scope = '*') {
  if (!profile?.groups) return uniqueChunks(profile?.chunks || []);
  if (!Array.isArray(profile.groups) || profile.groups.length > 4096 ||
      profile.groups.some(g=>typeof g?.scope!=='string' || !uniqueChunks(g.chunks))) return null;
  const groups = profile.groups.filter(g => scope === '*' || g.scope === '@frontend' || g.scope === scope || (scope !== '@frontend' && ['@shared','@weapons'].includes(g.scope)));
  if (scope !== '*' && !groups.some(g => g.scope === scope)) return null;
  return uniqueChunks(groups.flatMap(g => g.chunks));
}
export async function loadCatalog(manifest, key, base, emit, fetchFile = fetch, store = null, {scope = '*'} = {}) {
  const catalog = manifest.gpuPreparation;
  const unavailable = reason => ({ok:false,reason,profile:key,scope,downloaded:false,validated:false});
  if (![2, CATALOG_VERSION].includes(catalog?.version) || catalog.assetIdentity !== await assetIdentity(manifest))
    return unavailable('missing or stale catalog');
  const profile = Array.isArray(catalog.profiles) ? catalog.profiles.find(p => p?.key === key) : null;
  if (!profile) return unavailable('no catalog matches this renderer build and graphics profile');
  const maps = ['@frontend', ...Object.keys(manifest.client?.maps || {})];
  if (!profile || !['recorded-families-v1','exact-draws-v1'].includes(profile.coverage?.kind) ||
      !Array.isArray(profile.maps) || !maps.every(map => profile.maps.includes(map)) || !(profile.chunks?.length || profile.groups?.length))
    return unavailable('catalog does not cover this configuration and all maps');
  const selected = catalogChunks(profile, scope);
  if (!selected || !selected.length) return unavailable('catalog does not cover requested scope');
  if (selected.some(chunk => !validChunk(chunk))) return unavailable('invalid catalog chunk');
  if (profile.groups && !maps.every(map => profile.groups.some(g => g.scope === map)))
    return unavailable('catalog groups do not cover all maps');
  // Asset identity alone does not identify a regenerated recipe set. Keep this
  // identity constant across scopes but change it when this profile changes.
  const catalogIdentity = await sha256(new TextEncoder().encode(JSON.stringify(canonical({
    version:catalog.version,assetIdentity:catalog.assetIdentity,key:profile.key,maps:profile.maps,
    coverage:profile.coverage,chunks:profile.chunks,groups:profile.groups,
  }))));
  const total = selected.length;
  const storage = {readFailures:0,writeFailures:0,cleanupFailures:0,corruptChunks:0};
  const delivered = new Array(total);
  const verified = new Array(total);
  const attempt = async list => {
    const failed = [];
    let reason = '';
    for (const item of list) {
      try {
        const bytes = await downloadChunk(item.chunk, base, fetchFile, store, storage);
        verified[item.index] = true;
        if (!delivered[item.index]) {
          await emit(bytes.slice(), item.chunk);
          delivered[item.index] = true;
        }
      } catch (error) {
        reason = error?.message || String(error);
        failed.push(item);
      }
    }
    return {failed, reason};
  };
  let pending = selected.map((chunk, index) => ({chunk, index}));
  let {failed, reason} = await attempt(pending);
  let reattempts = 0;
  while (reattempts < MAX_DOWNLOAD_REATTEMPTS && shouldRetryDownloads(failed.length, total)) {
    ++reattempts;
    ({failed, reason} = await attempt(failed));
  }
  // A partial download or scoped map load must not evict the previous complete
  // generation while the replacement is still being assembled.
  if (!failed.length && scope === '*' && store?.prune) {
    const keep = [];
    for (const candidate of catalog.profiles || []) for (const chunk of catalogChunks(candidate) || [])
      if (typeof chunk?.sha256 === 'string') keep.push(chunk.sha256);
    try { await store.prune(keep); } catch { ++storage.cleanupFailures; }
  }
  const chunks = delivered.filter(Boolean).length;
  const downloadedChunks = verified.filter(Boolean).length;
  return {ok: failed.length === 0, scope, profile:key, catalogIdentity, assetIdentity:catalog.assetIdentity,
    downloaded:downloadedChunks===total,validated:downloadedChunks===total,downloadedChunks,chunks,failed:failed.length,reattempts,
    persistence:{status:!store?'unavailable':storage.readFailures||storage.writeFailures||storage.cleanupFailures?'incomplete':'complete',...storage},
    ...(failed.length ? {reason: reason || 'incomplete catalog download'} : {})};
}

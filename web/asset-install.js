// Full installation is a disk operation. Never buffer an archive or the whole
// installation in JS/Wasm memory just to download or verify it.
export const HASH_BLOCK = 4 * 1024 * 1024;
const hex = bytes => [...new Uint8Array(bytes)].map(v => v.toString(16).padStart(2, '0')).join('');
export const digest = async bytes => hex(await crypto.subtle.digest('SHA-256', bytes));

export function validateEntry(entry) {
  if (!entry || typeof entry.path !== 'string' || (!/^(main|zone)\//i.test(entry.path) && entry.path !== 'localization.txt') || /[\\\x00-\x1f?#:]/.test(entry.path) ||
      entry.path.split('/').some(p => !p || p === '.' || p === '..') ||
      !Number.isSafeInteger(entry.size) || entry.size < 0 || entry.blockSize !== HASH_BLOCK ||
      !Array.isArray(entry.hashes) || entry.hashes.length !== Math.ceil(entry.size / HASH_BLOCK) ||
      entry.hashes.some(h => !/^[a-f0-9]{64}$/.test(h))) throw new Error('Invalid installation entry');
  return entry;
}
export function installationEntries(inventory) {
  if (inventory?.version !== 1 || !Array.isArray(inventory.files) || !inventory.files.length)
    throw new Error('Missing full installation inventory; regenerate with gen_manifest.py --inventory-only');
  const seen = new Set();
  return inventory.files.map(e => {
    validateEntry(e);
    const key = e.path.toLowerCase();
    if (seen.has(key)) throw new Error(`Duplicate installation path: ${e.path}`);
    seen.add(key); return e;
  });
}

// Also verifies arbitrarily split HTTP chunks. Checksums are on decoded bytes,
// so Content-Encoding (Brotli) does not change the inventory identity.
export async function verifyStream(stream, entry, write = async () => {}, progress = () => {}) {
  validateEntry(entry);
  const reader = stream.getReader(), block = new Uint8Array(HASH_BLOCK);
  let used = 0, total = 0, index = 0;
  try {
    for (;;) {
      const {value, done} = await reader.read();
      if (done) break;
      let at = 0;
      if (total + value.length > entry.size) throw new Error(`${entry.path}: oversized download`);
      while (at < value.length) {
        const n = Math.min(block.length - used, value.length - at);
        block.set(value.subarray(at, at + n), used); used += n; at += n; total += n;
        if (used === block.length || total === entry.size) {
          const bytes = block.subarray(0, used);
          if (await digest(bytes) !== entry.hashes[index++]) throw new Error(`${entry.path}: checksum mismatch`);
          await write(bytes); progress(used); used = 0;
        }
      }
    }
    if (total !== entry.size || used || index !== entry.hashes.length) throw new Error(`${entry.path}: truncated download`);
    return total;
  } catch (error) {
    await reader.cancel(error).catch(() => {}); throw error;
  } finally { reader.releaseLock(); }
}

export function assetNetworkGuard({enabled, gameplay, record, now = () => performance.now()}) {
  const counters = {requests:0,gameplayRequests:0,blockedRequests:0};
  const guard = (path, kind) => {
    const active = gameplay();
    ++counters.requests;
    if (active) ++counters.gameplayRequests;
    if (enabled && active) ++counters.blockedRequests;
    record({kind: 'asset-request', path, source: kind, at: now(), gameplay: active, blocked: enabled && active});
    if (enabled && active) throw new Error(`Asset network blocked during gameplay: ${path}`);
  };
  guard.counters = counters;
  return guard;
}

// Receipt keys include every block checksum. Size and modification time prove
// that a previously verified OPFS file has not been replaced in the meantime.
export function receiptMatches(receipt, entry, file) {
  return receipt?.size === file.size && file.size === entry.size && receipt.modified === file.lastModified &&
    receipt.identity === JSON.stringify([entry.blockSize, entry.hashes]);
}
export function verificationReceipt(entry, file) {
  return {size: file.size, modified: file.lastModified, identity: JSON.stringify([entry.blockSize, entry.hashes])};
}

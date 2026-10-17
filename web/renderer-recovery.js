// Consume the automatic retry exactly once. Never persist a renderer choice
// after the recovery reload, or let it override an explicit user selection.
const attemptKey = 'kisak-webgpu-recovery';
export function rendererForLaunch(params, storage) {
  storage.removeItem('kisak-renderer'); // migrate the old sticky override
  const recovery = storage.getItem(attemptKey);
  storage.removeItem(attemptKey);
  const explicit = params.get('renderer') || 'auto';
  return recovery && params.get('gpuRecovery') === recovery ? 'webgl2' : explicit;
}
export function scheduleRendererRecovery(storage, reason, now = Date.now()) {
  storage.setItem('kisak-webgpu-failure', reason);
  const previous = Number(storage.getItem('kisak-webgpu-recovery-at') || 0);
  if (now - previous < 60000) return false;
  storage.setItem('kisak-webgpu-recovery-at', String(now));
  storage.setItem(attemptKey, String(now));
  return true;
}
export function clearRendererRecovery(storage) {
  for (const key of [attemptKey, 'kisak-renderer', 'kisak-webgpu-failure', 'kisak-webgpu-recovery-at']) storage.removeItem(key);
}

export function rendererFailure(reason) {
  const text=String(reason);
  const kind=/device.*(?:lost|removed|destroyed|reset)|GPU process.*(?:exit|crash)/i.test(text)?'device-loss':/out.of.memory|allocation failed/i.test(text)?'memory':
    /vertex.*(?:slot|stream)|validation|invalid.*(?:buffer|pipeline|command)/i.test(text)?'validation':
    /shader|pipeline compilation/i.test(text)?'pipeline':/adapter|unsupported|unavailable.*feature/i.test(text)?'capability':'renderer';
  return {kind,reason:text};
}

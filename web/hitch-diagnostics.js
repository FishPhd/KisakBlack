// All producer times use epoch milliseconds (timeOrigin + performance.now()).
// Correlation reports overlapping work; it does not claim that overlap proves causation.
export class HitchDiagnostics {
  constructor(limit = 12000) { this.limit = limit; this.events = []; this.frames = []; this.dropped = 0; }
  push(list, value) {
    list.push(value);
    if (list.length > this.limit) { this.dropped += list.length - this.limit; list.splice(0, list.length - this.limit); }
  }
  event(event) { this.push(this.events, event); }
  frame(frame) { this.push(this.frames, frame); }
  report(threshold = 50) {
    return {clock: 'epoch-ms', dropped: this.dropped, thresholdMs: threshold,
      hitches: this.frames.filter(f => f.intervalMs > threshold).map(f => {
        const start = f.at - f.intervalMs;
        const overlaps = this.events.filter(e => e.at <= f.at && (e.end ?? e.at) >= start);
        // A cached recipe hit is useful context, but cannot explain a stall.
        // Only measured work or a concrete first-use event counts as evidence.
        const workOverlaps = overlaps.filter(e => e.kind !== 'pipeline-use' && e.kind !== 'pipeline-skip' &&
          e.kind !== 'pipeline-miss' &&
          ((e.end > e.at) || e.firstTextureUpload || e.firstStaticBufferUpload));
        return {...f, overlaps, workOverlaps, unexplained: !workOverlaps.length && !(f.pipelineWaitMs > 0) && !(f.firstTextureUploads > 0) && !(f.firstStaticBufferUploads > 0)};
      })};
  }
}

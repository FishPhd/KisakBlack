// Pointer lock changes warp the OS cursor. The movement attached to the first
// event in a new coordinate space is not a gameplay delta.
export class MouseMotion {
  reset() { this.mode = null; this.x = this.y = undefined; this.rx = this.ry = 0; }
  constructor() { this.reset(); }
  sample(event, locked, capture) {
    const mode = capture ? (locked ? 'locked' : 'relative') : 'cursor';
    const changed = mode !== this.mode;
    this.mode = mode;
    let dx = 0, dy = 0;
    if (!changed && mode === 'locked') {
      dx = event.movementX; dy = event.movementY;
    } else if (!changed && mode === 'relative') {
      // Unlocked movementX/Y can include browser recentering and screen changes.
      dx = event.clientX - this.x; dy = event.clientY - this.y;
    }
    this.x = event.clientX; this.y = event.clientY;
    if (changed) this.rx = this.ry = 0;
    if (!Number.isFinite(dx) || !Number.isFinite(dy)) return [0, 0];
    // Preserve subpixel motion without biasing sensitivity at high polling rates.
    this.rx += dx; this.ry += dy;
    const x = Math.trunc(this.rx), y = Math.trunc(this.ry);
    this.rx -= x; this.ry -= y;
    return [x, y];
  }
}

// Match the engine's connectionless XX message. The page can send it while
// asset initialization blocks the guest's engine worker and packet loop.
export class LoadingKeepalive {
  constructor() { this.reset(); }
  reset() { this.started = null; this.sent = -Infinity; }
  packet({guest, joined, connection, now}) {
    if (!guest || !joined || connection < 6 || connection > 8 || !Number.isInteger(connection)) {
      this.reset();
      return null;
    }
    this.started ??= now;
    // Preserve real failure handling: a stalled load gets at most five minutes
    // of page-side keepalives, and active gameplay always uses engine packets.
    if (now - this.started >= 300000 || now - this.sent < 1000) return null;
    this.sent = now;
    return new Uint8Array([127, 0, 0, 2, 12, 2, 255, 255, 255, 255, 88, 88, 0]);
  }
}

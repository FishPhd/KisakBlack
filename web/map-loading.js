// Account for background prefetch as well as foreground transfers.
export class MapLoading {
  constructor(render) { this.render = render; this.bytes = new Map(); this.active = null; }
  start(map, entries) {
    if (this.active?.map === map) return;
    this.active = {map, entries: new Map(entries.map(e => [e.path.toLowerCase(), e.size])), loading: false};
    this.update();
  }
  transfer(entry, bytes) {
    this.bytes.set(entry.path.toLowerCase(), Math.min(entry.size, Math.max(0, bytes)));
    this.update();
  }
  fail(entry, error) {
    this.bytes.delete(entry.path.toLowerCase());
    if (this.active?.entries.has(entry.path.toLowerCase())) {
      this.active.error = `Map download failed: ${error.message}`;
      this.update();
    }
  }
  update() {
    if (!this.active) return;
    let total = 0, done = 0;
    for (const [key, size] of this.active.entries) {
      total += size;
      done += Math.min(size, this.bytes.get(key) || 0);
    }
    const percent = total ? Math.floor(100 * done / total) : null;
    const size = bytes => (bytes / 1048576).toFixed(1);
    this.render({map: this.active.map, percent, done, total, error: this.active.error,
      text: this.active.error || (percent !== null && percent < 100
        ? `Downloading map… ${percent}% · ${size(done)} / ${size(total)} MB` : 'Loading map…')});
  }
  observe(snapshot, frame) {
    const active = this.active;
    if (!active) return;
    if (snapshot.connection > 0 && snapshot.connection < 10) active.loading = true;
    if ((snapshot.connection === 10 && snapshot.map === active.map)
        || (active.loading && snapshot.frontendReady && snapshot.connection === 0))
      active.readyFrame ??= frame;
  }
  presented(frame) {
    if (this.active?.readyFrame !== undefined && frame > this.active.readyFrame) this.clear();
  }
  clear() { this.active = null; this.render(null); }
}

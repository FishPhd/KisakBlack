export const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
export class CDP {
  constructor(ws) {
    this.ws = ws; this.next = 0; this.pending = new Map(); this.listeners = new Set();
    ws.onmessage = ({ data }) => {
      const m = JSON.parse(data), p = this.pending.get(m.id);
      if (p) { clearTimeout(p.timer); this.pending.delete(m.id); m.error ? p.reject(new Error(m.error.message)) : p.resolve(m.result); }
      else if (!m.id) for (const listener of this.listeners) listener(m);
    };
    ws.onclose = () => {
      for (const p of this.pending.values()) { clearTimeout(p.timer); p.reject(new Error("CDP connection closed")); }
      this.pending.clear();
    };
  }
  static async connect(port, expectedUrl) {
    const r = await fetch(`http://127.0.0.1:${port}/json/version`, { signal: AbortSignal.timeout(3000) });
    const v = await r.json();
    if (expectedUrl && v.webSocketDebuggerUrl !== expectedUrl) throw new Error("Debugging port belongs to another browser");
    const ws = new WebSocket(v.webSocketDebuggerUrl);
    await new Promise((resolve, reject) => { ws.onopen = resolve; ws.onerror = reject; });
    return new CDP(ws);
  }
  send(method, params = {}, sessionId, timeout = 10000) {
    if (this.ws.readyState !== WebSocket.OPEN) return Promise.reject(new Error("CDP connection is not open"));
    return new Promise((resolve, reject) => {
      const id = ++this.next;
      const timer = setTimeout(() => { this.pending.delete(id); reject(new Error(`CDP timeout: ${method}`)); }, timeout);
      this.pending.set(id, { resolve, reject, timer });
      try { this.ws.send(JSON.stringify({ id, method, params, sessionId })); }
      catch(e) { clearTimeout(timer); this.pending.delete(id); reject(e); }
    });
  }
  async attach(targetId) { return (await this.send("Target.attachToTarget", { targetId, flatten: true })).sessionId; }
  async evaluate(expression, sessionId) {
    const r = await this.send("Runtime.evaluate", { expression, returnByValue: true, awaitPromise: true }, sessionId);
    if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception?.description || r.exceptionDetails.text);
    return r.result.value;
  }
  close() { this.ws.close(); }
}
export async function click(cdp, sid, x, y, button = "left", ms = 80) {
  if (![x, y].every(Number.isFinite)) throw new Error("click requires finite page coordinates");
  const buttons={left:1,right:2,middle:4,back:8,forward:16};
  if (!buttons[button] || !Number.isFinite(ms) || ms<0 || ms>10000) throw new Error("Invalid mouse button or hold duration (0..10000 ms)");
  await cdp.send("Page.bringToFront", {}, sid);
  await cdp.send("Input.dispatchMouseEvent", { type: "mouseMoved", x, y }, sid);
  // Let hover reach the engine before selecting its focused menu item.
  await sleep(100);
  await cdp.send("Input.dispatchMouseEvent", { type: "mousePressed", x, y, button, buttons: buttons[button], clickCount: 1 }, sid);
  try { await sleep(ms); }
  finally { await cdp.send("Input.dispatchMouseEvent", { type: "mouseReleased", x, y, button, buttons: 0, clickCount: 1 }, sid); }
}
export async function key(cdp, sid, code, ms = 100) {
  if (!Number.isFinite(ms) || ms < 0 || ms > 10000) throw new Error("key duration must be 0..10000 ms");
  const special = { Enter: ["Enter", 13], Escape: ["Escape", 27], Space: [" ", 32], Tab: ["Tab", 9],
    NumpadEnter:["Enter",13], Backspace:["Backspace",8], Insert:["Insert",45], Delete:["Delete",46],
    ShiftLeft:["Shift",16],ShiftRight:["Shift",16],ControlLeft:["Control",17],ControlRight:["Control",17],
    AltLeft:["Alt",18],AltRight:["Alt",18],CapsLock:["CapsLock",20],NumLock:["NumLock",144],Pause:["Pause",19],
    Minus:["-",189],Equal:["=",187],BracketLeft:["[",219],BracketRight:["]",221],Backslash:["\\",220],
    Semicolon:[";",186],Quote:["'",222],Comma:[",",188],Period:[".",190],Slash:["/",191],
    ArrowLeft: ["ArrowLeft", 37], ArrowUp: ["ArrowUp", 38], ArrowRight: ["ArrowRight", 39], ArrowDown: ["ArrowDown", 40],
    Home: ["Home",36], End: ["End",35], PageUp: ["PageUp",33], PageDown: ["PageDown",34], Backquote: ["`", 192] };
  let info = special[code];
  if (/^Key[A-Z]$/.test(code)) info = [code.slice(3).toLowerCase(), code.charCodeAt(3)];
  if (/^Digit[0-9]$/.test(code)) info = [code.slice(5), code.charCodeAt(5)];
  if (/^Numpad[0-9]$/.test(code)) info = [code.slice(6), 96+Number(code.slice(6))];
  if (/^F([1-9]|1[0-2])$/.test(code)) info = [code, 111 + Number(code.slice(1))];
  if (!info) throw new Error(`Unsupported key: ${code}`);
  await cdp.send("Page.bringToFront", {}, sid);
  await cdp.evaluate("document.getElementById('game')?.focus()", sid);
  const params = { code, key: info[0], windowsVirtualKeyCode: info[1] };
  await cdp.send("Input.dispatchKeyEvent", { ...params, type: "rawKeyDown" }, sid);
  try { await sleep(ms); }
  finally { await cdp.send("Input.dispatchKeyEvent", { ...params, type: "keyUp" }, sid); }
}

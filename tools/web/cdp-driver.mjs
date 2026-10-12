import fs from "node:fs";
import path from "node:path";
import net from "node:net";
import { createHash } from "node:crypto";
import { spawn } from "node:child_process";
import { fileURLToPath } from "node:url";
import { CDP, sleep, click, key } from "./cdp-client.mjs";

const here = path.dirname(fileURLToPath(import.meta.url)), repo = path.resolve(here, "../..");
const args = process.argv.slice(2);
function option(name, fallback) {
  const i = args.indexOf(name); if (i < 0) return fallback;
  if (!args[i + 1] || args[i + 1].startsWith("--")) throw new Error(`${name} requires a value`);
  const value = args[i + 1]; args.splice(i, 2); return value;
}
function flag(name) { const i = args.indexOf(name); if (i < 0) return false; args.splice(i, 1); return true; }
const sessionFile = path.resolve(option("--session", path.join(repo, "build-web/agent-session/session.json")));
const debugPort = option("--port", ""), targetOption = option("--target", ""), role = option("--role", "host");
const timeout = Number(option("--timeout", "180000")), browserOption = option("--browser", "");
const httpPortOption = option("--http-port", ""), profileOption = option("--profile", "");
const root = path.resolve(option("--root", path.join(repo, "build-web")));
const assets = path.resolve(option("--assets", path.join(repo, "build")));
const gen = path.resolve(option("--gen", path.join(root, "assets-gen"))), outOption = option("--out", "");
const headed = flag("--headed"), trace = flag("--trace");
const keepOpen = flag("--keep-open");
const map = option("--map", "mp_nuked");
const readCache = option("--read-cache", "on");
if (!["on", "off"].includes(readCache)) throw new Error("--read-cache must be on or off");
const [command = "help", ...rest] = args, dir = path.dirname(sessionFile);
let cdp, sid, session;
const readSession = () => JSON.parse(fs.readFileSync(sessionFile, "utf8"));
const writeSession = s => {
  fs.mkdirSync(dir, { recursive: true });
  fs.writeFileSync(sessionFile + ".tmp", JSON.stringify(s, null, 2)); fs.renameSync(sessionFile + ".tmp", sessionFile);
};
const output = value => console.log(JSON.stringify(value, null, 2));
async function snapshotBuild() {
  const end = Date.now()+timeout;
  while (Date.now()<end) {
    const mjs=fs.statSync(path.join(root,"kisak.mjs")), wasm=fs.statSync(path.join(root,"kisak.wasm"));
    // Emscripten writes intermediate Wasm before finalizing its module loader.
    if (mjs.mtimeMs < wasm.mtimeMs) { await sleep(500); continue; }
    const destination=path.join(dir,"runtime"); fs.mkdirSync(destination,{recursive:true});
    for (const name of ["kisak.mjs","kisak.wasm"]) fs.copyFileSync(path.join(root,name),path.join(destination,name));
    await sleep(250);
    if (fs.statSync(path.join(root,"kisak.mjs")).mtimeMs !== mjs.mtimeMs || fs.statSync(path.join(root,"kisak.wasm")).mtimeMs !== wasm.mtimeMs) continue;
    const imports=WebAssembly.Module.imports(new WebAssembly.Module(fs.readFileSync(path.join(destination,"kisak.wasm"))));
    if (imports.some(i=>i.module === "env") && fs.readFileSync(path.join(destination,"kisak.mjs"),"utf8").includes("{a:wasmImports}")) continue;
    const web=path.join(destination,"web"); fs.mkdirSync(web,{recursive:true});
    for (const name of ["index.html","kisak.js","transport.js","zombies.html","transport-test.html"])
      fs.copyFileSync(path.join(repo,"web",name),path.join(web,name));
    const hash=file=>createHash("sha256").update(fs.readFileSync(file)).digest("hex");
    return {root:destination,web,sourceRoot:root,build:{wasm:hash(path.join(destination,"kisak.wasm")),loader:hash(path.join(destination,"kisak.mjs")),shell:hash(path.join(web,"kisak.js"))}};
  }
  throw new Error("Build outputs are still changing; finish the web build before launch");
}
const freePort = requested => new Promise((resolve, reject) => {
  const server = net.createServer(); server.once("error", reject);
  server.listen(requested ? Number(requested) : 0, "127.0.0.1", () => {
    const port = server.address().port; server.close(() => resolve(port));
  });
});
const status = () => cdp.evaluate("window.kisak?.agent?.status() || {enabled:false,status:document.getElementById('status')?.textContent}", sid);
async function waitFor(predicate, label, duration = timeout, previousDocument) {
  const end = Date.now() + duration; let last;
  while (Date.now() < end) {
    last = await status();
    if (previousDocument && last.documentId === previousDocument) { await sleep(100); continue; }
    if (last.crash || last.phase === "failed") throw new Error(`${label}: game failed: ${JSON.stringify(last)}`);
    if (predicate(last)) return last;
    await sleep(200);
  }
  throw new Error(`Timed out waiting for ${label}. Last game state: ${JSON.stringify(last)}`);
}
async function screenshot(name) {
  const filename = path.resolve(name || path.join(dir, `${role}-${Date.now()}.png`));
  const r = await cdp.send("Page.captureScreenshot", { format: "png" }, sid);
  fs.writeFileSync(filename, Buffer.from(r.data, "base64")); return filename;
}
async function connect() {
  session = fs.existsSync(sessionFile) ? readSession() : null;
  cdp = await CDP.connect(Number(debugPort || session?.debugPort || 9222), debugPort ? undefined : session?.browserUrl);
  const targets = (await cdp.send("Target.getTargets")).targetInfos;
  const id = targetOption || (debugPort ? "" : session?.targets?.[role]);
  const page = id ? targets.find(t => t.targetId === id) : targets.find(t => t.type === "page" && /^https?:/.test(t.url));
  if (!page) throw new Error(`No ${role} game page. Use --target TARGET or join ROOM.`);
  sid = await cdp.attach(page.targetId); await cdp.send("Runtime.enable", {}, sid); return page;
}

// Detached recorder owns the processes and records page/worker errors between commands.
async function recordSession() {
  const s = readSession(), stream = fs.createWriteStream(path.join(dir, "browser.jsonl"), { flags: "a" });
  const serverLog = fs.openSync(path.join(dir, "server.log"), "a"), browserLog = fs.openSync(path.join(dir, "chrome.log"), "a");
  let server, browser, connection, closing = false, poll;
  const record = event => {
    if (closing && event.type !== "session.closed") return;
    stream.write(JSON.stringify({ at: new Date().toISOString(), ...event }) + "\n");
  };
  const stop = reason => {
    if (closing) return; closing = true;
    clearInterval(poll);
    connection?.close(); browser?.kill(); server?.kill();
    // Read latest target metadata, which interactive join commands may have updated.
    const latest = readSession(); latest.status = "closed"; latest.closedAt = new Date().toISOString(); latest.reason = reason;
    writeSession(latest); record({ type: "session.closed", reason }); stream.end();
  };
  try {
    server = spawn(process.execPath, [path.join(here, "serve.mjs"), "--root", s.root, "--assets", s.assets,
      "--gen", s.gen, "--web", s.web, "--port", String(s.httpPort), "--agent-test"], { cwd: repo, windowsHide: true, stdio: ["ignore", serverLog, serverLog] });
    server.on("error", e => stop(e.message)); server.on("exit", code => stop(`server exited ${code}`));
    s.serverPid = server.pid; writeSession(s);
    const readyBy = Date.now() + 15000;
    while (true) {
      if (closing || Date.now() > readyBy) throw new Error("development server did not start");
      try { if ((await fetch(`http://127.0.0.1:${s.httpPort}/__agent/config`)).ok) break; } catch {}
      await sleep(100);
    }
    browser = spawn(s.browser, ["--remote-debugging-address=127.0.0.1", `--remote-debugging-port=${s.debugPort}`,
      `--user-data-dir=${s.profile}`, "--no-first-run", "--no-default-browser-check", "--window-size=1280,720",
      "--disable-background-timer-throttling", "--disable-renderer-backgrounding", "--disable-backgrounding-occluded-windows",
      ...(s.headed ? [] : ["--headless=new"]), "about:blank"], { windowsHide: true, stdio: ["ignore", browserLog, browserLog] });
    browser.on("error", e => stop(e.message)); browser.on("exit", code => stop(`browser exited ${code}`));
    s.browserPid = browser.pid; writeSession(s);
    const browserBy = Date.now() + 20000;
    while (!connection) {
      if (closing || Date.now() > browserBy) throw new Error("browser debugging endpoint did not start");
      try { connection = await CDP.connect(s.debugPort); } catch { await sleep(100); }
    }
    s.browserUrl = (await (await fetch(`http://127.0.0.1:${s.debugPort}/json/version`)).json()).webSocketDebuggerUrl;
    const autoAttach = { autoAttach: true, waitForDebuggerOnStart: false, flatten: true }, targets = new Map(), pages = new Map();
    connection.listeners.add(m => {
      if (m.method === "Target.attachedToTarget") {
        const { sessionId, targetInfo } = m.params; targets.set(sessionId, targetInfo.targetId);
        if (targetInfo.type === "page") pages.set(targetInfo.targetId, { sid: sessionId, cursor: 0, document: null });
        connection.send("Runtime.enable", {}, sessionId).catch(() => {});
        connection.send("Log.enable", {}, sessionId).catch(() => {});
        connection.send("Inspector.enable", {}, sessionId).catch(() => {});
        connection.send("Target.setAutoAttach", autoAttach, sessionId).catch(() => {});
      }
      if (["Runtime.consoleAPICalled", "Runtime.exceptionThrown", "Log.entryAdded", "Inspector.targetCrashed"].includes(m.method))
        record({ target: targets.get(m.sessionId), type: m.method, data: m.params });
    });
    await connection.send("Target.setDiscoverTargets", { discover: true });
    await connection.send("Target.setAutoAttach", autoAttach);
    const page = (await connection.send("Target.getTargets")).targetInfos.find(t => t.type === "page");
    s.targets = { host: page.targetId };
    const psid = await connection.attach(page.targetId);
    await connection.send("Page.navigate", { url: s.url }, psid);
    s.status = "running"; writeSession(s); record({ type: "session.started", targets: s.targets });
    let polling = false;
    poll = setInterval(async () => {
      if (polling || closing) return;
      polling = true;
      try {
        for (const [target, p] of pages) {
          try {
            const snapshot = await connection.evaluate(`window.kisak?.agent ? {id:window.kisak.agent.status().documentId,state:window.kisak.agent.status(), logs:window.kisak.agent.logs(${p.cursor})} : null`, p.sid);
            if (!snapshot) continue;
            fs.writeFileSync(path.join(dir,`last-state-${target}.json`),JSON.stringify({observedAt:new Date().toISOString(),...snapshot.state}));
            if (p.document !== snapshot.id) {
              p.cursor = 0; p.document = snapshot.id;
              snapshot.logs = await connection.evaluate("window.kisak.agent.logs()", p.sid);
            }
            for (const line of snapshot.logs.records) record({ target, type: "engine.log", data: line });
            p.cursor = snapshot.logs.latest;
          } catch { /* Navigation and target close invalidate the old execution context. */ }
        }
      } finally { polling = false; }
    }, 500);
    process.on("SIGINT", () => stop("recorder interrupted")); process.on("SIGTERM", () => stop("recorder terminated"));
  } catch (e) { stop(e.stack || e.message); process.exitCode = 1; }
  finally { fs.closeSync(serverLog); fs.closeSync(browserLog); }
}

async function launch() {
  if (fs.existsSync(sessionFile)) {
    const existing = readSession();
    if (["starting", "running"].includes(existing.status)) {
      try {
        const active = await CDP.connect(existing.debugPort, existing.browserUrl); active.close();
        throw new Error("Session already running; close it or select another --session");
      } catch (e) { if (e.message.startsWith("Session already")) throw e; }
    }
  }
  const browser = browserOption || [process.env.CHROME_PATH, "C:/Program Files/Google/Chrome/Application/chrome.exe",
    "C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe"].find(p => p && fs.existsSync(p));
  if (!browser) throw new Error("Chrome/Edge not found; supply --browser PATH");
  if (!fs.existsSync(path.join(root, "kisak.wasm")) || !fs.existsSync(path.join(gen, "manifest.json")))
    throw new Error("Missing web build or generated manifest; build and generate assets first");
  const snapshot=await snapshotBuild();
  const httpPort = await freePort(httpPortOption), port = await freePort(debugPort);
  writeSession({ status: "starting", startedAt: new Date().toISOString(), httpPort, debugPort: port,
    ...snapshot, assets, gen, browser, headed, profile: path.resolve(profileOption || path.join(dir, "profile")),
    url: `http://127.0.0.1:${httpPort}/?assets=/assets/&gen=/gen/&agent=1${readCache === "off" ? "&readcache=0" : ""}` });
  const daemonLog = fs.openSync(path.join(dir, "recorder.log"), "a");
  const recorder = spawn(process.execPath, [fileURLToPath(import.meta.url), "_record", "--session", sessionFile],
    { cwd: repo, detached: true, windowsHide: true, stdio: ["ignore", daemonLog, daemonLog] });
  recorder.unref(); fs.closeSync(daemonLog);
  const end = Date.now() + 40000;
  while (Date.now() < end) {
    const current = readSession();
    if (current.status === "running") { await connect(); output({ session: sessionFile, url: current.url, game: await status() }); return; }
    if (current.status === "closed") throw new Error(current.reason);
    await sleep(100);
  }
  throw new Error(`Session launch timed out; inspect ${path.join(dir, "recorder.log")}`);
}
async function join(room) {
  if (!session || !room || !/^[a-zA-Z0-9_-]+$/.test(room)) throw new Error("join requires an owned session and room ID");
  const url = new URL(session.url); url.searchParams.set("join", room); url.searchParams.set("player", "agent-guest");
  const { targetId } = await cdp.send("Target.createTarget", { url: url.href });
  session = readSession(); session.targets.guest = targetId; writeSession(session);
  sid = await cdp.attach(targetId); await cdp.send("Page.bringToFront", {}, sid); return targetId;
}
async function closeSession() {
  if (!session || debugPort) throw new Error("close requires an owned --session without --port override");
  for (const targetId of Object.values(readSession().targets || {})) {
    try {
      const targetSid = await cdp.attach(targetId);
      await cdp.evaluate("window.kisak?.state?.send?.('quit')", targetSid);
    } catch { /* A tab may have already closed. */ }
  }
  await sleep(1000);
  await cdp.send("Browser.close");
  const end = Date.now()+10000;
  while (Date.now()<end && readSession().status !== "closed") await sleep(100);
  if (readSession().status !== "closed") throw new Error("Browser closed but recorder did not confirm server shutdown");
}
async function itemPoint(selected) {
  return cdp.evaluate(`(() => {
    const c = document.getElementById('game'), r = c.getBoundingClientRect();
    const s = Math.min(r.width/c.width,r.height/c.height), p = ${JSON.stringify(selected.rect)};
    return [r.left+(r.width-c.width*s)/2+(p[0]+p[2]/2)*s,r.top+(r.height-c.height*s)/2+(p[1]+p[3]/2)*s];
  })()`, sid);
}
async function clickItem(pattern) {
  const expression = new RegExp(pattern, "i"); let selected, previous;
  await waitFor(s => {
    const candidates = s.engine?.menus.flatMap(m => m.items.map(i => ({ ...i, menu: m.name }))) || [];
    selected = candidates.find(i => expression.test(`${i.name} ${i.text}`) && i.rect[2] > 0 && i.rect[3] > 0);
    const signature = selected ? JSON.stringify([selected.menu, selected.index, selected.rect]) : null;
    const stable = signature && signature === previous && s.engineAgeMs < 1000;
    previous = signature;
    return stable;
  }, `menu item ${pattern}`);
  const point = await itemPoint(selected);
  await click(cdp, sid, ...point); return selected;
}
async function enterGameplay(step) {
  await waitFor(s => s.capture && !s.engine.uiActive || s.engine?.menus.some(m => /team|class/.test(m.name)), "team or class selection");
  if ((await status()).engine.menus.some(m => /team/.test(m.name))) {
    step("auto assign clicked", await clickItem("AUTO.?ASSIGN"));
    await waitFor(s => s.capture && !s.engine.uiActive || s.engine?.menus.some(m => /class/.test(m.name)), "class selection");
  }
  if ((await status()).engine.menus.some(m => /class/.test(m.name)))
    step("class selected", await clickItem("^.*\\bSMG\\b.*$"));
  await waitFor(s => s.capture && !s.engine.uiActive && s.engine.inputReady !== false, "gameplay input (after countdown)");
}
async function selectMap() {
  await clickItem("CHANGE.?MAP");
  const mapList = s => s.engine?.menus.find(m => m.name === "select_map")?.items.find(i => i.list);
  let previous;
  let state = await waitFor(s => {
    const signature = mapList(s) ? JSON.stringify(mapList(s).rect) : null;
    const stable=signature && signature === previous;
    previous=signature; return stable;
  }, "map picker");
  const list = mapList(state), rect=[...list.rect];
  rect[3]=Math.min(rect[3]*0.05,20);
  const [x,y]=await itemPoint({rect});
  await cdp.send("Input.dispatchMouseEvent",{type:"mouseMoved",x,y},sid);
  await sleep(200); state=await status();
  const row = mapList(state).list.rows.find(r => r.map === map);
  if (!row) throw new Error(`Map ${map} is unavailable in the map picker`);
  for (let attempt = 0; mapList(state).list.cursor !== row.index; ++attempt) {
    if (attempt > 128) throw new Error("Map selection did not converge");
    const old = mapList(state).list.cursor;
    await key(cdp, sid, old < row.index ? "ArrowDown" : "ArrowUp", 80);
    state = await waitFor(s => mapList(s)?.list.cursor !== old, "map cursor movement");
  }
  await key(cdp,sid,"Enter");
  await waitFor(s => !s.engine?.menus.some(m => m.name === "select_map"), "map picker close");
  return row;
}
async function smoke() {
  const result = { startedAt: new Date().toISOString(), steps: [] }, hostSid = sid;
  const step = (name, data) => { result.steps.push({ name, data }); console.log(name); };
  const previous = (await status()).documentId;
  await cdp.send("Page.bringToFront", {}, sid); await cdp.send("Page.reload", {}, sid);
  step("menu ready", await waitFor(s => s.menuReady, "host menu", timeout, previous));
  step("play clicked", await clickItem("^.*\\bPLAY\\b.*$"));
  step("create game clicked", await clickItem("CREATE.?GAME|PRIVATE.?MATCH"));
  await waitFor(s => s.engine?.menus.some(m => /privatelobby/.test(m.name)), "Private Match lobby");
  step("map selected", await selectMap());
  step("start match clicked", await clickItem("START.?MATCH"));
  await waitFor(s => s.engine?.connection === 10 && s.engine.serverTime > 0, "host active match");
  await enterGameplay(step);
  const before = await status();
  let turned=before;
  const controlDeadline=Date.now()+15000;
  // The game's spawn script can freeze controls after the class menu closes.
  // Probe with short real key holds until the camera responds, with a strict limit.
  while (JSON.stringify(turned.engine.angles) === JSON.stringify(before.engine.angles) && Date.now()<controlDeadline) {
    await key(cdp,sid,"ArrowRight",300); await sleep(200); turned=await status();
  }
  if (JSON.stringify(turned.engine.angles) === JSON.stringify(before.engine.angles)) throw new Error("Keyboard camera did not respond within 15 seconds");
  await cdp.evaluate("window.kisak.agent.look(25,-10)", sid);
  const looked = await waitFor(s => JSON.stringify(s.engine.angles) !== JSON.stringify(turned.engine.angles), "relative camera response", 5000);
  let after = looked;
  // A spawn can briefly freeze movement, or face a solid wall. Verify displacement
  // with a bounded set of real inputs rather than assuming every spawn faces open ground.
  for (const code of ["KeyW", "KeyD", "KeyS"]) {
    await key(cdp,sid,code,1000); await sleep(200); after = await status();
    if (after.engine.origin.some((v,i) => Math.abs(v-looked.engine.origin[i]) > 2)) break;
  }
  if (!after.engine.origin.some((v,i) => Math.abs(v-looked.engine.origin[i]) > 2)) throw new Error("Player did not move");
  step("movement and look verified", { before: before.engine, after: after.engine });
  step("host screenshot", await screenshot(path.join(dir, "smoke-host.png")));
  await join((await status()).room);
  step("guest joined", await waitFor(s => s.engine?.connection === 10 && s.engine.serverTime > 0, "guest active match"));
  await enterGameplay(step);
  step("guest screenshot", await screenshot(path.join(dir, "smoke-guest.png")));
  await cdp.send("Target.closeTarget", { targetId: readSession().targets.guest }); sid = hostSid;
  result.passed = true; fs.writeFileSync(path.join(dir, "smoke.json"), JSON.stringify(result, null, 2));
  if (!keepOpen) await closeSession();
  output({ passed: true, artifacts: dir });
}
async function bench() {
  const count = Number(rest[0] || 5);
  if (!Number.isInteger(count) || count < 1 || count > 30) throw new Error("bench count must be 1..30");
  await cdp.send("Page.bringToFront", {}, sid);
  const previous = (await status()).documentId;
  const url = new URL(await cdp.evaluate("location.href", sid));
  if (readCache === "off") url.searchParams.set("readcache", "0"); else url.searchParams.delete("readcache");
  await cdp.send("Page.navigate", { url: url.href }, sid);
  if (session) { session = readSession(); session.url = url.href; writeSession(session); }
  await waitFor(s => s.menuReady, "initial asset-cache fill", timeout, previous);
  const runs = [], reads = [];
  for (let i = 0; i < count; ++i) {
    if (trace && i === 0) { await cdp.send("Profiler.enable", {}, sid); await cdp.send("Profiler.start", {}, sid); }
    const old = (await status()).documentId;
    await cdp.send("Page.reload", {}, sid);
    const s = await waitFor(s => s.menuReady, `warm start ${i + 1}`, timeout, old);
    if (s.bootTransferred !== 0) throw new Error("Warm-start run imported boot assets; prefill the profile first");
    runs.push(s.timings); console.log(`run ${i + 1}: ${s.timings.menuReady.toFixed(1)} ms`);
    reads.push({ calls: s.readCalls, ms: s.readMs });
    if (trace && i === 0) {
      const { profile } = await cdp.send("Profiler.stop", {}, sid);
      fs.writeFileSync(path.join(dir, "startup.cpuprofile"), JSON.stringify(profile));
    }
  }
  const sorted = runs.map(r => r.menuReady).sort((a, b) => a - b);
  const median = sorted.length % 2 ? sorted[Math.floor(sorted.length/2)] : (sorted[sorted.length/2-1] + sorted[sorted.length/2])/2;
  const result = { medianMs: median, runs, reads, readCache, build:session?.build, url: url.href, browser: session?.browser, profile: session?.profile };
  fs.writeFileSync(path.resolve(outOption || path.join(dir, "benchmark.json")), JSON.stringify(result, null, 2)); output(result);
}
function compare() {
  const [beforeFile,afterFile]=rest;
  if (!beforeFile || !afterFile) throw new Error("compare requires baseline and optimized JSON files");
  const before=JSON.parse(fs.readFileSync(beforeFile)),after=JSON.parse(fs.readFileSync(afterFile));
  const origin=r=>{const url=new URL(r.url);url.searchParams.delete("readcache");return url.href;};
  if (!before.build || JSON.stringify(before.build)!==JSON.stringify(after.build) || before.profile!==after.profile
      || before.browser!==after.browser || origin(before)!==origin(after)) throw new Error("Benchmarks must use the same build, browser, origin, and profile");
  if (before.runs.length<5 || after.runs.length<5 || !(before.medianMs>0) || !(after.medianMs>0)) throw new Error("At least five valid warm runs are required in each benchmark");
  const reduction=1-after.medianMs/before.medianMs, result={baselineMs:before.medianMs,optimizedMs:after.medianMs,reductionPercent:reduction*100,passed:reduction>=0.3,build:after.build};
  if(outOption) fs.writeFileSync(path.resolve(outOption),JSON.stringify(result,null,2));
  output(result); if(!result.passed) process.exitCode=1;
}

try {
  if (command === "_record") await recordSession();
  else if (command === "compare") compare();
  else if (command === "help") console.log(`Node 22+ browser driver:
  launch [--headed] [--browser PATH] [--http-port N] [--port N] [--assets PATH]
  status | wait [menu|game] | close | join ROOM | smoke | bench [5] [--trace] [--out FILE]
  compare BASELINE_JSON OPTIMIZED_JSON [--out FILE]   verifies the 30% median target
  click X Y [left|right|middle] [MS] | move X Y | wheel X Y DELTA
  click-item REGEX | key KeyboardEvent.code [MS] | look DX DY | shot [FILE]
  logs [SECONDS] | eval EXPRESSION | reload | stacks
All commands accept --session FILE and --role host|guest or --target ID.
Use a fixed --http-port and the same --session/profile for comparable warm starts.
Artifacts, profile, and continuous page/worker logs are beside session.json.
smoke closes the owned browser and server; --keep-open retains the host for inspection.
smoke selects Nuketown through the map picker; --map MAP changes the test map.
bench supports --read-cache off|on for A/B measurement without changing the build.`);
  else if (command === "launch") await launch();
  else {
    await connect();
    if (command === "status") output(await status());
    else if (command === "wait") output(await waitFor(s => rest[0] === "game" ? s.engine?.connection === 10 : s.menuReady, rest[0] || "menu"));
    else if (command === "close") {
      await closeSession(); output({ closed: sessionFile });
    } else if (command === "reload") {
      const previous = (await status()).documentId;
      await cdp.send("Page.reload", {}, sid);
      await waitFor(s => s.enabled && s.documentId !== previous, "new document", timeout, previous);
    }
    else if (command === "eval") output(await cdp.evaluate(rest.join(" "), sid));
    else if (command === "shot") output({ screenshot: await screenshot(rest[0]) });
    else if (command === "click") await click(cdp, sid, Number(rest[0]),Number(rest[1]),rest[2]||"left",Number(rest[3]||80));
    else if (command === "move" || command === "wheel") {
      const values=rest.map(Number);
      if (values.length < (command === "wheel" ? 3:2) || !values.every(Number.isFinite)) throw new Error("Mouse coordinates/delta must be finite");
      await cdp.send("Page.bringToFront",{},sid);
      await cdp.send("Input.dispatchMouseEvent",{type:command === "wheel" ? "mouseWheel":"mouseMoved",x:values[0],y:values[1],
        ...(command === "wheel" ? {deltaX:0,deltaY:values[2]}:{})},sid);
    }
    else if (command === "click-item") output(await clickItem(rest.join(" ")));
    else if (command === "key") await key(cdp, sid, rest[0], Number(rest[1] || 100));
    else if (command === "look") {
      await cdp.send("Page.bringToFront", {}, sid);
      await cdp.evaluate(`window.kisak.agent.look(${JSON.stringify(Number(rest[0]))},${JSON.stringify(Number(rest[1]))})`, sid);
    } else if (command === "join") output({ target: await join(rest[0]) });
    else if (command === "smoke") await smoke();
    else if (command === "bench") await bench();
    else if (command === "logs") {
      const end = Date.now() + Number(rest[0] || 10)*1000; let cursor = 0;
      const logFile = path.join(dir, "browser.jsonl");
      let offset = session && fs.existsSync(logFile) ? fs.statSync(logFile).size : 0;
      while (Date.now() < end) {
        const r = await cdp.evaluate(`window.kisak.agent.logs(${cursor})`, sid);
        for (const line of r.records) console.log(JSON.stringify(line)); cursor = r.latest; await sleep(250);
        if (session && fs.existsSync(logFile)) {
          const size = fs.statSync(logFile).size;
          if (size > offset) {
            const bytes = Buffer.alloc(size-offset), fd = fs.openSync(logFile,"r");
            fs.readSync(fd,bytes,0,bytes.length,offset); fs.closeSync(fd); offset = size;
            for (const row of bytes.toString().split("\n")) {
              try { const event = JSON.parse(row); if (event.type !== "engine.log") console.log(row); } catch {}
            }
          }
        }
      }
      if (session) console.log(`Full page and worker console: ${path.join(dir, "browser.jsonl")}`);
    } else if (command === "stacks") {
      const targets = (await cdp.send("Target.getTargets")).targetInfos.filter(t => /worker/.test(t.type));
      for (const target of targets) {
        const worker = await cdp.attach(target.targetId); let paused;
        const listener = m => { if (m.sessionId === worker && m.method === "Debugger.paused") paused = m.params; };
        cdp.listeners.add(listener);
        try {
          await cdp.send("Debugger.enable", {}, worker); await cdp.send("Debugger.pause", {}, worker);
          const until = Date.now()+4000; while (!paused && Date.now()<until) await sleep(50);
          output({ target: target.targetId, frames: paused?.callFrames.slice(0,16).map(f => ({ name: f.functionName, url: f.url, line: f.location.lineNumber })) });
        } finally {
          await cdp.send("Debugger.resume", {}, worker).catch(() => {});
          await cdp.send("Debugger.disable", {}, worker).catch(() => {}); cdp.listeners.delete(listener);
        }
      }
    } else throw new Error(`Unknown command: ${command}`);
  }
} catch (e) {
  console.error(e.message);
  if (session) try {
    let state,logs;
    try { state=await status(); logs=await cdp.evaluate("window.kisak?.agent?.logs()",sid); }
    catch {
      const target=targetOption || session.targets?.[role];
      const last=path.join(dir,`last-state-${target}.json`);
      if(fs.existsSync(last)) state={...JSON.parse(fs.readFileSync(last)),lastObserved:true};
    }
    const failure = { error: e.message, state, logs };
    if (session) {
      fs.writeFileSync(path.join(dir,"failure.json"),JSON.stringify(failure,null,2));
      if(cdp && sid) await screenshot(path.join(dir,"failure.png")).catch(()=>{});
      console.error(`Failure artifacts: ${dir}`);
    }
  } catch {}
  process.exitCode = 1;
  if (command === "smoke" && !keepOpen && session) await closeSession().catch(() => {});
} finally { cdp?.close(); }

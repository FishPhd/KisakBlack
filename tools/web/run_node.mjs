// Headless runner: boots the web build of the dedicated server under Node.
//
//   node tools/web/run_node.mjs --build build-web --gamedir D:\path\to\game [-- <engine args>]
//
// <gamedir> must contain main/ and zone/. Engine arguments default to a TDM server on mp_nuked.
// Lines typed on stdin are sent to the server console. Press Ctrl+C to stop.
//
// Options:
//   --build <dir>        directory with kisak.mjs / kisak.wasm (default: build-web)
//   --gamedir <dir>      host game directory (required)
//   --seconds <n>        exit after n seconds (default: run until Ctrl+C)
//   --cmd "<line>"       console line to send after startup; repeatable (e.g. --cmd "addbots 6")
//                        "@wait <s>" as a --cmd line pauses before the next one.
//   --cmd-delay <s>      seconds to wait before sending the --cmd lines (default 20)
//   --exit-on <regex>    exit 0 as soon as an output line matches (e.g. "Com_Error|FSTRACE-DONE")
//   --dump-stacks <s>    after s seconds pause every engine thread and print its call stack, then exit
//   --relay <ws-url>     host a room through the browser's WebSocket packet transport
//   --join <room>        join that room as a client (use +set dedicated 0)
//                        (hang diagnosis; the wasm needs function names: KISAK_WEB_PROFILING_FUNCS=ON)
//   --lazy-source <dir>  exercise the on-demand asset path headlessly, the way the browser page does:
//                        <gamedir>/manifest.txt lists files that are not in <gamedir> yet; they are
//                        copied from <dir> when the engine asks (Module.kisakEnsureFile) or when the
//                        next map is prefetched (Module.kisakPrefetchMap).

import fs from "node:fs";
import inspector from "node:inspector";
import os from "node:os";
import path from "node:path";
import readline from "node:readline";
import { GameTransport } from "../../web/transport.js";
import { pathToFileURL } from "node:url";

const argv = process.argv.slice(2);
const opts = { build: "build-web", gamedir: "", seconds: 0, cmds: [], cmdDelay: 20, exitOn: null, lazySource: "", dumpStacks: 0, relay: "", join: "" };
const engineArgs = [];
for (let i = 0; i < argv.length; ++i) {
  const a = argv[i];
  if (a === "--build") opts.build = argv[++i];
  else if (a === "--gamedir") opts.gamedir = argv[++i];
  else if (a === "--seconds") opts.seconds = Number(argv[++i]);
  else if (a === "--cmd") opts.cmds.push(argv[++i]);
  else if (a === "--cmd-delay") opts.cmdDelay = Number(argv[++i]);
  else if (a === "--exit-on") opts.exitOn = new RegExp(argv[++i]);
  else if (a === "--lazy-source") opts.lazySource = argv[++i];
  else if (a === "--dump-stacks") opts.dumpStacks = Number(argv[++i]);
  else if (a === "--relay") opts.relay = argv[++i];
  else if (a === "--join") opts.join = argv[++i];
  else if (a === "--") { engineArgs.push(...argv.slice(i + 1)); break; }
  else engineArgs.push(a);
}
if (!opts.gamedir) {
  console.error("run_node: --gamedir <host path with main/ and zone/> is required");
  process.exit(2);
}
if (engineArgs.length === 0) {
  engineArgs.push("+set", "dedicated", "1", "+set", "sv_maxclients", "12", "+set", "g_gametype", "tdm", "+map", "mp_nuked");
}

// ---- on-demand assets (headless stand-in for web/kisak.js) ----
const lazy = { manifest: new Map(), pending: new Map() };

// Case-insensitive path walk: the manifest keeps the case of the original install.
function resolveInsensitive(root, rel) {
  let cur = root;
  for (const part of rel.split("/").filter(Boolean)) {
    const hit = fs.readdirSync(cur).find((n) => n.toLowerCase() === part.toLowerCase());
    if (!hit) return null;
    cur = path.join(cur, hit);
  }
  return cur;
}

function ensureFile(rel) {
  const key = rel.toLowerCase();
  const entry = lazy.manifest.get(key);
  if (!entry) return Promise.reject(new Error(`not in manifest: ${rel}`));
  if (lazy.pending.has(key)) return lazy.pending.get(key);
  const p = (async () => {
    const dst = path.join(path.resolve(opts.gamedir), entry.path);
    if (fs.existsSync(dst) && fs.statSync(dst).size === entry.size) return;
    const src = resolveInsensitive(path.resolve(opts.lazySource), entry.path);
    if (!src) throw new Error(`source missing: ${entry.path}`);
    fs.mkdirSync(path.dirname(dst), { recursive: true });
    // Copy in chunks off the engine's critical path; write to a temp name so a partial file is never seen.
    const tmp = `${dst}.part`;
    await fs.promises.copyFile(src, tmp);
    await fs.promises.rename(tmp, dst);
  })();
  lazy.pending.set(key, p);
  p.catch(() => lazy.pending.delete(key));
  return p;
}

if (opts.lazySource) {
  const mf = path.join(path.resolve(opts.gamedir), "manifest.txt");
  for (const line of fs.readFileSync(mf, "utf8").split(/\r?\n/)) {
    if (!line) continue;
    const [p, size, map] = line.split("\t");
    lazy.manifest.set(p.toLowerCase(), { path: p, size: Number(size), map: map || "" });
  }
}

// ---- hang diagnosis: pause every engine thread after N seconds and print where each one is ----
// Needs function names in the wasm (link with -DKISAK_WEB_PROFILING_FUNCS=ON for an optimized build).
function dumpStacks() {
  const session = new inspector.Session();
  session.connect();
  const sessions = new Map();
  let pending = 0;
  session.on("NodeWorker.attachedToWorker", ({ params }) => {
    sessions.set(params.sessionId, params.workerInfo.title || params.workerInfo.workerId);
    const post = (id, method) =>
      session.post("NodeWorker.sendMessageToWorker", {
        sessionId: params.sessionId,
        message: JSON.stringify({ id, method }),
      });
    post(1, "Debugger.enable");
    post(2, "Debugger.pause");
    ++pending;
  });
  session.on("NodeWorker.receivedMessageFromWorker", ({ params }) => {
    const msg = JSON.parse(params.message);
    if (msg.method !== "Debugger.paused") return;
    const frames = msg.params.callFrames
      .slice(0, 14)
      .map((f) => `    ${f.functionName || "(anonymous)"}  ${f.url.split("/").pop()}:${f.location.lineNumber}:${f.location.columnNumber}`);
    console.log(`--- thread ${sessions.get(params.sessionId)}\n${frames.join("\n")}`);
    if (--pending <= 0) setTimeout(() => process.exit(0), 200);
  });
  session.post("NodeWorker.enable", { waitForDebuggerOnStart: false });
  setTimeout(() => process.exit(0), 8000); // threads that never pause (blocked in a futex wait) are skipped
}

const exitOnLine = (s) => {
  if (opts.exitOn && opts.exitOn.test(s)) {
    console.log(`run_node: matched --exit-on, stopping`);
    process.exit(0);
  }
};

const modulePath = path.resolve(opts.build, "kisak.mjs");
const { default: KisakModule } = await import(pathToFileURL(modulePath).href);

// Profile files (console_mp.log) must not live in the wasm heap. The default
// /profile mount is an in-memory file, and a noisy map load grows it until abort.
const profileDir = path.join(os.tmpdir(), "kisak-node-profile");
fs.mkdirSync(profileDir, { recursive: true });

const mod = await KisakModule({
  arguments: ["--gamedir", path.resolve(opts.gamedir), "--profiledir", profileDir, ...engineArgs],
  print: (s) => { console.log(s); exitOnLine(s); },
  printErr: (s) => { console.error(s); exitOnLine(s); },
  locateFile: (f) => path.resolve(opts.build, f),
  ...(opts.lazySource
    ? {
        kisakEnsureFile: ensureFile,
        kisakPrefetchMap: (map) => {
          console.error(`run_node: prefetch requested for ${map}`);
          for (const e of lazy.manifest.values())
            if (e.map.toLowerCase() === map.toLowerCase()) ensureFile(e.path).catch((err) => console.error("prefetch:", err.message));
        },
      }
    : {}),
});

const sendLine = (line) => mod.ccall("Kisak_ConsoleInput", null, ["string"], [line]);

// Use the browser's transport and the engine's NET contract for headless hosts/tests.
if (opts.relay) {
  const scratch = mod._malloc(4102);
  if (!scratch) throw new Error("network bridge allocation failed");
  const transport = new GameTransport({
    url: opts.relay, room: opts.join, rtc: false, log: console.log,
    ready: room => {
      console.log(`run_node: room ${room}`);
      if (opts.join) sendLine("connect 127.0.0.2:3074");
    },
    receive: packet => {
      if (packet.length < 7 || packet.length > 4102) return;
      mod.HEAPU8.set(packet.subarray(6), scratch);
      mod._Kisak_NetPush(packet[0], packet[1], packet[2], packet[3], packet[4] | packet[5] << 8, scratch, packet.length - 6);
    },
  });
  const pump = () => {
    for (let i = 0; i < 256; ++i) {
      const count = mod._Kisak_NetDrain(scratch, 4102);
      if (count <= 6) break;
      transport.send(mod.HEAPU8.slice(scratch, scratch + count));
    }
  };
  mod.kisakPumpNetwork = pump;
  setInterval(pump, 10);
  process.on("exit", () => transport.close());
}

readline.createInterface({ input: process.stdin }).on("line", sendLine);

if (opts.cmds.length) {
  setTimeout(async () => {
    for (const c of opts.cmds) {
      const wait = /^@wait\s+(\d+(?:\.\d+)?)$/.exec(c);
      if (wait) await new Promise((r) => setTimeout(r, Number(wait[1]) * 1000));
      else sendLine(c);
    }
  }, opts.cmdDelay * 1000);
}
if (opts.dumpStacks > 0) setTimeout(dumpStacks, opts.dumpStacks * 1000);
if (opts.seconds > 0) {
  setTimeout(() => {
    console.log(`run_node: ${opts.seconds}s elapsed, stopping`);
    process.exit(0);
  }, opts.seconds * 1000);
}

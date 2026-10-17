// Dev server for the browser build.
//
//   node tools/web/serve.mjs --root build-web --assets D:\path\to\game [--gen build-web/assets-gen] [--port 8080] [--host 127.0.0.1]
//
// SharedArrayBuffer (pthreads) needs a cross-origin isolated page, so every response carries
//   Cross-Origin-Opener-Policy: same-origin / Cross-Origin-Embedder-Policy: require-corp.
//
//   /                 web/index.html (the page shell; staged next to the build output by CMake)
//   /kisak.mjs ...    build output from --root (kisak.mjs, kisak.wasm)
//   /gen/<file>       generated assets from --gen (manifest.json, iw_web.iwd, localized_*_web.iwd)
//   /assets/<path>    the game directory from --assets, case-insensitive, with Range support. Only the
//                     files the manifest lists are ever requested; the rest of the install is not exposed
//                     (an allow-list is read from <gen>/manifest.json).
//
// This is a development server: use any static host that sets the two headers above for deployment.

import { createHash } from "node:crypto";
import fs from "node:fs";
import http from "node:http";
import path from "node:path";
import zlib from "node:zlib";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const opts = {
  root: "build-web",
  assets: "",
  gen: "",
  web: path.resolve(here, "..", "..", "web"),
  port: 8080,
  // Loopback only: this serves a game install, and localhost is a secure context (SharedArrayBuffer,
  // OPFS). Pass --host 0.0.0.0 to share it on the LAN (needs HTTPS for other machines).
  host: "127.0.0.1",
  kbps: 0,
  agentTest: false,
};
const argv = process.argv.slice(2);
for (let i = 0; i < argv.length; ++i) {
  const a = argv[i];
  if (a === "--root") opts.root = argv[++i];
  else if (a === "--assets") opts.assets = argv[++i];
  else if (a === "--gen") opts.gen = argv[++i];
  else if (a === "--web") opts.web = argv[++i];
  else if (a === "--port") opts.port = Number(argv[++i]);
  else if (a === "--host") opts.host = argv[++i];
  else if (a === "--throttle-kbps") opts.kbps = Number(argv[++i]); // simulate a slower link
  else if (a === "--agent-test") opts.agentTest = true;
  else {
    console.error(`serve: unknown option ${a}`);
    process.exit(2);
  }
}
opts.root = path.resolve(opts.root);
opts.gen = path.resolve(opts.gen || path.join(opts.root, "assets-gen"));
if (opts.agentTest && !["127.0.0.1", "localhost", "::1"].includes(opts.host))
  throw new Error("--agent-test requires a loopback --host");
if (!opts.assets) {
  const guess = path.resolve(opts.root, "..", "build");
  if (fs.existsSync(path.join(guess, "main"))) opts.assets = guess;
}
if (opts.assets) opts.assets = path.resolve(opts.assets);

const TYPES = {
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".mjs": "text/javascript; charset=utf-8",
  ".wasm": "application/wasm",
  ".json": "application/json",
  ".txt": "text/plain; charset=utf-8",
};

function resolveInsensitive(root, rel) {
  let cur = root;
  for (const part of rel.split("/").filter(Boolean)) {
    if (part === "..") return null;
    let names;
    try { names = fs.readdirSync(cur); } catch { return null; }
    const hit = names.find((n) => n.toLowerCase() === part.toLowerCase());
    if (!hit) return null;
    cur = path.join(cur, hit);
  }
  return cur;
}

// Allow-list: the install holds ~9 GB; only manifest files are served from /assets.
let allowed = new Set();
function loadAllowList() {
  try {
    const m = JSON.parse(fs.readFileSync(path.join(opts.gen, "manifest.json"), "utf8"));
    const all = [...m.boot, ...Object.values(m.maps).flat(),
                 ...(m.client ? [...m.client.boot, ...Object.values(m.client.maps).flat()] : [])].filter((e) => !e.gen);
    if (m.installation?.version === 1 && m.installation.path === 'installation.json') {
      const inventory = JSON.parse(fs.readFileSync(path.join(opts.gen, 'installation.json'), 'utf8'));
      if (inventory.version === 1) for (const e of inventory.files || []) {
        if (!e.gen && /^(main|zone)\//i.test(e.path) && !e.path.split('/').includes('..')) all.push(e);
      }
    }
    allowed = new Set(all.map((e) => e.path.toLowerCase()));
  } catch {
    allowed = new Set();
  }
}
loadAllowList();

const COOP = {
  "Cross-Origin-Opener-Policy": "same-origin",
  "Cross-Origin-Embedder-Policy": "require-corp",
  "Cross-Origin-Resource-Policy": "same-origin",
};

function send(res, status, headers, body) {
  res.writeHead(status, { ...COOP, "Cache-Control": "no-store", ...headers });
  res.end(body);
}

// Content hashes for kisak.wasm / kisak.mjs, keyed by path + size + mtime so a rebuild
// changes the ETag without hashing the binary on every request.
const etagCache = new Map();
function fileEtag(file, st, strong) {
  if (!strong) return `W/"${st.size}-${Math.floor(st.mtimeMs)}"`;
  const key = `${st.size}\0${Math.floor(st.mtimeMs)}`;
  const hit = etagCache.get(file);
  if (hit && hit.key === key) return hit.etag;
  const digest = createHash("sha256");
  const fd = fs.openSync(file, "r");
  const chunk = Buffer.allocUnsafe(1024 * 1024);
  try {
    let bytes;
    while ((bytes = fs.readSync(fd, chunk, 0, chunk.length, null)) > 0) digest.update(chunk.subarray(0, bytes));
  } finally { fs.closeSync(fd); }
  const hash = digest.digest("hex");
  const etag = `"${hash}"`;
  etagCache.set(file, { key, etag });
  return etag;
}

function etagMatches(header, etag) {
  if (!header || !etag) return false;
  return header.split(",").some((part) => {
    const t = part.trim();
    return t === etag || t === `W/${etag}`;
  });
}

function serveFile(req, res, file, cache) {
  let st;
  try { st = fs.statSync(file); } catch { return send(res, 404, {}, "not found"); }
  if (!st.isFile()) return send(res, 404, {}, "not found");
  const type = TYPES[path.extname(file).toLowerCase()] || "application/octet-stream";
  // Revalidate cached assets with content hashes. Generated packs can change while
  // retaining their size, so size/mtime alone is not an adequate cache identity.
  const cacheHeader = cache ? "public, no-cache" : "no-store";
  const etag = cache ? fileEtag(file, st, true) : "";
  if (cache && etagMatches(req.headers["if-none-match"], etag)) {
    res.writeHead(304, { ...COOP, ETag: etag, "Cache-Control": cacheHeader });
    return res.end();
  }
  let bodyFile = file, bodySize = st.size, encoding;
  const range = /^bytes=(\d*)-(\d*)$/.exec(req.headers.range || "");
  const accept = req.headers["accept-encoding"] || "";
  // Sibling <file>.br from tools/web/compress_gen.ps1 (pip install brotli). Sent only when the
  // file exists; a missing compressor does not change this path.
  if (cache && !range && accept.includes("br") && fs.existsSync(file + ".br")
      && fs.statSync(file + ".br").mtimeMs >= st.mtimeMs) {
    bodyFile = file + ".br";
    bodySize = fs.statSync(bodyFile).size;
    encoding = "br";
  }
  let start = 0, end = bodySize - 1, status = 200;
  if (range && !encoding) {
    if (range[1] !== "") start = Number(range[1]);
    if (range[2] !== "") end = Math.min(Number(range[2]), bodySize - 1);
    if (range[1] === "" && range[2] !== "") { start = Math.max(0, bodySize - Number(range[2])); end = bodySize - 1; }
    if (start > end) return send(res, 416, { "Content-Range": `bytes */${bodySize}` }, "");
    status = 206;
  }
  res.writeHead(status, {
    ...COOP,
    "Cache-Control": cacheHeader,
    ...(cache ? { ETag: etag } : {}),
    ...(encoding ? { "Content-Encoding": encoding, Vary: "Accept-Encoding" } : {}),
    "Accept-Ranges": "bytes",
    "Content-Type": type,
    "Content-Length": end - start + 1,
    ...(status === 206 ? { "Content-Range": `bytes ${start}-${end}/${bodySize}` } : {}),
  });
  if (req.method === "HEAD") return res.end();
  const stream = fs.createReadStream(bodyFile, { start, end });
  if (!opts.kbps) return stream.pipe(res);
  // crude throttle: pause between chunks
  stream.on("data", (chunk) => {
    stream.pause();
    res.write(chunk, () => setTimeout(() => stream.resume(), (chunk.length / (opts.kbps * 1024)) * 1000));
  });
  stream.on("end", () => res.end());
}

// Original game IWDs. The engine asks for images/foo.iwi; we pull that entry out of whichever
// archive in the install contains it, so the menu and the world use the real files.
const packIndex = new Map();
let packIndexed = false;
function indexPacks() {
  if (packIndexed || !opts.assets) return;
  packIndexed = true;
  const main = resolveInsensitive(opts.assets, "main");
  if (!main) return;
  let names = [];
  try { names = fs.readdirSync(main); } catch { return; }
  for (const n of names) {
    if (!n.toLowerCase().endsWith(".iwd")) continue;
    const file = path.join(main, n);
    let st;
    try { st = fs.statSync(file); } catch { continue; }
    const tailLen = Math.min(st.size, 22 + 65535);
    const tail = Buffer.alloc(tailLen);
    const fd = fs.openSync(file, "r");
    fs.readSync(fd, tail, 0, tailLen, st.size - tailLen);
    const eocd = tail.lastIndexOf(Buffer.from([0x50, 0x4b, 0x05, 0x06]));
    if (eocd < 0) { fs.closeSync(fd); continue; }
    const cdSize = tail.readUInt32LE(eocd + 12);
    const cdOff = tail.readUInt32LE(eocd + 16);
    if (cdOff === 0xffffffff || cdSize === 0xffffffff) { fs.closeSync(fd); continue; }
    const cd = Buffer.alloc(cdSize);
    fs.readSync(fd, cd, 0, cdSize, cdOff);
    fs.closeSync(fd);
    let p = 0;
    while (p + 46 <= cd.length && cd.readUInt32LE(p) === 0x02014b50) {
      const method = cd.readUInt16LE(p + 10);
      const compSize = cd.readUInt32LE(p + 20);
      const nameLen = cd.readUInt16LE(p + 28);
      const extraLen = cd.readUInt16LE(p + 30);
      const commentLen = cd.readUInt16LE(p + 32);
      const localOff = cd.readUInt32LE(p + 42);
      const name = cd.toString("utf8", p + 46, p + 46 + nameLen).replace(/\\/g, "/");
      const key = name.toLowerCase();
      if (key && !key.endsWith("/") && !packIndex.has(key))
        packIndex.set(key, { file, method, compSize, localOff });
      p += 46 + nameLen + extraLen + commentLen;
    }
  }
  console.log(`serve: indexed ${packIndex.size} files inside the install IWDs`);
}
function readPackEntry(ent) {
  const fd = fs.openSync(ent.file, "r");
  const hdr = Buffer.alloc(30);
  fs.readSync(fd, hdr, 0, 30, ent.localOff);
  const dataOff = ent.localOff + 30 + hdr.readUInt16LE(26) + hdr.readUInt16LE(28);
  const comp = Buffer.alloc(ent.compSize);
  fs.readSync(fd, comp, 0, ent.compSize, dataOff);
  fs.closeSync(fd);
  if (ent.method === 0) return comp;
  if (ent.method === 8) return zlib.inflateRawSync(comp);
  return null;
}

function cacheable(p) {
  // Game files and generated packs are content-addressed by size in the manifest.
  // kisak.wasm / kisak.mjs are cached separately (content-hash ETag, revalidated each boot).
  return p.startsWith("/assets/") || p.startsWith("/gen/");
}

function isEngineBinary(p) {
  return p === "/kisak.wasm" || p === "/kisak.mjs";
}

// Multi-guest datagram relay and RTC signaling. Tabs send "HOST" or "JOIN <id>", then game packets
// as binary frames. Each packet has sender ip (4) + engine's network-order port value (2).
// The host is 127.0.0.2 and guests are 127.0.0.3 through .33, using game port 3074.
// Protocol 3 adds text frames for the private-match party: roster names, lobby phase
// (lobby or match), map, and gametype. Game datagrams stay binary.
const rooms = new Map();
// Legacy HTTP heartbeats remain accepted. Discovery itself follows live relay
// rooms, so a lobby is visible before map loading and disappears on host exit.
const lobby = new Map();
function cleanLobbyText(value, max) {
  return String(value ?? "").replace(/[\t\r\n]/g, " ").trim().slice(0, max);
}
function lobbyRows() {
  const now = Date.now();
  const rows = [];
  for (const [id, room] of rooms) {
    const legacy = lobby.get(id);
    if (!room.host || !room.map || !room.gametype
      || (!room.listed && !(!room.listingExplicit && legacy && now - legacy.at <= 12000))) continue;
    const phase = room.phase;
    const members = room.guests.size + 1;
    const clients = phase === "lobby" ? members : Math.max(members, room.clients);
    const label = phase === "lobby" ? "Lobby" : phase === "loading" ? "Loading" : "In game";
    const hostName = cleanLobbyText(room.names.get(2) || "Host", 18);
    rows.push({ room: id, name: `${hostName} - ${label}`, map: room.map, gametype: room.gametype,
      clients, maxClients: room.maxClients, phase });
  }
  return rows;
}
function readJsonBody(req, res, done) {
  const chunks = [];
  let size = 0;
  req.on("data", chunk => {
    size += chunk.length;
    if (size > 4096) { req.destroy(); return; }
    chunks.push(chunk);
  });
  req.on("end", () => {
    try { done(JSON.parse(Buffer.concat(chunks).toString("utf8"))); }
    catch { send(res, 400, {}, "invalid json"); }
  });
}
function roomId() {
  const alphabet = "abcdefghjkmnpqrstuvwxyz23456789";
  let id = "";
  for (let i = 0; i < 4; i++) id += alphabet[(Math.random() * alphabet.length) | 0];
  return rooms.has(id) ? roomId() : id;
}
function partyText(value, max) {
  return String(value ?? "").replace(/[\u0000-\u001f\u007f]/g, "").trim().slice(0, max);
}
function partyToken(value, max) {
  const text = partyText(value, max);
  return /^[A-Za-z0-9_]+$/.test(text) ? text : "";
}
function roomMembers(room) {
  const members = [];
  if (room?.host) members.push(room.host);
  for (const guest of room?.guests?.values() || []) members.push(guest);
  return members;
}
function broadcastRoom(room, text) {
  for (const member of roomMembers(room)) wsSend(member.socket, 0x1, text);
}
function sendPartyState(socket, room) {
  if (room.map) wsSend(socket, 0x1, `MAP ${room.map}`);
  if (room.gametype) wsSend(socket, 0x1, `GAMETYPE ${room.gametype}`);
  for (const [id, name] of room.names) wsSend(socket, 0x1, `ROSTER ${id} ${name}`);
  for (const [id, rank] of room.ranks) wsSend(socket, 0x1, `RANK ${id} ${rank}`);
  wsSend(socket, 0x1, `PHASE ${room.phase}`);
}
function wsSend(socket, opcode, payload) {
  const data = Buffer.isBuffer(payload) ? payload : Buffer.from(payload);
  const header = [0x80 | opcode];
  if (data.length < 126) header.push(data.length);
  else if (data.length < 65536) header.push(126, data.length >> 8, data.length & 255);
  else return;
  socket.write(Buffer.concat([Buffer.from(header), data]));
}
function attachRelay(req, socket) {
  const key = req.headers["sec-websocket-key"];
  if (!key) return socket.destroy();
  const accept = createHash("sha1").update(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").digest("base64");
  socket.write("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n" +
    `Sec-WebSocket-Accept: ${accept}\r\n\r\n`);
  wsSend(socket, 0x1, "PROTOCOL 3");
  const peer = { socket, role: 0, room: null, buf: Buffer.alloc(0) };
  const leave = () => {
    if (!peer.room) return;
    const r = rooms.get(peer.room);
    if (!r) return;
    r.names?.delete(peer.id);
    r.ranks?.delete(peer.id);
    if (r.host === peer) r.host = null;
    if (r.host === null) {
      for (const guest of r.guests.values()) { wsSend(guest.socket, 0x1, "LEAVE 2"); guest.room = null; }
      rooms.delete(peer.room);
      lobby.delete(peer.room);
    } else {
      r.guests.delete(peer.id);
      broadcastRoom(r, `LEAVE ${peer.id}`);
    }
    peer.room = null;
  };
  socket.on("close", leave);
  socket.on("error", leave);
  socket.on("data", (chunk) => {
    if (peer.buf.length + chunk.length > 131072) { socket.destroy(); return; }
    peer.buf = Buffer.concat([peer.buf, chunk]);
    while (peer.buf.length >= 2) {
      const b0 = peer.buf[0], b1 = peer.buf[1];
      const opcode = b0 & 0x0f;
      let len = b1 & 0x7f, off = 2;
      if (len === 126) { if (peer.buf.length < 4) return; len = peer.buf.readUInt16BE(2); off = 4; }
      else if (len === 127) { socket.destroy(); return; }
      const masked = (b1 & 0x80) !== 0;
      if (!masked || !(b0 & 0x80) || len > 16384) { socket.destroy(); return; }
      if (peer.buf.length < off + 4 + len) return;
      let payload = peer.buf.subarray(off + (masked ? 4 : 0), off + (masked ? 4 : 0) + len);
      if (masked) {
        const mask = peer.buf.subarray(off, off + 4);
        payload = Buffer.from(payload);
        for (let i = 0; i < payload.length; i++) payload[i] ^= mask[i & 3];
      }
      peer.buf = peer.buf.subarray(off + (masked ? 4 : 0) + len);
      if (opcode === 0x8) { socket.end(); return; }
      if (opcode === 0x9) { wsSend(socket, 0xA, payload); continue; }
      if (opcode === 0x1) {
        const text = payload.toString("utf8").trim();
        if (text === "HOST") {
          const id = roomId();
          if (peer.room) continue;
          rooms.set(id, { host: peer, guests: new Map(), phase: "lobby", map: "", gametype: "", names: new Map(), ranks: new Map(),
            listed: false, listingExplicit: false, clients: 1, maxClients: 18 });
          peer.role = 1; peer.id = 2; peer.room = id;
          wsSend(socket, 0x1, `ROOM ${id}`);
          wsSend(socket, 0x1, "PHASE lobby");
        } else if (text.startsWith("JOIN ")) {
          const id = text.slice(5).trim();
          const r = rooms.get(id);
          if (peer.room || !r?.host || r.guests.size + 1 >= r.maxClients) { wsSend(socket, 0x1, "ERR no room or room full"); continue; }
          let guestId = 3;
          while (r.guests.has(guestId)) guestId++;
          r.guests.set(guestId, peer); peer.role = 2; peer.id = guestId; peer.room = id;
          wsSend(socket, 0x1, `ROOM ${id}`);
          // Phase and roster arrive before the host peer so a guest can wait in the lobby
          // instead of connecting as soon as peer 2 exists.
          sendPartyState(socket, r);
          wsSend(socket, 0x1, "PEER 2");
          wsSend(r.host.socket, 0x1, `PEER ${guestId}`);
        } else if (peer.room && /^(NAME|RANK|PHASE|MAP|GAMETYPE|LISTED|CLIENTS|CAPACITY) /.test(text)) {
          const r = rooms.get(peer.room);
          if (!r) continue;
          if (text.startsWith("NAME ")) {
            const name = partyText(text.slice(5), 31);
            if (!name) continue;
            r.names.set(peer.id, name);
            broadcastRoom(r, `ROSTER ${peer.id} ${name}`);
          } else if (text.startsWith("RANK ")) {
            const match = /^RANK (\d+) (\d+)$/.exec(text);
            if (!match || Number(match[1]) > 49 || Number(match[2]) > 15) continue;
            const rank = `${Number(match[1])} ${Number(match[2])}`;
            r.ranks.set(peer.id, rank);
            broadcastRoom(r, `RANK ${peer.id} ${rank}`);
          } else if (peer.role === 1 && text.startsWith("LISTED ")) {
            const value = text.slice(7);
            if (value !== "0" && value !== "1") continue;
            r.listed = value === "1";
            r.listingExplicit = true;
            if (!r.listed) lobby.delete(peer.room);
          } else if (peer.role === 1 && /^(CLIENTS|CAPACITY) \d+$/.test(text)) {
            const value = Number(text.split(" ")[1]);
            if (value >= 1 && value <= 18) r[text.startsWith("CLIENTS ") ? "clients" : "maxClients"] = value;
          } else if (peer.role === 1 && text.startsWith("PHASE ")) {
            const phase = text.slice(6).trim();
            if (!["lobby", "loading", "match"].includes(phase)) continue;
            r.phase = phase;
            broadcastRoom(r, `PHASE ${phase}`);
          } else if (peer.role === 1 && text.startsWith("MAP ")) {
            const map = partyToken(text.slice(4), 31);
            if (!map) continue;
            r.map = map;
            broadcastRoom(r, `MAP ${map}`);
          } else if (peer.role === 1 && text.startsWith("GAMETYPE ")) {
            const gametype = partyToken(text.slice(9), 15);
            if (!gametype) continue;
            r.gametype = gametype;
            broadcastRoom(r, `GAMETYPE ${gametype}`);
          }
        } else if (text.startsWith("SIGNAL ") && peer.room) {
          try {
            const signal = JSON.parse(text.slice(7));
            const r = rooms.get(peer.room);
            const other = peer.role === 1 ? r?.guests.get(signal.to) : signal.to === 2 ? r?.host : null;
            if (other && payload.length <= 16384)
              wsSend(other.socket, 0x1, "SIGNAL " + JSON.stringify({ from: peer.id, data: signal.data }));
          } catch { wsSend(socket, 0x1, "ERR bad signal"); }
        }
      } else if (opcode === 0x2 && peer.room && payload.length >= 7 && payload.length <= 4102) {
        const r = rooms.get(peer.room);
        if (payload[0] !== 127 || payload[1] || payload[2]) continue;
        const other = peer.role === 1 ? r?.guests.get(payload[3]) : payload[3] === 2 ? r?.host : null;
        if (!other || other.socket.writableLength > 65536) continue;
        const header = Buffer.from([127, 0, 0, peer.id, 3074 >> 8, 3074 & 255]);
        wsSend(other.socket, 0x2, Buffer.concat([header, payload.subarray(6)]));
      }
    }
  });
}

const server = http.createServer((req, res) => {
  let url;
  try { url = new URL(req.url, "http://x"); }
  catch { return send(res, 400, {}, "bad url"); }
  let p;
  try { p = decodeURIComponent(url.pathname); }
  catch { return send(res, 400, {}, "bad url"); }
    if (p === "/lobby/servers" && req.method === "GET") {
      return send(res, 200, { "Content-Type": "application/json" }, JSON.stringify(lobbyRows()));
    }
    if (p === "/lobby/announce" && req.method === "POST") {
      const origin = req.headers.origin || "";
      const type = req.headers["content-type"] || "";
      if (!origin.startsWith("http://") && !origin.startsWith("https://"))
        return send(res, 403, {}, "same-origin JSON POST required");
      if (origin !== `http://${req.headers.host}` && origin !== `https://${req.headers.host}`)
        return send(res, 403, {}, "same-origin JSON POST required");
      if (!type.startsWith("application/json"))
        return send(res, 403, {}, "same-origin JSON POST required");
      return readJsonBody(req, res, body => {
        const room = cleanLobbyText(body.room, 8);
        if (!/^[a-z0-9]{4}$/.test(room)) return send(res, 400, {}, "bad room");
        if (!rooms.get(room)?.host) return send(res, 404, {}, "room is not hosted");
        const clients = Number(body.clients);
        const maxClients = Number(body.maxClients);
        lobby.set(room, {
          room,
          name: cleanLobbyText(body.name, 31) || "Local match",
          map: cleanLobbyText(body.map, 31) || "mp",
          gametype: cleanLobbyText(body.gametype, 15) || "tdm",
          clients: Number.isInteger(clients) ? Math.max(1, Math.min(255, clients)) : 1,
          maxClients: Number.isInteger(maxClients) ? Math.max(1, Math.min(255, maxClients)) : 12,
          at: Date.now(),
        });
        send(res, 204, {}, "");
      });
    }
    if (p === "/__agent/config") {
      const loopback = ["127.0.0.1", "::1", "::ffff:127.0.0.1"].includes(req.socket.remoteAddress);
      return send(res, opts.agentTest && loopback ? 200 : 404,
        { "Content-Type": "application/json" }, JSON.stringify({ enabled: opts.agentTest && loopback }));
    }
    if (p === "/diagnostics") {
      if (req.method !== "POST" || req.headers.origin !== `http://${req.headers.host}`
          || req.headers["content-type"] !== "application/json")
        return send(res, 403, {}, "same-origin JSON POST required");
      const chunks = [];
      let size = 0;
      req.on("data", chunk => {
        size += chunk.length;
        if (size > 524288) { req.destroy(); return; }
        chunks.push(chunk);
      });
      req.on("end", () => {
        try {
          const record = JSON.parse(Buffer.concat(chunks).toString("utf8"));
          if (!/^[a-f0-9]{32}$/.test(record.session) || !Array.isArray(record.lines)
              || record.lines.length > 300 || record.lines.some(line => typeof line !== "string" || line.length > 1024))
            return send(res, 400, {}, "invalid diagnostic record");
          const dir = path.join(opts.root, "diagnostics");
          fs.mkdirSync(dir, { recursive: true });
          fs.writeFileSync(path.join(dir, record.session + ".json"), JSON.stringify({ ...record, savedAt: new Date().toISOString() }, null, 2));
          send(res, 204, {}, "");
        } catch { send(res, 400, {}, "invalid diagnostic record"); }
      });
      return;
    }
    const cache = cacheable(p);
    // The page files are served from the source directory when it exists (edits show up on reload);
    // the copy CMake stages next to the build output is the fallback.
    if (p === "/" || p === "/index.html") {
      const src = path.join(opts.web, "index.html");
      return serveFile(req, res, fs.existsSync(src) ? src : path.join(opts.root, "index.html"), false);
    }
    if (p.startsWith("/assets/")) {
      if (!opts.assets) return send(res, 404, {}, "no --assets directory configured");
      const rel = p.slice("/assets/".length);
      if (!allowed.has(rel.toLowerCase())) { loadAllowList(); if (!allowed.has(rel.toLowerCase())) return send(res, 403, {}, "not in manifest"); }
      const file = resolveInsensitive(opts.assets, rel);
      return file ? serveFile(req, res, file, true) : send(res, 404, {}, "not found");
    }
    if (p.startsWith("/pack/")) {
      indexPacks();
      const rel = p.slice("/pack/".length).toLowerCase();
      if (rel.includes("..")) return send(res, 400, {}, "bad path");
      const ent = packIndex.get(rel);
      if (!ent) return send(res, 404, {}, "not in any iwd");
      let body;
      try { body = readPackEntry(ent); } catch (e) { return send(res, 500, {}, String(e.message || e)); }
      if (!body) return send(res, 415, {}, "unsupported compression");
      return send(res, 200, {
        "Content-Type": "application/octet-stream",
        "Content-Length": body.length,
        "Cache-Control": "public, max-age=86400",
      }, body);
    }
    if (p.startsWith("/gen/")) {
      // Catalog chunks live below gen; preserve only this validated subpath.
      // Other generated assets retain the legacy basename-only lookup.
      const relative = p.slice('/gen/'.length);
      const file = path.join(opts.gen, /^gpu-catalog\/[a-f0-9]{64}\.bin$/.test(relative) ? relative : path.basename(p));
      return serveFile(req, res, file, true);
    }
    // page scripts first (repo web/), then build output. kisak.js stays no-store so a crash-log
    // edit shows up on reload; the wasm and the module loader revalidate with a content hash.
    const base = path.basename(p);
    for (const dir of [opts.web, opts.root]) {
      const file = path.join(dir, base);
      if (p === "/" + base && fs.existsSync(file))
        return serveFile(req, res, file, isEngineBinary(p) ? "engine" : (cache && dir === opts.root));
    }
    send(res, 404, {}, "not found");
  });
server.on("upgrade", (req, socket) => {
  let pathName = "";
  try { pathName = new URL(req.url, "http://x").pathname; }
  catch { socket.destroy(); return; }
  if (pathName !== "/relay") { socket.destroy(); return; }
  attachRelay(req, socket);
});
server.on("error", (err) => {
  if (err.code === "EADDRINUSE") {
    console.error(`serve: port ${opts.port} is already in use.`);
    console.error(`Open http://localhost:${opts.port}/?assets=/assets/&gen=/gen/`);
    console.error("Or stop the other server and run this command again.");
    process.exit(1);
  }
  console.error(err);
  process.exit(1);
});
server.listen(opts.port, opts.host, () => {
  console.log(`serve: http://localhost:${opts.port}/  [${opts.host}]  (root ${opts.root}, gen ${opts.gen}${opts.assets ? ", assets " + opts.assets : ""})`);
  console.log(`Open http://localhost:${opts.port}/?assets=/assets/&gen=/gen/`);
  console.log("relay: ws://localhost:" + opts.port + "/relay   (host creates a room, guests open ?join=ROOM)");
});

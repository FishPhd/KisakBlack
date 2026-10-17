// The engine owns sequencing, fragmentation, acknowledgements and reliability.
// WebRTC carries its datagrams directly; the existing HTTP server only signals peers.
function partyLabel(value, max) {
  return String(value ?? "").replace(/[\u0000-\u001f\u007f]/g, " ").trim().slice(0, max);
}

export class GameTransport {
  constructor({ url, room = "", iceServers = [], rtc = true, log = () => {}, receive = () => {}, ready = () => {}, failed = () => {}, left = () => {}, onPeer = () => {}, party = () => {} }) {
    this.host = !room;
    this.peers = new Map();
    this.log = log;
    this.receive = receive;
    this.left = left;
    this.onPeer = onPeer;
    this.party = party;
    this.iceServers = iceServers;
    this.rtc = rtc;
    this.protocolReady = false;
    this.partyState = { phase: "", map: "", gametype: "", roster: new Map() };
    this.partyRanks = new Map();
    this.desired = {};
    this.sentParty = {};
    let assignedRoom = "";
    const announceRoom = () => { if (this.protocolReady && assignedRoom) ready(assignedRoom); };
    const emitParty = () => this.party({
      phase: this.partyState.phase,
      map: this.partyState.map,
      gametype: this.partyState.gametype,
      roster: [...this.partyState.roster.entries()].map(([id, name]) => ({ id, name, ...(this.partyRanks.get(id) || {}) })).sort((a, b) => a.id - b.id),
    });
    this.flushParty = () => {
      if (!this.protocolReady || this.ws?.readyState !== WebSocket.OPEN) return;
      const send = (key, line) => {
        if (!this.desired[key] || this.desired[key] === this.sentParty[key]) return;
        this.ws.send(line);
        this.sentParty[key] = this.desired[key];
      };
      send("name", `NAME ${this.desired.name}`);
      send("rank", `RANK ${this.desired.rank}`);
      if (!this.host) return;
      send("listed", `LISTED ${this.desired.listed}`);
      send("clients", `CLIENTS ${this.desired.clients}`);
      send("capacity", `CAPACITY ${this.desired.capacity}`);
      send("map", `MAP ${this.desired.map}`);
      send("gametype", `GAMETYPE ${this.desired.gametype}`);
      send("phase", `PHASE ${this.desired.phase}`);
    };
    this.setParty = (update = {}) => {
      const name = partyLabel(update.name, 31);
      if (name) this.desired.name = name;
      if (this.host && typeof update.listed === "boolean") this.desired.listed = update.listed ? "1" : "0";
      if (this.host && Number.isInteger(update.clients) && update.clients >= 1 && update.clients <= 18)
        this.desired.clients = String(update.clients);
      if (this.host && Number.isInteger(update.maxClients) && update.maxClients >= 1 && update.maxClients <= 18)
        this.desired.capacity = String(update.maxClients);
      if (Number.isInteger(update.rank) && update.rank >= 0 && update.rank <= 49
        && Number.isInteger(update.prestige) && update.prestige >= 0 && update.prestige <= 15)
        this.desired.rank = `${update.rank} ${update.prestige}`;
      if (this.host && ["lobby", "loading", "match"].includes(update.phase)) this.desired.phase = update.phase;
      const map = partyLabel(update.map, 31);
      if (this.host && /^[A-Za-z0-9_]+$/.test(map)) this.desired.map = map;
      const gametype = partyLabel(update.gametype, 15);
      if (this.host && /^[A-Za-z0-9_]+$/.test(gametype)) this.desired.gametype = gametype;
      this.flushParty();
    };
    const fail = (message) => {
      clearTimeout(this.handshakeTimer);
      this.log(message);
      failed(message);
      this.close();
    };
    this.ws = new WebSocket(url);
    this.ws.binaryType = "arraybuffer";
    this.ws.onopen = () => {
      this.ws.send(room ? `JOIN ${room}` : "HOST");
      this.handshakeTimer = setTimeout(() => fail("network: incompatible relay server; restart tools/web/play.ps1 and reload both tabs"), 5000);
    };
    this.ws.onmessage = (event) => {
      if (typeof event.data !== "string") {
        this.receive(new Uint8Array(event.data));
        return;
      }
      const text = event.data;
      if (text.startsWith("PROTOCOL ")) {
        if (text !== "PROTOCOL 3") { fail("network: unsupported relay protocol; restart tools/web/play.ps1 and reload both tabs"); return; }
        this.protocolReady = true;
        clearTimeout(this.handshakeTimer);
        this.flushParty();
        announceRoom();
      }
      else if (text.startsWith("ROOM ")) { assignedRoom = text.slice(5); announceRoom(); }
      else if (text.startsWith("PHASE ")) {
        const phase = text.slice(6);
        if (["lobby", "loading", "match"].includes(phase)) {
          this.partyState.phase = phase;
          emitParty();
        }
      }
      else if (text.startsWith("MAP ")) {
        this.partyState.map = text.slice(4);
        emitParty();
      }
      else if (text.startsWith("GAMETYPE ")) {
        this.partyState.gametype = text.slice(9);
        emitParty();
      }
      else if (text.startsWith("RANK ")) {
        const match = /^RANK (\d+) (\d+) (\d+)$/.exec(text);
        if (match && Number(match[1]) >= 2 && Number(match[1]) <= 33 && Number(match[2]) <= 49 && Number(match[3]) <= 15) {
          this.partyRanks.set(Number(match[1]), { rank: Number(match[2]), prestige: Number(match[3]) });
          emitParty();
        }
      }
      else if (text.startsWith("ROSTER ")) {
        const rest = text.slice(7);
        const split = rest.indexOf(" ");
        const id = Number(split < 0 ? rest : rest.slice(0, split));
        const name = split < 0 ? "" : rest.slice(split + 1);
        if (Number.isInteger(id) && id >= 2 && id <= 33) {
          if (name) this.partyState.roster.set(id, name);
          else this.partyState.roster.delete(id);
          emitParty();
        }
      }
      else if (text.startsWith("PEER ")) this.addPeer(Number(text.slice(5)));
      else if (text.startsWith("LEAVE ")) {
        const id = Number(text.slice(6));
        this.partyState.roster.delete(id);
        this.partyRanks.delete(id);
        if (!this.host && id === 2) this.partyState.phase = "closed";
        emitParty();
        this.removePeer(id);
        this.left(id);
      }
      else if (text.startsWith("SIGNAL ")) {
        try {
          const signal = JSON.parse(text.slice(7));
          const peer = this.peers.get(signal.from);
          if (peer) peer.work = peer.work.then(() => this.signal(peer, signal.data)).catch((e) => this.log(`WebRTC: ${e.message}`));
        } catch (e) { this.log(`signaling: ${e.message}`); }
      } else if (text.startsWith("ERR")) fail(text);
    };
    this.ws.onerror = () => this.log("network: signaling socket error");
    this.ws.onclose = () => {
      clearTimeout(this.handshakeTimer);
      this.log("network: signaling disconnected; open WebRTC peers can keep playing");
    };
  }

  sendSignal(id, data) {
    if (this.ws.readyState === WebSocket.OPEN)
      this.ws.send("SIGNAL " + JSON.stringify({ to: id, data }));
  }

  addPeer(id) {
    if (!Number.isInteger(id) || id < 2 || id > 33 || this.peers.has(id)) return;
    const peer = { id, pc: null, channel: null, work: Promise.resolve(), candidates: [] };
    this.peers.set(id, peer);
    this.log(`network: peer 127.0.0.${id} joined`);
    this.onPeer(id);
    if (!this.rtc || typeof RTCPeerConnection === "undefined") return;
    try {
      const pc = peer.pc = new RTCPeerConnection({ iceServers: this.iceServers });
      pc.onicecandidate = (event) => { if (event.candidate) this.sendSignal(id, { candidate: event.candidate }); };
      pc.ondatachannel = (event) => this.bindChannel(peer, event.channel);
      pc.onconnectionstatechange = () => {
        if (pc.connectionState === "failed") this.log(`WebRTC peer ${id}: failed; using relay`);
      };
      if (this.host) {
        this.bindChannel(peer, pc.createDataChannel("game", { ordered: false, maxRetransmits: 0 }));
        peer.work = pc.createOffer().then((offer) => pc.setLocalDescription(offer))
          .then(() => this.sendSignal(id, { description: pc.localDescription }))
          .catch((e) => this.log(`WebRTC: ${e.message}; using relay`));
      }
    } catch (e) { this.log(`WebRTC: ${e.message}; using relay`); }
  }

  bindChannel(peer, channel) {
    if (channel.label !== "game") { channel.close(); return; }
    peer.channel = channel;
    channel.binaryType = "arraybuffer";
    channel.onopen = () => this.log(`WebRTC peer ${peer.id}: direct datagrams active`);
    channel.onmessage = (event) => {
      if (!(event.data instanceof ArrayBuffer) || !event.data.byteLength || event.data.byteLength > 4096) return;
      const packet = new Uint8Array(6 + event.data.byteLength);
      // The virtual address remains stable when changing between relay and WebRTC.
      packet.set([127, 0, 0, peer.id, 3074 >> 8, 3074 & 255]);
      packet.set(new Uint8Array(event.data), 6);
      this.receive(packet);
    };
    channel.onerror = () => this.log(`WebRTC peer ${peer.id}: channel error`);
  }

  async signal(peer, data) {
    if (!peer.pc || !data) return;
    if (data.description) {
      if ((this.host && data.description.type !== "answer") || (!this.host && data.description.type !== "offer")) return;
      await peer.pc.setRemoteDescription(data.description);
      for (const candidate of peer.candidates.splice(0)) await peer.pc.addIceCandidate(candidate);
      if (!this.host) {
        await peer.pc.setLocalDescription(await peer.pc.createAnswer());
        this.sendSignal(peer.id, { description: peer.pc.localDescription });
      }
    } else if (data.candidate) {
      if (peer.pc.remoteDescription) await peer.pc.addIceCandidate(data.candidate);
      else if (peer.candidates.length < 128) peer.candidates.push(data.candidate);
    }
  }

  send(packet) {
    if (packet.length < 7 || packet.length > 4102 || packet[0] !== 127 || packet[1] || packet[2]) return;
    const peer = this.peers.get(packet[3]);
    if (!peer) return;
    if (peer.channel?.readyState === "open") {
      // Drop stale datagrams under congestion; engine acknowledgements recover reliable messages.
      if (peer.channel.bufferedAmount > 65536) return;
      try { peer.channel.send(packet.subarray(6)); return; } catch { /* relay fallback */ }
    }
    if (this.ws.readyState === WebSocket.OPEN && this.ws.bufferedAmount < 65536)
      this.ws.send(packet);
  }

  removePeer(id) {
    const peer = this.peers.get(id);
    peer?.channel?.close();
    peer?.pc?.close();
    this.peers.delete(id);
  }

  close() {
    clearTimeout(this.handshakeTimer);
    for (const id of this.peers.keys()) this.removePeer(id);
    this.ws.close();
  }
}

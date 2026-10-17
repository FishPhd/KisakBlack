// Loaded PCM16 and MS ADPCM play through the page AudioContext. Each voice is a
// graph of AudioBufferSourceNodes so SD_StopVoice can stop it, and onended hands
// the slot back to the engine. AudioBuffers of loaded clips are cached by
// LoadedSound* so a repeated UI click does not convert PCM16 to float again.
//
// Voices 0-9 are stream slots (SND_IsStream). Those aliases are not decoded up
// front: Snd_StreamOpen / Snd_StreamAcquireWindow / Snd_StreamGetHeader feed one
// PCM or ADPCM window at a time, and each window is a scheduled
// AudioBufferSourceNode on the context web/kisak.js resumes (window.kisakAudio).
// Desktop XAudio asserts streams are not WMA.
//
// Loaded xWMA (SND_ASSET_FORMAT_WMA, XAudio format tag 353) is walked as packets
// here, decoded once with the web-only Rockbox WMAv2 decoder, and cached on the
// same loaded-voice PCM path. Invalid packets refuse the voice without Com_Error.
//
// The speaker map in voice->pan is folded to stereo and applied as per-channel
// gains. A surround fold (more than two output channels) is clamped to 1 so one
// voice cannot exceed unity from panning; stereo and mono maps are unchanged.
// Occlusion filters and reverb are not applied. dryLevel becomes the GainNode
// value, then voice-bus voices are multiplied by 0.5, announcer-group voices by
// 0.35, and aliases whose names contain "rocket" or "missile" by 0.4. Alias
// flag bits 12-13 select the desktop submix: 2 is the voice limiter, every
// other bus (explosions included) is the world compressor. Both sum into a
// final limiter before the destination. Do not create or resume an AudioContext here.

#include <sound/snd.h>
#include <sound/snd_db.h>
#include <sound/snd_globals.h>
#include <sound/snd_stream.h>
#include <sound/snd_utils.h>

#include <qcommon/common.h>
#include <universal/com_files.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <list>
#include <mutex>
#include <new>
#include <utility>
#include <vector>

#include <emscripten.h>
#include <emscripten/heap.h>
#include <web/web_assets.h>

static bool Web_HeapRange(const void *p, uint64_t size)
{
    const uintptr_t start = reinterpret_cast<uintptr_t>(p);
    const uint64_t heap = emscripten_get_heap_size();
    return start >= 0x10000 && start <= heap && size <= heap - start;
}

extern "C" int Kisak_DecodeWmav2(const uint8_t *, unsigned, unsigned, unsigned, unsigned, unsigned, int16_t *, unsigned);
static bool s_inited;

// snd.h sizes the voice tables at 74 (10 stream + 64 one-shot), matching SND_MAX_VOICES.
static const int kWebVoices = 74;
static const int kStreamSlots = 10;
// How many decoded windows to keep queued. The reader is not run to EOF up front.
static const int kStreamAhead = 3;
// Longer than any loaded one-shot. Stops a corrupt frame_count from reserving the whole heap.
static const unsigned kWebMaxFrames = 48000u * 180u;
static std::atomic<int> s_voiceGen[kWebVoices];
static std::atomic<int> s_voiceEndedGen[kWebVoices];
static bool s_streamOpen[kStreamSlots];
static bool s_streamHeaderReady[kStreamSlots];
static std::atomic<int> s_streamLive[kStreamSlots];
static std::atomic<int> s_streamEof[kStreamSlots];
struct WebVoiceParams
{
    int gen = -1, gain = -1, pitch = -1, pan01 = -1, pan23 = -1;
};
static WebVoiceParams s_lastVoiceParams[kWebVoices];
static std::mutex s_voiceParamsMu;

static const LoadedSound *s_failSnd[32];
static int s_failCount;
static std::list<std::pair<const LoadedSound *, std::vector<int16_t>>> s_adpcmDecoded, s_wmaDecoded;

// Main-thread onended. Only the generation that started the node may free it, so a
// stop/start of the same slot cannot retire the new voice.
extern "C" EMSCRIPTEN_KEEPALIVE void Kisak_VoiceEnded(int voiceIndex, int gen)
{
    if (voiceIndex < 0 || voiceIndex >= kWebVoices || gen == 0)
        return;
    if (s_voiceGen[voiceIndex].load(std::memory_order_acquire) == gen)
        s_voiceEndedGen[voiceIndex].store(gen, std::memory_order_release);
}

// One scheduled stream window finished. The voice ends only after EOF and the
// last window, so a gap between windows is not treated as the alias ending.
extern "C" EMSCRIPTEN_KEEPALIVE void Kisak_StreamChunkEnded(int voiceIndex, int gen)
{
    if (voiceIndex < 0 || voiceIndex >= kStreamSlots || gen == 0)
        return;
    if (s_voiceGen[voiceIndex].load(std::memory_order_acquire) != gen)
        return;
    int left = s_streamLive[voiceIndex].fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (left < 0)
    {
        s_streamLive[voiceIndex].store(0, std::memory_order_release);
        return;
    }
    if (left == 0 && s_streamEof[voiceIndex].load(std::memory_order_acquire))
        s_voiceEndedGen[voiceIndex].store(gen, std::memory_order_release);
}

char __cdecl SD_Init()
{
    s_inited = true;
    // clang-format off
    MAIN_THREAD_ASYNC_EM_ASM({
        // Do not create or resume a context here. The page starts itself before any click,
        // and a context born outside a gesture stays suspended. The page creates one on the
        // first click or key and this driver adopts window.kisakAudio when a sound plays.
        if (typeof window === "undefined") return; // Node's headless server has no audio graph.
        if (window.kisakAudio)
            Module.kisakAudio = window.kisakAudio;
        if (!Module.kisakAudioBuffers) Module.kisakAudioBuffers = new Map();
        if (!Module.kisakVoiceNodes) Module.kisakVoiceNodes = new Array(74);
        if (!Module.kisakVoiceGen) Module.kisakVoiceGen = new Array(74);
        if (!Module.kisakHold) Module.kisakHold = [];

        function kisakFree(ptr) {
            try { if (ptr && Module._free) Module._free(ptr); } catch (e) {}
        }
        function kisakFail(item) {
            kisakFree(item.dataPtr);
            item.dataPtr = 0;
            if (item.stream) {
                if (Module._Kisak_StreamChunkEnded) Module._Kisak_StreamChunkEnded(item.idx, item.gen);
            } else if (Module._Kisak_VoiceEnded) {
                Module._Kisak_VoiceEnded(item.idx, item.gen);
            }
        }
        function kisakApply(slot, gain, p0, p1, p2, p3) {
            if (!slot || !slot.pan) return;
            slot.pan[0].gain.value = p0 / 1000;
            slot.pan[1].gain.value = p1 / 1000;
            slot.pan[2].gain.value = p2 / 1000;
            slot.pan[3].gain.value = p3 / 1000;
            if (slot.gain) slot.gain.gain.value = gain / 1000;
        }
        // World bus is snd_master comp*; voice bus is limit*. DynamicsCompressorNode
        // is unity below its threshold, so the below-threshold dynamo gain M is the
        // post gain together with compMG / limitMG. M is SND_DspLog/SND_DspExp of
        // log(TH)*(1/R-1), the truncated series SND_DspDynamo actually runs. The closed
        // form TH^(1/R-1) is several times larger for a low threshold and would put
        // full-scale peaks back to unity. The node itself still uses a real log ratio
        // (clamped to 1..20) and is stereo-linked; attack/release are clamped to 0..1s.
        // A mix limiter (threshold -1 dB, ratio 20, attack 0) keeps makeup and stacked
        // voices from hard-clipping the destination. Reverb is not applied.
        function kisakDspLog(m) {
            const x = (m - 1) / (m + 1);
            return x + (x * x * x) / 3;
        }
        function kisakDspExp(x) {
            const x2 = x * x;
            const x3 = x2 * x;
            const x4 = x3 * x;
            const x5 = x4 * x;
            return 1 + x + x2 / 2 + x3 / 6 + x4 / 24 + x5 / 120;
        }
        function kisakBusCurve(enabled, pg, mg, th, ratio, ta, tr) {
            const curve = {};
            curve.pre = 1;
            curve.post = 1;
            curve.threshold = 0;
            curve.ratio = 1;
            curve.attack = 0;
            curve.release = 0.05;
            if (!(enabled > 0.0000152879) || !(th > 0.0000152879) || !(ratio > 0.0000152879)) return curve;
            let thUse = th;
            if (thUse > 1) thUse = 1;
            let rNode = ratio;
            if (rNode < 1) rNode = 1;
            if (rNode > 20) rNode = 20;
            let pre = pg;
            if (!(pre >= 0) || !isFinite(pre)) pre = 1;
            let m = kisakDspExp(kisakDspLog(thUse) * (1 / ratio - 1));
            if (!(m > 0) || !isFinite(m) || m > 32) m = 1;
            let post = mg * m;
            if (!(post >= 0) || !isFinite(post)) post = 1;
            let thDb = 20 * Math.log10(thUse);
            if (thDb < -100) thDb = -100;
            if (!(thDb <= 0)) thDb = 0;
            let attack = ta;
            if (!(attack >= 0) || !isFinite(attack)) attack = 0;
            if (attack > 1) attack = 1;
            let release = tr;
            if (!(release >= 0) || !isFinite(release)) release = 0.05;
            if (release > 1) release = 1;
            curve.pre = pre;
            curve.post = post;
            curve.threshold = thDb;
            curve.ratio = rNode;
            curve.attack = attack;
            curve.release = release;
            return curve;
        }
        function kisakApplyOneBus(bus, curve) {
            const prev = bus.curve;
            if (prev && prev.pre === curve.pre && prev.post === curve.post && prev.threshold === curve.threshold &&
                prev.ratio === curve.ratio && prev.attack === curve.attack && prev.release === curve.release) return;
            bus.pre.gain.value = curve.pre;
            bus.post.gain.value = curve.post;
            bus.comp.knee.value = 0;
            bus.comp.threshold.value = curve.threshold;
            bus.comp.ratio.value = curve.ratio;
            bus.comp.attack.value = curve.attack;
            bus.comp.release.value = curve.release;
            bus.curve = curve;
        }
        function kisakApplyBusParams(p) {
            const buses = Module.kisakBuses;
            if (!buses || !p) return;
            const world = (p.have | 0) ? kisakBusCurve(p.cE, p.cPG, p.cMG, p.cT, p.cR, p.cTA, p.cTR)
                                       : kisakBusCurve(0, 1, 1, 1, 1, 0, 0.05);
            const voice = (p.have | 0) ? kisakBusCurve(p.lE, p.lPG, p.lMG, p.lT, p.lR, p.lTA, p.lTR)
                                       : kisakBusCurve(0, 1, 1, 1, 1, 0, 0.05);
            kisakApplyOneBus(buses.world, world);
            kisakApplyOneBus(buses.voice, voice);
        }
        function kisakMakeDynBus(ctx) {
            const pre = ctx.createGain();
            const comp = ctx.createDynamicsCompressor();
            const post = ctx.createGain();
            pre.gain.value = 1;
            post.gain.value = 1;
            comp.threshold.value = 0;
            comp.knee.value = 0;
            comp.ratio.value = 1;
            comp.attack.value = 0;
            comp.release.value = 0.05;
            pre.connect(comp);
            comp.connect(post);
            const bus = {};
            bus.pre = pre;
            bus.comp = comp;
            bus.post = post;
            bus.curve = null;
            return bus;
        }
        function kisakDropBuses() {
            const buses = Module.kisakBuses;
            Module.kisakBuses = null;
            if (!buses) return;
            try { if (buses.limit) buses.limit.disconnect(); } catch (e) {}
            try { if (buses.mix) buses.mix.disconnect(); } catch (e) {}
            try { if (buses.world && buses.world.post) buses.world.post.disconnect(); } catch (e) {}
            try { if (buses.world && buses.world.comp) buses.world.comp.disconnect(); } catch (e) {}
            try { if (buses.world && buses.world.pre) buses.world.pre.disconnect(); } catch (e) {}
            try { if (buses.voice && buses.voice.post) buses.voice.post.disconnect(); } catch (e) {}
            try { if (buses.voice && buses.voice.comp) buses.voice.comp.disconnect(); } catch (e) {}
            try { if (buses.voice && buses.voice.pre) buses.voice.pre.disconnect(); } catch (e) {}
        }
        function kisakEnsureBuses(ctx) {
            const cur = Module.kisakBuses;
            if (cur && cur.ctx === ctx && cur.limit) return cur;
            kisakDropBuses();
            const world = kisakMakeDynBus(ctx);
            const voice = kisakMakeDynBus(ctx);
            const mix = ctx.createGain();
            mix.gain.value = 1;
            const limit = ctx.createDynamicsCompressor();
            limit.threshold.value = -1;
            limit.knee.value = 0;
            limit.ratio.value = 20;
            limit.attack.value = 0;
            limit.release.value = 0.05;
            world.post.connect(mix);
            voice.post.connect(mix);
            mix.connect(limit);
            limit.connect(ctx.destination);
            const buses = {};
            buses.ctx = ctx;
            buses.world = world;
            buses.voice = voice;
            buses.mix = mix;
            buses.limit = limit;
            Module.kisakBuses = buses;
            if (Module.kisakBusParams) kisakApplyBusParams(Module.kisakBusParams);
            return buses;
        }
        Module.kisakDropBuses = kisakDropBuses;
        Module.kisakSetBuses = function(have, cE, cPG, cMG, cT, cR, cTA, cTR, lE, lPG, lMG, lT, lR, lTA, lTR) {
            try {
                const p = {};
                p.have = have | 0;
                p.cE = cE;
                p.cPG = cPG;
                p.cMG = cMG;
                p.cT = cT;
                p.cR = cR;
                p.cTA = cTA;
                p.cTR = cTR;
                p.lE = lE;
                p.lPG = lPG;
                p.lMG = lMG;
                p.lT = lT;
                p.lR = lR;
                p.lTA = lTA;
                p.lTR = lTR;
                Module.kisakBusParams = p;
                const ctx = Module.kisakAudio || window.kisakAudio;
                if (!ctx) return;
                kisakEnsureBuses(ctx);
                kisakApplyBusParams(p);
            } catch (e) {}
        };
        function kisakClearSlot(idx) {
            const nodes = Module.kisakVoiceNodes;
            if (!nodes) return;
            const slot = nodes[idx];
            nodes[idx] = null;
            if (!slot) return;
            const srcs = slot.sources || [];
            for (let i = 0; i < srcs.length; i++) {
                const s = srcs[i];
                if (!s) continue;
                if (s.node) {
                    s.node.onended = null;
                    try { s.node.stop(); } catch (e) {}
                    try { s.node.disconnect(); } catch (e) {}
                }
                if (s.split) {
                    try { s.split.disconnect(); } catch (e) {}
                }
            }
            const pan = slot.pan || [];
            for (let i = 0; i < pan.length; i++) {
                try { pan[i].disconnect(); } catch (e) {}
            }
            try { if (slot.merger) slot.merger.disconnect(); } catch (e) {}
            try { if (slot.gain) slot.gain.disconnect(); } catch (e) {}
        }
        function kisakEnsureSlot(ctx, item) {
            if (!Module.kisakVoiceNodes) Module.kisakVoiceNodes = new Array(74);
            let slot = Module.kisakVoiceNodes[item.idx];
            if (slot && slot.gen === item.gen && slot.gain) return slot;
            kisakClearSlot(item.idx);
            const merger = ctx.createChannelMerger(2);
            const pan = [];
            for (let i = 0; i < 4; i++) {
                const g = ctx.createGain();
                g.gain.value = 0;
                g.connect(merger, 0, i % 2);
                pan.push(g);
            }
            const dry = ctx.createGain();
            merger.connect(dry);
            const buses = kisakEnsureBuses(ctx);
            dry.connect(item.voiceBus ? buses.voice.pre : buses.world.pre);
            slot = {};
            slot.gen = item.gen;
            slot.gain = dry;
            slot.merger = merger;
            slot.pan = pan;
            slot.sources = [];
            slot.nextTime = 0;
            slot.channels = item.ch;
            slot.paused = false;
            slot.pitch = 1;
            Module.kisakVoiceNodes[item.idx] = slot;
            return slot;
        }
        function kisakConnect(ctx, slot, node, ch) {
            if (ch === 1) {
                node.connect(slot.pan[0]);
                node.connect(slot.pan[1]);
                return null;
            }
            const split = ctx.createChannelSplitter(2);
            node.connect(split);
            split.connect(slot.pan[0], 0);
            split.connect(slot.pan[1], 0);
            split.connect(slot.pan[2], 1);
            split.connect(slot.pan[3], 1);
            return split;
        }
        function kisakBind(node, item, source) {
            const idx = item.idx;
            const gen = item.gen;
            const stream = item.stream;
            node.onended = function() {
                const slot = Module.kisakVoiceNodes && Module.kisakVoiceNodes[idx];
                if (!slot || slot.gen !== gen) return;
                if (slot.sources) {
                    const at = slot.sources.indexOf(source);
                    if (at >= 0) slot.sources.splice(at, 1);
                }
                try { node.disconnect(); } catch (e) {}
                if (source.split) {
                    try { source.split.disconnect(); } catch (e) {}
                }
                source.node = null;
                if (stream) {
                    if (Module._Kisak_StreamChunkEnded) Module._Kisak_StreamChunkEnded(idx, gen);
                } else {
                    kisakClearSlot(idx);
                    if (Module._Kisak_VoiceEnded) Module._Kisak_VoiceEnded(idx, gen);
                }
            };
        }
        Module.kisakStopVoice = function(idx, gen) {
            if (!Module.kisakVoiceGen) Module.kisakVoiceGen = new Array(74);
            const cur = Module.kisakVoiceGen[idx] | 0;
            if (gen < cur) return;
            Module.kisakVoiceGen[idx] = gen;
            if (Module.kisakPendingVoiceUpdates) Module.kisakPendingVoiceUpdates[idx] = null;
            const hold = Module.kisakHold || [];
            const next = [];
            for (let i = 0; i < hold.length; i++) {
                const item = hold[i];
                if (item.idx === idx && item.gen < gen) kisakFree(item.dataPtr);
                else next.push(item);
            }
            Module.kisakHold = next;
            kisakClearSlot(idx);
        };
        Module.kisakPauseVoice = function(idx) {
            const slot = Module.kisakVoiceNodes && Module.kisakVoiceNodes[idx];
            if (!slot || slot.paused) return;
            const ctx = Module.kisakAudio || window.kisakAudio;
            slot.paused = true;
            slot.pauseAt = ctx ? ctx.currentTime : 0;
            const srcs = slot.sources || [];
            for (let i = 0; i < srcs.length; i++) {
                const s = srcs[i];
                if (!s || !s.node) continue;
                if (ctx && s.startAt > slot.pauseAt + 0.001) {
                    s.node.onended = null;
                    try { s.node.stop(); } catch (e) {}
                    try { s.node.disconnect(); } catch (e) {}
                    if (s.split) {
                        try { s.split.disconnect(); } catch (e) {}
                        s.split = null;
                    }
                    s.node = null;
                    s.restart = true;
                } else {
                    try { s.node.playbackRate.value = 0; } catch (e) {}
                }
            }
        };
        Module.kisakUnpauseVoice = function(idx) {
            const slot = Module.kisakVoiceNodes && Module.kisakVoiceNodes[idx];
            if (!slot || !slot.paused) return;
            if (window.kisakAudio) Module.kisakAudio = window.kisakAudio;
            const ctx = Module.kisakAudio || window.kisakAudio;
            const now = ctx ? ctx.currentTime : (slot.pauseAt || 0);
            const dt = now - (slot.pauseAt || now);
            slot.paused = false;
            slot.pauseAt = 0;
            if (slot.nextTime) slot.nextTime += dt;
            const pitch = slot.pitch > 0 ? slot.pitch : 1;
            const srcs = slot.sources || [];
            for (let i = 0; i < srcs.length; i++) {
                const s = srcs[i];
                if (!s) continue;
                if (s.restart) {
                    s.startAt += dt;
                    s.restart = false;
                    if (!ctx || !s.buffer) continue;
                    const node = ctx.createBufferSource();
                    node.buffer = s.buffer;
                    node.loop = !!s.loop;
                    node.playbackRate.value = pitch;
                    s.split = kisakConnect(ctx, slot, node, s.channels);
                    s.node = node;
                    const bindItem = {};
                    bindItem.idx = idx;
                    bindItem.gen = slot.gen;
                    bindItem.stream = s.stream;
                    kisakBind(node, bindItem, s);
                    try { node.start(s.startAt); } catch (e) {}
                } else if (s.node) {
                    try { s.node.playbackRate.value = pitch; } catch (e) {}
                }
            }
        };
        Module.kisakUpdateVoice = function(idx, gen, gain, pitchMilli, p0, p1, p2, p3) {
            const slot = Module.kisakVoiceNodes && Module.kisakVoiceNodes[idx];
            if (!slot || slot.gen !== gen || !slot.gain) {
                if (!Module.kisakPendingVoiceUpdates) Module.kisakPendingVoiceUpdates = new Array(74);
                const pending = {};
                pending.gen = gen;
                pending.gain = gain;
                pending.pitchMilli = pitchMilli;
                pending.p0 = p0;
                pending.p1 = p1;
                pending.p2 = p2;
                pending.p3 = p3;
                Module.kisakPendingVoiceUpdates[idx] = pending;
                return;
            }
            kisakApply(slot, gain, p0, p1, p2, p3);
            if (pitchMilli > 0) slot.pitch = pitchMilli / 1000;
            if (slot.paused || !(slot.pitch > 0)) return;
            const srcs = slot.sources || [];
            for (let i = 0; i < srcs.length; i++) {
                const s = srcs[i];
                if (s && s.node) {
                    try { s.node.playbackRate.value = slot.pitch; } catch (e) {}
                }
            }
        };
        Module.kisakPlayPcm = function(item) {
            let keep = false;
            try {
                if (!Module.kisakVoiceGen) Module.kisakVoiceGen = new Array(74);
                const cur = Module.kisakVoiceGen[item.idx] | 0;
                if (cur > item.gen) {
                    kisakFree(item.dataPtr);
                    item.dataPtr = 0;
                    return;
                }
                if (window.kisakAudio) Module.kisakAudio = window.kisakAudio;
                const ctx = Module.kisakAudio || window.kisakAudio;
                if (!ctx) {
                    if (!Module.kisakHold) Module.kisakHold = [];
                    if (Module.kisakHold.length >= 48) {
                        kisakFail(item);
                        return;
                    }
                    Module.kisakHold.push(item);
                    keep = true;
                    return;
                }
                const n = item.n | 0;
                const ch = item.ch | 0;
                const rate = item.rate | 0;
                const dataPtr = item.dataPtr >>> 0;
                if (!(n > 0) || n > 48000 * 180 || (ch !== 1 && ch !== 2) || !(rate >= 3000 && rate <= 768000) || !dataPtr) {
                    kisakFail(item);
                    return;
                }
                Module.kisakVoiceGen[item.idx] = item.gen;
                let buf = null;
                if (item.cacheKey) {
                    if (!Module.kisakAudioBuffers) Module.kisakAudioBuffers = new Map();
                    const key = (item.cacheKey >>> 0) + ":" + n + ":" + ch + ":" + rate;
                    buf = Module.kisakAudioBuffers.get(key);
                    if (!buf) {
                        const start = dataPtr >>> 1;
                        const need = n * ch;
                        if (!HEAP16 || start + need > HEAP16.length) {
                            kisakFail(item);
                            return;
                        }
                        const src = HEAP16.subarray(start, start + need);
                        buf = ctx.createBuffer(ch, n, rate);
                        for (let c = 0; c < ch; c++) {
                            const o = buf.getChannelData(c);
                            for (let i = 0; i < n; i++) o[i] = src[i * ch + c] / 32768;
                        }
                        Module.kisakAudioBuffers.set(key, buf);
                    }
                } else {
                    const start = dataPtr >>> 1;
                    const need = n * ch;
                    if (!HEAP16 || start + need > HEAP16.length) {
                        kisakFail(item);
                        return;
                    }
                    const src = HEAP16.subarray(start, start + need);
                    buf = ctx.createBuffer(ch, n, rate);
                    for (let c = 0; c < ch; c++) {
                        const o = buf.getChannelData(c);
                        for (let i = 0; i < n; i++) o[i] = src[i * ch + c] / 32768;
                    }
                }
                const slot = kisakEnsureSlot(ctx, item);
                if (item.paused) slot.paused = true;
                kisakApply(slot, item.gain, item.p0, item.p1, item.p2, item.p3);
                const pitch = item.pitch > 0 ? item.pitch / 1000 : (slot.pitch > 0 ? slot.pitch : 1);
                slot.pitch = pitch;
                const node = ctx.createBufferSource();
                node.buffer = buf;
                node.loop = !!item.loop;
                if (!slot.paused && pitch > 0) node.playbackRate.value = pitch;
                const split = kisakConnect(ctx, slot, node, ch);
                const source = {};
                source.node = node;
                source.split = split;
                source.buffer = buf;
                source.startAt = 0;
                source.channels = ch;
                source.loop = item.loop;
                source.stream = item.stream;
                source.restart = false;
                kisakBind(node, item, source);
                const now = ctx.currentTime;
                let startAt = slot.nextTime || 0;
                if (!(startAt > now + 0.005)) startAt = now + 0.01;
                source.startAt = startAt;
                const dur = (n / rate) / (pitch > 0 ? pitch : 1);
                slot.nextTime = startAt + dur;
                slot.sources.push(source);
                if (slot.paused) {
                    source.restart = true;
                    source.node = null;
                    node.onended = null;
                    try { node.disconnect(); } catch (e) {}
                    if (split) {
                        try { split.disconnect(); } catch (e) {}
                        source.split = null;
                    }
                } else {
                    node.start(startAt);
                }
                const pending = Module.kisakPendingVoiceUpdates && Module.kisakPendingVoiceUpdates[item.idx];
                if (pending && pending.gen === item.gen) {
                    Module.kisakPendingVoiceUpdates[item.idx] = null;
                    Module.kisakUpdateVoice(item.idx, pending.gen, pending.gain, pending.pitchMilli,
                        pending.p0, pending.p1, pending.p2, pending.p3);
                }
            } catch (e) {
                if (!keep) kisakFail(item);
            } finally {
                if (!keep) kisakFree(item.dataPtr);
            }
        };
        Module.kisakFlushHold = function() {
            if (window.kisakAudio) Module.kisakAudio = window.kisakAudio;
            const ctx = Module.kisakAudio || window.kisakAudio;
            if (!ctx || !Module.kisakHold || !Module.kisakHold.length) return;
            const items = Module.kisakHold;
            Module.kisakHold = [];
            for (let i = 0; i < items.length; i++) Module.kisakPlayPcm(items[i]);
        };
    });
    // clang-format on
    return 1;
}

static void Web_StopVoiceNode(int voiceIndex)
{
    if (voiceIndex < 0 || voiceIndex >= kWebVoices)
        return;
    // Retire this generation so a late onended cannot free the next occupant.
    const int gen = s_voiceGen[voiceIndex].fetch_add(1, std::memory_order_acq_rel) + 1;
    s_voiceEndedGen[voiceIndex].store(0, std::memory_order_release);
    if (voiceIndex < kStreamSlots && s_streamOpen[voiceIndex])
    {
        s_streamOpen[voiceIndex] = false;
        s_streamHeaderReady[voiceIndex] = false;
        s_streamLive[voiceIndex].store(0, std::memory_order_release);
        s_streamEof[voiceIndex].store(0, std::memory_order_release);
        if (Snd_StreamStatus((unsigned)voiceIndex) != SND_STREAM_UNUSED)
            Snd_StreamClose((unsigned)voiceIndex);
    }
    // clang-format off
    MAIN_THREAD_ASYNC_EM_ASM({
        if (Module.kisakStopVoice) Module.kisakStopVoice($0, $1);
    }, voiceIndex, gen);
    // clang-format on
}

void SD_Shutdown()
{
    s_inited = false;
    for (int i = 0; i < kWebVoices; ++i)
        Web_StopVoiceNode(i);
    s_adpcmDecoded.clear();
    s_wmaDecoded.clear();
    s_failCount = 0;
    MAIN_THREAD_ASYNC_EM_ASM({
        if (Module.kisakAudioBuffers) Module.kisakAudioBuffers.clear();
        if (Module.kisakDropBuses) Module.kisakDropBuses();
        Module.kisakBusParams = null;
    });
}

void __cdecl SD_PauseVoice(int voiceIndex)
{
    if (voiceIndex < 0 || voiceIndex >= kWebVoices)
        return;
    // clang-format off
    MAIN_THREAD_ASYNC_EM_ASM({
        if (Module.kisakPauseVoice) Module.kisakPauseVoice($0);
    }, voiceIndex);
    // clang-format on
}

void __cdecl SD_PreUpdate()
{
    if (!s_inited)
        return;
    const snd_master *master = g_snd.global_constants ? SND_GetMasterCurrent() : nullptr;
    float compE = 0.f, compPG = 1.f, compMG = 1.f, compT = 1.f, compR = 1.f, compTA = 0.f, compTR = 0.05f;
    float limitE = 0.f, limitPG = 1.f, limitMG = 1.f, limitT = 1.f, limitR = 1.f, limitTA = 0.f, limitTR = 0.05f;
    if (master)
    {
        compE = master->compE;
        compPG = master->compPG;
        compMG = master->compMG;
        compT = master->compT;
        compR = master->compR;
        compTA = master->compTA;
        compTR = master->compTR;
        limitE = master->limitE;
        limitPG = master->limitPG;
        limitMG = master->limitMG;
        limitT = master->limitT;
        limitR = master->limitR;
        limitTA = master->limitTA;
        limitTR = master->limitTR;
    }
    // clang-format off
    MAIN_THREAD_ASYNC_EM_ASM({
        if (Module.kisakSetBuses) Module.kisakSetBuses($0, $1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14);
        if (Module.kisakFlushHold) Module.kisakFlushHold();
    }, master ? 1 : 0, compE, compPG, compMG, compT, compR, compTA, compTR, limitE, limitPG, limitMG, limitT, limitR, limitTA, limitTR);
    // clang-format on
}

void __cdecl SD_StopVoice(int voiceIndex)
{
    Web_StopVoiceNode(voiceIndex);
}

void __cdecl SD_UnpauseVoice(int voiceIndex)
{
    if (voiceIndex < 0 || voiceIndex >= kWebVoices)
        return;
    // clang-format off
    MAIN_THREAD_ASYNC_EM_ASM({
        if (Module.kisakUnpauseVoice) Module.kisakUnpauseVoice($0);
    }, voiceIndex);
    // clang-format on
}

static int Web_GainMilli(float gain)
{
    if (!(gain >= 0.f))
        gain = 0.f;
    int milli = (int)(gain * 1000.f + 0.5f);
    if (milli < 0)
        milli = 0;
    if (milli > 4000)
        milli = 4000;
    return milli;
}

static int Web_PitchMilli(snd_voice_t *voice)
{
    if (!voice || voice->paused || !voice->alias)
        return 0;
    float pitch = (float)SND_GetPitch(voice);
    if (!(pitch >= 0.f))
        pitch = 0.f;
    int milli = (int)(pitch * 1000.f + 0.5f);
    if (milli < 0)
        milli = 0;
    if (milli > 1900)
        milli = 1900;
    return milli;
}

// Fold the engine speaker map into one stereo ear. Output order matches
// SND_PanToSpeakermap: 0 L, 1 R, 2 C, 3 LFE, 4 back L, 5 back R, 6 rear L, 7 rear R.
// dryLevel stays on the voice GainNode, so this is only the directional mix.
static int Web_PanMilli(const snd_voice_t *voice, int inCh, int side, int channels)
{
    const bool left = side == 0;
    if (!voice || inCh < 0 || inCh > 1 || (side != 0 && side != 1))
        return 0;
    const int inCount = voice->pan.input_channel_count;
    const int outCount = voice->pan.output_channel_count;
    if (inCount <= 0 || outCount <= 0 || inCount > 2 || outCount > 8)
    {
        if (channels <= 1)
            return 1000;
        if (inCh == 0 && left)
            return 1000;
        if (inCh == 1 && !left)
            return 1000;
        return 0;
    }
    if (inCh >= inCount)
        return 0;
    if (outCount == 1)
        return Web_GainMilli(Snd_SpeakerMapGetVolume(&voice->pan, inCh, 0) * 0.70710677f);
    if (outCount == 2)
        return Web_GainMilli(Snd_SpeakerMapGetVolume(&voice->pan, inCh, side));
    static const float kL[8] = {1.f, 0.f, 0.70710677f, 0.70710677f, 1.f, 0.f, 1.f, 0.f};
    static const float kR[8] = {0.f, 1.f, 0.70710677f, 0.70710677f, 0.f, 1.f, 0.f, 1.f};
    const float *w = left ? kL : kR;
    float sum = 0.f;
    const int n = outCount < 8 ? outCount : 8;
    for (int out = 0; out < n; ++out)
        sum += Snd_SpeakerMapGetVolume(&voice->pan, inCh, out) * w[out];
    // Several speakers can land in one ear. Cap the fold at unity. Stereo
    // (outCount == 2) and mono return above and stay at their speaker volumes.
    if (sum > 1.f)
        sum = 1.f;
    return Web_GainMilli(sum);
}

// Bits 12-13 == 2 is the desktop voice bus. Web_AliasBusFlags returns 8 for it.
static bool Web_AliasIsVoiceBus(const snd_alias_t *alias)
{
    return alias && ((alias->flags & 0x3000u) >> 12) == 2u;
}

static bool Web_AliasNameContains(const snd_alias_t *alias, const char *needle)
{
    return alias && alias->name && needle && I_stristr(alias->name, needle) != nullptr;
}

// g_snd.announcerGroup stays 0 when SND_InitGroups never finds "voice_announcer".
// Confirm the name so that unset index does not quiet whatever group 0 is.
static bool Web_VoiceIsAnnouncer(const snd_voice_t *voice)
{
    if (!voice || voice->group < 0 || (unsigned)voice->group != g_snd.announcerGroup)
        return false;
    const SndDriverGlobals *globals = g_snd.global_constants;
    if (!globals || !globals->groups || (unsigned)voice->group >= globals->groupCount)
        return false;
    return I_stricmp(globals->groups[voice->group].name, "voice_announcer") == 0;
}

// Announcer is a voice-bus group, so 0.35 replaces the generic voice-bus 0.5.
// Rocket and missile aliases are scaled on top of that. World-bus gunshots are not.
static float Web_DryGainScale(const snd_voice_t *voice)
{
    float scale = 1.f;
    if (!voice)
        return scale;
    const snd_alias_t *alias = voice->alias;
    if (Web_VoiceIsAnnouncer(voice))
        scale = 0.35f;
    else if (Web_AliasIsVoiceBus(alias))
        scale = 0.5f;
    if (Web_AliasNameContains(alias, "rocket") || Web_AliasNameContains(alias, "missile"))
        scale *= 0.4f;
    return scale;
}

static int Web_DryGainMilli(const snd_voice_t *voice)
{
    if (!voice)
        return 0;
    return Web_GainMilli(voice->dryLevel * Web_DryGainScale(voice));
}

static void Web_VoicePan(const snd_voice_t *voice, int channels, int pan[4])
{
    pan[0] = Web_PanMilli(voice, 0, 0, channels);
    pan[1] = Web_PanMilli(voice, 0, 1, channels);
    pan[2] = channels > 1 ? Web_PanMilli(voice, 1, 0, channels) : 0;
    pan[3] = channels > 1 ? Web_PanMilli(voice, 1, 1, channels) : 0;
}

static int Web_BumpGen(unsigned voice)
{
    int gen = s_voiceGen[voice].fetch_add(1, std::memory_order_acq_rel) + 1;
    if (gen == 0)
        gen = s_voiceGen[voice].fetch_add(1, std::memory_order_acq_rel) + 1;
    s_voiceEndedGen[voice].store(0, std::memory_order_release);
    return gen;
}

// Bit 3 is the desktop voice bus ((alias flags & 0x3000) >> 12) == 2. Other buses,
// including explosions, stay on the world compressor. Loop, stream, and pause keep bits 0-2.
static int Web_AliasBusFlags(int voice)
{
    if (voice < 0 || voice >= kWebVoices)
        return 0;
    if (Web_AliasIsVoiceBus(g_snd.voice[voice].alias))
        return 8;
    return 0;
}

static void Web_QueuePcm(int voice, int gen, int rate, int channels, int frames, int16_t *copy, int cacheKey, int gainMilli,
                          int loop, int pitchMilli, const int pan[4], int stream)
{
    const int pan01 = (pan[0] & 0xffff) | ((pan[1] & 0xffff) << 16);
    const int pan23 = (pan[2] & 0xffff) | ((pan[3] & 0xffff) << 16);
    const int flags = (loop ? 1 : 0) | (stream ? 2 : 0) | ((voice >= 0 && g_snd.voice[voice].paused) ? 4 : 0) |
                      Web_AliasBusFlags(voice);
    // clang-format off
    MAIN_THREAD_ASYNC_EM_ASM({
        const pan01 = $10 | 0;
        const pan23 = $11 | 0;
        const flags = $8 | 0;
        const item = {};
        item.idx = $0 | 0;
        item.gen = $1 | 0;
        item.rate = $2 | 0;
        item.ch = $3 | 0;
        item.n = $4 | 0;
        item.dataPtr = $5 >>> 0;
        item.cacheKey = $6 >>> 0;
        item.gain = $7 | 0;
        item.loop = (flags & 1) !== 0;
        item.stream = (flags & 2) !== 0;
        item.paused = (flags & 4) !== 0;
        item.voiceBus = (flags & 8) !== 0;
        item.pitch = $9 | 0;
        item.p0 = pan01 & 65535;
        item.p1 = (pan01 >>> 16) & 65535;
        item.p2 = pan23 & 65535;
        item.p3 = (pan23 >>> 16) & 65535;
        if (Module.kisakFlushHold) Module.kisakFlushHold();
        if (!Module.kisakPlayPcm) {
            try { if (item.dataPtr && Module._free) Module._free(item.dataPtr); } catch (e) {}
            if (item.stream) {
                if (Module._Kisak_StreamChunkEnded) Module._Kisak_StreamChunkEnded(item.idx, item.gen);
            } else if (Module._Kisak_VoiceEnded) {
                Module._Kisak_VoiceEnded(item.idx, item.gen);
            }
            return;
        }
        Module.kisakPlayPcm(item);
    }, voice, gen, rate, channels, frames, (int)(intptr_t)copy, cacheKey, gainMilli, flags, pitchMilli, pan01, pan23);
    // clang-format on
}

static void Web_PumpStream(unsigned voice);

void __cdecl SD_UpdateVoice(unsigned int voiceIndex)
{
    if (voiceIndex >= (unsigned)kWebVoices)
        return;
    if (voiceIndex < (unsigned)kStreamSlots && s_streamOpen[voiceIndex])
        Web_PumpStream(voiceIndex);
    if (g_snd.voiceAliasHash[voiceIndex])
    {
        snd_voice_t *voice = &g_snd.voice[voiceIndex];
        const int gainMilli = Web_DryGainMilli(voice);
        const int pitchMilli = Web_PitchMilli(voice);
        int channels = voice->soundFileInfo.srcChannelCount;
        if (channels != 1 && channels != 2)
            channels = 1;
        int pan[4];
        Web_VoicePan(voice, channels, pan);
        const int pan01 = (pan[0] & 0xffff) | ((pan[1] & 0xffff) << 16);
        const int pan23 = (pan[2] & 0xffff) | ((pan[3] & 0xffff) << 16);
        const int updateGen = s_voiceGen[voiceIndex].load(std::memory_order_acquire);
        bool changed;
        {
            std::lock_guard<std::mutex> lock(s_voiceParamsMu);
            WebVoiceParams &last = s_lastVoiceParams[voiceIndex];
            changed = last.gen != updateGen || last.gain != gainMilli || last.pitch != pitchMilli ||
                last.pan01 != pan01 || last.pan23 != pan23;
            if (changed)
                last = {updateGen, gainMilli, pitchMilli, pan01, pan23};
        }
        if (changed)
        {
        // clang-format off
        MAIN_THREAD_ASYNC_EM_ASM({
            if (!Module.kisakUpdateVoice) return;
            const pan01 = $4 | 0;
            const pan23 = $5 | 0;
            Module.kisakUpdateVoice($0 | 0, $1 | 0, $2 | 0, $3 | 0, pan01 & 65535, (pan01 >>> 16) & 65535, pan23 & 65535, (pan23 >>> 16) & 65535);
        }, (int)voiceIndex, updateGen, gainMilli, pitchMilli, pan01, pan23);
        // clang-format on
        }
    }
    const int gen = s_voiceGen[voiceIndex].load(std::memory_order_acquire);
    const int ended = s_voiceEndedGen[voiceIndex].exchange(0, std::memory_order_acq_rel);
    if (!ended || ended != gen)
        return;
    if (g_snd.voiceAliasHash[voiceIndex])
        SND_StopVoice((int)voiceIndex);
}

bool SND_IsVoiceFree(int voiceIndex)
{
    if (voiceIndex < 0 || voiceIndex >= kWebVoices)
        return true;
    return g_snd.voiceAliasHash[voiceIndex] == 0;
}

static int16_t Clip16(int v)
{
    if (v > 32767)
        return 32767;
    if (v < -32768)
        return -32768;
    return (int16_t)v;
}

// Microsoft ADPCM, the format the desktop XAudio driver submits for loaded clips.
// Block layout matches iSND_CreateVoice: 262 bytes per channel, 7 coefs. snd->block_size
// is not that stride (XAudio never reads it); using it splits headers into samples.
static bool DecodeMsAdpcm(const snd_asset *snd, std::vector<int16_t> &out)
{
    static const int c1[7] = {256, 512, 0, 192, 240, 460, 392};
    static const int c2[7] = {0, -256, 0, 64, 0, -208, -232};
    static const int adapt[16] = {230, 230, 230, 230, 307, 409, 512, 614, 768, 614, 512, 409, 307, 230, 230, 230};
    const int ch = (int)snd->channel_count;
    if (ch < 1 || ch > 2 || !snd->data || !snd->data_size || !snd->frame_count)
        return false;
    const int blockAlign = 262 * ch;
    if ((int)snd->data_size < blockAlign)
        return false;
    // Same formula as AWFT.wSamplesPerBlock in snd_driver_xaudio2.cpp (512 for one channel).
    const int samplesPerBlock = 2 * blockAlign / ch - 12;
    if (samplesPerBlock <= 2)
        return false;
    const uint8_t *src = (const uint8_t *)snd->data;
    const uint8_t *end = src + snd->data_size;
    const size_t blocks = (size_t)snd->data_size / (size_t)blockAlign;
    size_t target = blocks * (size_t)samplesPerBlock;
    if (target > snd->frame_count)
        target = snd->frame_count;
    if (target > kWebMaxFrames)
        target = kWebMaxFrames;
    out.clear();
    try
    {
        out.reserve((target + 2) * (size_t)ch);
    }
    catch (const std::bad_alloc &)
    {
        out.clear();
        return false;
    }

    int pred[2] = {}, delta[2] = {}, s1[2] = {}, s2[2] = {};
    try
    {
    while (src + blockAlign <= end && out.size() / (size_t)ch < target)
    {
        const uint8_t *p = src;
        for (int c = 0; c < ch; ++c)
        {
            int pi = *p++;
            pred[c] = pi > 6 ? 0 : pi;
        }
        for (int c = 0; c < ch; ++c)
        {
            delta[c] = (int16_t)(p[0] | (p[1] << 8));
            p += 2;
            if (delta[c] < 16)
                delta[c] = 16;
        }
        for (int c = 0; c < ch; ++c)
        {
            s1[c] = (int16_t)(p[0] | (p[1] << 8));
            p += 2;
        }
        for (int c = 0; c < ch; ++c)
        {
            s2[c] = (int16_t)(p[0] | (p[1] << 8));
            p += 2;
        }
        for (int c = 0; c < ch; ++c)
            out.push_back((int16_t)s2[c]);
        for (int c = 0; c < ch; ++c)
            out.push_back((int16_t)s1[c]);
        int produced = 2;
        const uint8_t *nibEnd = src + blockAlign;
        while (p < nibEnd && produced < samplesPerBlock && out.size() / (size_t)ch < target)
        {
            const uint8_t byte = *p++;
            const int nibs[2] = {byte >> 4, byte & 0x0f};
            const int nch = ch == 1 ? 2 : 1;
            for (int n = 0; n < nch && produced < samplesPerBlock && out.size() / (size_t)ch < target; ++n)
            {
                for (int c = 0; c < ch; ++c)
                {
                    const int nibIndex = ch == 1 ? n : c;
                    int nib = nibs[nibIndex];
                    const int snib = nib & 8 ? nib - 16 : nib;
                    int sample = ((s1[c] * c1[pred[c]]) + (s2[c] * c2[pred[c]])) / 256;
                    sample += snib * delta[c];
                    sample = Clip16(sample);
                    s2[c] = s1[c];
                    s1[c] = sample;
                    delta[c] = (adapt[nib] * delta[c]) >> 8;
                    if (delta[c] < 16)
                        delta[c] = 16;
                    out.push_back((int16_t)sample);
                }
                ++produced;
            }
        }
        src += blockAlign;
    }
    }
    catch (const std::bad_alloc &)
    {
        out.clear();
        return false;
    }
    const unsigned framesDecoded = (unsigned)(out.size() / (size_t)ch);
    const unsigned frameDelta = framesDecoded > snd->frame_count ? framesDecoded - snd->frame_count
                                                                 : snd->frame_count - framesDecoded;
    // A matching file ends on frame_count, or within one block when the last block is short.
    // A file longer than the cap is accepted once that cap is filled. Anything else ran out of
    // bytes early (wrong stride) and must not be played.
    const bool capped = snd->frame_count > kWebMaxFrames && framesDecoded + (unsigned)samplesPerBlock >= kWebMaxFrames;
    if (framesDecoded == 0 || (!capped && frameDelta > (unsigned)samplesPerBlock))
        return false;
    return true;
}

static bool DecodeMsAdpcmWindow(const uint8_t *data, unsigned size, unsigned channels, std::vector<int16_t> &out)
{
    if (!data || channels < 1 || channels > 2)
        return false;
    const unsigned blockAlign = 262u * channels;
    if (size < blockAlign)
        return false;
    const unsigned samplesPerBlock = 2u * blockAlign / channels - 12u;
    if (samplesPerBlock <= 2)
        return false;
    const unsigned blocks = size / blockAlign;
    snd_asset window = {};
    window.data = (char *)data;
    window.data_size = blocks * blockAlign;
    window.channel_count = channels;
    window.frame_count = blocks * samplesPerBlock;
    window.format = SND_ASSET_FORMAT_MSADPCM;
    return DecodeMsAdpcm(&window, out);
}

static bool Web_FailedSnd(const LoadedSound *ls)
{
    if (!ls)
        return false;
    for (int i = 0; i < s_failCount; ++i)
    {
        if (s_failSnd[i] == ls)
            return true;
    }
    return false;
}

static void Web_RememberFail(const LoadedSound *ls)
{
    if (!ls || Web_FailedSnd(ls) || s_failCount >= 32)
        return;
    s_failSnd[s_failCount++] = ls;
}

struct WebRefuseSeen
{
    char name[64];
    int type;
    int format;
};

static void Web_LogRefused(const snd_alias_t *alias, int type, int format, const char *why)
{
    const char *name = (alias && alias->name) ? alias->name : "?";
    if (!why)
        why = "";
    char key[64];
    int n = 0;
    for (; n < 63 && name[n]; ++n)
        key[n] = name[n];
    key[n] = 0;
    static WebRefuseSeen seen[48];
    static int seenCount;
    for (int i = 0; i < seenCount; ++i)
    {
        if (seen[i].type == type && seen[i].format == format && !strcmp(seen[i].name, key))
            return;
    }
    if (seenCount >= 48)
        return;
    WebRefuseSeen *slot = &seen[seenCount++];
    memcpy(slot->name, key, sizeof(key));
    slot->type = type;
    slot->format = format;
    Com_PrintError(9, "web audio: refused '%s' type %d format %d%s\n", key, type, format, why);
}

static void Web_LogAdpcmOnce(const snd_alias_t *alias, const snd_asset *snd, unsigned decodedFrames)
{
    const LoadedSound *ls = (alias && alias->soundFile) ? alias->soundFile->u.loadSnd : nullptr;
    static const LoadedSound *seen[32];
    static int seenCount;
    static bool loggedNull;
    if (!ls)
    {
        if (loggedNull)
            return;
        loggedNull = true;
    }
    else
    {
        for (int i = 0; i < seenCount; ++i)
        {
            if (seen[i] == ls)
                return;
        }
        if (seenCount >= 32)
            return;
        seen[seenCount++] = ls;
    }
    Com_PrintError(9,
                   "web audio: ADPCM decode mismatch '%s' format %d rate %u ch %u block_size %u data_size %u frame_count %u decoded %u\n",
                   (alias && alias->name) ? alias->name : "?",
                   snd ? (int)snd->format : -1,
                   snd ? snd->frame_rate : 0u,
                   snd ? snd->channel_count : 0u,
                   snd ? snd->block_size : 0u,
                   snd ? snd->data_size : 0u,
                   snd ? snd->frame_count : 0u,
                   decodedFrames);
}

// XAudio's xWMA path (format tag 353 / WAVE_FORMAT_WMAUDIO2) is WMAv2 packets.
// Block align is the desktop driver's fixed packet size, not snd->block_size.
// Bitrate normalization matches FFmpeg's xwma demuxer: a few channel/rate pairs
// are written with a fake bitrate and the decoder would need the real one.
static int Web_NormalizeXwmaBitrate(int channels, unsigned rate, int bitrate)
{
    if (channels == 1)
    {
        if (rate == 22050 && (bitrate == 48000 || bitrate == 192000))
            return 20000;
        if (rate == 32000 && (bitrate == 48000 || bitrate == 192000))
            return 20000;
        if (rate == 44100 && (bitrate == 96000 || bitrate == 192000))
            return 48000;
    }
    else if (channels == 2)
    {
        if (rate == 22050 && (bitrate == 48000 || bitrate == 192000))
            return 32000;
        if (rate == 32000 && bitrate == 192000)
            return 48000;
    }
    return bitrate;
}

static bool Web_DecodeLoadedXwma(const snd_asset *snd, std::vector<int16_t> &pcm, unsigned *frames, char *why, unsigned whyLen)
{
    pcm.clear();
    if (frames)
        *frames = 0;
    if (why && whyLen)
        why[0] = 0;
    if (!snd || !why || whyLen < 8)
        return false;
    const unsigned channels = snd->channel_count;
    if ((channels != 1 && channels != 2) || !snd->data || !snd->data_size || !snd->frame_count || !snd->frame_rate || !snd->seek_table
        || !snd->seek_table_count || snd->seek_table_count > 8192u)
    {
        Com_sprintf(why, whyLen, " (xWMA tag 353, packet layout rejected)");
        return false;
    }
    const unsigned blockAlign = channels == 1 ? 2230u : 4096u;
    if (!Web_HeapRange(snd->data, snd->data_size)
        || !Web_HeapRange(snd->seek_table, (uint64_t)snd->seek_table_count * sizeof(*snd->seek_table))
        || (unsigned long long)snd->seek_table_count * blockAlign > snd->data_size)
    {
        Com_sprintf(why, whyLen, " (xWMA tag 353, packet layout rejected)");
        return false;
    }
    unsigned prev = 0;
    for (unsigned i = 0; i < snd->seek_table_count; ++i)
    {
        const unsigned cum = snd->seek_table[i];
        if (cum < prev)
        {
            Com_sprintf(why, whyLen, " (xWMA tag 353, packet layout rejected)");
            return false;
        }
        prev = cum;
    }
    if (!prev)
    {
        Com_sprintf(why, whyLen, " (xWMA tag 353, packet layout rejected)");
        return false;
    }
    // XAudio submits 6000 bytes/s per channel. data_size includes packet padding,
    // so estimating bitrate from data_size/frame_count corrupts short one-shots.
    int bitrate = 48000 * channels;
    bitrate = Web_NormalizeXwmaBitrate((int)channels, snd->frame_rate, bitrate);
    // Opt-in raw-packet diagnostic for comparing the exact fastfile sound with a reference decoder.
    if (Dvar_GetBool("web_audio_dump") && fs_homepath)
    {
        char path[512];
        snprintf(path, sizeof(path), "%s/wma_diagnostic.dat", fs_homepath->current.string);
        if (FILE *file = fopen(path, "wb"))
        {
            const unsigned metadata[] = {channels, snd->frame_rate, (unsigned)bitrate,
                blockAlign, snd->seek_table_count, snd->frame_count, snd->data_size};
            fwrite(metadata, sizeof(metadata), 1, file);
            fwrite(snd->data, snd->data_size, 1, file);
            fclose(file);
        }
    }
    if (snd->frame_count > kWebMaxFrames || snd->frame_rate < 8000 || snd->frame_rate > 48000)
    {
        Com_sprintf(why, whyLen, " (xWMA sample count or rate rejected)");
        return false;
    }
    try { pcm.resize((size_t)snd->frame_count * channels); }
    catch (const std::bad_alloc &) { return false; }
    const int decoded = Kisak_DecodeWmav2((const uint8_t *)snd->data, snd->seek_table_count,
        blockAlign, channels, snd->frame_rate, bitrate, pcm.data(), snd->frame_count);
    if (decoded <= 0)
    {
        pcm.clear();
        Com_sprintf(why, whyLen, " (WMAv2 decode failed)");
        return false;
    }
    pcm.resize((size_t)decoded * channels);
    if (frames) *frames = decoded;
    return true;
}

static bool Web_StreamFormatOk(const snd_asset *header)
{
    if (!header)
        return false;
    if (header->channel_count != 1 && header->channel_count != 2)
        return false;
    if (header->frame_rate < 3000 || header->frame_rate > 768000)
        return false;
    if (!header->frame_count)
        return false;
    return header->format == SND_ASSET_FORMAT_PCMS16 || header->format == SND_ASSET_FORMAT_MSADPCM;
}

static int16_t *Web_TakeWindow(const snd_asset *header, const char *data, unsigned size, unsigned *frames, bool *fatal)
{
    *frames = 0;
    *fatal = false;
    const unsigned ch = header->channel_count;
    if (header->format == SND_ASSET_FORMAT_PCMS16)
    {
        const unsigned frameBytes = ch * 2u;
        if (size < frameBytes)
            return nullptr;
        unsigned nframes = size / frameBytes;
        if (nframes > kWebMaxFrames)
            nframes = kWebMaxFrames;
        const size_t n = (size_t)nframes * ch;
        int16_t *copy = (int16_t *)malloc(n * sizeof(int16_t));
        if (!copy)
        {
            *fatal = true;
            return nullptr;
        }
        memcpy(copy, data, n * sizeof(int16_t));
        *frames = nframes;
        return copy;
    }
    if (header->format == SND_ASSET_FORMAT_MSADPCM)
    {
        if (size < 262u * ch)
            return nullptr;
        std::vector<int16_t> pcm;
        if (!DecodeMsAdpcmWindow((const uint8_t *)data, size, ch, pcm) || pcm.size() < ch)
        {
            *fatal = true;
            return nullptr;
        }
        unsigned nframes = (unsigned)(pcm.size() / ch);
        if (nframes > kWebMaxFrames)
            nframes = kWebMaxFrames;
        const size_t n = (size_t)nframes * ch;
        int16_t *copy = (int16_t *)malloc(n * sizeof(int16_t));
        if (!copy)
        {
            *fatal = true;
            return nullptr;
        }
        memcpy(copy, pcm.data(), n * sizeof(int16_t));
        *frames = nframes;
        return copy;
    }
    *fatal = true;
    return nullptr;
}

static void Web_PumpStream(unsigned voice);

static int Web_StartStream(SndStartAliasInfo *info, unsigned voice)
{
    SoundFile *file = info->alias->soundFile;
    const int id = info->playbackId ? info->playbackId : 1;
    const int type = file->type;
    if (!SND_IsStream(voice))
    {
        Web_LogRefused(info->alias, type, -1, " (not a stream slot)");
        return -1;
    }
    if (!file->exists || !file->u.streamSnd)
    {
        Web_LogRefused(info->alias, type, -1, file->exists ? " (missing stream)" : " (not found)");
        return -1;
    }
    if (s_streamOpen[voice])
        Web_StopVoiceNode((int)voice);
    if (Snd_StreamStatus(voice) != SND_STREAM_UNUSED)
    {
        Snd_StreamGetFreeWindows(voice);
        if (Snd_StreamStatus(voice) != SND_STREAM_UNUSED)
            Snd_StreamClose(voice);
    }

    char filename[260];
    filename[0] = 0;
    SND_AliasGetFileName(info->alias, filename, 256);
    if (!filename[0])
    {
        Web_LogRefused(info->alias, type, -1, " (empty stream name)");
        return -1;
    }
    unsigned primeSize = 0;
    char *primeData = nullptr;
    if (file->type == 3 && file->u.streamSnd->primeSnd && file->u.streamSnd->primeSnd->buffer && file->u.streamSnd->primeSnd->size)
    {
        primeData = file->u.streamSnd->primeSnd->buffer;
        primeSize = file->u.streamSnd->primeSnd->size;
    }
    Snd_StreamOpen(voice, filename, (info->alias->flags & 1) != 0, primeSize, primeData);
    const snd_asset *header = Snd_StreamGetHeader(voice);
    if (header && !Web_StreamFormatOk(header))
    {
        const char *why = header->format == SND_ASSET_FORMAT_WMA
                              ? " (stream is not PCM or ADPCM; xWMA tag 353 has no WMAv2 decoder)"
                              : " (stream is not PCM or ADPCM)";
        Web_LogRefused(info->alias, type, (int)header->format, why);
        file->exists = 0;
        Snd_StreamClose(voice);
        return -1;
    }

    s_streamLive[voice].store(0, std::memory_order_release);
    s_streamEof[voice].store(0, std::memory_order_release);
    // Leave the header unclaimed as loaded until the next SD_UpdateVoice. Subtitles are
    // queued after SD_StartAlias returns, and SND_StartLengthNotify only runs from
    // SND_SetSoundFileVoiceInfo. Promoting here would fire that list while it is still empty.
    s_streamHeaderReady[voice] = false;
    s_streamOpen[voice] = true;
    // Same claim order as loaded PCM: file info, then start info. Channel count and
    // length stay SFLS_LOADING until Snd_StreamGetHeader succeeds.
    SND_SetSoundFileVoiceInfo(voice, 0, 0, 0, 0, SFLS_LOADING);
    SND_SetVoiceStartInfo(voice, info);
    Web_BumpGen(voice);
    return id;
}

static void Web_RefuseOpenStream(unsigned voice, const char *why)
{
    snd_voice_t *sv = &g_snd.voice[voice];
    const snd_alias_t *alias = sv->alias;
    const snd_asset *header = Snd_StreamGetHeader(voice);
    const int type = (alias && alias->soundFile) ? alias->soundFile->type : -1;
    const int format = header ? (int)header->format : -1;
    Web_LogRefused(alias, type, format, why);
    if (alias && alias->soundFile)
        alias->soundFile->exists = 0;
    SND_StopVoice((int)voice);
}

static void Web_PumpStream(unsigned voice)
{
    if (voice >= (unsigned)kStreamSlots || !s_streamOpen[voice])
        return;
    if (!g_snd.voiceAliasHash[voice])
        return;
    snd_voice_t *sv = &g_snd.voice[voice];
    if (sv->paused)
        return;
    const snd_stream_status status = Snd_StreamStatus(voice);
    if (status == SND_STREAM_UNUSED)
    {
        s_streamOpen[voice] = false;
        return;
    }
    if (status == SND_STREAM_ERROR)
    {
        Web_RefuseOpenStream(voice, " (stream error)");
        return;
    }
    const snd_asset *header = Snd_StreamGetHeader(voice);
    if (!s_streamHeaderReady[voice])
    {
        if (!header)
        {
            if (status == SND_STREAM_EOF)
            {
                s_streamEof[voice].store(1, std::memory_order_release);
                if (s_streamLive[voice].load(std::memory_order_acquire) == 0)
                    s_voiceEndedGen[voice].store(s_voiceGen[voice].load(std::memory_order_acquire), std::memory_order_release);
            }
            return;
        }
        if (!Web_StreamFormatOk(header))
        {
            const char *why = header->format == SND_ASSET_FORMAT_WMA
                                  ? " (stream is not PCM or ADPCM; xWMA tag 353 has no WMAv2 decoder)"
                                  : " (stream is not PCM or ADPCM)";
            Web_RefuseOpenStream(voice, why);
            return;
        }
        unsigned total = 0;
        if (sv->alias && (sv->alias->flags & 1) == 0 && header->frame_rate)
            total = (unsigned)((1000ull * header->frame_count) / header->frame_rate);
        SND_SetSoundFileVoiceInfo(voice, (int)header->channel_count, (int)header->frame_rate, total, 0, SFLS_LOADED);
        SND_UpdateVoice(sv, 0.f);
        s_streamHeaderReady[voice] = true;
    }
    if (!header)
        header = Snd_StreamGetHeader(voice);
    if (!header)
        return;
    if (status == SND_STREAM_EOF || s_streamEof[voice].load(std::memory_order_acquire))
    {
        s_streamEof[voice].store(1, std::memory_order_release);
        if (s_streamLive[voice].load(std::memory_order_acquire) == 0)
            s_voiceEndedGen[voice].store(s_voiceGen[voice].load(std::memory_order_acquire), std::memory_order_release);
        return;
    }

    const int gen = s_voiceGen[voice].load(std::memory_order_acquire);
    for (int step = 0; step < 8 && s_streamOpen[voice] && s_streamLive[voice].load(std::memory_order_acquire) < kStreamAhead; ++step)
    {
        Snd_StreamGetFreeWindows(voice);
        unsigned size = 0;
        unsigned position = 0;
        char *data = nullptr;
        const snd_stream_status got = Snd_StreamAcquireWindow(voice, &size, &position, &data);
        if (got == SND_STREAM_STARVING)
            return;
        if (got == SND_STREAM_EOF)
        {
            s_streamEof[voice].store(1, std::memory_order_release);
            if (s_streamLive[voice].load(std::memory_order_acquire) == 0)
                s_voiceEndedGen[voice].store(gen, std::memory_order_release);
            return;
        }
        if (got == SND_STREAM_ERROR || got != SND_STREAM_OK || !data)
        {
            Web_RefuseOpenStream(voice, " (stream error)");
            return;
        }
        if (!size)
        {
            Snd_StreamReleaseWindow(voice, data);
            return;
        }
        unsigned frames = 0;
        bool fatal = false;
        int16_t *copy = Web_TakeWindow(header, data, size, &frames, &fatal);
        Snd_StreamReleaseWindow(voice, data);
        if (!copy)
        {
            if (!fatal)
                continue;
            Web_RefuseOpenStream(voice, header->format == SND_ASSET_FORMAT_MSADPCM ? " (stream ADPCM decode failed)"
                                                                                    : " (stream PCM rejected)");
            return;
        }
        int pan[4];
        Web_VoicePan(sv, (int)header->channel_count, pan);
        s_streamLive[voice].fetch_add(1, std::memory_order_acq_rel);
        Web_QueuePcm((int)voice, gen, (int)header->frame_rate, (int)header->channel_count, (int)frames, copy, 0,
                     Web_DryGainMilli(sv), 0, Web_PitchMilli(sv), pan, 1);
        (void)position;
    }
}

int __cdecl SD_StartAlias(SndStartAliasInfo *info, unsigned int voice)
{
    if (!s_inited || !info || !info->alias || !info->alias->soundFile)
        return -1;
    if (voice >= (unsigned)kWebVoices)
        return -1;
    struct AudioWork {
        const char *name;
        double began = emscripten_get_now();
        ~AudioWork() { Web_ReportAssetWork("audio-start-decode", name, began); }
    } work{info->alias->name};
    const int id = info->playbackId ? info->playbackId : 1;
    SoundFile *file = info->alias->soundFile;
    if (file->type != 1)
        return Web_StartStream(info, voice);
    if (!file->exists || !file->u.loadSnd)
    {
        Web_LogRefused(info->alias, file->type, -1, " (missing loaded sound)");
        return -1;
    }
    if (!Web_HeapRange(file->u.loadSnd, sizeof(LoadedSound)))
    {
        Web_LogRefused(info->alias, file->type, -1, " (loaded sound outside heap)");
        return -1;
    }
    const snd_asset *snd = &file->u.loadSnd->sound;
    const LoadedSound *ls = file->u.loadSnd;
    if (Web_FailedSnd(ls))
    {
        Web_LogRefused(info->alias, file->type, snd ? (int)snd->format : -1, nullptr);
        return -1;
    }
    if (!Web_HeapRange(snd->data, snd->data_size) || !snd->data_size
        || (snd->channel_count != 1 && snd->channel_count != 2) || !snd->frame_count || !snd->frame_rate)
    {
        Web_RememberFail(ls);
        Web_LogRefused(info->alias, file->type, snd ? (int)snd->format : -1, " (empty or bad channel count)");
        return -1;
    }

    const int16_t *pcm = nullptr;
    unsigned frames = 0;
    const unsigned channels = snd->channel_count;
    const unsigned rate = snd->frame_rate;
    std::vector<int16_t> wmaPcm;
    if (snd->format == SND_ASSET_FORMAT_PCMS16)
    {
        pcm = (const int16_t *)snd->data;
        frames = snd->frame_count;
    }
    else if (snd->format == SND_ASSET_FORMAT_MSADPCM)
    {
        auto &decoded = s_adpcmDecoded;
        const std::vector<int16_t> *found = nullptr;
        for (const auto &e : decoded)
        {
            if (e.first == ls)
            {
                found = &e.second;
                break;
            }
        }
        if (!found)
        {
            try { decoded.emplace_back(ls, std::vector<int16_t>()); }
            catch (const std::bad_alloc &) { return -1; }
            if (!DecodeMsAdpcm(snd, decoded.back().second))
            {
                const unsigned decodedFrames = snd->channel_count
                                                   ? (unsigned)(decoded.back().second.size() / snd->channel_count)
                                                   : 0u;
                Web_LogAdpcmOnce(info->alias, snd, decodedFrames);
                decoded.pop_back();
                Web_RememberFail(ls);
                Web_LogRefused(info->alias, file->type, (int)snd->format, " (ADPCM decode failed)");
                return -1;
            }
            found = &decoded.back().second;
        }
        pcm = found->data();
        if (!pcm)
        {
            Web_RememberFail(ls);
            Web_LogRefused(info->alias, file->type, (int)snd->format, " (ADPCM decode failed)");
            return -1;
        }
        frames = (unsigned)(found->size() / channels);
    }
    else if (snd->format == SND_ASSET_FORMAT_WMA)
    {
        auto &decoded = s_wmaDecoded;
        const std::vector<int16_t> *found = nullptr;
        for (const auto &entry : decoded) if (entry.first == ls) { found = &entry.second; break; }
        if (!found)
        {
            char why[160] = {};
            unsigned wmaFrames = 0;
            if (!Web_DecodeLoadedXwma(snd, wmaPcm, &wmaFrames, why, sizeof(why)))
            {
                Web_RememberFail(ls);
                Web_LogRefused(info->alias, file->type, (int)snd->format, why);
                return -1;
            }
            try { decoded.emplace_back(ls, std::move(wmaPcm)); }
            catch (const std::bad_alloc &) { return -1; }
            found = &decoded.back().second;
        }
        pcm = found->data();
        frames = (unsigned)(found->size() / channels);
    }
    else
    {
        Web_RememberFail(ls);
        Web_LogRefused(info->alias, file->type, (int)snd->format, nullptr);
        return -1;
    }

    if (frames > kWebMaxFrames)
        frames = kWebMaxFrames;
    // PCM byte count must not wrap. ADPCM and a decoded WMA buffer are already expanded.
    if (snd->format == SND_ASSET_FORMAT_PCMS16 && (uint64_t)frames * channels * 2ull > snd->data_size)
    {
        Web_LogRefused(info->alias, file->type, (int)snd->format, " (PCM shorter than frame_count)");
        return -1;
    }
    const uint64_t sampleCount64 = (uint64_t)frames * channels;
    if (!pcm || sampleCount64 == 0 || sampleCount64 > (uint64_t)kWebMaxFrames * 2ull)
    {
        Web_LogRefused(info->alias, file->type, (int)snd->format, " (no samples)");
        return -1;
    }
    // The page copies this on the main thread later. The zone that owns `pcm` can unload
    // on a map change before that runs, which is a wasm trap. This copy outlives the zone.
    const size_t sampleCount = (size_t)sampleCount64;
    int16_t *copy = (int16_t *)malloc(sampleCount * sizeof(int16_t));
    if (!copy)
        return -1;
    memcpy(copy, pcm, sampleCount * sizeof(int16_t));

    const bool looping = (info->alias->flags & 1) != 0;
    unsigned int total_msec = 0;
    if (!looping)
        total_msec = (unsigned int)info->startDelay + (unsigned int)((double)snd->frame_count * 1000.0 / (double)rate);

    // Same claim order as SND_StartAliasRam: file info, then start info (which requires a free voice).
    SND_SetSoundFileVoiceInfo(voice, (int)channels, (int)rate, total_msec, 0, SFLS_LOADED);
    SND_SetVoiceStartInfo(voice, info);

    int pan[4];
    Web_VoicePan(&g_snd.voice[voice], (int)channels, pan);
    // A zero speaker map schedules the one-shot at silence. Looping beds get later
    // updates; a gunshot does not. Center it when every ear is zero.
    if (pan[0] == 0 && pan[1] == 0 && pan[2] == 0 && pan[3] == 0)
    {
        pan[0] = 1000;
        pan[1] = 1000;
    }
    const int gainMilli = Web_DryGainMilli(&g_snd.voice[voice]);
    const int pitchMilli = Web_PitchMilli(&g_snd.voice[voice]);
    const int gen = Web_BumpGen(voice);
    Web_QueuePcm((int)voice, gen, (int)rate, (int)channels, (int)frames, copy, (int)(intptr_t)ls, gainMilli, looping ? 1 : 0,
                 pitchMilli, pan, 0);
    static const LoadedSound *queued[128];
    static unsigned queuedCount;
    bool seen = false;
    for (unsigned i = 0; i < queuedCount; ++i) seen |= queued[i] == ls;
    if (!seen && queuedCount < 128)
    {
        queued[queuedCount++] = ls;
        Com_Printf(9, "web audio: queued '%s' format %u (%u PCM frames)\n", info->alias->name, snd->format, frames);
    }
    return id;
}

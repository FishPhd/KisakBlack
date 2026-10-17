#include "web_d3d9_device.h"
#include <web/web_graphics.h>
#include <database/db_registry.h>
#include <gfx_d3d/r_init.h>
#include <gfx_d3d/r_material.h>
#include <universal/dvar.h>
#include <emscripten.h>
#include <emscripten/threading.h>
#include <set>
#include <cstdio>
#include <web/gpu/web_gpu.h>
#include <gfx_d3d/r_rendercmds.h>
#include <client_mp/cl_input_mp.h>
#include <mutex>
#include <string>
#include <deque>
#include <algorithm>
#include <client_mp/cl_scrn_mp.h>

namespace
{
std::atomic<bool> cancelWarmup{false};
std::atomic<bool> stalledWarmup{false};
std::atomic<unsigned> requestedGeneration{0}, completedGeneration{0};
std::mutex requestMutex;
std::string requestedMap, activeMap;
unsigned activeGeneration = 0;
std::deque<size_t> pendingLinks;
EM_JS(void, ReportWarmup, (int done, int total, int active, const char *map, int failures, const char *status), {
    const g = Module.kisakGl;
    if (g && g.stats) g.stats.postMessage({warmup: {done, total, active: !!active,
        map:UTF8ToString(map), stage:'map', failures, status:UTF8ToString(status)}});
});

struct Collector
{
    kd::Device *device;
    std::set<kd::ProgKey> seen;
};

void CollectTechniques(XAssetHeader header, void *data)
{
    auto &c = *static_cast<Collector *>(data);
    const auto *set = header.techniqueSet;
    if (!set) return;
    for (auto *tech : set->techniques)
    {
        if (!tech) continue;
        for (unsigned i = 0; i < tech->passCount; ++i)
        {
            const MaterialPass &pass = tech->passArray[i];
            if (!pass.vertexShader || !pass.pixelShader) continue;
            auto *vs = static_cast<kd::KVShader *>(pass.vertexShader->prog.vs);
            auto *ps = static_cast<kd::KPShader *>(pass.pixelShader->prog.ps);
            if (!vs || !ps) continue;
            uint32_t shadow = 0;
            for (const auto &s : ps->tr.samplers)
                if (s.type == 2 && s.proj && !s.raw && s.reg >= 0 && s.reg < 16)
                    shadow |= 1u << s.reg;
            for (uint32_t mask : {0u, shadow})
            {
                if (!c.seen.insert({vs, ps, mask}).second) continue;
                vs->AddRef(); ps->AddRef();
                c.device->warmTasks.push_back({vs, ps, mask,
                    std::string(set->name ? set->name : "?") + "/" + (tech->name ? tech->name : "?")});
            }
        }
    }
}
}

void Web_PrepareLegacyMap(const char *map, int localClientNum)
{
    if (webgpu::Requested() || !Web_ShaderWarmingEnabled()) return;
    DB_SyncXAssets();
    unsigned generation;
    {
        std::lock_guard lock(requestMutex);
        requestedMap = map;
        cancelWarmup.store(false);
        stalledWarmup.store(false);
        generation = requestedGeneration.fetch_add(1) + 1;
    }
    // Present on the GL owner continues while the game thread stays in loading.
    R_BeginRemoteScreenUpdate();
    const bool remote = R_IsInRemoteScreenUpdate();
    const double start = emscripten_get_now();
    double keepalive = start;
    while (completedGeneration.load() < generation && !cancelWarmup.load()) {
        if (!remote) SCR_UpdateScreen();
        const double now = emscripten_get_now();
        if (now - keepalive >= 1000) { CL_WritePacket(localClientNum); keepalive = now; }
        if (now - start > 120000) {
            stalledWarmup.store(true);
            cancelWarmup.store(true);
            fprintf(stderr, "WEB_GL_PREPARATION_INCOMPLETE map=%s reason=stalled\n", map);
            break;
        }
        emscripten_thread_sleep(2);
    }
    R_EndRemoteScreenUpdate(nullptr);
}

extern "C" EMSCRIPTEN_KEEPALIVE void Kisak_CancelShaderWarmup()
{
    cancelWarmup.store(true);
    webgpu::CancelWarmup();
}

namespace kd
{
void Device::clearShaderWarmup()
{
    for (auto &task : warmTasks) { task.vs->Release(); task.ps->Release(); }
    warmTasks.clear();
    warmCursor = 0;
    warmPollCursor = 0;
    warmDone = 0;
    pendingLinks.clear();
}

void Device::tickShaderWarmup()
{
    if (!onGl()) return;
    const unsigned generation = requestedGeneration.load();
    if (generation == completedGeneration.load()) return;
    if (activeGeneration != generation)
    {
        clearShaderWarmup();
        { std::lock_guard lock(requestMutex); activeMap = requestedMap; }
        activeGeneration = generation;
        warmWorld = rgp.world;
        warmStarted = false;
    }
    if (!Web_ShaderWarmingEnabled() || cancelWarmup.load())
    {
        ReportWarmup(warmDone, int(warmTasks.size()), 0, activeMap.c_str(), stalledWarmup ? 1 : 0,
            stalledWarmup ? "incomplete" : "skipped");
        clearShaderWarmup();
        completedGeneration.store(generation);
        return;
    }
    if (!warmStarted && warmWorld)
    {
        warmStarted = true;
        Collector collector{this, {}};
        DB_EnumXAssets_FastFile(ASSET_TYPE_TECHNIQUE_SET, CollectTechniques, &collector, false);
        ReportWarmup(0, (int)warmTasks.size(), !warmTasks.empty(), activeMap.c_str(), 0,
            warmTasks.empty() ? "complete" : "preparing");
    }
    if (warmTasks.empty()) { completedGeneration.store(generation); return; }
    const double start = emscripten_get_now();
    // Two submissions, or 4 ms, per frame. Synchronous drivers may take longer
    // for one shader; cancellation is observed between submissions.
    for (int batch = 0; batch < 2 && warmCursor < warmTasks.size(); ++batch)
    {
        if (cancelWarmup.load()) break;
        const auto index = warmCursor++;
        WarmTask &task = warmTasks[index];
        task.program = getProgram(task.vs, task.ps, task.shadow);
        if (task.program && task.program->pending) pendingLinks.push_back(index);
        else { task.completed = true; ++warmDone; }
        if (emscripten_get_now() - start >= 4) break;
    }
    // Poll a bounded number of links too; walking every loaded technique on
    // each frame would make warming itself an expensive render-loop task.
    const size_t polls = std::min<size_t>(32, pendingLinks.size());
    for (size_t poll = 0; poll < polls; ++poll)
    {
        const auto index = pendingLinks.front(); pendingLinks.pop_front();
        WarmTask &task = warmTasks[index];
        if (task.program->pending) finishProgram(task.program, false);
        if (!task.program->pending) { task.completed = true; ++warmDone; }
        else pendingLinks.push_back(index);
    }
    const int done = warmDone;
    if (start - warmReportAt >= 100 || done == (int)warmTasks.size())
    {
        warmReportAt = start;
        if (done != (int)warmTasks.size()) ReportWarmup(done, (int)warmTasks.size(), 1, activeMap.c_str(), 0, "preparing");
    }
    if (done == (int)warmTasks.size())
    {
        int failures = 0;
        for (const auto &task : warmTasks)
            if (!task.program || !task.program->ok)
            {
                ++failures;
                fprintf(stderr, "web: shader warming skipped failed technique %s\n", task.name.c_str());
            }
        ReportWarmup(done, int(warmTasks.size()), 0, activeMap.c_str(), failures, failures ? "incomplete" : "complete");
        clearShaderWarmup();
        completedGeneration.store(generation);
    }
}
}

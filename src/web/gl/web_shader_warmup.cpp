#include "web_d3d9_device.h"
#include <web/web_graphics.h>
#include <database/db_registry.h>
#include <gfx_d3d/r_init.h>
#include <gfx_d3d/r_material.h>
#include <universal/dvar.h>
#include <emscripten.h>
#include <set>
#include <cstdio>

namespace
{
std::atomic<bool> cancelWarmup{false};
EM_JS(void, ReportWarmup, (int done, int total, int active), {
    const g = Module.kisakGl;
    if (g && g.stats) g.stats.postMessage({warmup: {done: done, total: total, active: !!active}});
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

extern "C" EMSCRIPTEN_KEEPALIVE void Kisak_CancelShaderWarmup()
{
    cancelWarmup.store(true);
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
    ReportWarmup(0, 0, 0);
}

void Device::tickShaderWarmup()
{
    if (!onGl()) return;
    if (warmWorld != rgp.world)
    {
        clearShaderWarmup();
        warmWorld = rgp.world;
        warmStarted = false;
        cancelWarmup.store(false);
    }
    if (!web_warmShaders || !web_warmShaders->current.enabled || cancelWarmup.load())
    {
        if (!warmTasks.empty()) clearShaderWarmup();
        return;
    }
    if (!warmStarted && warmWorld)
    {
        warmStarted = true;
        Collector collector{this, {}};
        DB_EnumXAssets_FastFile(ASSET_TYPE_TECHNIQUE_SET, CollectTechniques, &collector, false);
        ReportWarmup(0, (int)warmTasks.size(), !warmTasks.empty());
    }
    if (warmTasks.empty()) return;
    const double start = emscripten_get_now();
    // Two submissions, or 4 ms, per frame. Synchronous drivers may take longer
    // for one shader; cancellation is observed between submissions.
    for (int batch = 0; batch < 2 && warmCursor < warmTasks.size(); ++batch)
    {
        if (cancelWarmup.load()) break;
        WarmTask &task = warmTasks[warmCursor++];
        task.program = getProgram(task.vs, task.ps, task.shadow);
        if (emscripten_get_now() - start >= 4) break;
    }
    // Poll a bounded number of links too; walking every loaded technique on
    // each frame would make warming itself an expensive render-loop task.
    for (unsigned poll = 0; poll < 32 && warmCursor; ++poll)
    {
        WarmTask &task = warmTasks[warmPollCursor++ % warmCursor];
        if (task.completed) continue;
        if (!task.program) continue;
        if (task.program->pending) finishProgram(task.program, false);
        if (!task.program->pending) { task.completed = true; ++warmDone; }
    }
    const int done = warmDone;
    if (start - warmReportAt >= 100 || done == (int)warmTasks.size())
    {
        warmReportAt = start;
        ReportWarmup(done, (int)warmTasks.size(), done != (int)warmTasks.size());
    }
    if (done == (int)warmTasks.size())
    {
        for (const auto &task : warmTasks)
            if (!task.program->ok)
                fprintf(stderr, "web: shader warming skipped failed technique %s\n", task.name.c_str());
        clearShaderWarmup();
    }
}
}

// JavaScript side of the WebGL2 back end. The render thread draws into its own OffscreenCanvas and
// hands each frame to the page as an ImageBitmap on BroadcastChannel "kisak-present". The page canvas
// stays on the main thread (web/kisak.js paints it with a bitmaprenderer). Transferring #game onto
// this pthread leaves a black page: Chrome does not composite an OffscreenCanvas WebGL buffer while
// the render thread never returns to the browser event loop.
//
// Under Node (the headless tests) there is no OffscreenCanvas: GlInit fails and the device runs in its
// "null" mode (all state and resources are tracked, nothing is drawn).
#include <emscripten.h>
#include <atomic>
#include <cstdint>

#include "web_d3d9_priv.h"
#include <web/web_assets.h>

EM_JS_DEPS(kisak_gl_deps, "$GL");

static std::atomic<int> s_presentReady{1};
static_assert(sizeof(s_presentReady) == 4, "presentation flag is one shared wasm word");
extern "C" EMSCRIPTEN_KEEPALIVE unsigned int Kisak_PresentReady()
{
    return (unsigned int)(uintptr_t)&s_presentReady;
}

EM_JS(int, kisak_gl_init, (int w, int h, const char *session, unsigned int ready), {
    try {
        if (typeof OffscreenCanvas === "undefined")
            return 0;
        const canvas = new OffscreenCanvas(w, h);
        const attrs = {
            majorVersion : 2,
            minorVersion : 0,
            alpha : false,
            depth : false,
            stencil : false,
            antialias : false,
            premultipliedAlpha : false,
            preserveDrawingBuffer : false,
            powerPreference : "high-performance",
            failIfMajorPerformanceCaveat : false,
            enableExtensionsByDefault : true
        };
        const gl = canvas.getContext("webgl2", attrs);
        if (!gl)
            return 0;
        const handle = GL.registerContext(gl, attrs);
        if (!GL.makeContextCurrent(handle))
            return 0;
        for (const e of [ "WEBGL_compressed_texture_s3tc", "WEBGL_compressed_texture_s3tc_srgb", "EXT_color_buffer_float",
                          "EXT_color_buffer_half_float", "OES_texture_float_linear", "EXT_texture_filter_anisotropic",
                          "EXT_texture_norm16", "EXT_texture_compression_rgtc", "KHR_parallel_shader_compile" ])
            gl.getExtension(e);
        Module.kisakGl = {
            canvas : canvas,
            gl : gl,
            channel : new BroadcastChannel("kisak-present-" + UTF8ToString(session)),
            stats : new BroadcastChannel("kisak-stats-" + UTF8ToString(session)),
            frames : 0,
            copies : 0,
            lastStats : 0,
            framesAtStats : 0,
            lastFrameAt : 0,
            frameIntervals : [],
            diagnosticFrames : [],
            copyMs : [],
            drawsAtStats : 0
        };
        Module.kisakGl.readyIndex = ready >>> 2;
        Atomics.store(HEAP32, Module.kisakGl.readyIndex, 1);
        console.log("kisak_gl_init: private canvas");
        return 1;
    } catch (e) {
        console.error("kisak_gl_init failed", e);
        return 0;
    }
});

EM_JS(void, kisak_gl_resize, (int w, int h), {
    const g = Module.kisakGl;
    if (!g)
        return;
    g.canvas.width = w;
    g.canvas.height = h;
});

EM_JS(void, kisak_gl_present, (unsigned draws), {
    const g = Module.kisakGl;
    if (!g)
        return;
    const frameAt = performance.now();
    if (g.lastFrameAt) {
        const intervalMs = frameAt - g.lastFrameAt;
        g.frameIntervals.push(intervalMs);
        g.diagnosticFrames.push({frameId:g.frames + 1, at:performance.timeOrigin + frameAt, intervalMs});
    }
    g.lastFrameAt = frameAt;
    g.frames++;
    if (g.channel && Atomics.compareExchange(HEAP32, g.readyIndex, 1, 0) === 1) {
        const copyAt = performance.now();
        const bmp = g.canvas.transferToImageBitmap();
        // Transfer the bitmap. Closing it here detaches the frame before the page can paint it.
        try {
            g.channel.postMessage({ bmp : bmp, n : g.frames });
            g.copies++;
            g.copyMs.push(performance.now() - copyAt);
            bmp.close(); // BroadcastChannel clones the bitmap; it does not accept a transfer list.
        } catch (e) {
            bmp.close();
            Atomics.store(HEAP32, g.readyIndex, 1);
            if (!g.warnedPresent) { console.error("web present: " + e.message); g.warnedPresent = true; }
        }
    }
    const now = performance.now();
    if (g.stats && now - g.lastStats >= 1000) {
        const dt = g.lastStats ? now - g.lastStats : 1000;
        const n = g.frames - g.framesAtStats;
        g.stats.postMessage({ frames : n, dt : dt, total : g.frames, copied : g.copies,
            draws : draws - g.drawsAtStats, frameIntervals : g.frameIntervals, copyMs : g.copyMs,
            diagnosticFrames : g.diagnosticFrames });
        g.frameIntervals = [];
        g.diagnosticFrames = [];
        g.copyMs = [];
        g.drawsAtStats = draws;
        g.lastStats = now;
        g.framesAtStats = g.frames;
    }
});

EM_JS(int, kisak_gl_has_ext, (const char *name), {
    const g = Module.kisakGl;
    if (!g)
        return 0;
    return g.gl.getExtension(UTF8ToString(name)) ? 1 : 0;
});

EM_JS(int, kisak_gl_max_texture, (), {
    const g = Module.kisakGl;
    return g ? g.gl.getParameter(g.gl.MAX_TEXTURE_SIZE) : 0;
});

EM_JS(void, kisak_gl_log, (const char *text), { console.log(UTF8ToString(text)); });

namespace kd
{
bool GlInit(int w, int h) { return kisak_gl_init(w, h, Web_SessionId(), Kisak_PresentReady()) != 0; }
void GlResize(int w, int h) { kisak_gl_resize(w, h); }
void GlPresent(unsigned draws) { kisak_gl_present(draws); }
bool GlHasExt(const char *name) { return kisak_gl_has_ext(name) != 0; }
int GlMaxTextureSize() { return kisak_gl_max_texture(); }
void GlLog(const char *text) { kisak_gl_log(text); }
} // namespace kd

// CONFIG is supplied by live.py. All captured data stays in build-re/live.
// Windows may enumerate a dependency before the executable; mainModule is not
// reliable during suspended startup. Resolve the executable by its pinned name.
const module = Process.getModuleByName('BlackOpsMP.exe');
if (module.name.toLowerCase() !== 'blackopsmp.exe' || Process.arch !== 'ia32')
  throw new Error('Expected the pinned x86 BlackOpsMP process');
const originalCode = Array.from(new Uint8Array(module.base.add(CONFIG.entry_rva).readByteArray(CONFIG.code_size)));
const deliver = new NativeCallback(pointer => {
  const words = [];
  for (let i = 0; i < 28; ++i) words.push(pointer.add(i * 4).readU32());
  send({type: 'capture', words}, pointer.add(112).readByteArray(0x26a4 * 2 + 0x8c));
}, 'void', ['pointer']);
const cm = new CModule(`
#include <gum/guminterceptor.h>
#include <glib.h>
#include <string.h>
extern void deliver(void *);
typedef struct {
  guint32 input[16], output[4], cw, mxcsr, regs[6];
  guint8 before[0x26a4], after[0x26a4], pml[0x8c];
} Capture;
static guint32 word(void * p) { guint32 v; memcpy(&v,p,4); return v; }
void on_enter(GumInvocationContext * ic) {
  guint16 cw; guint32 mxcsr;
  __asm__ volatile ("fnstcw %0" : "=m" (cw));
  __asm__ volatile ("stmxcsr %0" : "=m" (mxcsr));
  Capture * c = g_malloc0(sizeof(Capture));
  *((Capture **) gum_invocation_context_get_listener_invocation_data(ic, sizeof(Capture *))) = c;
  GumCpuContext * ctx = ic->cpu_context;
  guint8 * stack = (guint8 *) ctx->esp;
  guint8 * ps = (guint8 *) word(stack + 4);
  guint8 * pml = (guint8 *) ctx->edx;
  guint8 * direction = (guint8 *) ctx->ecx;
  guint8 * stop = *(guint8 **) ${module.base.add(CONFIG.global_rva)};
  c->cw = cw; c->mxcsr = mxcsr;
  c->input[0] = word(ps + 0xc);
  memcpy(c->input + 1, ps + 0x30, 12);
  memcpy(c->input + 4, direction, 12);
  c->input[7] = word(stack + 8); c->input[8] = word(stack + 12);
  c->input[9] = word(pml + 0x24); c->input[10] = word(stop + 0x18); c->input[11] = 1;
  c->regs[0] = (guint32) ps; c->regs[1] = ctx->esp; c->regs[2] = ctx->ecx;
  c->regs[3] = ctx->edx; c->regs[4] = ctx->eax; c->regs[5] = ctx->eip;
  memcpy(c->before, ps, 0x26a4); memcpy(c->pml, pml, 0x8c);
}
void on_leave(GumInvocationContext * ic) {
  Capture * c = *((Capture **) gum_invocation_context_get_listener_invocation_data(ic, sizeof(Capture *)));
  guint8 * ps = (guint8 *) c->regs[0];
  memcpy(c->after, ps, 0x26a4); memcpy(c->output, ps + 0x30, 12); c->output[3] = word(ps + 0xc);
  deliver(c); g_free(c);
}
`, {deliver});
Interceptor.attach(module.base.add(CONFIG.entry_rva), {onEnter: cm.on_enter, onLeave: cm.on_leave});
send({type: 'ready', module: module.path, base: module.base.toString(), code_before_hook: originalCode});

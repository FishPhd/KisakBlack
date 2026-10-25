// The complete production TU is compiled, including functions outside this
// pilot. MSVC resolves their references before COMDAT removal. These guards
// deliberately terminate rather than invent behavior if any adapter reaches
// an unsupported dependency. Never use a guard as a reference implementation.
#include <cstdlib>
#include <cstdio>
#include <physics/phys_traverse.h>
[[noreturn]] static void unsupported() {
    std::fputs("BLOCKED: selected adapter reached an unsupported dependency\n", stderr);
    std::abort();
}
bool Assert_MyHandler(const char*, int, int, const char*, ...) { unsupported(); }
float Vec3NormalizeTo(const float*, float*) { unsupported(); }
float Vec3Dot(const float*, const float*) { unsupported(); }
float Vec4Normalize(float*) { unsupported(); }
void NET_Sleep(unsigned int) { unsupported(); }
void Com_Printf(int, const char*, ...) { unsupported(); }
// Constructing a real pmove_t retains its proximity visitor's vtable. The
// slide-checker contract never invokes collision updates or diagnostics.
// Reaching either dependency must block rather than supply invented behavior.
void colgeom_visitor_t::intersect_box(float*, float*, int) { unsupported(); }
void StatMon_Warning(int, int, char*) { unsupported(); }

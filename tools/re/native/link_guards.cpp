// The complete production TU is compiled, including functions outside this
// pilot. MSVC resolves their references before COMDAT removal. These guards
// deliberately terminate rather than invent behavior if any adapter reaches
// an unsupported dependency. Never use a guard as a reference implementation.
#include <cstdlib>
#include <cstdio>
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
int G_IsServerGameSystem(int) { unsupported(); }

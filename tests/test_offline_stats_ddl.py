"""Compile and exercise the production offline schema selector in isolation.

Run from a Visual Studio x86 developer shell:
    python tests/test_offline_stats_ddl.py

Asset lookup and the offline switch are controlled inputs. This tests selection
and buffer boundaries, not archived-schema contents or a complete match.
"""

from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src/live/live_stats.cpp"


def main():
    text = SOURCE.read_text(encoding="utf-8")
    start = text.index("static ddlDef_t *LiveStats_SelectOfflineStatsDDL(")
    end = text.index("\n}\n#endif", start) + 2
    selector = text[start:end]
    harness = r'''
#include <ddl/ddl_api.h>
#include <ddl/ddl_buffer.h>
#include <cassert>
#include <cstring>
#include <cstdio>

static bool offline = true;
static bool archiveDefault = false;
static int lookups = 0;
static ddlRoot_t primary{}, archive{};
constexpr int ASSET_TYPE_DDL = 0;
struct AssetHeader { ddlRoot_t *ddlRoot; };
int LiveStorage_GetStatsBufferSize() { return 40168; }
bool Dvar_GetBool(const char *) { return offline; }
bool DB_IsXAssetDefault(int, const char *name)
{
    return archiveDefault && !strcmp(name, "ddl_mp/stats_archive.ddl");
}
void Com_Printf(int, const char *, ...) {}
AssetHeader DB_FindXAssetHeader(int, char *name, bool, int)
{
    ++lookups;
    return { !strcmp(name, "ddl_mp/stats.ddl") ? &primary : &archive };
}
'''
    harness += selector
    harness += r'''

ddlDef_t schema(int version, int bytes)
{
    ddlDef_t result{};
    result.version = version;
    result.size = (bytes - int(sizeof(ddlBufferHeader_t))) * 8;
    return result;
}

int main()
{
    auto v100 = schema(100, 40548);
    auto v99 = schema(99, 40269);
    auto v98 = schema(98, 40168);
    auto v97 = schema(97, 38568);
    primary.ddlDef = &v100;
    archive.ddlDef = &v99;
    v99.next = &v98;
    v98.next = &v97;
    assert(LiveStats_SelectOfflineStatsDDL(&v100) == &v98);

    // A single extra payload bit must not fit, regardless of version.
    auto overByBit = schema(101, 40168);
    ++overByBit.size;
    overByBit.next = &v99;
    archive.ddlDef = &overByBit;
    assert(LiveStats_SelectOfflineStatsDDL(&v100) == &v98);

    // Reject malformed negative sizes and select by version, not list order.
    auto invalid = schema(102, 40168);
    invalid.size = -1;
    invalid.next = &overByBit;
    archive.ddlDef = &invalid;
    v97.next = nullptr;
    auto fittingPrimary = schema(99, 40168);
    primary.ddlDef = &fittingPrimary;
    assert(LiveStats_SelectOfflineStatsDDL(&v100) == &fittingPrimary);

    lookups = 0;
    assert(LiveStats_SelectOfflineStatsDDL(&v98) == &v98);
    assert(lookups == 0);

    offline = false;
    assert(LiveStats_SelectOfflineStatsDDL(&v100) == &v100);
    assert(lookups == 0);
    offline = true;
    assert(LiveStats_SelectOfflineStatsDDL(nullptr) == nullptr);
    assert(lookups == 0);

    // With no fitting schema, retain the original so bounds checks still fail.
    primary.ddlDef = &v100;
    archive.ddlDef = nullptr;
    assert(LiveStats_SelectOfflineStatsDDL(&v100) == &v100);
    primary.ddlDef = nullptr;
    assert(LiveStats_SelectOfflineStatsDDL(&v100) == &v100);
    archive.ddlDef = &v98;
    archiveDefault = true;
    assert(LiveStats_SelectOfflineStatsDDL(&v100) == &v100);
    puts("9 offline stats-schema selection checks passed");
}
'''
    with tempfile.TemporaryDirectory(prefix="kisak-stats-ddl-") as work:
        directory = Path(work)
        cpp = directory / "stats_ddl_test.cpp"
        exe = directory / "stats_ddl_test.exe"
        cpp.write_text(harness, encoding="utf-8")
        result = subprocess.run(
            ["cl", "/nologo", "/std:c++20", "/EHsc", "/W4",
             f"/I{ROOT / 'src'}", str(cpp), f"/Fe{exe}"],
            cwd=directory, text=True, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        if result.returncode:
            raise RuntimeError(result.stdout)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()

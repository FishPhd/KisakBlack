"""Native production stat routing with pinned stock PC DDL metadata.

This is a controlled-context regression test, not original-native gameplay
qualification. Original instructions establish the routing rule independently;
storage/service bindings below provide fixture inputs and do not emulate a login.
No installed profile is read or written. Requires the same VS x86 tools as the
other standalone multiplayer checks.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import struct
import subprocess
import zlib

from mp_fidelity_common import ROOT, definition, reference
from test_cac_ownership import schema_cpp


def stock_schema():
    """Decode a stat projection from the actual hash-pinned asset."""
    saved = json.loads((ROOT / "tests/cac_ownership_reference.json").read_text())["stock_schema"]
    origin = saved["origin"]
    raw = (ROOT / origin["fastfile"]).read_bytes()
    assert hashlib.sha256(raw).hexdigest() == origin["fastfile_sha256"]
    data = zlib.decompress(raw[12:])
    assert hashlib.sha256(data).hexdigest() == origin["inflated_sha256"]
    pos = origin["serialized_rva"]
    assert hashlib.sha256(data[pos:pos + origin["serialized_size"]]).hexdigest() == origin["serialized_sha256"]
    virtual, strings = 0, {}

    def words(count):
        nonlocal pos, virtual
        result = list(struct.unpack_from("<" + "i" * count, data, pos))
        pos += count * 4
        virtual += count * 4
        return result

    def align():
        nonlocal virtual
        virtual = (virtual + 3) & ~3

    def string(pointer):
        nonlocal pos, virtual
        if pointer != -1:
            return ("packed", pointer & 0xffffffff)
        end = data.index(0, pos)
        value = data[pos:end].decode()
        strings[virtual] = value
        virtual += end + 1 - pos
        pos = end + 1
        return value

    version, bits, _, struct_count, _, enum_count, _ = words(7)
    align()
    headers = [words(4) for _ in range(struct_count)]
    structs = {}
    for index, (name, size, count, _) in enumerate(headers):
        name = string(name)
        align()
        records = [words(12) for _ in range(count)]
        structs[str(index)] = dict(name=name, size=size, members=[[string(m[0])] + m[1:] for m in records])
    align()
    headers = [words(3) for _ in range(enum_count)]
    enums = {}
    for index, (name, count, _) in enumerate(headers):
        name = string(name)
        align()
        enums[str(index)] = dict(name=name, members=[string(p) for p in words(count)])
    # The PC asset uses packed stream addresses for repeated strings. This
    # anchor is already pinned by the retained ownership-schema projection.
    delta = 0x151054 - next(k for k, v in strings.items() if v == "purchasedattachments")

    def resolve(value):
        if isinstance(value, tuple):
            return strings[((value[1] - 1) & 0x1fffffff) - delta + 1]
        if isinstance(value, list):
            return [resolve(v) for v in value]
        if isinstance(value, dict):
            return {k: resolve(v) for k, v in value.items()}
        return value

    assert (version, bits, struct_count, enum_count) == (saved["version"], saved["size"], 26, 13)
    root = resolve(structs["0"])
    root["members"] = [m for m in root["members"] if m[0] in {"playerstatslist", "cg_subtitles"}]
    return dict(version=version, size=bits, structs={"0": root}, enums={"0": resolve(enums["0"])}, origin=origin)


def original_evidence(pe):
    # Independent instruction identities: cdecl path/output arguments; common
    # root; permission at member+44; exact training/dedicated/storage callees;
    # matching diagnostics; callers assembling PlayerStatsList/key-table paths.
    owners = [
        ("LiveStats_GetIntPlayerStatInternal", 0x1d8440, 180, "366407c1193eef3261eae2ce9104b8e22b5ae9b30b9ee2fbb34a6ad7c39509f2"),
        ("LiveStats_SetIntPlayerStatInternal", 0x18dcd0, 192, "87cf131a586482460ab5c3a83b7c979f84ff4c53441380c49f17213f078a0035"),
    ]
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32
    dis = Cs(CS_ARCH_X86, CS_MODE_32)
    records = []
    for name, rva, size, expected in owners:
        raw = pe.get_data(rva, size)
        assert hashlib.sha256(raw).hexdigest() == expected, name
        instructions = list(dis.disasm(raw, rva + 0x400000))
        assert sum(i.size for i in instructions) == size
        calls = [int(i.op_str, 16) - 0x400000 for i in instructions if i.mnemonic == "call"]
        assert {0xe4030, 0x224190, 0x1b1310, 0x4da40, 0x227b50} <= set(calls)
        assert any(i.mnemonic == "cmp" and "+ 0x2c], 2" in i.op_str for i in instructions)
        records.append(dict(name=name, rva=hex(rva), size=size, sha256=expected, calls=[hex(c) for c in calls]))
    assert pe.get_data(0x1d84a8, 11).hex() == "6a016a066a00e88d55e7ff"  # controller 0, bank 6, verify true
    assert pe.get_data(0x18dcd0, 22).hex() == "a188a0830383ec1085c0740e8b40188038007406" + "32c0"
    assert pe.get_data(0x626778, 16).split(b"\0")[0] == b"PlayerStatsList"
    return records


def harness(schema, live_source):
    source = r'''
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cstdarg>
#include <vector>
#include <live/live_stats.h>
#include <live/live_storage.h>
#include <ddl/ddl_api.h>
#include <ddl/ddl_buffer.h>
#include <ddl/ddl_converter.h>
#include <universal/dvar.h>
static ddlState_t root{},cac{},g_statsRootState{}; static ddlDef_t ddl{};
static persistentStats normal{},global{},snapshot{};
static dvar_t training{},dedicatedSetting{},modDir{};
const dvar_t *xblive_basictraining=&training,*dedicated=&dedicatedSetting,*fs_gameDirVar=&modDir;
static int storageCalls=0,failures=0,checks=0,diagnostics=0;
static bool signedIn=true,demonware=true,fetched=true,validated=true,validateResult=true;
static int validationCalls=0;
#define CHECK(x) do {++checks;if(!(x)){++failures;fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);}}while(0)
bool Assert_MyHandler(const char*file,int line,int,const char*format,...){fprintf(stderr,"BLOCKED assertion %s:%d\n",file,line);std::abort();}
void DDL_PrintError(const char*,...){++diagnostics;}
void DDL_NoCheckPrintWarning(const char*,...){std::abort();}
void Com_PrintError(int,const char*,...){++diagnostics;}
void I_strncpyz(char*d,const char*s,int n){snprintf(d,n,"%s",s);}
int I_strcmp(const char*a,const char*b){return strcmp(a,b);}
char *I_strlwr(char*s){for(char*p=s;*p;++p)*p=(char)tolower((unsigned char)*p);return s;}
int I_stricmp(const char*a,const char*b){return _stricmp(a,b);}
bool Live_IsUserSignedInToLive(){return signedIn;}
bool Live_IsUserSignedInToDemonware(int c){assert(c==0);return demonware;}
unsigned __int8 LiveStorage_DoWeHaveCurrentStats(int c){assert(c==0);return fetched;}
unsigned __int8 LiveStorage_AreStatsDDLValidated(int c,statsLocation l){assert(c==0 && l==STATS_LOCATION_NORMAL);return validated;}
int LiveStorage_ValidateWithDDL(int c,statsLocation l){assert(c==0 && l==STATS_LOCATION_NORMAL);++validationCalls;return validateResult;}
persistentStats *LiveStorage_GetStatsBuffer(int c,statsLocation location,bool verify){
 assert(c==0 && verify);++storageCalls;
 assert(location==STATS_LOCATION_NORMAL || location==STATS_LOCATION_GLOBAL);
 return location==STATS_LOCATION_GLOBAL?&global:&normal;
}
'''
    defs = [
        ("src/ddl/ddl_lookup.cpp", ["DDL_Lookup_FindMemberDef", "DDL_Lookup_MoveTo", "DDL_Lookup_MoveToIndex", "DDL_Lookup_ClearState"]),
        ("src/ddl/ddl_converter.cpp", ["DDL_Converter_IsLeaf"]),
        ("src/ddl/ddl_buffer.cpp", ["DDL_Buffer_ReadHeader", "DDL_Buffer_WriteHeader", "DDL_Buffer_ReadBits", "DDL_Buffer_WriteBits"]),
        ("src/ddl/ddl_api.cpp", ["DDL_Reset", "DDL_MoveTo", "DDL_MoveToPath", "DDL_MoveToName", "DDL_MoveToIndex", "DDL_SetUserFlag", "DDL_GetUserFlag", "DDL_SetValueChanged", "DDL_IsStateReadyForIO", "DDL_WriteIntValueIfChanged", "DDL_WriteIntValueInternal", "DDL_ReadIntValueInternal", "DDL_ReadInt64ValueInternal", "DDL_ReadStringValueInternal", "DDL_ReadValueInternal", "DDL_GetValue", "DDL_SetInt", "DDL_GetInt"]),
        (live_source, ["LiveStats_CanPerformStatOperation", "LiveStats_GetIntPlayerStatInternal", "LiveStats_SetIntPlayerStatInternal", "LiveStats_GetIntPlayerStatFromBase", "LiveStats_GetIntPlayerStatFromForcedBase", "LiveStats_GetIntPlayerStat", "LiveStats_SetIntPlayerStat", "LiveStats_SetPlayerStat", "LiveStats_SetBasicTrainingState", "LiveStats_GetBasicTrainingState"]),
    ]
    source += "unsigned __int8 stringResult[68];\n"
    for path, symbols in defs:
        for name in symbols:
            source += "\n" + definition(ROOT / path, name) + "\n"
    # These production headers instantiate network tables even in this isolated
    # fixture; retain real table definitions rather than fake serialization.
    fields = ROOT / "src/qcommon/msg_mp.cpp"
    for name in re.findall(r"^const NetField (\w+)\[", fields.read_text(), re.M):
        source += definition(fields, name, data=True)
    source += schema_cpp(schema).replace('assert(DDL_MoveTo(&root,&cac,1,"cacloadouts"));', "")
    source += r'''
ddlState_t state(const char*name){ddlState_t s;const char*p[]={"PlayerStatsList",name};assert(DDL_MoveToPath(&root,&s,2,p));return s;}
unsigned direct(const char*name,persistentStats&b){auto s=state(name);return DDL_GetInt(&s,(char*)&b);}
void seed(const char*name,persistentStats&b,unsigned value){auto s=state(name);assert(DDL_SetInt(&s,value,(char*)&b));}
int main(){
 static_assert(sizeof(ddlMemberDef_t)==48 && sizeof(ddlState_t)==16 && sizeof(persistentStats)==0xA005);
 initSchema();g_statsRootState=root;modDir.current.string="";
 const char*names[]={"RANKXP","CODPOINTS","CURRENCYSPENT","PLEVEL","STATS_VERSION"};
 for(const char*name:names){
  auto s=state(name);printf("stock %s permission=%d offset=%d bits=%d\n",name,s.member->permission,s.absoluteOffset,s.member->size/s.member->arraySize);
  for(int dvarTraining=0;dvarTraining<2;++dvarTraining)for(int bufferTraining=0;bufferTraining<2;++bufferTraining)for(int server=0;server<2;++server){
   memset(&normal,0,sizeof(normal));memset(&global,0,sizeof(global));memset(&snapshot,0,sizeof(snapshot));
   training.current.enabled=dvarTraining!=0;dedicatedSetting.current.integer=server;
   seed(name,normal,11);seed(name,global,23);seed(name,snapshot,37);
   LiveStats_SetBasicTrainingState(normal.statsBuffer,bufferTraining!=0);
   bool route=s.member->permission==2 && !dvarTraining && !bufferTraining && !server;
   auto expectedNormal=normal,expectedGlobal=global;storageCalls=0;int out=-1;
   CHECK(LiveStats_GetIntPlayerStatFromBase(&out,name,(char*)&normal));CHECK(out==(route?23:11));CHECK(storageCalls==int(route));
   CHECK(!memcmp(&normal,&expectedNormal,sizeof(normal)) && !memcmp(&global,&expectedGlobal,sizeof(global)));
   seed(name,route?expectedGlobal:expectedNormal,0);
   storageCalls=0;CHECK(LiveStats_SetIntPlayerStat(0,name,0));
   CHECK(direct(name,normal)==unsigned(route?11:0));CHECK(direct(name,global)==unsigned(route?0:23));CHECK(storageCalls==1+int(route));
   CHECK(!memcmp(&normal,&expectedNormal,sizeof(normal)) && !memcmp(&global,&expectedGlobal,sizeof(global)));
   // Explicit-base snapshot IO must stay on the supplied bank, even when its
   // stock permission would redirect a normal player-stat operation globally.
   storageCalls=0;out=-1;CHECK(LiveStats_GetIntPlayerStatFromForcedBase(&out,name,(char*)&snapshot));CHECK(out==37);
   auto expectedSnapshot=snapshot;seed(name,expectedSnapshot,0);
   LiveStats_SetPlayerStat("PlayerStatsList",name,(char*)&snapshot,0);CHECK(direct(name,snapshot)==0);CHECK(storageCalls==0);
   CHECK(!memcmp(&snapshot,&expectedSnapshot,sizeof(snapshot)));
  }
 }
 // A genuine stock permission-3 member must stay in the supplied bank.
 const char*localPath[]={"cg_subtitles"};ddlState_t local;assert(DDL_MoveToPath(&root,&local,1,localPath));assert(local.member->permission==3);
 for(int server=0;server<2;++server){
  training.current.enabled=false;dedicatedSetting.current.integer=server;LiveStats_SetBasicTrainingState(normal.statsBuffer,false);
  assert(DDL_SetInt(&local,1,(char*)&normal));assert(DDL_SetInt(&local,0,(char*)&global));int out=-1;storageCalls=0;
  CHECK(LiveStats_GetIntPlayerStatInternal(&out,1,localPath,(char*)&normal));CHECK(out==1);CHECK(storageCalls==0);
  CHECK(LiveStats_SetIntPlayerStatInternal(0,1,localPath,(char*)&normal,0));CHECK(DDL_GetInt(&local,(char*)&normal)==0);CHECK(storageCalls==0);
 }
 training.current.enabled=false;dedicatedSetting.current.integer=0;LiveStats_SetBasicTrainingState(normal.statsBuffer,false);
 // Active mods reject writes before path lookup/bank selection, preserving both
 // DDL buffers, their dirty bits, and all metadata. A null mod dvar is allowed.
 seed("CODPOINTS",normal,10);seed("CODPOINTS",global,20);auto n=normal,g=global;
 modDir.current.string="mods/fixture";const char*path[]={"PlayerStatsList","CODPOINTS"};storageCalls=0;
 CHECK(!LiveStats_SetIntPlayerStatInternal(0,2,path,(char*)&normal,0));CHECK(storageCalls==0);
 CHECK(!memcmp(&n,&normal,sizeof(n)) && !memcmp(&g,&global,sizeof(g)));
 fs_gameDirVar=nullptr;CHECK(LiveStats_SetIntPlayerStatInternal(0,2,path,(char*)&normal,7));CHECK(direct("CODPOINTS",global)==7);fs_gameDirVar=&modDir;modDir.current.string="";
 // Existing native service/fetch/DDL gates remain closed when their respective
 // prerequisites are absent. This test never creates a service session.
 CHECK(LiveStats_CanPerformStatOperation(0));signedIn=false;CHECK(!LiveStats_CanPerformStatOperation(0));signedIn=true;
 demonware=false;CHECK(!LiveStats_CanPerformStatOperation(0));demonware=true;
 fetched=false;CHECK(!LiveStats_CanPerformStatOperation(0));fetched=true;
 validated=false;validateResult=false;CHECK(!LiveStats_CanPerformStatOperation(0));CHECK(validationCalls==1);
 validateResult=true;CHECK(LiveStats_CanPerformStatOperation(0));CHECK(validationCalls==2);
 printf("checks=%d failures=%d\n",checks,failures);return failures?1:0;
}
'''
    return source, defs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vcvars", type=Path, default=Path("C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars32.bat"))
    parser.add_argument("--out", type=Path, default=ROOT / "build-re/stats-bank-routing")
    parser.add_argument("--live-source", type=Path, default=Path("src/live/live_stats.cpp"))
    args = parser.parse_args()
    original, pe = reference()
    evidence = original_evidence(pe)
    schema = stock_schema()
    args.out.mkdir(parents=True, exist_ok=True)
    source, defs = harness(schema, args.live_source)
    cpp, exe = args.out / "routing.cpp", args.out / "routing.exe"
    cpp.write_text(source)
    cmd = args.out / "build.cmd"
    cmd.write_text(f'@echo off\ncall "{args.vcvars}" -vcvars_ver=14.37 >nul\nif errorlevel 1 exit /b %errorlevel%\nwhere cl >"{args.out / "compiler-path.txt"}"\nwhere link >"{args.out / "linker-path.txt"}"\ncl /nologo /std:c++20 /EHsc /O2 /fp:precise /MT /DKISAK_MP /I. /Isrc /Isrc/libs /Isrc/universal "{cpp}" /Fe"{exe}" /Fo"{args.out / "routing.obj"}" >"{args.out / "build.log"}" 2>&1\nexit /b %errorlevel%\n')
    result = subprocess.run(["cmd", "/d", "/c", str(cmd)], cwd=ROOT)
    if result.returncode:
        raise RuntimeError((args.out / "build.log").read_text())
    result = subprocess.run([str(exe)], cwd=ROOT, capture_output=True, text=True)
    print(result.stdout, end="")
    print(result.stderr, end="")
    report = dict(status="pass" if result.returncode == 0 else "mismatch", original_sha256=hashlib.sha256(original.read_bytes()).hexdigest(),
                  original_owners=evidence, instruction_reviewed_bytes=sum(r["size"] for r in evidence),
                  native_original_replay="blocked: complete lookup/storage/service closure not qualified; no dependency outputs are fabricated",
                  behavioral_scope="Extracted production owners and real DDL lookup/integer IO, pinned stock PC stat schema projection; controlled storage/service context; excludes live awards, persistence and gameplay",
                  stock_schema=schema["origin"], output=result.stdout, errors=result.stderr,
                  source_sha256={str(p): hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p, _ in defs},
                  runner_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                  toolchain={name: dict(path=(p := (args.out / (name + "-path.txt")).read_text().splitlines()[0]), sha256=hashlib.sha256(Path(p).read_bytes()).hexdigest()) for name in ("compiler", "linker")},
                  flags="/std:c++20 /EHsc /O2 /fp:precise /MT /DKISAK_MP; assertions enabled")
    (args.out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    raise SystemExit(result.returncode)


if __name__ == "__main__":
    main()

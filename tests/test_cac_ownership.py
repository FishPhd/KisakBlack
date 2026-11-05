"""Production CAC ownership/DDL integration using hash-pinned stock metadata.

Run with Python from the repository root. VS2022 x86 tools are located through
--vcvars. No game/profile is launched or touched. Original instruction evidence
is checked separately; controlled fixture bindings are not original execution.
See replay_cac_ddl.py for immutable original-native/Unicorn integer IO captures.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import subprocess
import struct
import sys
import zlib

ROOT = Path(__file__).resolve().parents[1]
from mp_fidelity_common import definition


def selected(path, symbols):
    return "\n".join(definition(ROOT / path, symbol) for symbol in symbols)


def verify_reference(reference):
    """Check immutable metadata against local original assets when available."""
    import pefile
    binary = ROOT / "build/BlackOpsMP.exe"
    if not binary.exists():
        raise RuntimeError("BLOCKED: original executable is required to check identities")
    assert hashlib.sha256(binary.read_bytes()).hexdigest() == reference["reference_sha256"]
    pe = pefile.PE(str(binary))
    for owner in reference["functions"]:
        for code in owner["code"]:
            assert hashlib.sha256(pe.get_data(code["rva"], code["size"])).hexdigest() == code["sha256"], owner["name"]
    origin = reference["stock_schema"]["origin"]
    raw = (ROOT / origin["fastfile"]).read_bytes()
    assert hashlib.sha256(raw).hexdigest() == origin["fastfile_sha256"]
    inflated = zlib.decompress(raw[12:])
    assert hashlib.sha256(inflated).hexdigest() == origin["inflated_sha256"]
    begin = origin["serialized_rva"]
    assert hashlib.sha256(inflated[begin:begin + origin["serialized_size"]]).hexdigest() == origin["serialized_sha256"]
    # Decode this pinned PC asset's inline DDL records, then verify the retained
    # projection. The virtual stream aligns independently of the packed file.
    pos, virtual, strings = begin, 0, {}
    def words(count):
        nonlocal pos, virtual
        values = list(struct.unpack_from("<" + "i" * count, inflated, pos))
        pos += count * 4
        virtual += count * 4
        return values
    def align():
        nonlocal virtual
        virtual = (virtual + 3) & ~3
    def string(pointer):
        nonlocal pos, virtual
        if pointer != -1:
            return ("packed", pointer & 0xffffffff)
        end = inflated.index(0, pos)
        value = inflated[pos:end].decode()
        strings[virtual] = value
        virtual += end + 1 - pos
        pos = end + 1
        return value
    version, bits, _, struct_count, _, enum_count, _ = words(7)
    align()
    headers = [words(4) for _ in range(struct_count)]
    structs = []
    for name, size, count, _ in headers:
        name = string(name)
        align()
        records = [words(12) for _ in range(count)]
        structs.append(dict(name=name, size=size, members=[[string(m[0])] + m[1:] for m in records]))
    align()
    headers = [words(3) for _ in range(enum_count)]
    enums = []
    for name, count, _ in headers:
        name = string(name)
        align()
        records = words(count)
        enums.append(dict(name=name, members=[string(p) for p in records]))
    assert pos == begin + origin["serialized_size"]
    # This asset reuses its purchasedattachments string at virtual stream offset
    # 0x151054. Its packed-pointer binding is specific to the hash above.
    delta = 0x151054 - next(k for k, v in strings.items() if v == "purchasedattachments")
    def resolve(value):
        if isinstance(value, tuple):
            return strings[((value[1] - 1) & 0x1fffffff) - delta + 1]
        if isinstance(value, list):
            return [resolve(v) for v in value]
        if isinstance(value, dict):
            return {k: resolve(v) for k, v in value.items()}
        return value
    schema = reference["stock_schema"]
    assert (version, bits) == (schema["version"], schema["size"])
    for index, expected in schema["structs"].items():
        actual = resolve(structs[int(index)])
        if index == "0":
            actual["members"] = [m for m in actual["members"] if m[0] in {e[0] for e in expected["members"]}]
        assert actual == expected, ("stock struct", index)
    for index, expected in schema["enums"].items():
        assert resolve(enums[int(index)]) == expected, ("stock enum", index)


def schema_cpp(schema):
    code = []
    for index, record in schema["structs"].items():
        members = []
        for member in record["members"]:
            # Unsigned schema limits retain the original 32-bit words.
            values = [json.dumps(member[0])] + [str(v) if n not in (5, 6, 7, 8) else f"{v & 0xffffffff}u" for n, v in enumerate(member[1:], 1)]
            members.append("{" + ",".join(values) + "}")
        code.append(f"ddlMemberDef_t members{index}[]={{" + ",\n".join(members) + "};")
    code.append("ddlStructDef_t structures[17]{}; ddlEnumDef_t enums[11]{};")
    for index, record in schema["enums"].items():
        code.append(f"const char *enumNames{index}[]={{" + ",".join(json.dumps(x) for x in record["members"]) + "};")
    code.append("void initSchema(){")
    for index, record in schema["structs"].items():
        code.append(f"structures[{index}]={{{json.dumps(record['name'])},{record['size']},{len(record['members'])},members{index}}};")
    for index, record in schema["enums"].items():
        code.append(f"enums[{index}]={{{json.dumps(record['name'])},{len(record['members'])},enumNames{index}}};")
    code.append(f"ddl={{{schema['version']},{schema['size']},structures,17,enums,11,nullptr}}; DDL_Reset(&root,&ddl);")
    code.append('assert(DDL_MoveTo(&root,&cac,1,"cacloadouts"));}')
    return "\n".join(code)


def build_harness(reference):
    source = r'''
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cstdarg>
#include <vector>
#include <bgame/bg_unlockable_items.h>
#include <ddl/ddl_api.h>
#include <ddl/ddl_buffer.h>
#include <ddl/ddl_converter.h>
#include <live/live_storage.h>
#include <universal/dvar.h>

static ddlState_t root{},cac{};static ddlDef_t ddl{};
static persistentStats stats{};
static int diagnostics=0;
unlockableItemsData s_unlockableItems{};
static bool fixtureFreeItems=false;
static int fixtureUnlockLevel=10;
static dvar_t customInfo{};
const dvar_t *ui_useCustomClassInfo=&customInfo;
const dvar_t *bodyTypeFromGun=nullptr;
bool Assert_MyHandler(const char*file,int line,int,const char*format,...){fprintf(stderr,"BLOCKED assertion %s:%d: ",file,line);va_list ap;va_start(ap,format);vfprintf(stderr,format,ap);va_end(ap);fflush(stderr);std::abort();}
void Com_Printf(int,const char*,...){++diagnostics;}
void Com_PrintError(int,const char*format,...){va_list ap;va_start(ap,format);vfprintf(stderr,format,ap);va_end(ap);fflush(stderr);std::abort();}
void DDL_PrintError(const char*format,...){va_list ap;va_start(ap,format);vfprintf(stderr,format,ap);va_end(ap);fflush(stderr);std::abort();}
void DDL_NoCheckPrintWarning(const char*format,...){va_list ap;va_start(ap,format);vfprintf(stderr,format,ap);va_end(ap);fflush(stderr);std::abort();}
char *va(const char *format,...){static char data[16][256];static int i=0;char*p=data[i++&15];va_list ap;va_start(ap,format);vsnprintf(p,256,format,ap);va_end(ap);return p;}
void I_strncpyz(char*d,const char*s,int n){snprintf(d,n,"%s",s);}
int I_strcmp(const char*a,const char*b){return strcmp(a,b);}
char *I_strlwr(char*s){for(char*p=s;*p;++p)*p=(char)tolower((unsigned char)*p);return s;}
int I_stricmp(const char*a,const char*b){return _stricmp(a,b);}
int I_strnicmp(const char*a,const char*b,int n){return _strnicmp(a,b,n);}

// Private context bindings: every unsupported game/service path terminates.
// These provide inputs to production class/DDL code, not an original oracle.
const ddlState_t *LiveStats_GetRootDDLState(){return &root;}
ddlState_t *LiveStats_GetCacDDLState(){return &cac;}
persistentStats *LiveStorage_GetStatsBuffer(int c,statsLocation location,bool){assert(c==0);assert(location==STATS_LOCATION_NORMAL||location==STATS_LOCATION_GLOBAL);return &stats;}
char BG_UnlockablesAllItemsFree(){return fixtureFreeItems;}
bool BG_UnlockablesIsItemLockedForRank(int,int,int){std::abort();}
int BG_GetWeaponOptionCount(){return 72;}
int BG_GetWeaponOptionUnlockLvl(int){return fixtureUnlockLevel;}
int BG_GetWeaponOptionCost(int){return 100;}
int Dvar_GetInt(const dvar_t*d){assert(d==&customInfo);return d->current.integer;}
unsigned char GetCustomClassLoadoutItemForClassName(int,const char*,const char*){std::abort();}
void UI_Gametype_EquipCustomClassLoadoutItem(int,const char*,int,const char*){std::abort();}
'''
    source += definition(ROOT / "src/bgame/bg_unlockable_items.cpp", "s_itemGroupNames", data=True) + "\n"
    source += definition(ROOT / "src/bgame/bg_weapons_attachment.cpp", "s_attachmentPointNames", data=True) + "\n"
    defs = [
        ("src/ddl/ddl_lookup.cpp", ["DDL_Lookup_FindMemberDef", "DDL_Lookup_MoveTo", "DDL_Lookup_ClearState"]),
        ("src/ddl/ddl_converter.cpp", ["DDL_Converter_IsLeaf"]),
        ("src/ddl/ddl_buffer.cpp", ["DDL_Buffer_ReadHeader", "DDL_Buffer_WriteHeader", "DDL_Buffer_ReadBits", "DDL_Buffer_WriteBits"]),
        ("src/ddl/ddl_api.cpp", ["DDL_Reset", "DDL_MoveTo", "DDL_SetUserFlag", "DDL_SetValueChanged", "DDL_IsStateReadyForIO", "DDL_WriteIntValueIfChanged", "DDL_WriteIntValueInternal", "DDL_ReadIntValueInternal", "DDL_SetInt", "DDL_GetInt"]),
        ("src/live/live_stats.cpp", ["LiveStats_MoveToProItemVersion"]),
        ("src/bgame/bg_unlockable_items.cpp", ["BG_UnlockablesGetItemInfo", "BG_UnlockablesSetItemPurchased", "BG_UnlockablesIsItemPurchasedFromBuffer", "BG_UnlockablesSetItemAttachmentPurchased", "BG_UnlockablesSetItemOptionPurchased", "BG_UnlockablesSetProItemUnlocked", "BG_UnlockablesClearWeaponInfo", "SV_CacValidate_IsItemPurchased", "SV_CACValidate_SetItemPurchased", "SV_CACValidate_SetItemAttachmentPurchased", "SV_CACValidate_SetItemOptionPurchased", "SV_CACValidate_SetProItemUnlocked", "SV_CACValidate_ClearWeaponInfo", "SV_CACValidate_IsItemOptionPurchased"]),
    ]
    for path, symbols in defs:
        source += "\n" + selected(path, symbols) + "\n"
    source += "\n" + selected("src/bgame/bg_weapons_attachment.cpp", ["BG_GetAttachmentPointName"]) + "\n"
    source += selected("src/bgame/bg_unlockable_items.cpp", [
        "BG_UnlockablesGetItemGroupName", "BG_UnlockablesIsItemValidNotNull",
        "BG_UnlockablesGetAssociatedBodyWithPrimary", "BG_UnlockablesGetAssociatedBodyWithPerk",
        "BG_UnlockablesGetAssociatedBody", "BG_UnlockablesEquipAssociatedBody",
        "BG_UnlockablesEquipClassSpecialPropertiesToSlot", "BG_UnlockablesEquipClassToSlot",
        "BG_UnlockablesGetEquippedItemInSlot",
    ]) + "\n"
    # Production headers instantiate network descriptor tables even though this
    # fixture never serializes network state; retain the actual definitions.
    fields = ROOT / "src/qcommon/msg_mp.cpp"
    source += "\n" + "\n".join(
        definition(fields, name, data=True)
        for name in re.findall(r'^const NetField (\w+)\[', fields.read_text(), re.M)
    ) + "\n"
    source += schema_cpp(reference["stock_schema"])
    source += r'''
ddlState_t lookup(const char*parent,int item,const char*leaf,int index=-1){
 ddlState_t s; char itemName[12],indexName[12];snprintf(itemName,12,"%d",item);snprintf(indexName,12,"%d",index);
 assert(index<0?DDL_MoveTo(&root,&s,3,parent,itemName,leaf):DDL_MoveTo(&root,&s,4,parent,itemName,leaf,indexName));return s;
}
void write(const char*p,int item,const char*leaf,unsigned v,int index=-1){auto s=lookup(p,item,leaf,index);assert(DDL_SetInt(&s,v,(char*)&stats));}
unsigned read(const char*p,int item,const char*leaf,int index=-1){auto s=lookup(p,item,leaf,index);return DDL_GetInt(&s,(char*)&stats);}
void ownAll(int item){
 write("itemstats",item,"purchased",1);for(int i=0;i<3;++i)write("itemstats",item,"isproversionunlocked",1,i);
 for(int i=0;i<32;++i)write("weaponstats",item,"purchasedattachments",1,i);
 for(int i=0;i<72;++i)write("weaponstats",item,"purchasedoptions",1,i);
}
void cleared(int item){
 assert(read("itemstats",item,"purchased")==0);for(int i=0;i<3;++i)assert(read("itemstats",item,"isproversionunlocked",i)==0);
 for(int i=0;i<32;++i)assert(read("weaponstats",item,"purchasedattachments",i)==(i>=24));
 for(int i=0;i<72;++i)assert(read("weaponstats",item,"purchasedoptions",i)==0);
}
int main(){
 static_assert(sizeof(ddlMemberDef_t)==48 && sizeof(ddlState_t)==16 && sizeof(itemInfo_t)==0x128);
 initSchema();
 for(int item: {0,1,63,255}){auto &info=s_unlockableItems.itemTable[item];info.isValid=true;info.cost=100;info.name="fixture";
  assert(BG_UnlockablesSetItemPurchased(0,item,true));assert(BG_UnlockablesIsItemPurchasedFromBuffer(stats.statsBuffer,item));
  assert(SV_CacValidate_IsItemPurchased(stats.statsBuffer,item,-1,-1));
  write("itemstats",item,"new",1);assert(SV_CACValidate_SetItemPurchased((char*)&stats,item,false));assert(read("itemstats",item,"new")==1);
  assert(!BG_UnlockablesIsItemPurchasedFromBuffer(stats.statsBuffer,item));assert(read("itemstats",item,"purchased")==0);
 }
 for(int item: {0,1,63}){
  for(int a: {0,1,23,31}){assert(BG_UnlockablesSetItemAttachmentPurchased(0,item,a,true));assert(read("weaponstats",item,"purchasedattachments",a)==1);
   assert(SV_CACValidate_SetItemAttachmentPurchased((char*)&stats,item,a,false));assert(read("weaponstats",item,"purchasedattachments",a)==0);}
  for(int a: {0,1,31,32,63,71}){assert(BG_UnlockablesSetItemOptionPurchased(0,item,a,true));assert(read("weaponstats",item,"purchasedoptions",a)==1);
   assert(SV_CACValidate_SetItemOptionPurchased((char*)&stats,item,a,false));assert(read("weaponstats",item,"purchasedoptions",a)==0);}
  ownAll(item);BG_UnlockablesClearWeaponInfo(0,item);cleared(item);
  ownAll(item);SV_CACValidate_ClearWeaponInfo((char*)&stats,item);cleared(item);
 }
 // Original six-argument validation: checkWeapon only skips base ownership.
 write("weaponstats",1,"purchasedoptions",1,63);
 assert(!SV_CACValidate_IsItemOptionPurchased(stats.statsBuffer,1,63,0,10,true));
 assert(SV_CACValidate_IsItemOptionPurchased(stats.statsBuffer,1,63,0,10,false));
 assert(!SV_CACValidate_IsItemOptionPurchased(stats.statsBuffer,1,63,0,9,false));
 assert(read("weaponstats",1,"purchasedoptions",63)==0 && diagnostics==1);
 assert(SV_CACValidate_SetItemPurchased((char*)&stats,1,true));write("weaponstats",1,"purchasedoptions",1,63);
 assert(SV_CACValidate_IsItemOptionPurchased(stats.statsBuffer,1,63,0,10,true));
 assert(SV_CACValidate_IsItemOptionPurchased(stats.statsBuffer,1,0,0,-1,true));
 // The actual player class path writes stock slot IDs and resets weapon-specific
 // options when the primary changes. Custom-game settings paths terminate.
 for(int i=0;i<10;++i){auto &info=s_unlockableItems.itemTable[i];info.isValid=true;info.cost=100;info.name="fixture";}
 BG_UnlockablesEquipClassToSlot(0,"customclass1",1,"primary");
 BG_UnlockablesEquipClassToSlot(0,"customclass1",2,"primaryattachmenttop");
 BG_UnlockablesEquipClassToSlot(0,"customclass1",3,"primarycamo");
 BG_UnlockablesEquipClassToSlot(0,"customclass1",63,"primary");
 assert(BG_UnlockablesGetEquippedItemInSlot(0,"customclass1","primary")==63);
 assert(BG_UnlockablesGetEquippedItemInSlot(0,"customclass1","primaryattachmenttop")==0);
 assert(BG_UnlockablesGetEquippedItemInSlot(0,"customclass1","primarycamo")==0);
 const char* slots[]={"primaryattachmenttop","primarycamo","secondary","primarygrenade","specialgrenade","equipment","specialty1","specialty2","specialty3"};
 for(int i=0;i<9;++i) BG_UnlockablesEquipClassToSlot(0,"customclass1",i+1,slots[i]);
 // Save/read is an isolated byte round-trip, not a successful service upload.
 auto before=stats;std::vector<unsigned char> serialized(stats.statsBuffer,stats.statsBuffer+sizeof(stats.statsBuffer));memset(stats.statsBuffer,0,sizeof(stats.statsBuffer));
 memcpy(stats.statsBuffer,serialized.data(),serialized.size());assert(!memcmp(stats.statsBuffer,before.statsBuffer,sizeof(stats.statsBuffer)));
 assert(read("weaponstats",1,"purchasedoptions",63)==1);
 assert(BG_UnlockablesGetEquippedItemInSlot(0,"customclass1","primary")==63);
 for(int i=0;i<9;++i) assert(BG_UnlockablesGetEquippedItemInSlot(0,"customclass1",slots[i])==unsigned(i+1));
 puts("PASS: production ownership setters/readers; indexed stock schema;72-option reset; six-argument validation/rank clearing; class equipment and weapon-change resets; isolated serialization roundtrip. Original gameplay/service qualification excluded.");
}
'''
    return source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vcvars", type=Path, default=Path("C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars32.bat"))
    parser.add_argument("--out", type=Path, default=ROOT / "build-re/cac-ownership")
    args = parser.parse_args()
    reference = json.loads((ROOT / "tests/cac_ownership_reference.json").read_text())
    verify_reference(reference)
    # All corrected callers must supply their declared varargs depth.
    text = (ROOT / "src/bgame/bg_unlockable_items.cpp").read_text()
    for call in re.finditer(r'DDL_MoveTo\([^\n]+', text):
        depth = re.search(r'&searchState, ([0-9]+),', call.group())
        if depth:
            assert len(call.group().split(',')) >= int(depth.group(1)) + 3, call.group()
    args.out.mkdir(parents=True, exist_ok=True)
    cpp = args.out / "ownership.cpp"
    cpp.write_text(build_harness(reference))
    exe = args.out / "ownership.exe"
    cmd = args.out / "build.cmd"
    cmd.write_text(f'@echo off\ncall "{args.vcvars}" -vcvars_ver=14.37 >nul\nif errorlevel 1 exit /b %errorlevel%\ncl /nologo /std:c++20 /EHsc /O2 /fp:precise /DKISAK_MP /I. /Isrc /Isrc/libs /Isrc/universal "{cpp}" /Fe"{exe}" /Fo"{args.out / "ownership.obj"}" >"{args.out / "build.log"}" 2>&1\nexit /b %errorlevel%\n')
    result = subprocess.run(["cmd", "/d", "/c", str(cmd)], cwd=ROOT)
    if result.returncode:
        raise RuntimeError((args.out / "build.log").read_text())
    subprocess.run([str(exe)], check=True, cwd=ROOT)


if __name__ == "__main__":
    main()

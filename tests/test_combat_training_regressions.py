"""Compile production definitions with controlled dependency inputs.

Run in a VS x86 developer shell. These checks cover stats storage boundaries,
64-bit schema conversion, and vehicle-tag sentinels/vectors. They do not replace
original-executable replay or a sustained in-game match.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def definition(path, signature):
    text = (ROOT / path).read_text(encoding="utf-8")
    start = text.index(signature)
    return text[start:text.index("\n}", start) + 2]


def structure(name):
    text = (ROOT / "src/live/live_storage.h").read_text(encoding="utf-8")
    start = text.index("struct " + name + " ")
    return text[start:text.index("\n};", start) + 3]


def main():
    harness = r'''
#include <cassert>
#include <cstring>
#include <cstddef>
#include <vector>
#include <array>
#include <ddl/ddl_api.h>
#include <live/stats_buffer.h>
using _BYTE = unsigned char;
struct playerFileOperations;
'''
    harness += structure("persistentStats") + "\n" + structure("playerNetworkData")
    harness += r'''
static unsigned __int64 saved64;
unsigned __int64 DDL_GetInt64(const ddlState_t *, char *buffer)
{ unsigned __int64 result; memcpy(&result, buffer, 8); return result; }
bool DDL_SetInt64(const ddlState_t *, unsigned __int64 value, char *buffer)
{ saved64=value; memcpy(buffer, &value, 8); return true; }
unsigned int DDL_GetInt(const ddlState_t *, char *) { assert(false); return 0; }
bool DDL_SetInt(const ddlState_t *, unsigned int, char *) { assert(false); return false; }
char *DDL_GetString(const ddlState_t *, char *) { assert(false); return nullptr; }
int DDL_SetString(const ddlState_t *, const char *, char *) { assert(false); return 0; }
void DDL_PrintError(const char *, ...) { assert(false); }
'''
    harness += definition("src/ddl/ddl_converter.cpp", "void __cdecl DDL_Converter_CopyStates(")
    harness += r'''
struct VehicleInput {
 short pitch{},roll{},barrelPitch{},yaw{},steerPitch{},steerYaw{},minigun_rotation{};
 short gunnerYaw[4]{},gunnerPitch[4]{};
 float barrelRecoil{},wheelHeight[6]{},nitrousWheelRotation[6]{},extra_wheel_rot_scale{};
 unsigned char tag_body=255,tag_turret=255,tag_barrel=255,tag_barrel_recoil=255,tag_minigun_spin=255;
 unsigned char tag_gunner_turret[4],tag_gunner_barrel[4],wheelBoneIndex[6],tag_extra_tank_wheels[4];
 VehicleInput() {
  memset(tag_gunner_turret,255,4);memset(tag_gunner_barrel,255,4);
  memset(wheelBoneIndex,255,6);memset(tag_extra_tank_wheels,255,4);
 }
};
struct cpose_t { VehicleInput vehicle; };
struct DObj {};
#define iassert(v) assert(v)
bool Assert_MyHandler(const char *,int,int,const char *,...) { assert(false); return false; }
const float vec3_origin[3]={};
float recoilVec[3]={-1,0,0};
struct TagCall { unsigned bone; std::array<float,3> position,angles; };
std::vector<TagCall> calls;
int DObjSetRotTransIndex(const DObj *,int *,unsigned bone) { assert(bone<254); return 1; }
void DObjSetLocalTagInternal(const DObj *,const float *p,const float *a,unsigned bone)
{ calls.push_back({bone,{p[0],p[1],p[2]},{a[0],a[1],a[2]}}); }
'''
    harness += definition("src/xanim/dobj_utils.cpp", "void __cdecl DObjSetLocalTag(")
    harness += definition("src/cgame_mp/cg_pose_mp.cpp", "void CG_Vehicle_DoControllers(")
    harness += r'''
int main() {
 static_assert(STATS_BUFFER_SIZE==40960);
 static_assert(MODIFIED_STATS_BYTE_SIZE==5120);
 static_assert(sizeof(persistentStats)==0xa005);
 static_assert(offsetof(persistentStats,isChecksumValid)==0xa000);
 static_assert(offsetof(persistentStats,statsValidatedWithDDL)==0xa002);
 static_assert(offsetof(persistentStats,statsFetched)==0xa003);
 static_assert(offsetof(playerNetworkData,basicTrainingStats)==0x1e00f);
 static_assert(offsetof(playerNetworkData,fileOps)==0x3c020);
 static_assert(sizeof(playerNetworkData)==0x3c024);
 struct Guarded { persistentStats data; unsigned char guard[16]; } stats{};
 memset(stats.guard,0x5a,sizeof(stats.guard));
 stats.data.isChecksumValid=true;stats.data.statsValidatedWithDDL=true;stats.data.statsFetched=true;
 memset(stats.data.statsBuffer,0xa5,STATS_BUFFER_SIZE);
 assert(stats.data.isChecksumValid && stats.data.statsValidatedWithDDL && stats.data.statsFetched);
 for(auto v:stats.guard) assert(v==0x5a);
 ddlMemberDef_t member{};member.type=3;ddlState_t state{};state.member=&member;
 for(unsigned __int64 value: {0ULL,0x100000000ULL,0x123456789abcdef0ULL,0xffffffffffffffffULL}) {
  char from[8],to[8]{};memcpy(from,&value,8);
  DDL_Converter_CopyStates(&state,from,&state,to);
  assert(saved64==value && !memcmp(from,to,8));
 }
 DObj obj;int bits[5]{};cpose_t pose;
 CG_Vehicle_DoControllers(&pose,&obj,bits);assert(calls.empty());
 pose.vehicle.tag_body=1;pose.vehicle.pitch=8192;pose.vehicle.roll=-8192;
 pose.vehicle.tag_turret=2;pose.vehicle.yaw=16384;
 pose.vehicle.tag_barrel=3;pose.vehicle.barrelPitch=4096;
 pose.vehicle.tag_barrel_recoil=4;pose.vehicle.barrelRecoil=3;
 pose.vehicle.tag_gunner_turret[0]=5;pose.vehicle.gunnerYaw[0]=-16384;
 pose.vehicle.tag_gunner_barrel[0]=6;pose.vehicle.gunnerPitch[0]=8192;
 pose.vehicle.tag_minigun_spin=7;pose.vehicle.minigun_rotation=16384;
 CG_Vehicle_DoControllers(&pose,&obj,bits);assert(calls.size()==7);
 assert((calls[0].angles==std::array<float,3>{45,0,-45}));
 assert((calls[1].angles==std::array<float,3>{0,90,0}));
 assert((calls[2].angles==std::array<float,3>{22.5,0,0}));
 assert((calls[3].position==std::array<float,3>{-3,0,0}));
 assert(calls[4].bone==5 && (calls[4].angles==std::array<float,3>{0,-90,0}));
 assert(calls[5].bone==6 && (calls[5].angles==std::array<float,3>{45,0,0}));
 assert(calls[6].bone==7 && (calls[6].angles==std::array<float,3>{0,0,90}));
}
'''
    with tempfile.TemporaryDirectory(prefix="kisak-training-") as work:
        directory = Path(work)
        cpp = directory / "regressions.cpp"
        exe = directory / "regressions.exe"
        cpp.write_text(harness, encoding="utf-8")
        result = subprocess.run(
            ["cl", "/nologo", "/std:c++20", "/EHsc", "/W4", "/fp:precise",
             f"/I{ROOT / 'src'}", str(cpp), f"/Fe{exe}"], cwd=directory,
            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if result.returncode:
            raise RuntimeError(result.stdout)
        subprocess.run([str(exe)], check=True)
    print("Stats layout, 64-bit conversion, and vehicle-tag regression checks passed")


if __name__ == "__main__":
    main()

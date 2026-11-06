"""Replay the complete original quick-scope update and its spread consumer.

Run with build-re/venv/Scripts/python.exe from the repository root. Requires
MSVC 14.37 x86 and the pinned original. Generated binaries, immutable captures,
and reports stay in ignored build-re/. This does not qualify the full PM_Weapon
dependency closure or live multiplayer. No original callee is stubbed.
"""
import hashlib
import json
import os
from pathlib import Path
import random
import re
import struct
import subprocess
import sys

from mp_fidelity_common import ROOT, reference, image_bytes, definition, exact_definition, read_source, _mask
from capstone import Cs, CS_ARCH_X86, CS_MODE_32, CS_OP_MEM
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
import unicorn.x86_const as x

os.chdir(ROOT)
out = ROOT / 'build-re/multiplayer/nov06/quickscope-update'
out.mkdir(parents=True, exist_ok=True)
_, pe = reference()
evidence = json.loads((ROOT / 'tests/quickscope_update_reference.json').read_text())
for region in evidence['regions']:
    assert hashlib.sha256(image_bytes(pe, int(region['rva'], 16), region['size'])).hexdigest() == region['sha256']
base = pe.OPTIONAL_HEADER.ImageBase
cs = Cs(CS_ARCH_X86, CS_MODE_32)
cs.detail = True
for callsite in evidence['ordered_caller_calls']:
    instruction = next(cs.disasm(image_bytes(pe, int(callsite['rva'], 16), 5), base + int(callsite['rva'], 16)))
    assert instruction.mnemonic == 'call' and instruction.operands[0].imm == base + int(callsite['target_rva'], 16)
dispatcher = definition(Path('src/bgame/bg_weapons.cpp'), 'PM_Weapon')
order = ['const float previousAdsFraction = ps->fWeaponPosFrac;',
         'PM_UpdateAimDownSightLerp(pm, pml);', 'PM_UpdateSpinLerp(pm, pml);',
         'PM_UpdateScopeZoom(pm);', 'PM_UpdateHoldBreath(pm, pml);',
         'PM_UpdateQuickScope(pm, pml, previousAdsFraction);']
positions = [dispatcher.index(item) for item in order]
assert positions == sorted(positions)
# Compare the complete source switch with the retail two-level table. These
# are independently inspected destination identities, not callee substitutes.
destinations = dict(zip((0x5cf4ec,0x5cf4dd,0x5cf45b,0x5cf44d,0x5cf46a,
    0x5cf493,0x5cf49f,0x5cf4ab,0x5cf4f7,0x5cf501,0x5cf517,0x5cf50d,
    0x5cf522,0x5cf52e,0x5cf553,0x5cf68f,0x5cf55f,0x5cf566,0x5cf5f4,
    0x5cf625,0x5cf4b6,0x5cf4c6,0x5cf64f,0x5cf65b,0x5cf5c2,0x5cf5ce),
    ('PM_Weapon_FinishWeaponRaise','PM_Weapon_FinishWeaponChange',
     'PM_Weapon_FinishReload','PM_Weapon_FinishReloadStart','PM_Weapon_FinishReloadEnd',
     'PM_Weapon_MeleeFire','PM_Weapon_MeleeEnd','PM_Weapon_Idle',
     'PM_Weapon_OffHandPrepare','PM_Weapon_OffHandHold','PM_Weapon_OffHand',
     'PM_Weapon_OffHandStart','PM_Weapon_OffHandEnd','PM_Detonate',
     'Sprint_State_Loop','return','ContinuousFire_State_Loop','shared_firing',
     'PM_Weapon_FinishNightVisionWear','PM_Weapon_FinishNightVisionRemove',
     'PM_AddEvent','PM_Weapon_Idle','PM_WeaponSwimIn','PM_WeaponSwimOut',
     'Dtp_State_Loop','Dtp_IsSliding')))
enum_values={name:int(value,0)for name,value in re.findall(
    r'^\s*(WEAPON_\w+)\s*=\s*(0x[\da-fA-F]+|\d+)',read_source('src/bgame/bg_weapons.h'),re.M)}
switch_start=dispatcher.index('{',dispatcher.index('switch ( ps->weaponstate )'))
masked=_mask(dispatcher);end=switch_start+1;depth=1
while depth:
    depth+=(masked[end]=='{')-(masked[end]=='}');end+=1
switch=masked[switch_start+1:end-1]
labels=list(re.finditer(r'\b(?:case\s+(\w+)|default)\s*:',switch))
pending=[];source_routes={}
for index,label in enumerate(labels):
    pending.append(enum_values[label[1]] if label[1] else 'default')
    block=switch[label.end():labels[index+1].start()if index+1<len(labels)else len(switch)].strip()
    if not block:continue
    if block.startswith('goto LABEL_106;'):action='PM_Weapon_FinishReload'
    elif block.startswith('goto LABEL_103;'):action='shared_firing'
    elif block.startswith('return;'):action='return'
    else:action=re.search(r'\b((?:PM_|Sprint_|ContinuousFire_|Dtp_)\w+)\s*\(',block)[1]
    for state in pending:source_routes[state]=action
    pending=[]
pointer_table=struct.unpack('<26I',image_bytes(pe,0x1cf694,104))
indices=image_bytes(pe,0x1cf6fc,47)
for state,index in enumerate(indices,1):
    assert source_routes.get(state,source_routes['default'])==destinations[pointer_table[index]],state
assert re.search(r'case\s+WEAPON_BREAKING_DOWN:\s*PM_Weapon_Idle\(ps\);\s*PM_AddEvent\(ps, 0x28u\);',dispatcher)
units = [(0x3bf320, 350), (0x2b94f0, 116), (0x129ff0, 15),
         (0x153510, 12), (0x1344a0, 44), (0x1603c0, 75), (0x3bd940, 103)]
constants = [(0x1344cc, 16), (0x691100, 4), (0x6100ac, 4),
             (0x6c2a60, 4), (0x6a1cf0, 16)]
globals_ = [(r, 4) for r in (0x851a8c, 0x84f8ac, 0x851adc, 0x84f874,
                              0x84f928, 0x8519e0, 0x886fe8, 0x886378, 0x886ff0, 0x88639c)]
globals_.append((0x880258, 8192))
fixups = []
instructions = {}
for r, n in units:
    for i in cs.disasm(image_bytes(pe, r, n), base + r):
        instructions[i.address] = i
        for operand in i.operands:
            if operand.type == CS_OP_MEM and operand.mem.disp >= base:
                assert i.disp_size == 4
                fixups.append((i.address - base + i.disp_offset, operand.mem.disp - base))
for offset in range(0, 16, 4):
    a = 0x1344cc + offset
    fixups.append((a, struct.unpack('<I', image_bytes(pe, a, 4))[0] - base))
regions = ','.join(f'{r}:{n}' for r, n in units + constants + globals_)
relocations = ','.join(f'{a}:{b}' for a, b in fixups)
selected = []
for path, names in [
    ('src/bgame/bg_weapons_def.cpp', ('BG_GetWeaponVariantDef', 'BG_GetWeaponDef')),
    ('src/bgame/bg_weapons.cpp', ('BG_IsUseAsMeleeWeapon', 'BG_GetViewmodelWeaponIndex',
                                 'PM_UpdateQuickScope', 'BG_GetQuickScopeSpread')),
    ('src/bgame/bg_weapons_load_obj.cpp', ('BG_GetPlayerAnimTypeIndex',)),
    ('src/universal/com_math.cpp', ('DiffTrack',)),
]:
    for name in names:
        selected.append((path, name, definition(Path(path), name)))
source = '\n'.join(b for _, _, b in selected)
source += exact_definition(Path('src/physics/phys_traverse.cpp'), 'colgeom_visitor_t::colgeom_visitor_t()')
source += definition(Path('src/universal/com_math.cpp'), 'Vec3Max')
source += exact_definition(Path('src/cgame_mp/cg_predict_mp.cpp'), 'pmove_t::pmove_t()')
fields = Path('src/qcommon/msg_mp.cpp')
source += '\n'.join(definition(fields, name, data=True) for name in
                    re.findall(r'^const NetField (\w+)\[', fields.read_text(), re.M))
cpp = r'''#include <windows.h>
#include <bgame/bg_local.h>
#include <bgame/bg_weapons.h>
#include <bgame/bg_weapons_def.h>
#include <bgame/bg_weapons_load_obj.h>
#include <bgame/bg_misc.h>
#include <cgame_mp/cg_predict_mp.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <vector>
#include <set>
#include <algorithm>
#include <io.h>
#include <fcntl.h>
#include <xmmintrin.h>
#include "tests/original_native_loader.h"
WeaponVariantDef* bg_weaponVariantDefs[2048]{};
unsigned bg_lastParsedWeaponIndex = 2047;
int g_playerAnimTypeIndex_revivee=7, g_playerAnimTypeIndex_sniper=11;
int g_playerAnimTypeIndex_sniper_rearclip=13, g_playerAnimTypeIndex_briefcase=17;
const dvar_s *player_aqs_time, *player_aqs_scale, *player_breath_gasp_lerp;
const dvar_s *bg_aqs, *player_aqs_spread_max, *player_aqs_spread_multiplier;
bool Assert_MyHandler(const char*,int,int,const char*,...){std::abort();}
void colgeom_visitor_t::intersect_box(float*,float*,int){std::abort();}
void StatMon_Warning(int,int,char*){std::abort();}
''' + source + r'''
static_assert(sizeof(playerState_s)==0x26e8 && sizeof(WeaponDef)==0x808);
static_assert(offsetof(playerState_s,quickScopeScale)==0x4ec);
static_assert(offsetof(playerState_s,quickScopeTimer)==0x4f0);
static_assert(offsetof(playerState_s,fWeaponPosFrac)==0x168);
static_assert(offsetof(pml_t,frametime)==0x24 && offsetof(pml_t,msec)==0x28);
static_assert(offsetof(WeaponDef,playerAnimType)==0x18 && offsetof(WeaponDef,overlayReticle)==0x4b4);
static_assert(offsetof(dvar_s,current)==0x18);
__declspec(naked) static void call_update(void*,pmove_t*,pml_t*,float){__asm{
 mov ecx,[esp+4]
 mov eax,[esp+8]
 push [esp+16]
 push [esp+16]
 call ecx
 add esp,8
 ret}}
__declspec(naked) static float call_spread(void*,playerState_s*){__asm{
 mov ecx,[esp+4]
 mov eax,[esp+8]
 sub esp,4
 call ecx
 movss [esp],xmm0
 fld [esp]
 add esp,4
 ret}}
int main(){
 _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
 auto image=map_original("build/BlackOpsMP.exe","REGIONS","FIXUPS");if(!image)return 2;
 for(unsigned r:{0x851a8cu,0x84f8acu,0x851adcu,0x84f874u,0x84f928u,0x8519e0u,0x886fe8u,0x886378u,0x886ff0u,0x88639cu,0x880258u}){
  DWORD old;if(!VirtualProtect(image+r,r==0x880258u?8192:4,PAGE_READWRITE,&old))return 3;
 }
 unsigned v[20];unsigned short cw=0x027f;
 while(fread(v,sizeof(v),1,stdin)==1){
  playerState_s ps{};pmove_t pm{};pml_t pml{};pm.ps=&ps;
  ps.weapon=1;ps.offHandIndex=2;ps.meleeWeapon=3;
  memcpy(&ps.weaponstate,v,4);ps.weapFlags=v[1];
  memcpy(&ps.fWeaponPosFrac,v+2,4);memcpy(&ps.quickScopeScale,v+4,4);
  memcpy(&ps.quickScopeTimer,v+5,4);memcpy(&pml.msec,v+6,4);
  memcpy(&pml.frametime,v+7,4);memcpy(&ps.aimSpreadScale,v+16,4);
  WeaponDef assets[3]{};WeaponVariantDef variants[3]{};
  for(unsigned i=0;i<3;++i){variants[i].weapDef=assets+i;bg_weaponVariantDefs[i+1]=variants+i;}
  assets[0].bHasBayonet=(v[13]&1)!=0;assets[0].bUseAsMelee=(v[13]&2)!=0;
  for(unsigned i=0;i<3;++i){assets[i].overlayReticle=static_cast<weapOverlayReticle_t>(v[11+i*3]);assets[i].playerAnimType=v[12+i*3];}
  dvar_s settings[6]{};const unsigned slot[]={0x851a8c,0x84f8ac,0x851adc,0x84f874,0x84f928,0x8519e0};
  for(unsigned i=0;i<3;++i)memcpy(&settings[i].current,v+8+i,4);
  settings[3].current.enabled=(v[18]&1)!=0;memcpy(&settings[4].current,v+17,4);memcpy(&settings[5].current,v+19,4);
  player_aqs_time=settings;player_aqs_scale=settings+1;player_breath_gasp_lerp=settings+2;
  bg_aqs=settings+3;player_aqs_spread_max=settings+4;player_aqs_spread_multiplier=settings+5;
  for(unsigned i=0;i<6;++i)*reinterpret_cast<dvar_s**>(image+slot[i])=settings+i;
  *reinterpret_cast<int*>(image+0x886fe8)=7;*reinterpret_cast<int*>(image+0x886378)=11;
  *reinterpret_cast<int*>(image+0x886ff0)=13;*reinterpret_cast<int*>(image+0x88639c)=17;
  memcpy(image+0x880258,bg_weaponVariantDefs,sizeof(bg_weaponVariantDefs));
  const auto before=ps;const auto before_pm=pm;const auto before_pml=pml;
  WeaponDef before_assets[3];memcpy(before_assets,assets,sizeof(assets));
  WeaponVariantDef before_variants[3];memcpy(before_variants,variants,sizeof(variants));
  dvar_s before_settings[6];memcpy(before_settings,settings,sizeof(settings));
  auto check=[&](){auto expected=before;expected.quickScopeScale=ps.quickScopeScale;expected.quickScopeTimer=ps.quickScopeTimer;
   if(memcmp(&expected,&ps,sizeof(ps))||memcmp(&before_pm,&pm,sizeof(pm))||memcmp(&before_pml,&pml,sizeof(pml))||
    memcmp(before_assets,assets,sizeof(assets))||memcmp(before_variants,variants,sizeof(variants))||
    memcmp(before_settings,settings,sizeof(settings))||memcmp(image+0x880258,bg_weaponVariantDefs,sizeof(bg_weaponVariantDefs)))std::abort();};
  unsigned row[6];float previous;memcpy(&previous,v+3,4);
  for(unsigned backend=0;backend<2;++backend){
   ps=before;__asm{fninit
   fldcw cw}_mm_setcsr(0x1f80);
   for(unsigned frame=0;frame<=(v[18]>>8);++frame){
    if(!backend)call_update(image+0x3bf320,&pm,&pml,previous);else PM_UpdateQuickScope(&pm,&pml,previous);
    check();
   }
   float spread=!backend?call_spread(image+0x3bd940,&ps):BG_GetQuickScopeSpread(&ps);
   memcpy(row+backend*3,&ps.quickScopeScale,4);memcpy(row+backend*3+1,&ps.quickScopeTimer,4);memcpy(row+backend*3+2,&spread,4);check();
  }
  fwrite(row,sizeof(row),1,stdout);
 }
}
'''
# Each fixture has three asset records encoded as overlay/animation pairs. The
# spare third word of each record preserves room for independent flag coverage.
cpp = cpp.replace('REGIONS', regions).replace('FIXUPS', relocations)
# Keep all fixture words disjoint: asset overlays/animations use 11/12,14/15,
# and a fixed third asset, while spread configuration uses16..19.
cpp = cpp.replace('v[11+i*3]', '(i==2?1:v[11+i*3])').replace('v[12+i*3]', '(i==2?19:v[12+i*3])')
(out / 'runner.cpp').write_text(cpp)
stem = str(out / 'runner')
(out / 'build.cmd').write_text('@echo off\ncall "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvarsall.bat" x86 -vcvars_ver=14.37 >nul\ncl /nologo /std:c++20 /O2 /Gy /Gw /fp:precise /arch:SSE2 /DNDEBUG /DKISAK_MP /DWIN32 /EHsc /I. /Isrc /Isrc/libs /Isrc/universal '+stem+'.cpp /Fe'+stem+'.exe /Fo'+stem+'.obj /link /OPT:REF >'+stem+'-build.log 2>&1\nexit /b %errorlevel%\n')
compiled = subprocess.run(['cmd', '/d', '/c', str(out / 'build.cmd')], capture_output=True)
if compiled.returncode:
    print((out / 'runner-build.log').read_text());sys.exit(2)

def bits(f):
    return struct.unpack('<I', struct.pack('<f', f))[0]

rng = random.Random(20261106)
cases = []
for state in range(52):
    for ads, previous in [(0,0),(.000999,0),(.001,0),(.25,0),(.5,.75),(1,.9),(1,1)]:
        for overlay, animation in [(0,0),(0,11),(0,13),(1,19)]:
            v = [state, 2, bits(ads), bits(previous), bits(4), 250, 16,bits(.016),
                 bits(.25),bits(8),bits(6),overlay,animation,0,1,19,bits(0),bits(255),1,bits(2)]
            cases.append(v)
for _ in range(1500):
    v = list(rng.choice(cases));v[1]=rng.randrange(4);v[4]=bits(rng.uniform(-2,12))
    v[5]=rng.choice([0,1,249,250,251,0xffffffff]);v[6]=rng.choice([0,1,16,33,250,1000])
    v[8]=bits(rng.choice([0,.001,.25,1,30]));v[9]=bits(rng.uniform(0,50))
    v[10]=bits(rng.uniform(0,12));v[13]=rng.randrange(4);v[18]=rng.randrange(2)
    cases.append(v)
for ads_bits in (0,0x80000000,0x3a83126e,0x3a83126f,0x3a831270,
                 0x3f7fffff,0x3f800000,0x3f800001):
    for timer in (0,1,249,250,251,0x7fffffff,0x80000000,0xffffffff):
        for msec in (0,1,16,250,0x7fffffff,0x80000000,0xffffffff):
            v=list(cases[0]);v[2]=v[3]=ads_bits;v[5]=timer;v[6]=msec;v[11]=1
            cases.append(v)
for frames in (2,16,64):
    for ads in (0,.001,.5,1):
        v=list(cases[0]);v[2]=v[3]=bits(ads);v[11]=1;v[18]=1|((frames-1)<<8)
        cases.append(v)
for duration_bits in (0,0x3a83126e,0x3a83126f,0x3a831270,
                      0x3e7fffff,0x3e800000,0x3e800001,bits(30)):
    for ads in (0,.5,1):
        for timer in (0,1,249,250,251):
            v=list(cases[0]);v[8]=duration_bits;v[2]=v[3]=bits(ads);v[5]=timer;v[11]=1
            cases.append(v)
packed=b''.join(struct.pack('<20I',*v)for v in cases)
(out/'cases.bin').write_bytes(packed)
native=subprocess.run([str(out/'runner.exe')],input=packed,capture_output=True)
if native.returncode:
    raise RuntimeError(f'BLOCKED native {native.returncode}; completed{len(native.stdout)//24}; {native.stderr[-500:]!r}')
rows=list(struct.iter_unpack('<6I',native.stdout));assert len(rows)==len(cases)
original=b''.join(struct.pack('<3I',*r[:3])for r in rows)
capture=out/('original-'+hashlib.sha256(original).hexdigest()+'.bin')
if capture.exists():assert capture.read_bytes()==original
else:
    with capture.open('xb')as file:file.write(original)

# Independently execute unchanged original code at its retail addresses. All
# original callee bodies, constants and switch-table entries are present.
uc=Uc(UC_ARCH_X86,UC_MODE_32);pages=set()
def mapping(a,n):
    for page in range(a&~4095,(a+n+4095)&~4095,4096):
        if page not in pages:uc.mem_map(page,4096);pages.add(page)
for r,n in units+constants+globals_:
    mapping(base+r,n);uc.mem_write(base+r,image_bytes(pe,r,n))
DATA=0x20000000;SP=0x30001000;STOP=0x40000000
mapping(DATA,0x20000);mapping(SP-0x1000,0x2000);mapping(STOP,1)
reads=[];writes=[];executed=set();stopped=False
def code(emu,a,n,user):
    global stopped
    if a==STOP:stopped=True;emu.emu_stop();return
    if a not in instructions:raise RuntimeError(f'BLOCKED original code {a:x}')
    executed.add(a)
def memory(emu,access,a,n,value,user):
    if SP-0x1000<=a and a+n<=SP+0x100:return
    allowed=writes if access==17 else reads
    if not any(start<=a and a+n<=end for start,end in allowed):
        raise RuntimeError(f'BLOCKED original memory {access} {a:x}+{n}')
uc.hook_add(UC_HOOK_CODE,code);uc.hook_add(UC_HOOK_MEM_READ|UC_HOOK_MEM_WRITE,memory)
def call(entry,args,eax):
    global stopped
    stopped=False;uc.mem_write(SP-0x1000,bytes(0x1100));uc.mem_write(SP,struct.pack('<'+'I'*(1+len(args)),STOP,*args))
    for reg in (x.UC_X86_REG_EAX,x.UC_X86_REG_EBX,x.UC_X86_REG_ECX,x.UC_X86_REG_EDX,x.UC_X86_REG_ESI,x.UC_X86_REG_EDI,x.UC_X86_REG_EBP):uc.reg_write(reg,0)
    uc.reg_write(x.UC_X86_REG_EAX,eax);uc.reg_write(x.UC_X86_REG_ESP,SP);uc.reg_write(x.UC_X86_REG_EFLAGS,0x202)
    uc.reg_write(x.UC_X86_REG_MXCSR,0x1f80);uc.reg_write(x.UC_X86_REG_FPCW,0x027f)
    uc.emu_start(base+entry,0xffffffff,count=10000)
    assert stopped and uc.reg_read(x.UC_X86_REG_ESP)==SP+4
for index,(v,row)in enumerate(zip(cases,rows)):
    ps=bytearray(0x26e8);pm=bytearray(0x100);pml=bytearray(0x100)
    def put(b,o,w):struct.pack_into('<I',b,o,w)
    for o,w in [(0x144,1),(0x134,2),(0x154,3),(0x158,v[0]),(0x10,v[1]),(0x168,v[2]),(0x4ec,v[4]),(0x4f0,v[5]),(0x560,v[16])]:put(ps,o,w)
    put(pm,0,DATA);put(pml,0x28,v[6]);put(pml,0x24,v[7]);objects=[(DATA,ps),(DATA+0x3000,pm),(DATA+0x4000,pml)]
    table=bytearray(8192)
    for i in range(3):
        asset=bytearray(0x808);variant=bytearray(0xe4);aa=DATA+0x5000+i*0x1000;va=DATA+0x9000+i*0x100
        put(asset,0x4b4,1 if i==2 else v[11+i*3]);put(asset,0x18,19 if i==2 else v[12+i*3])
        if i==0:asset[0x569]=bool(v[13]&1);asset[0x64c]=bool(v[13]&2)
        put(variant,8,aa);put(table,4*(i+1),va);objects.extend([(aa,asset),(va,variant)])
    settings=[v[8],v[9],v[10],v[18]&1,v[17],v[19]]
    bindings=[(base+0x880258,table)]
    for i,r in enumerate([0x851a8c,0x84f8ac,0x851adc,0x84f874,0x84f928,0x8519e0]):
        setting=bytearray(0x60);put(setting,0x18,settings[i]);objects.append((DATA+0xa000+i*0x100,setting))
        bindings.append((base+r,struct.pack('<I',DATA+0xa000+i*0x100)))
    for r,value in [(0x886fe8,7),(0x886378,11),(0x886ff0,13),(0x88639c,17)]:bindings.append((base+r,struct.pack('<I',value)))
    for a,b in objects+bindings:uc.mem_write(a,bytes(b))
    reads=[(a,a+len(b))for a,b in objects+bindings]+[(base+r,base+r+n)for r,n in constants]
    writes=[(DATA+0x4ec,DATA+0x4f4)]
    for frame in range(1+(v[18]>>8)):
        call(0x3bf320,[DATA+0x4000,v[3]],DATA+0x3000)
    observed=struct.unpack('<2I',uc.mem_read(DATA+0x4ec,8));assert observed==row[:2],(index,observed,row)
    call(0x3bd940,[],DATA)
    spread=uc.reg_read(x.UC_X86_REG_XMM0)&0xffffffff;assert spread==row[2],(index,spread,row)
    for a,b in objects+bindings:
        expected=bytearray(b)
        if a==DATA:expected[0x4ec:0x4f4]=uc.mem_read(DATA+0x4ec,8)
        assert bytes(uc.mem_read(a,len(b)))==expected,(index,hex(a))
mismatches=[i for i,r in enumerate(rows)if r[:3]!=r[3:]]
report=dict(status='mismatch' if mismatches else 'pass',cases=len(cases),production_mismatches=len(mismatches),
    first_mismatch=({'index':mismatches[0],'input':cases[mismatches[0]],'output':rows[mismatches[0]]}if mismatches else None),
    native_unicorn_mismatches=0,reference_sha256=hashlib.sha256((ROOT/'build/BlackOpsMP.exe').read_bytes()).hexdigest(),
    original_capture=str(capture),original_outputs_sha256=hashlib.sha256(original).hexdigest(),
    input_sha256=hashlib.sha256(packed).hexdigest(),original_function_bytes=350,declared_closure_bytes=sum(n for _,n in units),
    executed_instruction_bytes=sum(instructions[a].size for a in executed),
    caller_order_instruction_audit='pass; five original call targets and production pre-ADS snapshot order checked',
    dispatch_switch_route_audit='pass; all47 raw states against original two-level table; route identity only, not execution of every callee',
    fixtures_where_original_mutates_timer_or_scale=sum(r[:2]!=(v[4],v[5])for v,r in zip(cases,rows)),
    units=[dict(rva=hex(r),size=n,sha256=hashlib.sha256(image_bytes(pe,r,n)).hexdigest())for r,n in units],
    selected=[dict(path=p,symbol=n,sha256=hashlib.sha256(b.encode()).hexdigest())for p,n,b in selected],
    compiler_profile='MSVC14.37 x86 /O2 /Gy /Gw /fp:precise /arch:SSE2; FPCW027f MXCSR1f80',
    scope='Complete quick-scope updater and spread consumer with original viewmodel selector, getters, animation-type switch and DiffTrack. Finite fixtures; all PS bytes except timer/scale and all assets/settings/pmove/pml/table bytes guarded. Does not execute full PM_Weapon or ADS lerp. Dispatcher snapshot and call order require separate instruction audit. No candidate byte-identity or instruction-similarity claim.')
compiler_root=Path('C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.37.32822')
report['compiler_identities']={str(compiler_root/p):hashlib.sha256((compiler_root/p).read_bytes()).hexdigest()
                               for p in ['bin/HostX86/x86/cl.exe','bin/HostX86/x86/link.exe']}
report['native_fixups']=[dict(operand_rva=hex(a),target_rva=hex(b))for a,b in fixups]
report['source_head']=subprocess.check_output(['git','rev-parse','HEAD']).decode().strip()
report['file_identities']={str(p):hashlib.sha256(Path(p).read_bytes()).hexdigest()for p in
    {Path(__file__),Path('tests/original_native_loader.h'),Path('tests/mp_fidelity_common.py'),
     Path('tests/quickscope_update_reference.json'),out/'runner.cpp',out/'runner.exe',out/'build.cmd',
     Path('src/cgame_mp/cg_predict_mp.cpp'),Path('src/physics/phys_traverse.cpp'),
     Path('src/qcommon/msg_mp.cpp'),*(Path(p)for p,_,_ in selected)}}
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:report[k]for k in ('status','cases','production_mismatches','first_mismatch','original_function_bytes','declared_closure_bytes','executed_instruction_bytes')},indent=2))
sys.exit(bool(mismatches))

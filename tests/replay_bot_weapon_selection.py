"""Full retail Bot_UpdateWeapon replay against selected production definitions.

Run from the repository root with build-re/venv/Scripts/python.exe.
Requires the pinned original, Unicorn/Capstone, and MSVC 14.37 x86.
Generated C++, binaries, content-addressed immutable original outputs and reports
stay under ignored build-re/multiplayer/nov01/bot-selector/. This is a finite
behavioral comparison, not live gameplay or candidate instruction-byte identity.
No original dependency is replaced with a synthetic result. Unexpected code,
memory accesses, native faults, or terminating diagnostic guards block the run.
"""
import datetime,hashlib,json,os,random,re,struct,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
os.chdir(ROOT)
started_at=datetime.datetime.now(datetime.timezone.utc).isoformat()
from mp_fidelity_common import reference,image_bytes,definition,exact_definition
from capstone import Cs,CS_ARCH_X86,CS_MODE_32
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE,UC_HOOK_MEM_READ,UC_HOOK_MEM_WRITE
import unicorn.x86_const as x
out=ROOT/'build-re/multiplayer/nov01/bot-selector';out.mkdir(parents=True,exist_ok=True)
_,pe=reference(); cs=Cs(CS_ARCH_X86,CS_MODE_32)
units=[(0x471f30,687),(0xd6000,17),(0x129ff0,15),(0x153510,12),(0x59680,111),(0x22cc80,65),(0x470af0,32)]
fixups=[(0x47201a,0x32741ac),(0xd600c,0x3043800),(0x129ff7,0x880258),(0x153517,0x880258)]
selected=[]
def select(path,*names):
 for name in names:
  body=definition(Path(path),name);selected.append((path,name,body))
select('src/bgame/bg_weapons_def.cpp','BG_GetWeaponVariantDef','BG_GetWeaponDef')
select('src/bgame/bg_weapons_ammo.cpp','BG_AmmoForWeapon')
select('src/bgame/bg_weapons.cpp','BG_GetAmmoNotInClipForAmmoIndex','BG_GetAmmoNotInClip')
select('src/bgame/bg_mantle.cpp','BG_GetAmmoInClipForWeaponDef','BG_GetAmmoInClip')
select('src/bgame/bg_weapons_ammo.cpp','BG_WeaponAmmo')
select('src/bgame/bg_misc.cpp','BG_GetHeldWeaponSlot','BG_GetHeldWeaponConst','BG_PlayerHasWeapon')
select('src/demo/demo_common.cpp','Demo_GetDemoState','Demo_IsPlaying')
select('src/game_mp/g_main_mp.cpp','G_GetPlayerState')
select('src/server_mp/sv_bot_mp.cpp','Bot_IsNonNitrousVehicle','Bot_UpdateWeapon')
source='\n'.join(b for _,_,b in selected)
_exact_definition=exact_definition
source+='\n'+_exact_definition(Path('src/game_mp/g_main_mp.cpp'),'gclient_s::gclient_s()')
source+='\n'+_exact_definition(Path('src/physics/phys_traverse.cpp'),'colgeom_visitor_t::colgeom_visitor_t()')
source+='\n'+definition(Path('src/universal/com_math.cpp'),'Vec3Max')
field_source=Path('src/qcommon/msg_mp.cpp')
source+='\n'+'\n'.join(definition(field_source,n,data=True) for n in re.findall(r'^const NetField (\w+)\[',field_source.read_text(),re.M))
(out/'selected.cpp').write_text(source)
loads='\n'.join('const unsigned char c%d[]={%s};memcpy(image+%s,c%d,sizeof(c%d));'%(i,','.join(hex(b)for b in image_bytes(pe,r,n)),hex(r),i,i)for i,(r,n)in enumerate(units))
patches='\n'.join('*reinterpret_cast<unsigned*>(image+%s)=reinterpret_cast<unsigned>(image+%s);'%(hex(a),hex(b))for a,b in fixups)
cpp=r'''#include <windows.h>
#include <server_mp/sv_bot_mp.h>
#include <server_mp/sv_main_mp.h>
#include <game/g_scr_vehicle.h>
#include <game_mp/g_main_mp.h>
#include <bgame/bg_weapons.h>
#include <bgame/bg_weapons_ammo.h>
#include <bgame/bg_misc.h>
#include <demo/demo_common.h>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <cstdlib>
#include <io.h>
#include <fcntl.h>
#include <xmmintrin.h>
WeaponVariantDef* bg_weaponVariantDefs[2048]{};
unsigned int bg_lastParsedWeaponIndex=7;
const dvar_t*sv_botMeleeDist;
level_locals_t level{};
demoMain demo{};
bool Assert_MyHandler(const char*,int,int,const char*,...){std::abort();}
void colgeom_visitor_t::intersect_box(float*,float*,int){std::abort();}
void StatMon_Warning(int,int,char*){std::abort();}
'''+source+r'''
static_assert(sizeof(gclient_s)==0x2a28 && sizeof(playerState_s)==0x26e8);
static_assert(offsetof(client_t,gentity)==0x556c && offsetof(gentity_s,s)==0);
static_assert(offsetof(bot_info_t,threat)==0x3e8 && offsetof(bot_info_t,weapon)==0x420);
static_assert(offsetof(playerState_s,heldWeapons)==0x1d8 && sizeof(PlayerHeldWeapon)==0x1c);
static_assert(offsetof(playerState_s,ammoNotInClip)==0x37c && offsetof(playerState_s,ammoInClip)==0x3f4);
static_assert(offsetof(WeaponVariantDef,iAmmoIndex)==0x44 && offsetof(WeaponVariantDef,iClipIndex)==0x4c);
static_assert(offsetof(gentity_s,pTurretInfo)==0x150 && offsetof(gentity_s,scr_vehicle)==0x158);
static_assert(offsetof(scr_vehicle_s,nitrousVehicle)==0x69c);
static_assert(offsetof(WeaponDef,weapType)==0x1c && offsetof(WeaponDef,weapClass)==0x20 && offsetof(WeaponDef,requireLockonToFire)==0x47c);
static_assert(offsetof(WeaponVariantDef,weapDef)==8 && offsetof(usercmd_s,weapon)==0x18 && sizeof(usercmd_s)==0x34);
static_assert(offsetof(bot_threat_t,distSq)==8 && sizeof(bot_info_t)==0x460);
__declspec(naked)static void original_call(void*,client_t*,bot_info_t*,usercmd_s*){__asm{
 mov ecx,[esp+4]
 mov eax,[esp+8]
 mov edx,[esp+16]
 push edx
 mov edx,[esp+16]
 push edx
 call ecx
 add esp,8
 ret}}
int main(){_setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);unsigned short cw=0x027f;__asm{fninit
fldcw cw}_mm_setcsr(0x1f80);
auto image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x3300000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));if(!image)return 2;
'''+loads+'\n'+patches+r'''
 static client_t client{},saved_client{};static gclient_s player{};static gentity_s self{},enemy{};static scr_vehicle_s vehicle{};
static WeaponDef assets[8]{};static WeaponVariantDef variants[8]{};static bot_info_t info{};static usercmd_s cmd{};static dvar_s setting{};
unsigned v[64];
while(fread(v,sizeof(v),1,stdin)==1){
 memset(&client,0,sizeof(client));memset(&player,0,sizeof(player));memset(&self,0,sizeof(self));memset(&enemy,0,sizeof(enemy));memset(&vehicle,0,sizeof(vehicle));memset(&info,0,sizeof(info));memset(&cmd,0,sizeof(cmd));
 auto&ps=player.ps;memcpy(&ps.weaponstate,v,4);info.weapon=v[1];cmd.weapon=(unsigned short)v[2];
 client.gentity=&self;self.s.number=0;level.clients=&player;level.maxclients=1;demo.state=0;
 info.threat.enemy=v[3]?&enemy:nullptr;enemy.pTurretInfo=(v[3]==2||v[3]==5)?reinterpret_cast<decltype(enemy.pTurretInfo)>(1):nullptr;
 enemy.scr_vehicle=v[3]>=3?&vehicle:nullptr;vehicle.nitrousVehicle=v[3]==4?reinterpret_cast<NitrousVehicle*>(1):nullptr;
 memcpy(&info.threat.distSq,v+4,4);memcpy(&setting.current.value,v+5,4);sv_botMeleeDist=&setting;
 for(unsigned i=0;i<8;++i){memset(assets+i,0,sizeof(assets[i]));memset(variants+i,0,sizeof(variants[i]));
 assets[i].weapType=(weapType_t)v[6+3*i];assets[i].weapClass=(weapClass_t)v[7+3*i];assets[i].requireLockonToFire=v[8+3*i]!=0;
 variants[i].weapDef=assets+i;variants[i].iAmmoIndex=100+i;variants[i].iClipIndex=200+i;bg_weaponVariantDefs[i]=variants+i;}
 for(unsigned i=0;i<15;++i){ps.heldWeapons[i].weapon=v[46+i];ps.ammoNotInClip[i].ammoIndex=500+i;ps.ammoInClip[i].clipIndex=600+i;}
 for(unsigned i=0;i<8;++i){unsigned slot=(i+v[61])%15;ps.ammoNotInClip[slot].ammoIndex=100+i;memcpy(&ps.ammoNotInClip[slot].count,v+30+i,4);ps.ammoInClip[slot].clipIndex=200+i;memcpy(&ps.ammoInClip[slot].count,v+38+i,4);}
 cmd.button_bits.array[0]=v[62];cmd.button_bits.array[1]=v[63];
 auto table=reinterpret_cast<WeaponVariantDef**>(image+0x880258);memcpy(table,bg_weaponVariantDefs,sizeof(bg_weaponVariantDefs));
 *reinterpret_cast<gclient_s**>(image+0x3043800)=&player;*reinterpret_cast<const dvar_s**>(image+0x32741ac)=&setting;
 auto bb=info;auto pp=player;auto cc=cmd;auto ee=enemy;auto vv=vehicle;auto ss=self;auto dd=setting;auto ll=level;auto de=demo;
 memcpy(&bb,&info,sizeof(info));memcpy(&pp,&player,sizeof(player));memcpy(&cc,&cmd,sizeof(cmd));memcpy(&ee,&enemy,sizeof(enemy));memcpy(&vv,&vehicle,sizeof(vehicle));memcpy(&ss,&self,sizeof(self));memcpy(&dd,&setting,sizeof(setting));memcpy(&ll,&level,sizeof(level));memcpy(&de,&demo,sizeof(demo));
 memcpy(&saved_client,&client,sizeof(client));
 WeaponDef aa[8];WeaponVariantDef va[8];memcpy(aa,assets,sizeof(aa));memcpy(va,variants,sizeof(va));
 __asm{fninit
 fldcw cw}_mm_setcsr(0x1f80);
 original_call(image+0x471f30,&client,&info,&cmd);unsigned orig=cmd.weapon;
 auto check=[&](){auto expected=cc;expected.weapon=cmd.weapon;
 if(memcmp(&expected,&cmd,sizeof(cmd))||memcmp(&bb,&info,sizeof(info))||memcmp(&pp,&player,sizeof(player))||memcmp(&ee,&enemy,sizeof(enemy))||memcmp(&vv,&vehicle,sizeof(vehicle))||memcmp(&ss,&self,sizeof(self))||memcmp(&dd,&setting,sizeof(setting))||memcmp(aa,assets,sizeof(aa))||memcmp(va,variants,sizeof(va))||memcmp(&ll,&level,sizeof(level))||memcmp(&de,&demo,sizeof(demo))||memcmp(table,bg_weaponVariantDefs,sizeof(bg_weaponVariantDefs))){std::fprintf(stderr,"preservation: cmd%d bot%d ps%d enemy%d vehicle%d self%d setting%d assets%d variants%d level%d demo%d table%d\n",memcmp(&expected,&cmd,sizeof(cmd)),memcmp(&bb,&info,sizeof(info)),memcmp(&pp,&player,sizeof(player)),memcmp(&ee,&enemy,sizeof(enemy)),memcmp(&vv,&vehicle,sizeof(vehicle)),memcmp(&ss,&self,sizeof(self)),memcmp(&dd,&setting,sizeof(setting)),memcmp(aa,assets,sizeof(aa)),memcmp(va,variants,sizeof(va)),memcmp(&ll,&level,sizeof(level)),memcmp(&de,&demo,sizeof(demo)),memcmp(table,bg_weaponVariantDefs,sizeof(bg_weaponVariantDefs)));std::exit(3);}};check();
 if(memcmp(&saved_client,&client,sizeof(client)))return 4;
 cmd=cc;__asm{fninit
 fldcw cw}_mm_setcsr(0x1f80);
 Bot_UpdateWeapon(&info,&client,&cmd);check();if(memcmp(&saved_client,&client,sizeof(client)))return 5;unsigned result[2]={orig,cmd.weapon};fwrite(result,sizeof(result),1,stdout);
}return 0;}
'''
(out/'runner.cpp').write_text(cpp)
stem=str(out/'runner').replace('/','\\')
(out/'build.cmd').write_text('@echo off\ncall "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvarsall.bat" x86 -vcvars_ver=14.37 >nul\ncl /nologo /std:c++20 /O2 /Gy /Gw /fp:precise /arch:SSE2 /DNDEBUG /DKISAK_MP /DWIN32 /EHsc /I. /Isrc /Isrc/libs /Isrc/universal '+stem+'.cpp /Fe'+stem+'.exe /Fo'+stem+'.obj /link /OPT:REF >'+stem+'-build.log 2>&1\nexit /b %errorlevel%\n')
if len(sys.argv)>1 and sys.argv[1]=='generate':sys.exit()
compiled=subprocess.run(['cmd','/d','/c',str(out/'build.cmd')],capture_output=True)
if compiled.returncode:print((out/'runner-build.log').read_text());sys.exit(2)
print('built')
def bits(f):return struct.unpack('<I',struct.pack('<f',f))[0]
rng=random.Random(20261101)
fixtures=[]
base_defs=[(0,0,0),(0,0,0),(2,6,0),(2,6,1),(5,0,0),(0,7,0),(0,4,0),(2,4,1)]
inventories=[[],[1],[2],[3],[4,5],[3,2,1],[1,2,3,4,5,6,7],[7,6,5,4,3,2,1]]
def fixture(state=0,bw=1,cmd=1,enemy=1,dist=100,melee=10,inventory=6,ammo=0,shift=0):
 v=[state,bw,cmd,enemy,bits(dist),bits(melee)]
 for d in base_defs:v.extend(d)
 v.extend(([0]*8 if ammo==1 else [0,30,2,3,1,10,20,1]))
 v.extend(([0]*8 if ammo in (1,2) else [0,10,1,1,1,10,10,1]))
 held=list(inventories[inventory]);v.extend((held+[0]*15)[:15]);v.extend([shift,rng.getrandbits(32),rng.getrandbits(32)])
 assert len(v)==64
 return v
for state in [*range(52),0xffffffff,0x80000000,0x7fffffff]:
 for enemy in range(6):
  for ammo in range(3):fixtures.append(fixture(state=state,enemy=enemy,ammo=ammo,inventory=(state+enemy)%8,shift=(state+enemy)%15))
for enemy in range(6):
 for bw in range(8):
  for cmd in range(8):fixtures.append(fixture(bw=bw,cmd=cmd,enemy=enemy,inventory=(bw+cmd)%8,ammo=(bw+cmd)%3))
for _ in range(512):
 v=fixture(state=rng.choice([0,28,29,30]),bw=rng.randrange(8),cmd=rng.randrange(8),enemy=rng.randrange(6),inventory=rng.randrange(8),ammo=rng.randrange(3),shift=rng.randrange(15))
 for i in range(8):v[6+3*i:9+3*i]=[rng.randrange(8),rng.randrange(13),rng.randrange(2)]
 for i in range(30,46):v[i]=rng.choice([0,0,1,2,30,999,0xffffffff])
 v[46:61]=[rng.randrange(8) for _ in range(15)]
 v[4]=rng.choice([0,bits(99),bits(100),bits(101),0x80000000,0x7f800000,0xff800000,0x7fc00001,0x7f800001])
 fixtures.append(v)
packed=b''.join(struct.pack('<64I',*v)for v in fixtures);(out/'cases.bin').write_bytes(packed)
native=subprocess.run([str((out/'runner.exe').resolve())],input=packed,capture_output=True)
if native.returncode:raise RuntimeError(f'native blocked {native.returncode}: {native.stderr[-1000:]!r}, completed {len(native.stdout)//8}')
assert len(native.stdout)==8*len(fixtures),(len(native.stdout),len(fixtures))
rows=list(struct.iter_unpack('<2I',native.stdout));original=b''.join(struct.pack('<I',a)for a,b in rows)
original_sha=hashlib.sha256(original).hexdigest();capture=out/('original-'+original_sha+'.bin')
if capture.exists():assert capture.read_bytes()==original
else:
 with capture.open('xb')as f:f.write(original)
(out/'candidate.bin').write_bytes(b''.join(struct.pack('<I',b)for a,b in rows))
print('native',len(fixtures),'production mismatches',sum(a!=b for a,b in rows),flush=True)
BASE=0x400000;DATA=0x20000000;SP=0x30000800;STOP=0x40000000
emu=Uc(UC_ARCH_X86,UC_MODE_32);pages=set()
def map_range(a,n):
 for p in range(a&~4095,(a+n+4095)&~4095,4096):
  if p not in pages:emu.mem_map(p,4096);pages.add(p)
code=set()
for r,n in units:
 raw=image_bytes(pe,r,n);map_range(BASE+r,n);emu.mem_write(BASE+r,raw);code|={i.address for i in cs.disasm(raw,BASE+r)}
for r,n in [(0x880258,8192),(0x32741ac,4),(0x3043800,4)]:map_range(BASE+r,n)
map_range(DATA,0x40000);map_range(SP-0x800,0x2000);map_range(STOP,1)
reads=[];writes=[];seen=set();executed=set()
def trace(uc,a,n,user):
 if a==STOP:seen.add(a);uc.emu_stop();return
 if a not in code:raise RuntimeError(f'unsupported code {a:x}')
 executed.add(a)
def memory(uc,access,a,n,value,user):
 if SP-0x800<=a and a+n<=SP+0x100:return
 allowed=writes if access==17 else reads
 if not any(b<=a and a+n<=e for b,e in allowed):raise RuntimeError(f'unsupported memory {access} {a:x}+{n}')
emu.hook_add(UC_HOOK_CODE,trace);emu.hook_add(UC_HOOK_MEM_READ|UC_HOOK_MEM_WRITE,memory)
uc_outputs=[]
for ci,(v,row) in enumerate(zip(fixtures,rows)):
 client=bytearray(0x6000);ps=bytearray(0x2a28);selfent=bytearray(0x348);enemy=bytearray(0x348);vehicle=bytearray(0x700);bot=bytearray(0x460);cmd=bytearray(0x34);setting=bytearray(0x60)
 struct.pack_into('<I',client,0x556c,DATA+0x14000)
 struct.pack_into('<I',ps,0x158,v[0]);struct.pack_into('<I',bot,0x420,v[1]);struct.pack_into('<H',cmd,0x18,v[2]);struct.pack_into('<2I',cmd,4,*v[62:64])
 struct.pack_into('<I',bot,0x3e8,DATA+0x15000 if v[3] else 0);struct.pack_into('<I',bot,0x3f0,v[4]);struct.pack_into('<I',setting,0x18,v[5])
 struct.pack_into('<I',enemy,0x150,int(v[3] in(2,5)));struct.pack_into('<I',enemy,0x158,DATA+0x16000 if v[3]>=3 else 0);struct.pack_into('<I',vehicle,0x69c,int(v[3]==4))
 objects=[(DATA,client),(DATA+0x10000,ps),(DATA+0x14000,selfent),(DATA+0x15000,enemy),(DATA+0x16000,vehicle),(DATA+0x18000,bot),(DATA+0x19000,cmd),(DATA+0x1a000,setting)]
 for i in range(15):
  struct.pack_into('<I',ps,0x1d8+28*i,v[46+i]);struct.pack_into('<2I',ps,0x37c+8*i,500+i,0);struct.pack_into('<2I',ps,0x3f4+8*i,600+i,0)
 table=bytearray(8192)
 for i in range(8):
  asset=bytearray(0x808);variant=bytearray(0xe4);struct.pack_into('<2I',asset,0x1c,*v[6+3*i:8+3*i]);asset[0x47c]=int(v[8+3*i]!=0)
  struct.pack_into('<I',variant,8,DATA+0x20000+0x1000*i);struct.pack_into('<I',variant,0x44,100+i);struct.pack_into('<I',variant,0x4c,200+i)
  slot=(i+v[61])%15;struct.pack_into('<2I',ps,0x37c+8*slot,100+i,v[30+i]);struct.pack_into('<2I',ps,0x3f4+8*slot,200+i,v[38+i])
  struct.pack_into('<I',table,4*i,DATA+0x30000+0x100*i);objects.extend([(DATA+0x20000+0x1000*i,asset),(DATA+0x30000+0x100*i,variant)])
 for a,b in objects:emu.mem_write(a,bytes(b))
 bindings=[(BASE+0x880258,table),(BASE+0x3043800,struct.pack('<I',DATA+0x10000)),(BASE+0x32741ac,struct.pack('<I',DATA+0x1a000))]
 for a,b in bindings:emu.mem_write(a,bytes(b))
 reads=[(DATA+0x556c,DATA+0x5570),(DATA+0x14000,DATA+0x14004),(DATA+0x10158,DATA+0x1015c),(DATA+0x101d8,DATA+0x1046c),(DATA+0x183e8,DATA+0x183ec),(DATA+0x183f0,DATA+0x183f4),(DATA+0x18420,DATA+0x18424),(DATA+0x15150,DATA+0x1515c),(DATA+0x1669c,DATA+0x166a0),(DATA+0x19018,DATA+0x1901a),(DATA+0x1a018,DATA+0x1a01c)]
 reads.extend((a,a+len(b))for a,b in bindings)
 for i in range(8):
  aa=DATA+0x20000+0x1000*i;va=DATA+0x30000+0x100*i;reads.extend([(aa+0x1c,aa+0x24),(aa+0x47c,aa+0x47d),(va+8,va+12),(va+0x44,va+0x50)])
 writes=[(DATA+0x19018,DATA+0x1901a)]
 emu.mem_write(SP-0x800,bytes(0x1000));emu.mem_write(SP,struct.pack('<3I',STOP,DATA+0x18000,DATA+0x19000))
 for reg in (x.UC_X86_REG_EAX,x.UC_X86_REG_EBX,x.UC_X86_REG_ECX,x.UC_X86_REG_EDX,x.UC_X86_REG_ESI,x.UC_X86_REG_EDI,x.UC_X86_REG_EBP):emu.reg_write(reg,0)
 emu.reg_write(x.UC_X86_REG_EAX,DATA);emu.reg_write(x.UC_X86_REG_ESP,SP);emu.reg_write(x.UC_X86_REG_EFLAGS,0x202);emu.reg_write(x.UC_X86_REG_MXCSR,0x1f80);emu.reg_write(x.UC_X86_REG_FPCW,0x027f)
 seen.clear()
 try:emu.emu_start(BASE+0x471f30,0xffffffff,count=50000)
 except Exception as ex:raise RuntimeError(f'case {ci} eip{emu.reg_read(x.UC_X86_REG_EIP):x}')from ex
 assert seen=={STOP} and emu.reg_read(x.UC_X86_REG_ESP)==SP+4
 observed=struct.unpack('<H',emu.mem_read(DATA+0x19018,2))[0];assert observed==row[0],(ci,observed,row)
 for a,b in objects+bindings:
  actual=bytes(emu.mem_read(a,len(b)));expected=bytearray(b)
  if a==DATA+0x19000:expected[0x18:0x1a]=actual[0x18:0x1a]
  assert actual==expected,(ci,hex(a))
 uc_outputs.append(observed)
 if ci%500==0:print('UC',ci,flush=True)
report=dict(status='pass' if all(a==b for a,b in rows) else 'mismatch',reference_sha256=hashlib.sha256(Path('build/BlackOpsMP.exe').read_bytes()).hexdigest(),cases=len(fixtures),native_unicorn_mismatches=0,production_mismatches=sum(a!=b for a,b in rows),original_outputs_sha256=original_sha,case_sha256=hashlib.sha256(packed).hexdigest(),original_capture=str(capture),full_selector_original_bytes=687,declared_closure_bytes=sum(n for r,n in units),executed_original_bytes=sum(i.size for r,n in units for i in cs.disasm(image_bytes(pe,r,n),BASE+r)if i.address in executed),units=[dict(rva=hex(r),size=n,sha256=hashlib.sha256(image_bytes(pe,r,n)).hexdigest())for r,n in units],native_fixups=[dict(operand_rva=hex(a),target_rva=hex(b),original_bytes=image_bytes(pe,a,4).hex())for a,b in fixups],selected_functions=[dict(path=p,symbol=n,sha256=hashlib.sha256(b.encode()).hexdigest())for p,n,b in selected],runner_sha256=hashlib.sha256((out/'runner.exe').read_bytes()).hexdigest(),profile='MSVC14.37 x86 /O2 /Gy /Gw /fp:precise /arch:SSE2 /DNDEBUG /DKISAK_MP /DWIN32 FPCW027f MXCSR1f80',source_head=subprocess.check_output(['git','rev-parse','HEAD']).decode().strip(),scope='Full selector and complete declared original dependency closure. Actual production structures and extracted production definitions, with real player lookup, weapon/variant getters, reserve/clip loops and held presence. Read-only fixture objects and table; sole permitted result write is cmd.weapon ushort. 8 initialized definitions including weapon0. Corpus states0..51/extremes, six target kinds, 15 held slots, reserve/clip slot placement, empty/reserve-only ammo, eligibility/class/lockon permutations and exceptional distances. No combat/AI-loop/live gameplay or universal-equivalence/byte-identity claim. Production-only demo getter diagnostics execute real selected state getter with private demo state0; unsupported diagnostics terminate.',unchanged_selector_from_base=subprocess.check_output(['git','diff','a16c447','--','src/server_mp/sv_bot_mp.cpp']).decode()=='')
report['started_at_utc']=started_at
report['tool_sha256']=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
report['selected_cpp_sha256']=hashlib.sha256((out/'selected.cpp').read_bytes()).hexdigest()
report['runner_cpp_sha256']=hashlib.sha256((out/'runner.cpp').read_bytes()).hexdigest()
report['build_command_sha256']=hashlib.sha256((out/'build.cmd').read_bytes()).hexdigest()
compiler_root=Path('C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.37.32822')
report['compiler_identities']={str(compiler_root/p):hashlib.sha256((compiler_root/p).read_bytes()).hexdigest() for p in ['bin/HostX86/x86/cl.exe','bin/HostX86/x86/link.exe']}
report['source_file_identities']={p:hashlib.sha256(Path(p).read_bytes()).hexdigest()for p,_,_ in selected}
report['original_byte_coverage_by_unit']=[dict(rva=hex(r),declared_bytes=n,executed_instruction_bytes=sum(i.size for i in cs.disasm(image_bytes(pe,r,n),BASE+r)if i.address in executed))for r,n in units]
report['candidate_instruction_similarity']='Not measured by this behavioral replay. Bot source unchanged from a16c447; base/candidate EXE comparison is recorded separately.'
report['raw_byte_identity']='No claim. Native execution relocates only the four declared 32-bit absolute operands; Unicorn executes unchanged original bytes at their original virtual addresses.'
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:report[k]for k in ['status','cases','production_mismatches','original_outputs_sha256','full_selector_original_bytes','declared_closure_bytes','executed_original_bytes']},indent=2))
sys.exit(0 if report['status']=='pass' else 1)


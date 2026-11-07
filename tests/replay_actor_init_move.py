"""Original-native/Unicorn/production Actor_InitMove state comparison.

The static visitor is already initialized; first-use CRT atexit registration is
unsupported. Both real original scalar and SSE memset routes are included.
Production virtual dispatch is a separate regression, not original GJK replay.
"""
import argparse,hashlib,json,random,re,struct,subprocess
from pathlib import Path
from capstone import Cs,CS_ARCH_X86,CS_MODE_32,CS_OP_MEM
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE,UC_HOOK_MEM_READ,UC_HOOK_MEM_WRITE
import unicorn.x86_const as x
from mp_fidelity_common import ROOT,reference,image_bytes,definition,exact_definition

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source',type=Path,default=ROOT/'src/game_mp/actor_mp.cpp')
parser.add_argument('--out',type=Path,default=ROOT/'build-re/actor-init-move')
args=parser.parse_args();out=args.out;out.mkdir(parents=True,exist_ok=True)
original,pe=reference();BASE=0x400000;ENTRY=0x1dd8c0;SIZE=543
contract=json.loads((ROOT/'tests/actor_init_move_reference.json').read_text())
assert hashlib.sha256(image_bytes(pe,ENTRY,SIZE)).hexdigest()==contract['functions'][0]['code'][0]['sha256']
units=[(ENTRY,SIZE),(0x5ca590,122),(0x5d8588,143),(0x5d8531,87)]
GUARD=0x39e3100;DUMMY=0x39e2a48;CPU=0x4fc2160;VTABLE=0xac2c00
constants=[(0x6323a8,4),(0x66e294,4),(GUARD,4),(DUMMY,0x6b8),(CPU,4)]
cs=Cs(CS_ARCH_X86,CS_MODE_32);cs.detail=True;instructions={};fixups=[]
for r,n in units:
 for i in cs.disasm(image_bytes(pe,r,n),BASE+r):
  instructions[i.address]=i
  for operand in i.operands:
   if operand.type==CS_OP_MEM and operand.mem.disp>=BASE:
    assert i.disp_size==4
    fixups.append((i.address-BASE+i.disp_offset,operand.mem.disp-BASE))
source=r'''
#include <windows.h>
#include <game_mp/actor_mp.h>
#include <physics/phys_traverse.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>
#include <set>
#include <algorithm>
#include <new>
#include <io.h>
#include <fcntl.h>
#include "tests/original_native_loader.h"
bool Assert_MyHandler(const char*,int,int,const char*,...){std::abort();}
void colgeom_visitor_t::intersect_box(float*,float*,int){std::fprintf(stderr,"BLOCKED world traversal\n");std::abort();}
void StatMon_Warning(int,int,char*){std::abort();}
'''
source+=exact_definition(ROOT/'src/physics/phys_traverse.cpp','colgeom_visitor_t::colgeom_visitor_t()')
source+=definition(ROOT/'src/universal/com_math.cpp','Vec3Max')
source+=definition(args.source,'Actor_InitMove')
fields=ROOT/'src/qcommon/msg_mp.cpp'
source+='\n'.join(definition(fields,n,data=True)for n in re.findall(r'^const NetField (\w+)\[',fields.read_text(),re.M))
source+=r'''
static_assert(offsetof(actor_physics_t,m_gjkcc_input)==0x114);
static_assert(offsetof(gentity_s,r.mins)==0xf0&&offsetof(gentity_s,r.maxs)==0xfc);
__declspec(noinline) void dispatch(colgeom_visitor_t*visitor,const CollisionAabbTree*tree){visitor->visit(tree);}
int main(int argc,char**argv){
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
 _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
 bool native=argc>1&&!strcmp(argv[1],"original");
 auto image=native?map_original("build/BlackOpsMP.exe","REGIONS","FIXUPS"):nullptr;
 if(native&&!image)return 2;
 if(native){DWORD old;for(unsigned r:{GUARD,DUMMY,CPU})VirtualProtect(image+r,4,PAGE_READWRITE,&old);*(unsigned*)(image+GUARD)=1;*(unsigned*)(image+DUMMY)=VTABLE;}
 colgeom_visitor_inlined_t<200> probe;
 fprintf(stderr,"layout actor=%u physics=%u actor_physics=%u proximity=%u visitor=%u\n",sizeof(actor_s),sizeof(actor_physics_t),offsetof(actor_s,Physics),offsetof(actor_physics_t,proximity_data),sizeof(probe));
 unsigned words[10];unsigned count=0;
 while(fread(words,sizeof(words),1,stdin)==1){
  const unsigned short control=0x027f;const unsigned mxcsr=0x1f80;
  __asm {
   fninit
   fldcw control
   ldmxcsr mxcsr
  }
  alignas(16) unsigned char memory[sizeof(actor_s)+16],entityMemory[sizeof(gentity_s)];
  auto actor=reinterpret_cast<actor_s*>(memory+8*words[8]);auto entity=reinterpret_cast<gentity_s*>(entityMemory);
  for(unsigned i=0;i<sizeof(actor_s);++i)((unsigned char*)actor)[i]=(unsigned char)(i*37+words[9]);
  for(unsigned i=0;i<sizeof(gentity_s);++i)entityMemory[i]=(unsigned char)(i*17+words[9]);
  actor->ent=entity;entity->s.number=words[0];entity->health=100;
  memcpy(entity->r.mins,words+1,12);memcpy(entity->r.maxs,words+4,12);
  auto before=std::vector<unsigned char>(entityMemory,entityMemory+sizeof(gentity_s));
  auto actorBefore=std::vector<unsigned char>((unsigned char*)actor,(unsigned char*)actor+sizeof(actor_s));
  if(native){*(unsigned*)(image+CPU)=words[7];((void(__fastcall*)(actor_s*))(image+ENTRY))(actor);}
  else Actor_InitMove(actor);
  if(memcmp(entityMemory,before.data(),before.size()))return 31;
  auto vptr=native?*(uintptr_t*)((unsigned char*)actor+0x34c):*(uintptr_t*)&actor->Physics.proximity_data;
  uintptr_t expected=native?VTABLE:*(uintptr_t*)&probe;
  if(vptr!=0&&vptr!=expected){std::fprintf(stderr,"BLOCKED unexpected vptr %p\n",(void*)vptr);return 32;}
  unsigned offset=native?0x234:offsetof(actor_s,Physics),size=native?0x7d0:sizeof(actor_physics_t);
  if(memcmp(actor,actorBefore.data(),offset)||memcmp((char*)actor+offset+size,actorBefore.data()+offset+size,sizeof(actor_s)-offset-size))return 36;
  // Current headers align hybrid_vector to 16; original actor layout differs.
  // Project every physics field into original offsets, preserving field bits.
  // No source/raw-byte identity is inferred from this logical state contract.
  std::vector<unsigned char> result(0x7d0);
  if(native)memcpy(result.data(),(char*)actor+0x234,0x7d0);
  else{
   memcpy(result.data(),&actor->Physics,0x118);
   auto&v=actor->Physics.proximity_data;unsigned char*p=result.data()+0x118;
   const hybrid_vector*vectors[]={&v.m_mn,&v.m_mx,&v.m_p0,&v.m_p1,&v.m_delta,&v.m_rvec};
   for(int i=0;i<6;++i)memcpy(p+4+16*i,vectors[i],16);
   memcpy(p+100,&v.m_radius,4);memcpy(p+104,&v.m_mask,4);memcpy(p+108,&v.m_threadInfo,4);
   memcpy(p+112,&v.nprims,4);memcpy(p+116,&v.overflow,1);memcpy(p+120,v.prims,1600);
  }
  *(unsigned*)(result.data()+0x118)=vptr==expected?1:0;
  fwrite(result.data(),result.size(),1,stdout);
  if(!native&&vptr){
   CollisionAabbTree tree{};dispatch(&actor->Physics.proximity_data,&tree);
   if(actor->Physics.proximity_data.nprims!=1||actor->Physics.proximity_data.prims[0].tree!=&tree||actor->Physics.proximity_data.prims[0].type!=0)return 33;
   Actor_InitMove(actor);
   if(*(uintptr_t*)&actor->Physics.proximity_data!=expected||actor->Physics.proximity_data.nprims||actor->Physics.proximity_data.overflow)return 34;
   dispatch(&actor->Physics.proximity_data,&tree);
   if(actor->Physics.proximity_data.nprims!=1)return 35;
  }
  ++count;
 }
}
'''
regions=units+constants
source=source.replace('REGIONS',','.join(f'{r}:{n}'for r,n in regions)).replace('FIXUPS',','.join(f'{a}:{b}'for a,b in fixups))
for name,value in [('GUARD',GUARD),('DUMMY',DUMMY),('CPU',CPU),('VTABLE',VTABLE),('ENTRY',ENTRY)]:source=source.replace(name,hex(value))
(out/'runner.cpp').write_text(source);stem=str(out/'runner')
(out/'build.cmd').write_text('@echo off\ncall "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvarsall.bat" x86 -vcvars_ver=14.37 >nul\nif errorlevel 1 exit /b %errorlevel%\ncl /nologo /std:c++20 /O2 /fp:precise /MT /DNDEBUG /DKISAK_MP /EHsc /I. /Isrc /Isrc/libs /Isrc/universal '+stem+'.cpp /Fe'+stem+'.exe /Fo'+stem+'.obj >'+stem+'-build.log 2>&1\nexit /b %errorlevel%\n')
build=subprocess.run(['cmd','/d','/c',str(out/'build.cmd')],capture_output=True)
if build.returncode:raise RuntimeError((out/'runner-build.log').read_text()[-5000:])
bits=lambda v:struct.unpack('<I',struct.pack('<f',v))[0]
cases=[]
for cpu in (0,1):
 for alignment in (0,1):
  for number in (0,1,31,1022):
   for mins,maxs in [((-16,-16,0),(16,16,72)),((-8,-12,-3),(8,12,35)),((-0.,0.,-0.),(0.,-0.,0.))]:
    cases.append((number,*(bits(v)for v in mins+maxs),cpu,alignment,0xa5))
rng=random.Random(20261107)
for _ in range(64):
 mins=tuple(rng.uniform(-100,0)for _ in range(3));maxs=tuple(rng.uniform(0,100)for _ in range(3))
 cases.append((rng.randrange(1023),*(bits(v)for v in mins+maxs),rng.randrange(2),rng.randrange(2),rng.randrange(256)))
inputs=b''.join(struct.pack('<10I',*v)for v in cases);record_size=0x7d0
native=subprocess.run([str(out/'runner.exe'),'original'],input=inputs,capture_output=True)
if native.returncode:raise RuntimeError(f'BLOCKED original {native.returncode}: {native.stderr[-500:]!r}')
expected=native.stdout;assert len(expected)==record_size*len(cases)
assert len(cases)==contract['immutable_replay']['cases']
assert hashlib.sha256(inputs).hexdigest()==contract['immutable_replay']['inputs_sha256']
assert hashlib.sha256(expected).hexdigest()==contract['immutable_replay']['original_outputs_sha256'],'BLOCKED changed original expectations'
capture=out/('original-'+hashlib.sha256(expected).hexdigest()+'.bin')
if capture.exists():assert capture.read_bytes()==expected
else:
 with capture.open('xb')as file:file.write(expected)
production=subprocess.run([str(out/'runner.exe'),'production'],input=inputs,capture_output=True)
if production.returncode:raise RuntimeError(f'BLOCKED production {production.returncode}: {production.stderr[-500:]!r}')
assert len(production.stdout)==len(expected)
mismatches=[i for i in range(len(cases))if production.stdout[i*record_size:(i+1)*record_size]!=expected[i*record_size:(i+1)*record_size]]
uc=Uc(UC_ARCH_X86,UC_MODE_32);pages=set()
def mapping(a,n):
 for page in range(a&~4095,(a+n+4095)&~4095,4096):
  if page not in pages:uc.mem_map(page,4096);pages.add(page)
for r,n in regions:mapping(BASE+r,n);uc.mem_write(BASE+r,image_bytes(pe,r,n))
DATA=0x20000000;STACK=0x30000000;SP=STACK+0x3000;RET=0x40000000
mapping(DATA,0x4000);mapping(STACK,0x4000);mapping(RET,1)
executed=set();stopped=False;reads=[];writes=[]
def code(emu,a,n,user):
 global stopped
 if a==RET:stopped=True;emu.emu_stop();return
 if a not in instructions:raise RuntimeError(f'BLOCKED code {a:x}')
 executed.add(a)
def memory(emu,access,a,n,value,user):
 if STACK<=a and a+n<=STACK+0x4000:return
 if not any(start<=a and a+n<=end for start,end in (writes if access==17 else reads)):raise RuntimeError(f'BLOCKED memory {access} {a:x}+{n}')
uc.hook_add(UC_HOOK_CODE,code);uc.hook_add(UC_HOOK_MEM_READ|UC_HOOK_MEM_WRITE,memory)
uc_mismatches=[]
for k,words in enumerate(cases):
 actor=DATA+8*words[8];entity=DATA+0x3000
 state=bytearray((i*37+words[9])&255 for i in range(0x2780));struct.pack_into('<I',state,0,entity)
 ent=bytearray((i*17+words[9])&255 for i in range(0x2f8));struct.pack_into('<I',ent,0,words[0]);ent[0xf0:0x108]=struct.pack('<6I',*words[1:7])
 uc.mem_write(actor,bytes(state));uc.mem_write(entity,bytes(ent));uc.mem_write(BASE+GUARD,struct.pack('<I',1));uc.mem_write(BASE+DUMMY,struct.pack('<I',VTABLE));uc.mem_write(BASE+CPU,struct.pack('<I',words[7]));uc.mem_write(STACK,bytes(0x4000))
 reads=[(actor,actor+0x2780),(entity,entity+len(ent))]+[(BASE+r,BASE+r+n)for r,n in regions]
 writes=[(actor+0x234,actor+0x234+0x7d0)]
 for reg in (x.UC_X86_REG_EAX,x.UC_X86_REG_EBX,x.UC_X86_REG_EDX,x.UC_X86_REG_ESI,x.UC_X86_REG_EDI,x.UC_X86_REG_EBP):uc.reg_write(reg,0)
 uc.reg_write(x.UC_X86_REG_ECX,actor);uc.reg_write(x.UC_X86_REG_ESP,SP);uc.reg_write(x.UC_X86_REG_EFLAGS,0x202);uc.reg_write(x.UC_X86_REG_FPCW,0x027f);uc.reg_write(x.UC_X86_REG_MXCSR,0x1f80)
 uc.mem_write(SP,struct.pack('<I',RET));stopped=False;uc.emu_start(BASE+ENTRY,0xffffffff,count=100000)
 assert stopped and uc.reg_read(x.UC_X86_REG_ESP)==SP+4
 result=bytearray(uc.mem_read(actor+0x234,record_size));assert struct.unpack_from('<I',result,0x118)[0]==VTABLE
 struct.pack_into('<I',result,0x118,1)
 if bytes(result)!=expected[k*record_size:(k+1)*record_size]:uc_mismatches.append(k)
 assert bytes(uc.mem_read(entity,len(ent)))==bytes(ent)
report=dict(status='pass'if not mismatches and not uc_mismatches else'mismatch',cases=len(cases),production_mismatches=mismatches,native_unicorn_mismatches=uc_mismatches,original_sha256=hashlib.sha256(original.read_bytes()).hexdigest(),original_owner_bytes=SIZE,declared_closure_bytes=sum(n for r,n in units),executed_original_bytes=sum(instructions[a].size for a in executed),original_outputs_sha256=hashlib.sha256(expected).hexdigest(),inputs_sha256=hashlib.sha256(inputs).hexdigest(),regions=[dict(rva=hex(r),size=n,sha256=hashlib.sha256(image_bytes(pe,r,n)).hexdigest())for r,n in units],source_sha256=hashlib.sha256(args.source.read_bytes()).hexdigest(),runner_sha256=hashlib.sha256((out/'runner.exe').read_bytes()).hexdigest(),production_layout=production.stderr.decode().strip(),production_virtual_visit_and_reinitialization='pass'if not mismatches else'failed state precondition',scope='Whole Actor_InitMove with already-initialized static visitor; real original scalar/SSE memset closure; all physics fields projected across header layout differences, actor bytes outside physics and entity data guarded. Production virtual visit and repeated initialization regression are separate from original replay.',exclusions='First-use static initialization/CRT atexit, diagnostics, full GJK/world collision, lifetime root cause of earlier cluster-link crash, gameplay and persistence unqualified. No raw struct layout/byte identity claim. Unsupported code/memory or world traversal terminates.')
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));raise SystemExit(report['status']!='pass')

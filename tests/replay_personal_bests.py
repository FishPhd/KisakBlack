"""Original-native/Unicorn/production personal-best row and DDL mutation replay.

Controlled valid DDL states are fixture inputs; every original callee is real.
Full server owner, lookup, diagnostics, services and gameplay remain unqualified.
"""
from pathlib import Path
import hashlib,json,random,re,struct,subprocess,sys
from capstone import Cs,CS_ARCH_X86,CS_MODE_32,CS_OP_MEM
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE,UC_HOOK_MEM_READ,UC_HOOK_MEM_WRITE
import unicorn.x86_const as x
from mp_fidelity_common import ROOT,reference,image_bytes,definition

out=ROOT/'build-re/personal-bests';out.mkdir(parents=True,exist_ok=True)
original,pe=reference();BASE=0x400000
contract=json.loads((ROOT/'tests/personal_bests_reference.json').read_text())
units=[(0x509580,256),(0x6e10,42),(0x13c590,49),(0x17420,91),(0x4ec8d0,78),
       (0xced00,84),(0x4ec700,107),(0x17abb0,165),(0x2a9b40,100)]
COUNT=0x3f165ac;LIST=0x3f02090
regions=units+[(COUNT,4),(LIST,20000)]
cs=Cs(CS_ARCH_X86,CS_MODE_32);cs.detail=True
instructions={};fixups=[]
for r,n in units:
    for i in cs.disasm(image_bytes(pe,r,n),BASE+r):
        instructions[i.address]=i
        for operand in i.operands:
            if operand.type==CS_OP_MEM and operand.mem.disp>=BASE:
                assert i.disp_size==4
                fixups.append((i.address-BASE+i.disp_offset,operand.mem.disp-BASE))
defs=[('src/ddl/ddl_converter.cpp','DDL_Converter_IsLeaf'),
      ('src/ddl/ddl_buffer.cpp','DDL_Buffer_ReadHeader'),('src/ddl/ddl_buffer.cpp','DDL_Buffer_WriteHeader'),
      ('src/ddl/ddl_buffer.cpp','DDL_Buffer_ReadBits'),('src/ddl/ddl_buffer.cpp','DDL_Buffer_WriteBits'),
      ('src/ddl/ddl_api.cpp','DDL_SetUserFlag'),('src/ddl/ddl_api.cpp','DDL_SetValueChanged'),
      ('src/ddl/ddl_api.cpp','DDL_IsStateReadyForIO'),('src/ddl/ddl_api.cpp','DDL_WriteIntValueIfChanged'),
      ('src/ddl/ddl_api.cpp','DDL_WriteIntValueInternal'),('src/ddl/ddl_api.cpp','DDL_ReadIntValueInternal'),
      ('src/ddl/ddl_api.cpp','DDL_SetInt'),('src/ddl/ddl_api.cpp','DDL_GetInt'),
      ('src/live/live_stats.cpp','LiveStats_UpdatePersonalBest')]
source=r'''
#include <windows.h>
#include <live/live_stats.h>
#include <ddl/ddl_buffer.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <set>
#include <algorithm>
#include <io.h>
#include <fcntl.h>
#include "tests/original_native_loader.h"
unsigned int s_currentPersonalBest[1];personalBest_t s_personalBests[1][1000];
bool Assert_MyHandler(const char*,int,int,const char*,...){std::abort();}
void Com_PrintError(int,const char*,...){std::abort();}
void DDL_PrintError(const char*,...){std::abort();}
void DDL_NoCheckPrintWarning(const char*,...){std::abort();}
'''
source+='\n'.join(definition(ROOT/p,n)for p,n in defs)
fields=ROOT/'src/qcommon/msg_mp.cpp'
source+='\n'.join(definition(fields,n,data=True)for n in re.findall(r'^const NetField (\w+)\[',fields.read_text(),re.M))
source+=r'''
static_assert(sizeof(personalBest_t)==20 && sizeof(ddlState_t)==16 && sizeof(ddlMemberDef_t)==48);
__declspec(naked) unsigned char invoke(void*,int,char*,char*,ddlState_t*,ddlState_t*,const char*){__asm{
 push esi
 mov edx,[esp+8]
 mov esi,[esp+28]
 push [esp+32]
 push [esp+28]
 push [esp+28]
 push [esp+28]
 push [esp+28]
 call edx
 add esp,20
 pop esi
 ret
}}
static const char*names[]={"KILLS","DEATHS","SCORE","HITS"};
static const char*prefixes[]={"","tdm"};
static unsigned identity(const char*p,const char*const*options,unsigned count){
 if((uintptr_t)p==0xa5a5a5a5u)return 0;
 for(unsigned i=0;i<count;++i)if(p==options[i])return i+1;
 std::fprintf(stderr,"BLOCKED unexpected output pointer %p\n",p);std::exit(30);
}
int main(int argc,char**argv){
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
 _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
 bool original=argc>1&&!strcmp(argv[1],"original");
 auto image=original?map_original("build/BlackOpsMP.exe","REGIONS","FIXUPS"):nullptr;
 if(original&&!image)return 2;
 if(original){DWORD old;VirtualProtect(image+COUNT,4,PAGE_READWRITE,&old);VirtualProtect(image+LIST,20000,PAGE_READWRITE,&old);}
 unsigned v[7];
 while(fread(v,sizeof(v),1,stdin)==1){
  char live[40960],old[40960];
  for(unsigned i=0;i<40960;++i)live[i]=old[i]=char((37*i+v[6])&255);
  live[8]&=~1;old[8]&=~1;
  memcpy(live+40,v,4);memcpy(old+40,v+1,4);memcpy(live+56+4*v[4],v+2,4);
  auto oldCopy=std::vector<char>(old,old+40960);
  ddlMemberDef_t member{};member.size=128;member.type=2;member.max=0xffffffffu;member.arraySize=4;member.enumIndex=0;
  ddlEnumDef_t enumeration{"PlayerStats",4,names};ddlDef_t ddl{};ddl.version=100;ddl.size=327360;ddl.enumList=&enumeration;ddl.enumCount=1;
  ddlState_t stats{0,0,&member,&ddl},bests{128+32*(int)v[4],(int)v[4],&member,&ddl};
  auto memberCopy=member;auto enumCopy=enumeration;auto ddlCopy=ddl;auto statsCopy=stats;auto bestsCopy=bests;
  personalBest_t*list=original?(personalBest_t*)(image+LIST):s_personalBests[0];
  unsigned*count=original?(unsigned*)(image+COUNT):s_currentPersonalBest;
  memset(list,0xa5,20000);*count=v[3];
  unsigned result=original?invoke(image+0x509580,0,live,old,&stats,&bests,prefixes[v[5]]):
      (unsigned char)LiveStats_UpdatePersonalBest(0,live,old,&stats,&bests,prefixes[v[5]]);
  if(memcmp(old,oldCopy.data(),40960)||memcmp(&member,&memberCopy,sizeof(member))||memcmp(&ddl,&ddlCopy,sizeof(ddl))||memcmp(&enumeration,&enumCopy,sizeof(enumeration))||memcmp(&stats,&statsCopy,sizeof(stats))||memcmp(&bests,&bestsCopy,sizeof(bests)))return 31;
  unsigned header[]={result&255,*count};fwrite(header,sizeof(header),1,stdout);
  for(int i=0;i<1000;++i){auto row=list[i];unsigned words[]={identity(row.name,names,4),identity(row.prefix,prefixes,2),(unsigned)row.value,(unsigned)row.delta,(unsigned)row.rating};fwrite(words,sizeof(words),1,stdout);}
  fwrite(live,40960,1,stdout);fwrite(old,40960,1,stdout);
 }
}
'''
source=source.replace('REGIONS',','.join(f'{r}:{n}'for r,n in regions)).replace('FIXUPS',','.join(f'{a}:{b}'for a,b in fixups)).replace('image+COUNT',f'image+{COUNT}').replace('image+LIST',f'image+{LIST}')
(out/'runner.cpp').write_text(source)
stem=str(out/'runner')
(out/'build.cmd').write_text('@echo off\ncall "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvarsall.bat" x86 -vcvars_ver=14.37 >nul\nif errorlevel 1 exit /b %errorlevel%\ncl /nologo /std:c++20 /O2 /fp:precise /MT /DNDEBUG /DKISAK_MP /EHsc /I. /Isrc /Isrc/libs /Isrc/universal '+stem+'.cpp /Fe'+stem+'.exe /Fo'+stem+'.obj >'+stem+'-build.log 2>&1\nexit /b %errorlevel%\n')
compiled=subprocess.run(['cmd','/d','/c',str(out/'build.cmd')],capture_output=True)
if compiled.returncode:raise RuntimeError((out/'runner-build.log').read_text())
pairs=[(0,0),(1,0),(9,1),(0,1),(0x80000000,0),(0xffffffff,0),(0xffffffff,0x80000000),(0x7fffffff,0xffffffff),(0x80000000,0x7fffffff)]
cases=[(a,b,c,n,index,prefix,37)for a,b in pairs for c in (0,1,0x80000000,0xffffffff)for n in (0,999)for index,prefix in ((0,0),(3,1))]
rng=random.Random(20261106)
cases += [tuple(rng.getrandbits(32)for _ in range(3))+(rng.randrange(1000),rng.randrange(4),rng.randrange(2),rng.randrange(256))for _ in range(80)]
packed=b''.join(struct.pack('<7I',*c)for c in cases);record_size=8+20000+81920
expected_run=subprocess.run([str(out/'runner.exe'),'original'],input=packed,capture_output=True)
if expected_run.returncode:raise RuntimeError(f'BLOCKED original {expected_run.returncode}: {expected_run.stderr[-500:]!r}')
expected=expected_run.stdout;assert len(expected)==len(cases)*record_size
assert len(cases)==contract['immutable_replay']['personal_best']['cases']
assert hashlib.sha256(packed).hexdigest()==contract['immutable_replay']['personal_best']['inputs_sha256']
assert hashlib.sha256(expected).hexdigest()==contract['immutable_replay']['personal_best']['original_outputs_sha256'],'BLOCKED original expectation changed'
capture=out/('original-'+hashlib.sha256(expected).hexdigest()+'.bin')
if capture.exists():assert capture.read_bytes()==expected
else:
 with capture.open('xb')as file:file.write(expected)
production=subprocess.run([str(out/'runner.exe'),'production'],input=packed,capture_output=True)
if production.returncode:raise RuntimeError(f'BLOCKED production {production.returncode}: {production.stderr[-500:]!r}')
assert len(production.stdout)==len(expected)
mismatches=[i for i in range(len(cases))if expected[i*record_size:(i+1)*record_size]!=production.stdout[i*record_size:(i+1)*record_size]]

uc=Uc(UC_ARCH_X86,UC_MODE_32);pages=set()
def mapping(a,n):
 for page in range(a&~4095,(a+n+4095)&~4095,4096):
  if page not in pages:uc.mem_map(page,4096);pages.add(page)
for r,n in regions:mapping(BASE+r,n);uc.mem_write(BASE+r,image_bytes(pe,r,n))
DATA=0x20000000;STACK=0x30000000;SP=STACK+0x3000;RET=0x40000000
mapping(DATA,0x18000);mapping(STACK,0x4000);mapping(RET,1)
live=DATA+0x1000;old=DATA+0xb000;member=DATA;ddl=DATA+0x100;enum=DATA+0x200;nameptrs=DATA+0x300;stats=DATA+0x400;bests=DATA+0x500
names=[DATA+0x600+i*16 for i in range(4)];prefixes=[DATA+0x700,DATA+0x710]
reads=[];writes=[];executed=set();stopped=False
def code(emu,a,n,user):
 global stopped
 if a==RET:stopped=True;emu.emu_stop();return
 if a not in instructions:raise RuntimeError(f'BLOCKED original code {a:x}')
 executed.add(a)
def memory(emu,access,a,n,value,user):
 if STACK<=a and a+n<=STACK+0x4000:return
 allowed=writes if access==17 else reads
 if not any(start<=a and a+n<=end for start,end in allowed):raise RuntimeError(f'BLOCKED original memory {access} {a:x}+{n}')
uc.hook_add(UC_HOOK_CODE,code);uc.hook_add(UC_HOOK_MEM_READ|UC_HOOK_MEM_WRITE,memory)
emulator_mismatches=[]
for k,(cur,last,best,count,index,prefix,seed)in enumerate(cases):
 uc.mem_write(STACK,bytes(0x4000));uc.mem_write(BASE+COUNT,struct.pack('<I',count));uc.mem_write(BASE+LIST,b'\xa5'*20000)
 b=bytearray((37*i+seed)&255 for i in range(40960));b[8]&=~1
 before=bytearray(b);struct.pack_into('<I',before,40,last);struct.pack_into('<I',b,40,cur);struct.pack_into('<I',b,56+4*index,best)
 objects=[(live,bytes(b)),(old,bytes(before)),(member,struct.pack('<12I',0,128,0,2,0,0,0xffffffff,0,0,4,0,0)),
          (ddl,struct.pack('<7I',100,327360,0,0,enum,1,0)),(enum,struct.pack('<3I',0,4,nameptrs)),
          (nameptrs,struct.pack('<4I',*names)),(stats,struct.pack('<4I',0,0,member,ddl)),(bests,struct.pack('<4I',128+32*index,index,member,ddl))]
 objects += list(zip(names,[b'KILLS\0',b'DEATHS\0',b'SCORE\0',b'HITS\0']))+[(prefixes[0],b'\0'),(prefixes[1],b'tdm\0')]
 for a,b in objects:uc.mem_write(a,b)
 reads=[(a,a+len(b))for a,b in objects]+[(BASE+r,BASE+r+n)for r,n in regions]
 writes=[(live,live+40960),(BASE+COUNT,BASE+COUNT+4),(BASE+LIST,BASE+LIST+20000)]
 for reg in (x.UC_X86_REG_EAX,x.UC_X86_REG_EBX,x.UC_X86_REG_ECX,x.UC_X86_REG_EDX,x.UC_X86_REG_EDI,x.UC_X86_REG_EBP):uc.reg_write(reg,0)
 uc.reg_write(x.UC_X86_REG_ESI,bests);uc.reg_write(x.UC_X86_REG_ESP,SP);uc.reg_write(x.UC_X86_REG_EFLAGS,0x202);uc.reg_write(x.UC_X86_REG_FPCW,0x027f);uc.reg_write(x.UC_X86_REG_MXCSR,0x1f80)
 uc.mem_write(SP,struct.pack('<6I',RET,0,live,old,stats,prefixes[prefix]));stopped=False
 uc.emu_start(BASE+0x509580,0xffffffff,count=100000)
 assert stopped and uc.reg_read(x.UC_X86_REG_ESP)==SP+4
 result=struct.pack('<2I',uc.reg_read(x.UC_X86_REG_EAX)&255,struct.unpack('<I',uc.mem_read(BASE+COUNT,4))[0])
 rows=bytearray(uc.mem_read(BASE+LIST,20000))
 for i in range(1000):
  name,pref=struct.unpack_from('<2I',rows,20*i)
  assert name==0xa5a5a5a5 or name in names
  assert pref==0xa5a5a5a5 or pref in prefixes
  struct.pack_into('<2I',rows,20*i,0 if name==0xa5a5a5a5 else names.index(name)+1,0 if pref==0xa5a5a5a5 else prefixes.index(pref)+1)
 result+=bytes(rows)+bytes(uc.mem_read(live,40960))+bytes(uc.mem_read(old,40960))
 if result!=expected[k*record_size:(k+1)*record_size]:emulator_mismatches.append(k)
 for a,b in objects:
  if a!=live:assert bytes(uc.mem_read(a,len(b)))==b
report=dict(status='pass'if not mismatches and not emulator_mismatches else'mismatch',original_sha256=hashlib.sha256(original.read_bytes()).hexdigest(),cases=len(cases),production_mismatches=mismatches,native_unicorn_mismatches=emulator_mismatches,
 original_outputs_sha256=hashlib.sha256(expected).hexdigest(),inputs_sha256=hashlib.sha256(packed).hexdigest(),original_owner_bytes=256,declared_closure_bytes=sum(n for r,n in units),executed_original_bytes=sum(instructions[a].size for a in executed),
 regions=[dict(rva=hex(r),size=n,sha256=hashlib.sha256(image_bytes(pe,r,n)).hexdigest())for r,n in units],
 source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest()for p,n in defs},runner_sha256=hashlib.sha256((out/'runner.exe').read_bytes()).hexdigest(),
 scope='Complete personal-best helper with real original integer DDL dependencies over valid controlled DDL states. All 1000 rows and both 40960-byte buffers compared; output pointers normalized by exact fixture identity. Nonzero unsigned deltas, prefix/name, row value/delta, stable rating and unsigned best comparison covered.',
 exclusions='No full server owner, stock lookup, diagnostics/cap-exceeded branch, service, gameplay, XP awards, persistence, instruction similarity or byte identity qualification. Unsupported dependencies/accesses terminate.')
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items()if k not in ('regions','source_sha256')},indent=2))
raise SystemExit(0 if report['status']=='pass'else 1)

"""Compare the original accuracy arithmetic inline region with production.

This does not replay the full accuracy owner or its lookup dependencies. Fixture
bindings supply operand inputs to the extracted production owner; no bindings
replace original instructions/callees. Native inline execution ends at a declared
harness RET immediately before the original owner's next lookup call.
"""
import hashlib,json,random,re,struct,subprocess
from capstone import Cs,CS_ARCH_X86,CS_MODE_32
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE,UC_HOOK_MEM_READ,UC_HOOK_MEM_WRITE
import unicorn.x86_const as x
from mp_fidelity_common import ROOT,reference,image_bytes,definition

out=ROOT/'build-re/match-accuracy';out.mkdir(parents=True,exist_ok=True)
original,pe=reference();BASE=0x400000;entry=0x50b07d;end=0x50b0d7
contract=json.loads((ROOT/'tests/personal_bests_reference.json').read_text())['immutable_replay']['accuracy_inline']
assert hashlib.sha256(image_bytes(pe,entry,end-entry)).hexdigest()==contract['region']['sha256']
units=[(entry,end-entry)];constants=[(0x68a7d4,4),(0x64c170,4)]
cs=Cs(CS_ARCH_X86,CS_MODE_32);cs.detail=True
instructions={i.address:i for i in cs.disasm(image_bytes(pe,entry,end-entry),BASE+entry)}
fixups=[(i.address-BASE+i.disp_offset,o.mem.disp-BASE)for i in instructions.values()for o in i.operands if o.type==3 and o.mem.disp>=BASE]
source=r'''
#include <windows.h>
#include <live/live_stats.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <set>
#include <algorithm>
#include <io.h>
#include <fcntl.h>
#include <float.h>
#include "tests/original_native_loader.h"
static char live[1],old[1];static unsigned hits,shots,stored;
bool Assert_MyHandler(const char*,int,int,const char*,...){std::abort();}
unsigned LiveStats_GetPlayerStat(const char*list,const char*name,char*buffer){
 if(!strcmp(list,"PlayerStatsList")){if(!strcmp(name,"TOTAL_SHOTS"))return buffer==live?shots:0;if(!strcmp(name,"HITS"))return buffer==live?hits:0;}
 if(!strcmp(list,"HighestStats")&&!strcmp(name,"HIGHEST_ACCURACY")&&buffer==old)return 0;
 std::abort();
}
void LiveStats_SetPlayerStat(const char*list,const char*name,char*buffer,unsigned value){if(strcmp(list,"HighestStats")||strcmp(name,"HIGHEST_ACCURACY")||buffer!=live)std::abort();stored=value;}
'''+definition(ROOT/'src/live/live_stats.cpp','LiveStats_UpdatetBestMatchAccuracy')
fields=ROOT/'src/qcommon/msg_mp.cpp'
source+='\n'.join(definition(fields,n,data=True)for n in re.findall(r'^const NetField (\w+)\[',fields.read_text(),re.M))
source+=r'''
__declspec(naked) unsigned invoke(void*,unsigned,unsigned){__asm{
 push ebx
 push ebp
 push esi
 sub esp,32
 mov edx,[esp+48]
 mov eax,[esp+52]
 mov ebx,[esp+56]
 call edx
 mov eax,ebx
 add esp,32
 pop esi
 pop ebp
 pop ebx
 ret
}}
int main(){
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);_setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
 auto image=map_original("build/BlackOpsMP.exe","REGIONS","FIXUPS");if(!image)return 2;
 DWORD oldProtect;VirtualProtect(image+END,1,PAGE_EXECUTE_READWRITE,&oldProtect);image[END]=0xc3;FlushInstructionCache(GetCurrentProcess(),image+END,1);
 unsigned values[2];unsigned short cw=0x027f;
 while(fread(values,sizeof(values),1,stdin)==1){hits=values[0];shots=values[1];stored=0;__asm fldcw cw
 unsigned native=invoke(image+ENTRY,hits,shots);__asm fldcw cw
 LiveStats_UpdatetBestMatchAccuracy(live,old);unsigned output[]={native,stored};fwrite(output,sizeof(output),1,stdout);}
}
'''
source=source.replace('REGIONS',','.join(f'{r}:{n}'for r,n in [(entry,end-entry+1)]+constants)).replace('FIXUPS',','.join(f'{a}:{b}'for a,b in fixups)).replace('END',str(end)).replace('ENTRY',str(entry))
(out/'runner.cpp').write_text(source);stem=str(out/'runner')
(out/'build.cmd').write_text('@echo off\ncall "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvarsall.bat" x86 -vcvars_ver=14.37 >nul\nif errorlevel 1 exit /b %errorlevel%\ncl /nologo /std:c++20 /O2 /fp:precise /MT /DNDEBUG /DKISAK_MP /EHsc /I. /Isrc /Isrc/libs /Isrc/universal '+stem+'.cpp /Fe'+stem+'.exe /Fo'+stem+'.obj >'+stem+'-build.log 2>&1\nexit /b %errorlevel%\n')
build=subprocess.run(['cmd','/d','/c',str(out/'build.cmd')],capture_output=True)
if build.returncode:raise RuntimeError((out/'runner-build.log').read_text())
values=[0,1,2,429496,429497,450000,0x7fffffff,0x80000000,0xffffffff]
cases=[(a,b)for a in values for b in values if b]+[(450000,500000)]
rng=random.Random(20261106);cases += [(rng.getrandbits(32),rng.randrange(1,0x100000000))for _ in range(128)]
inputs=b''.join(struct.pack('<2I',*c)for c in cases)
run=subprocess.run([str(out/'runner.exe')],input=inputs,capture_output=True)
if run.returncode:raise RuntimeError(f'BLOCKED native {run.returncode} {run.stderr[-300:]!r}')
rows=list(struct.iter_unpack('<2I',run.stdout));assert len(rows)==len(cases)
expected=b''.join(struct.pack('<I',r[0])for r in rows);capture=out/('original-'+hashlib.sha256(expected).hexdigest()+'.bin')
assert len(cases)==contract['cases']
assert hashlib.sha256(expected).hexdigest()==contract['original_outputs_sha256'],'BLOCKED original expectation changed'
if capture.exists():assert capture.read_bytes()==expected
else:
 with capture.open('xb')as file:file.write(expected)
uc=Uc(UC_ARCH_X86,UC_MODE_32);pages=set()
for r,n in units+constants:
 for page in range((BASE+r)&~4095,(BASE+r+n+4095)&~4095,4096):
  if page not in pages:uc.mem_map(page,4096);pages.add(page)
 uc.mem_write(BASE+r,image_bytes(pe,r,n))
STACK=0x20000000;SP=STACK+0x800;uc.mem_map(STACK,0x1000);executed=set();stopped=False
def code(emu,a,n,user):
 global stopped
 if a==BASE+end:stopped=True;emu.emu_stop();return
 if a not in instructions:raise RuntimeError(f'BLOCKED code {a:x}')
 executed.add(a)
def memory(emu,access,a,n,value,user):
 if STACK<=a and a+n<=STACK+0x1000:return
 if access!=17 and any(BASE+r<=a and a+n<=BASE+r+size for r,size in constants):return
 raise RuntimeError(f'BLOCKED memory {access} {a:x}+{n}')
uc.hook_add(UC_HOOK_CODE,code);uc.hook_add(UC_HOOK_MEM_READ|UC_HOOK_MEM_WRITE,memory)
for (hits,shots),row in zip(cases,rows):
 uc.mem_write(STACK,bytes(0x1000));uc.reg_write(x.UC_X86_REG_ESP,SP);uc.reg_write(x.UC_X86_REG_EAX,hits);uc.reg_write(x.UC_X86_REG_EBX,shots);uc.reg_write(x.UC_X86_REG_EBP,0)
 uc.reg_write(x.UC_X86_REG_EFLAGS,0x202);uc.reg_write(x.UC_X86_REG_FPCW,0x027f);uc.reg_write(x.UC_X86_REG_MXCSR,0x1f80);stopped=False
 uc.emu_start(BASE+entry,0xffffffff,count=1000);assert stopped
 assert uc.reg_read(x.UC_X86_REG_EBX)==row[0]
 assert uc.reg_read(x.UC_X86_REG_FPCW)==0x027f
mismatches=[i for i,(a,b)in enumerate(rows)if a!=b]
report=dict(status='pass'if not mismatches else'mismatch',original_sha256=hashlib.sha256(original.read_bytes()).hexdigest(),cases=len(cases),production_mismatches=mismatches,native_unicorn_mismatches=0,original_inline_bytes=end-entry,executed_original_bytes=sum(instructions[a].size for a in executed),original_outputs_sha256=hashlib.sha256(expected).hexdigest(),inputs_sha256=hashlib.sha256(inputs).hexdigest(),runner_sha256=hashlib.sha256((out/'runner.exe').read_bytes()).hexdigest(),
 region=dict(rva=hex(entry),size=end-entry,sha256=hashlib.sha256(image_bytes(pe,entry,end-entry)).hexdigest()),native_harness_ret_rva=hex(end),source_sha256=hashlib.sha256((ROOT/'src/live/live_stats.cpp').read_bytes()).hexdigest(),
 scope='Original unsigned conversion/multiply/divide/FISTP64/low-DWORD arithmetic inline region only; production lookup bindings supply operands. Full accuracy owner, original lookup, gameplay and services remain unqualified. Harness RET replaces the following call outside the declared inline region; Unicorn stops at that boundary.')
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));raise SystemExit(bool(mismatches))

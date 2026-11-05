"""Compare original-native, Unicorn and production DDL ownership integer IO.

Run from the repository root with build-re/venv/Scripts/python.exe.
Requires VS2022 x86 tools; --vcvars can select another pinned installation.
Original captures are content-addressed under ignored build-re/cac-ddl-io/.
This qualifies IO at stock schema offsets, not original lookup/CAC callers,
service writes, respawn or complete gameplay. Diagnostics block this domain.
"""
from pathlib import Path
import sys,json,hashlib,struct,subprocess,argparse
from mp_fidelity_common import reference,image_bytes,definition
from capstone import Cs,CS_ARCH_X86,CS_MODE_32
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE,UC_HOOK_MEM_READ,UC_HOOK_MEM_WRITE
import unicorn.x86_const as x

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--schema',type=Path,default=Path('tests/cac_ownership_reference.json'))
parser.add_argument('--out',type=Path,default=Path('build-re/cac-ddl-io'))
parser.add_argument('--vcvars',type=Path,default=Path('C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars32.bat'))
args=parser.parse_args()
out=args.out;out.mkdir(parents=True,exist_ok=True)
schema=json.loads(args.schema.read_text())
schema=schema.get('stock_schema',schema)
ref,pe=reference()
fixture=json.loads(args.schema.read_text())
assert fixture.get('reference_sha256',hashlib.sha256(ref.read_bytes()).hexdigest())==hashlib.sha256(ref.read_bytes()).hexdigest()
regions=[(0x6e10,42),(0x13c590,49),(0x17420,91),(0x4ec8d0,78),
         (0xced00,84),(0x4ec700,107),(0x17abb0,165),(0x2a9b40,100)]
cs=Cs(CS_ARCH_X86,CS_MODE_32)
region_arg=','.join(f'{r}:{n}' for r,n in regions)
loader=Path('tests/original_native_loader.h').read_text()
source=r'''
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <set>
#include <algorithm>
#include <io.h>
#include <fcntl.h>
#include <ddl/ddl_api.h>
#include <ddl/ddl_buffer.h>
static_assert(sizeof(ddlState_t)==16 && sizeof(ddlMemberDef_t)==48 && sizeof(ddlDef_t)==28);
bool Assert_MyHandler(const char*,int,int,const char*,...){std::abort();}
void DDL_PrintError(const char*,...){std::abort();}
void DDL_NoCheckPrintWarning(const char*,...){std::abort();}
'''+loader
defs=[('src/ddl/ddl_converter.cpp','DDL_Converter_IsLeaf'),
      ('src/ddl/ddl_buffer.cpp','DDL_Buffer_ReadHeader'),
      ('src/ddl/ddl_buffer.cpp','DDL_Buffer_WriteHeader'),
      ('src/ddl/ddl_buffer.cpp','DDL_Buffer_ReadBits'),
      ('src/ddl/ddl_buffer.cpp','DDL_Buffer_WriteBits'),
      ('src/ddl/ddl_api.cpp','DDL_SetUserFlag'),
      ('src/ddl/ddl_api.cpp','DDL_SetValueChanged'),
      ('src/ddl/ddl_api.cpp','DDL_IsStateReadyForIO'),
      ('src/ddl/ddl_api.cpp','DDL_WriteIntValueIfChanged'),
      ('src/ddl/ddl_api.cpp','DDL_WriteIntValueInternal'),
      ('src/ddl/ddl_api.cpp','DDL_ReadIntValueInternal'),
      ('src/ddl/ddl_api.cpp','DDL_SetInt'),('src/ddl/ddl_api.cpp','DDL_GetInt')]
for p,s in defs:source+='\n'+definition(Path(p),s)+'\n'
source+=r'''
static int fault(EXCEPTION_POINTERS*p){std::fprintf(stderr,"BLOCKED fault %08x at %p\n",p->ExceptionRecord->ExceptionCode,p->ExceptionRecord->ExceptionAddress);return EXCEPTION_EXECUTE_HANDLER;}
static unsigned get(void*fn,ddlState_t*s,char*b){__try{return ((unsigned(__cdecl*)(ddlState_t*,char*))fn)(s,b);}__except(fault(GetExceptionInformation())){std::exit(20);}}
static unsigned set(void*fn,ddlState_t*s,char*b,unsigned v){__try{return ((bool(__cdecl*)(ddlState_t*,unsigned,char*))fn)(s,v,b);}__except(fault(GetExceptionInformation())){std::exit(20);}}
int main(int argc,char**argv){
SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
_setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
bool original=argc>1 && !strcmp(argv[1],"original");
auto image=original?map_original("build/BlackOpsMP.exe","REGIONS",""):nullptr;
if(original&&!image)return 2;
unsigned in[7];
while(fread(in,sizeof(in),1,stdin)==1){
char b[40960];for(unsigned i=0;i<sizeof(b);++i)b[i]=char((i*37+in[6])&255);b[8]&=~1;
ddlMemberDef_t m{};m.size=in[1]*in[2];m.type=2;m.arraySize=in[2];m.min=in[3];m.max=in[4];
ddlDef_t d{};d.version=SCHEMA_VERSION;d.size=SCHEMA_BITS;ddlState_t s{};s.absoluteOffset=in[0];s.arrayIndex=-1;s.member=&m;s.ddl=&d;
auto originalMember=m;auto originalDef=d;auto originalState=s;
unsigned result[3];result[0]=original?get(image+0x6e10,&s,b):DDL_GetInt(&s,b);
result[1]=original?set(image+0x13c590,&s,b,in[5]):DDL_SetInt(&s,in[5],b);
result[2]=original?get(image+0x6e10,&s,b):DDL_GetInt(&s,b);
if(memcmp(&m,&originalMember,sizeof(m))||memcmp(&d,&originalDef,sizeof(d))||memcmp(&s,&originalState,sizeof(s)))return 21;
fwrite(result,sizeof(result),1,stdout);fwrite(b,sizeof(b),1,stdout);
}
}
'''.replace('REGIONS',region_arg).replace('SCHEMA_VERSION',str(schema['version'])).replace('SCHEMA_BITS',str(schema['size']))
(out/'ddl_io_probe.cpp').write_text(source)
cmd='@echo off\ncall "'+str(args.vcvars)+'" -vcvars_ver=14.37 >nul\nif errorlevel 1 exit /b %errorlevel%\nwhere cl >"'+str(out/'compiler-path.txt')+'"\nwhere link >"'+str(out/'linker-path.txt')+'"\ncl /nologo /O2 /fp:precise /std:c++20 /MT /EHsc /DNDEBUG /Isrc "'+str(out/'ddl_io_probe.cpp')+'" /Fe"'+str(out/'ddl_io_probe.exe')+'" /Fo"'+str(out/'ddl_io_probe.obj')+'" >"'+str(out/'ddl_io_build.log')+'" 2>&1\nexit /b %errorlevel%\n'
(out/'ddl_io_build.cmd').write_text(cmd)
r=subprocess.run(['cmd','/d','/c',str(out/'ddl_io_build.cmd')],capture_output=True)
if r.returncode:print((out/'ddl_io_build.log').read_text());raise SystemExit(2)
structs=schema['structs']
def getstruct(index):return structs[str(index)] if isinstance(structs,dict) else structs[index]
root=getstruct(0)
def find(s,name):return next(m for m in s['members'] if m[0]==name)
cases=[];paths=[]
for parent,indices,leaves in [('itemstats',(0,1,63,255),('purchased','new')),
                              ('weaponstats',(0,1,63),('purchasedattachments','purchasedattachpoints','purchasedoptions'))]:
 owner=find(root,parent); child=getstruct(owner[4]);stride=owner[1]//owner[9]
 for index in indices:
  for leaf in leaves:
   m=find(child,leaf);bits=m[1]//m[9]
   # Exercise start, adjacent and last members while preserving the actual stride.
   for subindex in sorted({0,min(1,m[9]-1),m[9]-1}):
    offset=owner[2]+index*stride+m[2]+subindex*bits
    assert m[3]<=2 and 1<=bits<=32 and offset+bits<=schema['size']
    paths.append(f'{parent}/{index}/{leaf}/{subindex}')
    for seed in (0,37):
     for value in sorted({m[5],m[6]}):cases.append((offset,bits,m[9],m[5],m[6],value,seed))
requests=b''.join(struct.pack('<7I',*case) for case in cases)
exe=str((out/'ddl_io_probe.exe').resolve())
original=subprocess.run([exe,'original'],input=requests,capture_output=True)
if original.returncode:print(original.stderr.decode());raise SystemExit(3)
expected=original.stdout; record_size=40972
assert len(expected)==len(cases)*record_size
# Expected bytes never come from production, and existing captures cannot change.
capture=out/('original-'+hashlib.sha256(expected).hexdigest()+'.bin')
if capture.exists():assert capture.read_bytes()==expected,'Original capture integrity failure'
else:
 with capture.open('xb') as stream:stream.write(expected)
actual=subprocess.run([exe,'production'],input=requests,capture_output=True,check=True).stdout
assert len(actual)==len(expected)
prod_mismatches=[i for i in range(len(cases)) if actual[i*record_size:(i+1)*record_size]!=expected[i*record_size:(i+1)*record_size]]

BASE=0x400000;DATA=0x20000000;STACK=0x30000000;SP=STACK+0x3000;RET=0x40000000
u=Uc(UC_ARCH_X86,UC_MODE_32);pages=set();code=set()
instruction_sizes={}
for r,n in regions:
 for a in range((BASE+r)&~4095,(BASE+r+n+4095)&~4095,4096):pages.add(a)
 for i in cs.disasm(image_bytes(pe,r,n),BASE+r):code.add(i.address);instruction_sizes[i.address]=i.size
for a in pages:u.mem_map(a,4096)
for r,n in regions:u.mem_write(BASE+r,image_bytes(pe,r,n))
u.mem_map(DATA,0x20000);u.mem_map(STACK,0x4000);u.mem_map(RET,4096)
state=DATA;member=DATA+0x100;ddl=DATA+0x200;buffer=DATA+0x1000;executed=set()
def trace(uc,a,n,user):
 if a==RET:uc.emu_stop();return
 if a not in code:raise RuntimeError(f'Blocked undeclared code {a:x}')
 executed.add(a)
def memory(uc,access,a,n,v,user):
 if STACK<=a and a+n<=STACK+0x4000:return
 if state<=a and a+n<=state+16 and access!=17:return
 if member<=a and a+n<=member+48 and access!=17:return
 if ddl<=a and a+n<=ddl+28 and access!=17:return
 if buffer<=a and a+n<=buffer+40960:return
 raise RuntimeError(f'Blocked memory {access} {a:x}+{n}')
u.hook_add(UC_HOOK_CODE,trace);u.hook_add(UC_HOOK_MEM_READ|UC_HOOK_MEM_WRITE,memory)
def call(r,args):
 for reg in (x.UC_X86_REG_EAX,x.UC_X86_REG_EBX,x.UC_X86_REG_ECX,x.UC_X86_REG_EDX,x.UC_X86_REG_ESI,x.UC_X86_REG_EDI,x.UC_X86_REG_EBP):u.reg_write(reg,0)
 u.reg_write(x.UC_X86_REG_EFLAGS,2);u.reg_write(x.UC_X86_REG_ESP,SP)
 u.mem_write(SP,struct.pack('<'+'I'*(len(args)+1),RET,*args));u.emu_start(BASE+r,RET,count=100000)
 if u.reg_read(x.UC_X86_REG_EIP)!=RET:raise RuntimeError('Blocked: instruction budget exhausted')
 return u.reg_read(x.UC_X86_REG_EAX)
emu_mismatches=[]
for index,(offset,bits,array,minv,maxv,value,seed) in enumerate(cases):
 u.mem_write(STACK,b'\0'*0x4000)
 b=bytearray((i*37+seed)&255 for i in range(40960));b[8]&=~1
 u.mem_write(buffer,bytes(b));u.mem_write(state,struct.pack('<4I',offset,0xffffffff,member,ddl))
 u.mem_write(member,struct.pack('<12I',0,bits*array,0,2,0,minv,maxv,0,0,array,0xffffffff,0))
 u.mem_write(ddl,struct.pack('<7I',schema['version'],schema['size'],0,0,0,0,0))
 a=call(0x6e10,[state,buffer]);success=call(0x13c590,[state,value,buffer])&255;c=call(0x6e10,[state,buffer])
 result=struct.pack('<3I',a,success,c)+bytes(u.mem_read(buffer,40960))
 if result!=expected[index*record_size:(index+1)*record_size]:emu_mismatches.append(index)
report=dict(status='pass' if not prod_mismatches and not emu_mismatches else 'mismatch',cases=len(cases),
 native_unicorn_mismatches=emu_mismatches,production_mismatches=prod_mismatches,
 original_bytes=sum(n for r,n in regions),original_sha256=hashlib.sha256(ref.read_bytes()).hexdigest(),
 executed_original_instructions=len(executed),executed_original_code_bytes=sum(instruction_sizes[a] for a in executed),
 fixture_inputs_sha256=hashlib.sha256(requests).hexdigest(),
 capture_sha256=hashlib.sha256(expected).hexdigest(),runner_sha256=hashlib.sha256(Path(exe).read_bytes()).hexdigest(),
 capture=str(capture),schema_sha256=hashlib.sha256(args.schema.read_bytes()).hexdigest(),stock_paths=paths,
 probe_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),vcvars=str(args.vcvars),
 toolchain={name:dict(path=(path:=(out/(name+'-path.txt')).read_text().splitlines()[0]),sha256=hashlib.sha256(Path(path).read_bytes()).hexdigest()) for name in ('compiler','linker')},
 build_flags='/O2 /fp:precise /std:c++20 /MT /EHsc /DNDEBUG',
 loader_source_sha256=hashlib.sha256(Path('tests/original_native_loader.h').read_bytes()).hexdigest(),
 source_sha256={p:hashlib.sha256(Path(p).read_bytes()).hexdigest() for p,s in defs},
 regions=[dict(rva=hex(r),size=n,sha256=hashlib.sha256(image_bytes(pe,r,n)).hexdigest()) for r,n in regions],
 coverage='Complete original DDL_GetInt/SetInt and valid integer IO dependency paths, all 40960 buffer bytes compared at selected stock ownership offsets. Python selects offsets from stock schema metadata; original MoveTo and CAC callers are not replay-qualified.',
 exclusions='Out-of-range writes, invalid states, diagnostics and undeclared dependencies block; no service, filesystem, MoveTo, spawn or gameplay qualification.')
(out/'ddl_io_report.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
raise SystemExit(0 if report['status']=='pass' else 1)

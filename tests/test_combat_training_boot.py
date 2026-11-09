"""Native launch-policy checks; not an original-native gameplay replay.

Select the production helpers with explicit frontend/command context bindings.
Stock menu assets own the difficulty UI and its difficulty values. Full boot,
menu interaction, match entry and play require the separate native smoke.
"""
import hashlib,json,re,subprocess,zlib
from pathlib import Path
from mp_fidelity_common import ROOT,definition,reference

original,_=reference()
authority=json.loads((ROOT/'tests/combat_training_frontend_reference.json').read_text())
assert hashlib.sha256(original.read_bytes()).hexdigest()==authority['original_sha256']
for asset in authority['assets']:
 raw=(ROOT/asset['fastfile']).read_bytes()
 assert hashlib.sha256(raw).hexdigest()==asset['sha256'],'BLOCKED stock UI asset identity'
 inflated=zlib.decompress(raw[12:])
 assert hashlib.sha256(inflated).hexdigest()==asset['inflated_sha256']
 for action in asset['actions']:
  data=inflated[action['offset']:action['offset']+action['size']]
  assert hashlib.sha256(data).hexdigest()==action['sha256']
  assert data==action['text'].encode()+b'\0'
 actions={action['kind']:action for action in asset['actions'] if action['kind']!='difficulty_choice'}
 assert 'execNow if ( ! dvarbool ( "xblive_basictraining_popup" ) ) openmenu "basictraining_msg" ;' in actions['lobby_on_open']['text']
 assert actions['first_use_accept']['text']=='setDvar xblive_basictraining_popup "1" ; open basictraining_difficulty ;'

out=ROOT/'build-re/combat-training-boot';out.mkdir(parents=True,exist_ok=True)
source=r'''
#include <qcommon/common.h>
#include <client/cl_main.h>
#include <ui/ui_shared.h>
#include <ui/ui_main.h>
#include <universal/q_shared.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#undef assert
#define assert(condition) do{if(!(condition))std::abort();}while(0)
static dvar_t server{},dedicatedSetting{};const dvar_t *com_sv_running=&server,*dedicated=&dedicatedSetting;
static uiInfo_s info{};static menuDef_t menu{};
int com_numConsoleLines;char *com_consoleLines[32];
static bool s_combatTrainingBootPending=true,acknowledged=false,playing=false;
static connstate_t state=CA_DISCONNECTED;
static std::string error,mod,commands;static const char*missing=nullptr;
static std::vector<std::string>tokens,lines;static int nesting=-1,checks=0;
bool Assert_MyHandler(const char*,int,int,const char*,...){std::abort();}
int I_stricmp(const char*a,const char*b){return _stricmp(a,b);}
void Cmd_TokenizeString(const char*text){assert(nesting==-1);++nesting;tokens.clear();std::istringstream input(text);std::string token;while(input>>std::quoted(token))tokens.push_back(token);}
void Cmd_EndTokenizedString(){assert(nesting==0);--nesting;tokens.clear();}
int Cmd_Argc(){assert(nesting==0);return (int)tokens.size();}
const char*Cmd_Argv(int n){assert(nesting==0);return n<(int)tokens.size()?tokens[n].c_str():"";}
connstate_t CL_GetLocalClientConnectionState(int n){assert(!n);return state;}
bool Demo_IsPlaying(){return playing;}
const char*Dvar_GetString(const char*n){if(!strcmp(n,"com_errorMessage"))return error.c_str();if(!strcmp(n,"fs_game"))return mod.c_str();std::abort();}
bool Dvar_GetBool(const char*n){assert(!strcmp(n,"xblive_basictraining_popup"));return acknowledged;}
uiInfo_s*UI_GetInfo(int n){assert(!n);return &info;}
menuDef_t*Menus_FindByName(const UiContext*dc,const char*n){assert(dc==&info.uiDC);assert(!strcmp(n,"menu_xboxlive_basictraining")||!strcmp(n,"basictraining_difficulty")||!strcmp(n,"basictraining_msg"));return missing&&!strcmp(n,missing)?nullptr:&menu;}
void Cbuf_AddText(int n,const char*text){assert(!n);commands+=text;}
'''
path=ROOT/'src/qcommon/common.cpp'
source+='\n'+definition(path,'Com_HasExplicitFrontendStartup')
source+='\n'+definition(path,'Com_OpenCombatTrainingOnBoot')
fields=ROOT/'src/qcommon/msg_mp.cpp'
source+='\n'.join(definition(fields,n,data=True)for n in re.findall(r'^const NetField (\w+)\[',fields.read_text(),re.M))
source+=r'''
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
void reset(std::initializer_list<const char*>args={}){
 s_combatTrainingBootPending=true;acknowledged=false;playing=false;state=CA_DISCONNECTED;
 server.current.enabled=false;dedicatedSetting.current.integer=0;error.clear();mod.clear();commands.clear();missing=nullptr;
 lines.clear();for(auto arg:args)lines.push_back(arg);com_numConsoleLines=(int)lines.size();for(int i=0;i<com_numConsoleLines;++i)com_consoleLines[i]=lines[i].data();assert(nesting==-1);
}
int main(){
 reset();Com_OpenCombatTrainingOnBoot();
 CHECK(!s_combatTrainingBootPending&&commands.find("openmenu menu_xboxlive_basictraining\n")!=std::string::npos);
 CHECK(commands.find("openmenu basictraining_difficulty\n")==std::string::npos);
 CHECK(commands.find("onlinegame 1\n")<commands.find("xblive_basictraining 1\n"));
 CHECK(commands.find("exec default_xboxlive.cfg\nexec dvar_defaults.cfg\n")!=std::string::npos);
 CHECK(commands.find("set ui_mapname mp_mountain\nset ui_gametype tdm\n")!=std::string::npos);
 CHECK(commands.find("bot_difficulty")==std::string::npos);
 const auto once=commands;Com_OpenCombatTrainingOnBoot();CHECK(commands==once);
 reset({"","set fs_h \"D:/test profile\"","set logfile 2","seta r_fullscreen 0"});acknowledged=true;Com_OpenCombatTrainingOnBoot();
 const auto selector=commands.find("openmenu basictraining_difficulty\n"),lobby=commands.find("openmenu menu_xboxlive_basictraining\n");
 CHECK(selector!=std::string::npos&&lobby!=std::string::npos&&selector>lobby);
 CHECK(!commands.empty()&&nesting==-1);
 for(const char*command:{"map mp_nuked","devmap mp_mountain","connect localhost:28960","demo test","exec match.cfg","openmenu main_text"}){
  reset({command});Com_OpenCombatTrainingOnBoot();CHECK(commands.empty()&&!s_combatTrainingBootPending&&nesting==-1);
 }
 for(const char*command:{"set logfile 2; openmenu main","set r_fullscreen 0; exec match.cfg","set logfile 2; set xblive_privatematch 1","set logfile 2\nopenmenu main","set logfile 2\rexec match.cfg"}){
  reset({command});Com_OpenCombatTrainingOnBoot();CHECK(commands.empty()&&!s_combatTrainingBootPending&&nesting==-1);
 }
 reset({"set name \"player; name\""});Com_OpenCombatTrainingOnBoot();CHECK(!commands.empty()&&!s_combatTrainingBootPending&&nesting==-1);
 for(const char*command:{"set onlinegame 0","seta xblive_privatematch 1","SET ui_mapname mp_nuked","seta UI_GAMETYPE dm","set systemlink 1","set splitscreen 1","set xblive_basictraining 0","set xblive_theater 1","set fs_game mods/test"}){
  reset({command});Com_OpenCombatTrainingOnBoot();CHECK(commands.empty()&&!s_combatTrainingBootPending&&nesting==-1);
 }
 reset();dedicatedSetting.current.integer=1;Com_OpenCombatTrainingOnBoot();CHECK(commands.empty()&&!s_combatTrainingBootPending);
 reset();server.current.enabled=true;Com_OpenCombatTrainingOnBoot();CHECK(commands.empty()&&!s_combatTrainingBootPending);
 reset();playing=true;Com_OpenCombatTrainingOnBoot();CHECK(commands.empty()&&!s_combatTrainingBootPending);
 for(connstate_t connection:{CA_CONNECTING,CA_CHALLENGING,CA_CONNECTED,CA_LOADING,CA_ACTIVE}){
  reset();state=connection;Com_OpenCombatTrainingOnBoot();CHECK(commands.empty()&&!s_combatTrainingBootPending);
 }
 for(connstate_t connection:{CA_LOGO,CA_CINEMATIC}){
  reset();state=connection;Com_OpenCombatTrainingOnBoot();CHECK(commands.empty()&&s_combatTrainingBootPending);
  state=CA_DISCONNECTED;Com_OpenCombatTrainingOnBoot();CHECK(!commands.empty()&&!s_combatTrainingBootPending);
 }
 reset();error="EXE_CANNOT_FIND_MAP";Com_OpenCombatTrainingOnBoot();CHECK(commands.empty()&&!s_combatTrainingBootPending);
 reset();mod="mods/test";Com_OpenCombatTrainingOnBoot();CHECK(commands.empty()&&!s_combatTrainingBootPending);
 for(const char*name:{"menu_xboxlive_basictraining","basictraining_difficulty","basictraining_msg"}){
  reset();missing=name;Com_OpenCombatTrainingOnBoot();CHECK(commands.empty()&&s_combatTrainingBootPending);
  missing=nullptr;Com_OpenCombatTrainingOnBoot();CHECK(!commands.empty()&&!s_combatTrainingBootPending);
 }
 reset();state=CA_UICINEMATIC;Com_OpenCombatTrainingOnBoot();CHECK(!commands.empty());
 printf("checks=%d; native launch policy pass; original menu execution/gameplay unqualified\n",checks);
}
'''
(out/'runner.cpp').write_text(source);stem=str(out/'runner')
(out/'build.cmd').write_text('@echo off\ncall "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvarsall.bat" x86 -vcvars_ver=14.37 >nul\nif errorlevel 1 exit /b %errorlevel%\ncl /nologo /std:c++20 /O2 /fp:precise /MT /DNDEBUG /DKISAK_MP /EHsc /I. /Isrc /Isrc/libs /Isrc/universal '+stem+'.cpp /Fe'+stem+'.exe /Fo'+stem+'.obj >'+stem+'-build.log 2>&1\nexit /b %errorlevel%\n')
build=subprocess.run(['cmd','/d','/c',str(out/'build.cmd')],capture_output=True)
if build.returncode:raise RuntimeError((out/'runner-build.log').read_text()[-4000:])
result=subprocess.run([str(out/'runner.exe')],capture_output=True,text=True)
report=dict(status='pass'if not result.returncode else'mismatch',output=result.stdout,errors=result.stderr,source_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),scope='Selected production native startup helpers with explicit command/frontend context. No original-gameplay or original instruction qualification; launch default is an intentional requested policy.')
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(result.stdout,end='');print(result.stderr,end='');raise SystemExit(result.returncode)

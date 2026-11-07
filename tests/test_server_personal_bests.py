"""Production server-stat completion against a pinned stock DDL projection.

Item/rank/clan catalogue context is controlled, not an original-game oracle.
The full original owner and external services remain unqualified. See the
separate original-native personal-best helper replay for immutable expectations.
"""
import argparse,hashlib,json,re,subprocess
from pathlib import Path
from mp_fidelity_common import ROOT,definition,exact_definition,reference
from test_stats_bank_routing import stock_schema
from test_cac_ownership import schema_cpp

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--live-source',type=Path,default=Path('src/live/live_stats.cpp'))
parser.add_argument('--out',type=Path,default=ROOT/'build-re/server-personal-bests')
args=parser.parse_args();out=args.out;out.mkdir(parents=True,exist_ok=True)
original,pe=reference();schema=stock_schema(progression=True)
source=r'''
#include <live/live_stats.h>
#include <live/live_storage_win.h>
#include <server/server.h>
#include <bgame/bg_unlockable_items.h>
#include <client/cl_rank.h>
#include <ddl/ddl_buffer.h>
#include <ddl/ddl_converter.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cstdarg>
#include <vector>
#include <algorithm>
static ddlState_t root{},cac{},g_statsRootState{};static ddlDef_t ddl{};
serverStatic_t svs{};static client_t clients[1]{};
static dvar_t maxclients{},training{};const dvar_t *com_maxclients=&maxclients,*xblive_basictraining=&training;
unlockableItemsData s_unlockableItems{};rankData_t s_rankData{};
unsigned int s_currentPersonalBest[1];personalBest_t s_personalBests[1][1000];
static int checks=0,failures=0,warnings=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);}}while(0)
bool Assert_MyHandler(const char*f,int line,int,const char*fmt,...){fprintf(stderr,"BLOCKED assertion %s:%d\n",f,line);std::abort();}
void DDL_PrintError(const char*fmt,...){va_list ap;va_start(ap,fmt);vfprintf(stderr,fmt,ap);va_end(ap);std::abort();}
void Com_PrintError(int,const char*fmt,...){va_list ap;va_start(ap,fmt);vfprintf(stderr,fmt,ap);va_end(ap);std::abort();}
void Com_PrintWarning(int,const char*,...){std::abort();}
void Com_Printf(int,const char*,...){std::abort();}
void DDL_NoCheckPrintWarning(const char*,...){++warnings;}
void I_strncpyz(char*d,const char*s,int n){snprintf(d,n,"%s",s);}
int I_strcmp(const char*a,const char*b){return strcmp(a,b);}
int I_stricmp(const char*a,const char*b){return _stricmp(a,b);}
char*I_strlwr(char*s){for(char*p=s;*p;++p)*p=(char)tolower((unsigned char)*p);return s;}
char*va(const char*fmt,...){static char ring[32][1024];static unsigned i=0;char*p=ring[i++%32];va_list ap;va_start(ap,fmt);vsnprintf(p,1024,fmt,ap);va_end(ap);return p;}
// Catalogue/settings inputs, never original dependency outputs or login stubs.
char BG_UnlockablesAllItemsUnlocked(){return 0;}
const char*BG_UnlockablesGetClanTagFeature(unsigned int i,clanTagFeatureColumn_t c){assert(i<256&&c==CLANTAG_COL_UNLOCKLVL);return nullptr;}
bool BG_UnlockablesIsClanTagFeatureLockedForRank(int,int,unsigned int){std::abort();}
void SV_SetClanTagFeatureNewForClient(char*,int,int,unsigned int){std::abort();}
'''
defs=[('src/ddl/ddl_lookup.cpp',['DDL_Lookup_FindMemberDef','DDL_Lookup_MoveTo','DDL_Lookup_MoveToIndex','DDL_Lookup_ClearState']),
 ('src/ddl/ddl_converter.cpp',['DDL_Converter_IsLeaf']),
 ('src/ddl/ddl_buffer.cpp',['DDL_Buffer_ReadHeader','DDL_Buffer_WriteHeader','DDL_Buffer_ReadBits','DDL_Buffer_WriteBits']),
 ('src/ddl/ddl_api.cpp',['DDL_Reset','DDL_MoveTo','DDL_MoveToName','DDL_MoveToPath','DDL_MoveToIndex','DDL_IterateFirst','DDL_IterateNext','DDL_IteratePrev','DDL_CopyFromBaseState','DDL_GetUserFlag','DDL_SetUserFlag','DDL_SetValueChanged','DDL_IsStateReadyForIO','DDL_WriteIntValueIfChanged','DDL_WriteIntValueInternal','DDL_ReadIntValueInternal','DDL_SetInt','DDL_GetInt']),
 (str(args.live_source),['LiveStats_GetRootDDLState','LiveStats_GetBasicTrainingState','LiveStats_GetPlayerStat','LiveStats_SetPlayerStat','LiveStats_GetItemStat','LiveStats_SetItemStat','LiveStats_MoveToProItemVersion','LiveStats_ProVersionUnlocked','LiveStats_UpdateItemPersonalBest','LiveStats_UpdatePersonalBest','LiveStats_UpdatetBestMatchAccuracy','LiveStats_UpdatetBestMatchKdRatio','SV_UpdatePersonalBestsForClient']),
 ('src/live/live_storage_win.cpp',['SV_GetStatFromBlob']),
 ('src/client/cl_rank.cpp',['CL_GetRankForXp']),
 ('src/bgame/bg_unlockable_items.cpp',['BG_UnlockablesGetItemInfo','BG_UnlockablesIsItemValidNotNull','BG_UnlockablesIsItemInDefaultClass','BG_UnlockablesIsItemLockedForRank','BG_UnlockablesIsProItem','BG_UnlockablesGetProItem','SV_SetItemNewForClient'])]
for path,names in defs:
 for name in names:
  body = exact_definition(ROOT/path, 'unsigned int __cdecl LiveStats_GetItemStat(ddlState_t *searchStateStats, const char *statMember, char *buffer)') if name == 'LiveStats_GetItemStat' else definition(ROOT/path,name)
  source+='\n'+body+'\n'
fields=ROOT/'src/qcommon/msg_mp.cpp'
source+='\n'.join(definition(fields,n,data=True)for n in re.findall(r'^const NetField (\w+)\[',fields.read_text(),re.M))
source+=schema_cpp(schema)
source+=r'''
ddlState_t state(std::initializer_list<const char*>p){ddlState_t s=root;for(const char*part:p)assert(DDL_MoveToName(&s,&s,part));return s;}
unsigned read(char*b,std::initializer_list<const char*>p){auto s=state(p);return DDL_GetInt(&s,b);}
void seed(char*b,std::initializer_list<const char*>p,unsigned v){auto s=state(p);assert(DDL_SetInt(&s,v,b));}
int main(){
 initSchema();g_statsRootState=root;svs.clients=clients;maxclients.current.integer=1;
 // A controlled rank-table input exercises the original lookup algorithm.
 // These thresholds are fixture data, not stock XP thresholds or gameplay rules.
 s_rankData.isValid=true;s_rankData.topRow=1;s_rankData.minxp[0]=0;s_rankData.maxxp[0]=100;s_rankData.rankId[0]=0;
 s_rankData.minxp[1]=100;s_rankData.maxxp[1]=0x7fffffff;s_rankData.rankId[1]=1;
 for(int i:{62,63,64,65}){auto&item=s_unlockableItems.itemTable[i];item.isValid=true;item.defaultClass[0]=CLASS_NONE;item.name="fixture";}
 s_unlockableItems.itemTable[62].defaultClass[0]=CLASS_ASSAULT;
 s_unlockableItems.itemTable[63].unlockLevel=1;
 s_unlockableItems.itemTable[64].group=ITEMGROUP_SPECIALTY;
 s_unlockableItems.itemTable[65].group=ITEMGROUP_SPECIALTY;s_unlockableItems.itemTable[65].count=1;
 char*live=(char*)clients[0].globalStats,*old=(char*)clients[0].globalStatsStable;
 memset(clients[0].stats,0x5a,sizeof(clients[0].stats));
 seed(live,{"PlayerStatsList","RANKXP"},150);seed(old,{"PlayerStatsList","RANKXP"},50);
 seed(live,{"PlayerStatsList","KILLS"},10);seed(old,{"PlayerStatsList","KILLS"},4);
 seed(live,{"PersonalBests","KILLS"},5);
 seed(live,{"PlayerStatsByGameMode","tdm","KILLS"},12);seed(old,{"PlayerStatsByGameMode","tdm","KILLS"},2);
 seed(live,{"ItemStats","63","stats","kills","statValue"},9);seed(old,{"ItemStats","63","stats","kills","statValue"},3);
 seed(live,{"ItemStats","62","stats","kills","statValue"},9);
 for(const char*index:{"0","1","2"})seed(live,{"ItemStats","64","isProVersionUnlocked",index},1);
 seed(live,{"PlayerStatsList","TOTAL_SHOTS"},500000);seed(live,{"PlayerStatsList","HITS"},450000);
 seed(old,{"HighestStats","HIGHEST_ACCURACY"},1000);
 auto stable=std::vector<char>(old,old+40960);auto before=std::vector<char>(live,live+40960);
 s_currentPersonalBest[0]=17;SV_UpdatePersonalBestsForClient(0);
 CHECK(s_currentPersonalBest[0]==17);CHECK(!memcmp(live,before.data(),40960));
 clients[0].statsValidated=1;SV_UpdatePersonalBestsForClient(0);
 CHECK(read(live,{"PersonalBests","KILLS"})==6);
 CHECK(read(live,{"PlayerBestStatsByGameMode","tdm","stats","KILLS"})==10);
 CHECK(read(live,{"ItemStats","63","stats","kills","personalBest"})==6);
 CHECK(read(live,{"ItemStats","62","stats","kills","personalBest"})==0);
 CHECK(read(live,{"ItemStats","63","new"})==1);
 CHECK(read(live,{"ItemStats","65","new"})==1);
 CHECK(read(live,{"HighestStats","HIGHEST_ACCURACY"})==9000);
 CHECK(!memcmp(old,stable.data(),40960));
 CHECK(std::all_of(clients[0].stats,clients[0].stats+sizeof(clients[0].stats),[](unsigned char c){return c==0x5a;}));
 bool globalRow=false,modeRow=false;
 for(unsigned i=0;i<s_currentPersonalBest[0];++i){auto&r=s_personalBests[0][i];
  if(r.name&&r.prefix&&!strcmp(r.name,"kills")&&!strcmp(r.prefix,"")){globalRow=true;CHECK(r.value==6);CHECK(r.delta==1);}
  if(r.name&&r.prefix&&!strcmp(r.name,"kills")&&!strcmp(r.prefix,"tdm")){modeRow=true;CHECK(r.value==10);}
 }
 CHECK(globalRow&&modeRow);CHECK(s_currentPersonalBest[0]<1000);
 // The original owner always uses GLOBAL/GLOBALSTABLE, even when training is
 // enabled. It must not copy training/normal data into those banks.
 training.current.enabled=true;auto liveAfter=std::vector<char>(live,live+40960);
 SV_UpdatePersonalBestsForClient(0);CHECK(!memcmp(live,liveAfter.data(),40960));CHECK(!memcmp(old,stable.data(),40960));
 printf("checks=%d failures=%d rows=%u expected-range-warnings=%d\n",checks,failures,s_currentPersonalBest[0],warnings);
 return failures?1:0;
}
'''
(out/'owner.cpp').write_text(source)
stem=str(out/'owner')
(out/'build.cmd').write_text('@echo off\ncall "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvarsall.bat" x86 -vcvars_ver=14.37 >nul\nif errorlevel 1 exit /b %errorlevel%\ncl /nologo /std:c++20 /O2 /fp:precise /MT /DKISAK_MP /EHsc /I. /Isrc /Isrc/libs /Isrc/universal '+stem+'.cpp /Fe'+stem+'.exe /Fo'+stem+'.obj >'+stem+'-build.log 2>&1\nexit /b %errorlevel%\n')
compiled=subprocess.run(['cmd','/d','/c',str(out/'build.cmd')],capture_output=True)
if compiled.returncode:raise RuntimeError((out/'owner-build.log').read_text())
result=subprocess.run([str(out/'owner.exe')],capture_output=True,text=True);print(result.stdout,end='');print(result.stderr,end='')
report=dict(status='pass'if result.returncode==0 else'mismatch',output=result.stdout,errors=result.stderr,stock_schema_sha256=hashlib.sha256(json.dumps(schema,sort_keys=True).encode()).hexdigest(),
 source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest()for p,n in defs},
 scope='Extracted production owner and real DDL, item ownership, pro unlock, rank lookup and personal-best dependencies; stock field projection; controlled item/rank catalogue context. Validated-bank guard, global and per-mode bests, item/pro NEW, read-only stable/normal buffers and high-count accuracy.',
 exclusions='Full original-native/Unicorn server owner remains blocked; no original lookup/catalogue closure or stock-rank threshold qualification. Clan feature metadata intentionally unsupported; accidental use terminates. No service, gameplay, award amount, UI or persistence qualification.')
(out/'report.json').write_text(json.dumps(report,indent=2)+'\n');raise SystemExit(result.returncode)

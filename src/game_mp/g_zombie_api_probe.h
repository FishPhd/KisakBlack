#pragma once
#include <utility>
#include <array>

#ifdef KISAK_WEB
// Campaign and Zombies builtins missing from the shared multiplayer tables.
// Scr_GetFunction / Scr_GetMethod consult this list only after those tables miss,
// and only while zombiemode is on, so multiplayer scripts keep their own functions.
// A hit logs once and returns a default that will not Scr_Error by itself.
// web_probeZombieRuntime still terminates on the first call.
// The .gsc call sites live in fastfiles, so the defaults follow the binding-audit
// names rather than a line-by-line init trace.
namespace
{
enum ZombieStubResult : unsigned char
{
    ZOMBIE_STUB_UNDEFINED = 0,
    ZOMBIE_STUB_FALSE,
    ZOMBIE_STUB_TRUE,
    ZOMBIE_STUB_FAR,
    ZOMBIE_STUB_EMPTY_STRING,
    ZOMBIE_STUB_EMPTY_ARRAY,
    ZOMBIE_STUB_ZERO_VECTOR,
    ZOMBIE_STUB_FORWARD_VECTOR,
};

struct ZombieMissingBuiltin
{
    const char *name;
    ZombieStubResult result;
};

constexpr ZombieMissingBuiltin zombieMissingBuiltins[] = {
    { "activateclientexploder", ZOMBIE_STUB_UNDEFINED },
    { "addvehicletocompass", ZOMBIE_STUB_UNDEFINED },
    { "allowcrouch", ZOMBIE_STUB_UNDEFINED },
    { "allowlean", ZOMBIE_STUB_UNDEFINED },
    { "allowmelee", ZOMBIE_STUB_UNDEFINED },
    { "allowpickupweapons", ZOMBIE_STUB_UNDEFINED },
    { "allowprone", ZOMBIE_STUB_UNDEFINED },
    { "allowstand", ZOMBIE_STUB_UNDEFINED },
    { "badplace_arc", ZOMBIE_STUB_UNDEFINED },
    { "badplace_cylinder", ZOMBIE_STUB_UNDEFINED },
    { "bloodimpact", ZOMBIE_STUB_UNDEFINED },
    { "bulletspread", ZOMBIE_STUB_UNDEFINED },
    { "canshoot", ZOMBIE_STUB_FALSE },
    { "canuseturret", ZOMBIE_STUB_FALSE },
    { "changefontscaleovertime", ZOMBIE_STUB_UNDEFINED },
    { "changelevel", ZOMBIE_STUB_UNDEFINED },
    { "checkgrenadethrow", ZOMBIE_STUB_UNDEFINED },
    { "checkgrenadethrowpos", ZOMBIE_STUB_UNDEFINED },
    { "codeplayloopedfx", ZOMBIE_STUB_UNDEFINED },
    { "commitsave", ZOMBIE_STUB_UNDEFINED },
    { "createthreatbiasgroup", ZOMBIE_STUB_UNDEFINED },
    { "deactivateclientexploder", ZOMBIE_STUB_UNDEFINED },
    { "disablegrenadesuicide", ZOMBIE_STUB_UNDEFINED },
    { "distance2dsquared", ZOMBIE_STUB_UNDEFINED },
    { "dontinterpolate", ZOMBIE_STUB_UNDEFINED },
    { "dropweapon", ZOMBIE_STUB_UNDEFINED },
    { "enablehealthshield", ZOMBIE_STUB_UNDEFINED },
    { "enterprone", ZOMBIE_STUB_UNDEFINED },
    { "entsearch", ZOMBIE_STUB_EMPTY_ARRAY },
    { "exitprone", ZOMBIE_STUB_UNDEFINED },
    { "findbestcovernode", ZOMBIE_STUB_UNDEFINED },
    { "findpath", ZOMBIE_STUB_FALSE },
    { "forcelevelend", ZOMBIE_STUB_UNDEFINED },
    { "getaiarray", ZOMBIE_STUB_EMPTY_ARRAY },
    { "getaispeciesarray", ZOMBIE_STUB_EMPTY_ARRAY },
    { "getaivelocity", ZOMBIE_STUB_ZERO_VECTOR },
    { "getallvehiclenodes", ZOMBIE_STUB_EMPTY_ARRAY },
    { "getanynodearray", ZOMBIE_STUB_EMPTY_ARRAY },
    { "getcentroid", ZOMBIE_STUB_ZERO_VECTOR },
    { "getcinematictimeremaining", ZOMBIE_STUB_FALSE },
    { "getcurrentweaponclipammo", ZOMBIE_STUB_FALSE },
    { "getdifficulty", ZOMBIE_STUB_EMPTY_STRING },
    { "getlinkedent", ZOMBIE_STUB_UNDEFINED },
    { "getpersistentprofilevar", ZOMBIE_STUB_EMPTY_STRING },
    { "getplayerviewheight", ZOMBIE_STUB_FALSE },
    { "getsnapshotindexarray", ZOMBIE_STUB_EMPTY_ARRAY },
    { "getspawnerarray", ZOMBIE_STUB_EMPTY_ARRAY },
    { "getthreatbias", ZOMBIE_STUB_FALSE },
    { "getthreatbiasgroup", ZOMBIE_STUB_EMPTY_STRING },
    { "getturret", ZOMBIE_STUB_UNDEFINED },
    { "getvehiclenodearray", ZOMBIE_STUB_EMPTY_ARRAY },
    { "getvisionsetnaked", ZOMBIE_STUB_EMPTY_STRING },
    { "getweaponaccuracy", ZOMBIE_STUB_FALSE },
    { "getweaponclipmodel", ZOMBIE_STUB_EMPTY_STRING },
    { "getweaponforwarddir", ZOMBIE_STUB_FORWARD_VECTOR },
    { "getweaponmuzzlepoint", ZOMBIE_STUB_ZERO_VECTOR },
    { "getweaponrenderoptions", ZOMBIE_STUB_FALSE },
    { "gib", ZOMBIE_STUB_UNDEFINED },
    { "haseyes", ZOMBIE_STUB_FALSE },
    { "hideviewmodel", ZOMBIE_STUB_UNDEFINED },
    { "iscoopepd", ZOMBIE_STUB_FALSE },
    { "isnodeoccupied", ZOMBIE_STUB_FALSE },
    { "isnotarget", ZOMBIE_STUB_FALSE },
    { "issaverecentlyloaded", ZOMBIE_STUB_FALSE },
    { "issavesuccessful", ZOMBIE_STUB_TRUE },
    { "issuppressed", ZOMBIE_STUB_FALSE },
    { "isturretactive", ZOMBIE_STUB_FALSE },
    { "iswaitingonsound", ZOMBIE_STUB_FALSE },
    { "lookatentity", ZOMBIE_STUB_UNDEFINED },
    { "magicgrenade", ZOMBIE_STUB_UNDEFINED },
    { "magicgrenademanual", ZOMBIE_STUB_UNDEFINED },
    { "makefakeai", ZOMBIE_STUB_UNDEFINED },
    { "missionfailed", ZOMBIE_STUB_UNDEFINED },
    { "missionsuccess", ZOMBIE_STUB_UNDEFINED },
    { "modelhasphyspreset", ZOMBIE_STUB_FALSE },
    { "numremoteclients", ZOMBIE_STUB_FALSE },
    { "oktospawn", ZOMBIE_STUB_TRUE },
    { "pause3dcinematic", ZOMBIE_STUB_UNDEFINED },
    { "playerlinkto", ZOMBIE_STUB_UNDEFINED },
    { "playerlinktoabsolute", ZOMBIE_STUB_UNDEFINED },
    { "playerpositionvalid", ZOMBIE_STUB_TRUE },
    { "playersetgroundreferenceent", ZOMBIE_STUB_UNDEFINED },
    { "playweapondeatheffects", ZOMBIE_STUB_UNDEFINED },
    { "prefetchlevel", ZOMBIE_STUB_UNDEFINED },
    { "recordline", ZOMBIE_STUB_UNDEFINED },
    { "refreshhudammocounter", ZOMBIE_STUB_UNDEFINED },
    { "reloadbuttonpressed", ZOMBIE_STUB_FALSE },
    { "removevehiclefromcompass", ZOMBIE_STUB_UNDEFINED },
    { "reportclientdisconnected", ZOMBIE_STUB_UNDEFINED },
    { "resetadswidthandlerp", ZOMBIE_STUB_UNDEFINED },
    { "resetmissiledetonationtime", ZOMBIE_STUB_UNDEFINED },
    { "reviveplayer", ZOMBIE_STUB_UNDEFINED },
    { "ropesetflag", ZOMBIE_STUB_UNDEFINED },
    { "savegame", ZOMBIE_STUB_UNDEFINED },
    { "savegamenocommit", ZOMBIE_STUB_UNDEFINED },
    { "setadswidthandlerp", ZOMBIE_STUB_UNDEFINED },
    { "setailimit", ZOMBIE_STUB_UNDEFINED },
    { "setautopickup", ZOMBIE_STUB_UNDEFINED },
    { "setblur", ZOMBIE_STUB_UNDEFINED },
    { "setdeathcontents", ZOMBIE_STUB_UNDEFINED },
    { "setdebugangles", ZOMBIE_STUB_UNDEFINED },
    { "setdebugorigin", ZOMBIE_STUB_UNDEFINED },
    { "setdoublevision", ZOMBIE_STUB_UNDEFINED },
    { "setenginevolume", ZOMBIE_STUB_UNDEFINED },
    { "setexploderid", ZOMBIE_STUB_UNDEFINED },
    { "setmaxhealth", ZOMBIE_STUB_UNDEFINED },
    { "setmissiondvar", ZOMBIE_STUB_UNDEFINED },
    { "setpersistentprofilevar", ZOMBIE_STUB_UNDEFINED },
    { "setphysparams", ZOMBIE_STUB_UNDEFINED },
    { "setproneanimnodes", ZOMBIE_STUB_UNDEFINED },
    { "setsaveddvar", ZOMBIE_STUB_UNDEFINED },
    { "setscripthintstring", ZOMBIE_STUB_UNDEFINED },
    { "setshadowhint", ZOMBIE_STUB_UNDEFINED },
    { "setsoundblend", ZOMBIE_STUB_UNDEFINED },
    { "setteamforentity", ZOMBIE_STUB_UNDEFINED },
    { "setthreatbias", ZOMBIE_STUB_UNDEFINED },
    { "setthreatbiasgroup", ZOMBIE_STUB_UNDEFINED },
    { "settransported", ZOMBIE_STUB_UNDEFINED },
    { "setuinextlevel", ZOMBIE_STUB_UNDEFINED },
    { "setvehicleattachments", ZOMBIE_STUB_UNDEFINED },
    { "setvolfog", ZOMBIE_STUB_UNDEFINED },
    { "shoot", ZOMBIE_STUB_UNDEFINED },
    { "showviewmodel", ZOMBIE_STUB_UNDEFINED },
    { "snapshotacknowledged", ZOMBIE_STUB_TRUE },
    { "splitviewallowed", ZOMBIE_STUB_FALSE },
    { "start3dcinematic", ZOMBIE_STUB_UNDEFINED },
    { "startactorreact", ZOMBIE_STUB_UNDEFINED },
    { "startcameratween", ZOMBIE_STUB_UNDEFINED },
    { "startfadingblur", ZOMBIE_STUB_UNDEFINED },
    { "startrevive", ZOMBIE_STUB_UNDEFINED },
    { "stop3dcinematic", ZOMBIE_STUB_UNDEFINED },
    { "stoprevive", ZOMBIE_STUB_UNDEFINED },
    { "stopshoot", ZOMBIE_STUB_UNDEFINED },
    { "stopsounds", ZOMBIE_STUB_UNDEFINED },
    { "stopuseturret", ZOMBIE_STUB_UNDEFINED },
    { "throwgrenade", ZOMBIE_STUB_UNDEFINED },
    { "trackscriptstate", ZOMBIE_STUB_UNDEFINED },
    { "transmittargetname", ZOMBIE_STUB_UNDEFINED },
    { "updategamerprofile", ZOMBIE_STUB_UNDEFINED },
    { "updateplayersightaccuracy", ZOMBIE_STUB_UNDEFINED },
    { "updateprone", ZOMBIE_STUB_UNDEFINED },
    { "uploadscore", ZOMBIE_STUB_UNDEFINED },
    { "usecovernode", ZOMBIE_STUB_UNDEFINED },
    { "useturret", ZOMBIE_STUB_UNDEFINED },
    { "useweaponhidetags", ZOMBIE_STUB_UNDEFINED },
    { "visionsetlaststand", ZOMBIE_STUB_UNDEFINED },
    { "visionsetnaked", ZOMBIE_STUB_UNDEFINED },
    { "watersimenable", ZOMBIE_STUB_UNDEFINED },
    { "weapondualwieldweaponname", ZOMBIE_STUB_EMPTY_STRING },
    { "weaponisgasweapon", ZOMBIE_STUB_FALSE },
};

using ZombieProbeFunction = void (__cdecl *)();
using ZombieProbeMethod = void (__cdecl *)(scr_entref_t);

bool zombieStubLogged[ARRAY_COUNT(zombieMissingBuiltins)];
char zombieStubEmptyString[] = "";
float zombieStubZeroVector[3] = { 0.0f, 0.0f, 0.0f };
float zombieStubForwardVector[3] = { 1.0f, 0.0f, 0.0f };

template<size_t Index> void ZombieProbeMissingFunction()
{
    const ZombieMissingBuiltin &builtin = zombieMissingBuiltins[Index];
    if (Dvar_GetBool("web_probeZombieRuntime"))
    {
        Scr_Error(va("Zombies runtime probe reached unimplemented API '%s'", builtin.name), true);
        return;
    }
    if (!zombieStubLogged[Index])
    {
        zombieStubLogged[Index] = true;
        Com_Printf(23, "Zombies: stubbed unimplemented builtin '%s'\n", builtin.name);
    }
    switch (builtin.result)
    {
    case ZOMBIE_STUB_FALSE:
        Scr_AddBool(0, SCRIPTINSTANCE_SERVER);
        break;
    case ZOMBIE_STUB_TRUE:
        Scr_AddBool(1, SCRIPTINSTANCE_SERVER);
        break;
    case ZOMBIE_STUB_FAR:
        Scr_AddFloat(1.0e12f, SCRIPTINSTANCE_SERVER);
        break;
    case ZOMBIE_STUB_EMPTY_STRING:
        Scr_AddString(zombieStubEmptyString, SCRIPTINSTANCE_SERVER);
        break;
    case ZOMBIE_STUB_EMPTY_ARRAY:
        Scr_MakeArray(SCRIPTINSTANCE_SERVER);
        break;
    case ZOMBIE_STUB_ZERO_VECTOR:
        Scr_AddVector(zombieStubZeroVector, SCRIPTINSTANCE_SERVER);
        break;
    case ZOMBIE_STUB_FORWARD_VECTOR:
        Scr_AddVector(zombieStubForwardVector, SCRIPTINSTANCE_SERVER);
        break;
    case ZOMBIE_STUB_UNDEFINED:
    default:
        Scr_AddUndefined(SCRIPTINSTANCE_SERVER);
        break;
    }
}
template<size_t Index> void ZombieProbeMissingMethod(scr_entref_t)
{
    ZombieProbeMissingFunction<Index>();
}
template<size_t... Index> constexpr auto ZombieProbeFunctions(std::index_sequence<Index...>)
{
    return std::array<ZombieProbeFunction, sizeof...(Index)>{ZombieProbeMissingFunction<Index>...};
}
template<size_t... Index> constexpr auto ZombieProbeMethods(std::index_sequence<Index...>)
{
    return std::array<ZombieProbeMethod, sizeof...(Index)>{ZombieProbeMissingMethod<Index>...};
}
constexpr auto zombieProbeFunctions = ZombieProbeFunctions(std::make_index_sequence<ARRAY_COUNT(zombieMissingBuiltins)>{});
constexpr auto zombieProbeMethods = ZombieProbeMethods(std::make_index_sequence<ARRAY_COUNT(zombieMissingBuiltins)>{});

static int ZombieProbeIndex(const char **name, int *type)
{
    if (!zombiemode || !zombiemode->current.enabled) return -1;
    for (size_t i = 0; i < ARRAY_COUNT(zombieMissingBuiltins); ++i)
        if (!strcmp(*name, zombieMissingBuiltins[i].name))
        {
            *name = zombieMissingBuiltins[i].name;
            *type = 0;
            return static_cast<int>(i);
        }
    return -1;
}
}
#endif

#pragma once
#include <cgame/cg_scr_main.h>

struct ZombieClientScriptBinding
{
    const char *file;
    const char *label;
    int cscr_data_t::*field;
};

// Compilation and binding consume the same ordered table.
inline constexpr ZombieClientScriptBinding zombieClientScriptBindings[] = {
    {"codescripts/delete", "main", &cscr_data_t::delete_},
    {"codescripts/struct", "initstructs", &cscr_data_t::initstructs},
    {"codescripts/struct", "createstruct", &cscr_data_t::createstruct},
    {"codescripts/struct", "findstruct", &cscr_data_t::findstruct},
    {"clientscripts/_callbacks", "statechange", &cscr_data_t::clientsysstatechange},
    {"clientscripts/_callbacks", "maprestart", &cscr_data_t::maprestart},
    {"clientscripts/_callbacks", "localclientconnect", &cscr_data_t::localclientconnect},
    {"clientscripts/_callbacks", "localclientdisconnect", &cscr_data_t::localclientdisconnect},
    {"clientscripts/_callbacks", "entityspawned", &cscr_data_t::entityspawned},
    {"clientscripts/_callbacks", "playerspawned", &cscr_data_t::playerspawned},
    {"clientscripts/_callbacks", "client_flag_callback", &cscr_data_t::clientFlagCB},
    {"clientscripts/_callbacks", "client_flagasval_callback", &cscr_data_t::clientFlagAsValCB},
    {"clientscripts/_callbacks", "CodeCallback_CreatingCorpse", &cscr_data_t::corpse_callback},
    {"clientscripts/_callbacks", "CodeCallback_PlayerFootstep", &cscr_data_t::playerFootstep},
    {"clientscripts/_callbacks", "CodeCallback_PlayerJump", &cscr_data_t::playerJump},
    {"clientscripts/_callbacks", "CodeCallback_PlayerLand", &cscr_data_t::playerLand},
    {"clientscripts/_callbacks", "CodeCallback_PlayerFoliage", &cscr_data_t::playerFoliage},
    {"clientscripts/_callbacks", "level_notify", &cscr_data_t::levelnotify},
    {"clientscripts/_callbacks", "sound_notify", &cscr_data_t::dogSoundNotify},
    {"clientscripts/_callbacks", "airsupport", &cscr_data_t::airsupport},
    {"clientscripts/_callbacks", "entityshutdown_callback", &cscr_data_t::entityshutdownCB},
    {"clientscripts/_callbacks", "glass_smash", &cscr_data_t::glassSmash},
    {"clientscripts/_callbacks", "CodeCallback_GibEvent", &cscr_data_t::gibEvent},
};

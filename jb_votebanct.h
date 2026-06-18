#ifndef _INCLUDE_METAMOD_SOURCE_STUB_PLUGIN_H_
#define _INCLUDE_METAMOD_SOURCE_STUB_PLUGIN_H_ 

#include <ISmmPlugin.h>
#include <cstdio>
#include <sh_vector.h>
#include <iserver.h>
#include "vector.h"
#include <keyvalues.h>
#include <string>
#include <vector> 
class CBasePlayerController;
#include "CCSPlayerController.h"
#include "igameevents.h"
#include <complex>
#include <iomanip>
#include "metamod_oslink.h"

#include "include/menus.h"
#include "include/admin.h"
#include "include/jailbreak_api.h"

#include "include/mysql_mm.h"
#include "include/sql_mm.h"
#include "include/sqlite_mm.h"



class jb_votebanct final : public ISmmPlugin, public IMetamodListener {
public:
    bool Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late);
    bool Unload(char* error, size_t maxlen);
    void AllPluginsLoaded();
    void OnGameServerSteamAPIActivated();
private:
    const char* GetAuthor();
    const char* GetName();
    const char* GetDescription();
    const char* GetURL();
    const char* GetLicense();
    const char* GetVersion();
    const char* GetDate();
    const char* GetLogTag();
};

#endif
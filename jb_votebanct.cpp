#include "jb_votebanct.h"
#include <random>
#include <cstdio>
#include <algorithm>

#define MAX_PLAYERS 64

#define CS_TEAM_NONE 0
#define CS_TEAM_SPECTATOR 1
#define CS_TEAM_T 2
#define CS_TEAM_CT 3

jb_votebanct g_jb_votebanct;
PLUGIN_EXPOSE(jb_votebanct, g_jb_votebanct);

// SYSTEM API`s
IVEngineServer2* engine = nullptr;
CGlobalVars* gpGlobals = nullptr;
CGameEntitySystem* g_pGameEntitySystem = nullptr;
CEntitySystem* g_pEntitySystem = nullptr;

// API
IUtilsApi* utils;
IMenusApi* menus_api;
IPlayersApi* players_api;
IJailbreakApi* jailbreak_api;
IAdminApi* admin_api;

IMySQLClient* mysql_client;
IMySQLConnection* connection;

// VARS


std::map<std::string, std::string> phrases;
bool g_bPendingChatReason[64] = {false};

struct PlayersVoteban{
    bool bHaveActiveVote = false;
    int iInitiator = -1;
    int iTarget = -1;
    std::string sTargetName = "";
    std::string sVoteReason = "";
    float flNextVotebanTime = 0.0f;

    void Reset(){
        bHaveActiveVote = false;
        iInitiator = -1;
        iTarget = -1;
        sTargetName = "";
        sVoteReason = "";
    }

};
PlayersVoteban g_PlayersVoteban[64];


bool g_bVoteInProgress = false;
int g_iVoteInitiaor = -1;
int g_iVoteTarget = -1;
std::string g_sVoteTargetName = "";

int g_iYesVotes = 0;
int g_iNoVotes = 0;
int g_iTotalVotersRequired = 0; 
int g_iVotesCast = 0;       


// =========================================
// CONFIG VARS
// =========================================
bool b_debug = true;

float g_flPercentRequired;

float g_flGlobalVotebanCooldown;
float g_flPlayerVotebanCooldown;
float g_flNextGlobalVotebanTime = 0.0f;

int g_iBanDuration;

std::string g_sAdminPermission;

//==========================================
// HELPERS
//==========================================


// =========================================
// CONFIGS 
// =========================================

void LoadDatabase() {
    KeyValues* databases = new KeyValues("Databases");
    if (!databases->LoadFromFile(g_pFullFileSystem, "addons/configs/databases.cfg")) {
        utils->ErrorLog("%s | Failed to load database config.",g_PLAPI->GetLogTag());
        delete databases;
        return;
    }
    KeyValues* database = databases->FindKey("jailbreak");
    if (!database) {
        utils->ErrorLog("%s | Failed to find \"jailbreak\" database in config.",g_PLAPI->GetLogTag());
        delete databases;
        return;
    }
    MySQLConnectionInfo info;
    info.host = database->GetString("host", "");
    info.user = database->GetString("user", "");
    info.pass = database->GetString("pass", "");
    info.database = database->GetString("database", "");
    info.port = database->GetInt("port", 3306);
    connection = mysql_client->CreateMySQLConnection(info);
    connection->Connect([databases](bool connect) {
        if (!connect) {
            META_CONPRINTF("%s Failed to connect to MySQL\n", g_PLAPI->GetLogTag());
            connection = nullptr;
        } 
        delete databases;
    });
}


void LoadConfig() {
    KeyValues* config = new KeyValues("Config");
    const char* path = "addons/configs/Jailbreak/voteban.ini";
    if (!config->LoadFromFile(g_pFullFileSystem, path)) {
        utils->ErrorLog("%s Failed to load: %s",g_PLAPI->GetLogTag(), path);
        delete config;
        return;
    }

    g_sAdminPermission = config->GetString("AdminPermission","@admin/root");
    g_flPercentRequired = config->GetFloat("PercentRequired",70.0f);

    g_flGlobalVotebanCooldown = config->GetFloat("GlobalCooldown", 120.0f);
    g_flPlayerVotebanCooldown = config->GetFloat("PlayerCooldown", 300.0f);

    g_iBanDuration = config->GetInt("BanDuration",1800);

    delete config;
}

void LoadTranslations() {
    phrases.clear();
    KeyValues* g_kvPhrases = new KeyValues("Phrases");
    const char *pszPath = "addons/translations/jailbreak_voteban.phrases.txt";

    if (!g_kvPhrases->LoadFromFile(g_pFullFileSystem, pszPath))
    {
        utils->ErrorLog("%s Failed to load %s", g_PLAPI->GetLogTag(), pszPath);
        delete g_kvPhrases;
        return;
    }

    const char* language = utils->GetLanguage();

    for (KeyValues *pKey = g_kvPhrases->GetFirstTrueSubKey(); pKey; pKey = pKey->GetNextTrueSubKey()) {
        phrases[std::string(pKey->GetName())] = std::string(pKey->GetString(language));
    }
    delete g_kvPhrases;
}

const char* GetTranslation(const char* key) {
    auto it = phrases.find(key);
    if (it == phrases.end()) return key;
    else return it->second.c_str();
}

void PrintSlotPrefixed(int iSlot, const char* content) {
    if (!content || content[0] == '\0') return;
    char buf[512];
    g_SMAPI->Format(buf, sizeof(buf), "%s %s", GetTranslation("Prefix"), content);
    utils->PrintToChat(iSlot, buf);
}

void PrintAllPrefixed(const char* content) {
    if (!content || content[0] == '\0') return;
    char buf[512];
    g_SMAPI->Format(buf, sizeof(buf), "%s %s", GetTranslation("Prefix"), content);
    utils->PrintToChatAll(buf);
}

// =========================================
// MAIN
// =========================================

void BanPlayer(uint64_t iSteamID64, uint64_t iInitiatorSID, const char* szInitiatorName, const char* szReason) {
    time_t now = std::time(nullptr);
    int64_t iCreatedAt = (int64_t)now;
    int64_t iExpireAt = iCreatedAt + (int64_t)g_iBanDuration;
    char reason[512];
    g_SMAPI->Format(reason,sizeof(reason),"Voteban by %s (%s)",szInitiatorName,szReason);
    char query[512];
    g_SMAPI->Format(query, sizeof(query), 
        "INSERT INTO jb_punishments (sid64, adminsid, created_at, expires_at, reason) "
        "VALUES (%llu, %llu, %lld, %lld, '%s') "
        "ON DUPLICATE KEY UPDATE created_at = VALUES(created_at), expires_at = VALUES(expires_at), reason = VALUES(reason);",
        iSteamID64, iInitiatorSID, iCreatedAt, iExpireAt, connection->Escape(reason).c_str()
    );


    if (!connection) return;
    
    connection->Query(query, [](ISQLQuery* res) {
    });
}

void EndGlobalVoteban() {
    g_bVoteInProgress = false;

    std::string sReason = "Error";
    if (g_iVoteInitiaor != -1 && !g_PlayersVoteban[g_iVoteInitiaor].sVoteReason.empty()) {
        sReason = g_PlayersVoteban[g_iVoteInitiaor].sVoteReason;
    }
    for (int i = 0; i < MAX_PLAYERS; i++) {
        menus_api->ClosePlayerMenu(i);

        if (g_PlayersVoteban[i].bHaveActiveVote && g_PlayersVoteban[i].iTarget == g_iVoteTarget) {
            g_PlayersVoteban[i].Reset();
        }
    }

    if (g_iTotalVotersRequired == 0) return;

    float flPercentage = ((float)g_iYesVotes / (float)g_iTotalVotersRequired) * 100.0f;


    char szMessage[512];

    if (flPercentage >= g_flPercentRequired) {
        PrintAllPrefixed(GetTranslation("Voteban_Success"));
        g_SMAPI->Format(szMessage, sizeof(szMessage), GetTranslation("Voteban_PlayerBan"), g_sVoteTargetName.c_str(), flPercentage, g_flPercentRequired);
        PrintAllPrefixed(szMessage);
        
        auto pTargetEnt = CCSPlayerController::FromSlot(g_iVoteTarget);
        if (pTargetEnt && pTargetEnt->IsConnected()) {
            uint64_t targetSid = pTargetEnt->m_steamID;
            if (g_iVoteInitiaor != -1 && !g_PlayersVoteban[g_iVoteInitiaor].sVoteReason.empty()) {
                sReason = g_PlayersVoteban[g_iVoteInitiaor].sVoteReason;
            }

            std::string sInitiatorName = "Server";
            uint64_t initiatorSid = 0;
            
            auto pInitiator = CCSPlayerController::FromSlot(g_iVoteInitiaor);
            if (pInitiator) {
                sInitiatorName = pInitiator->GetPlayerName();
                initiatorSid = pInitiator->m_steamID;
            }
            
            BanPlayer(targetSid, initiatorSid, sInitiatorName.c_str(), sReason.c_str());
            players_api->ChangeTeam(g_iVoteTarget, CS_TEAM_T);
        }
    } else {
        g_SMAPI->Format(szMessage, sizeof(szMessage), GetTranslation("Voteban_Failed"), g_sVoteTargetName.c_str(), flPercentage, g_flPercentRequired);
        PrintAllPrefixed(szMessage);
    }

    if (g_iVoteInitiaor != -1) {
        g_flNextGlobalVotebanTime = gpGlobals->curtime + g_flGlobalVotebanCooldown;
        g_PlayersVoteban[g_iVoteInitiaor].flNextVotebanTime = gpGlobals->curtime + g_flPlayerVotebanCooldown;

        g_PlayersVoteban[g_iVoteInitiaor].Reset();
    }

    g_iVoteInitiaor = -1;
    g_iVoteTarget = -1;
    g_sVoteTargetName = "";
}

void StartGlobalVoteban(int iInitiator) {
    if (g_bVoteInProgress) {
        PrintSlotPrefixed(iInitiator, GetTranslation("Voteban_AlreadyInProgress"));
        return;
    }

    g_iVoteInitiaor = iInitiator;
    g_iVoteTarget = g_PlayersVoteban[iInitiator].iTarget;
    g_sVoteTargetName = g_PlayersVoteban[iInitiator].sTargetName;

    auto initiatorController = CCSPlayerController::FromSlot(iInitiator);
    if (!initiatorController) return;
    
    g_bVoteInProgress = true;
    g_PlayersVoteban[iInitiator].bHaveActiveVote = true;

    g_iYesVotes = 0;
    g_iNoVotes = 0;
    g_iVotesCast = 0;
    g_iTotalVotersRequired = 0;

    Menu hVoteMenu;
    char msg[256];
    g_SMAPI->Format(msg,sizeof(msg),GetTranslation("Voteban_StartedByPlayerReason"),
    initiatorController->GetPlayerName(),
    g_PlayersVoteban[iInitiator].sTargetName.c_str(),
    g_PlayersVoteban[iInitiator].sVoteReason.c_str()
    );
    PrintAllPrefixed(msg);
    char szTitle[256];
    g_SMAPI->Format(szTitle, sizeof(szTitle), GetTranslation("Voteban_GlobalMenuTitle"), g_sVoteTargetName.c_str());
    menus_api->SetTitleMenu(hVoteMenu, szTitle);

    menus_api->AddItemMenu(hVoteMenu,"",GetTranslation("Vote_VoteHint"),ITEM_DISABLED);

    menus_api->AddItemMenu(hVoteMenu, "yes", GetTranslation("Vote_Yes"), ITEM_DEFAULT);
    menus_api->AddItemMenu(hVoteMenu, "no", GetTranslation("Vote_No"), ITEM_DEFAULT);
    menus_api->SetExitMenu(hVoteMenu, false);

    menus_api->SetCallback(hVoteMenu, [](const char* szBack, const char* szFront, int iItem, int iSlot) {
        if (!g_bVoteInProgress) {
            menus_api->ClosePlayerMenu(iSlot);
            return;
        }
        
        if (strcmp(szBack, "yes") == 0) g_iYesVotes++;
        else if (strcmp(szBack, "no") == 0) g_iNoVotes++;

        menus_api->ClosePlayerMenu(iSlot);

        g_iVotesCast++;

        if (g_iVotesCast >= g_iTotalVotersRequired) {
            EndGlobalVoteban();
        }
    });

    for (int i = 0; i < MAX_PLAYERS; i++) {
        auto pController = CCSPlayerController::FromSlot(i);
        if (!pController || !pController->IsConnected() || pController->IsBot() || pController->GetTeam() <= 1) continue;
        auto pPawn = pController->GetPlayerPawn();
        if (!pPawn) continue;
        if (g_iVoteTarget == i) continue;
        
        g_iTotalVotersRequired++;
        menus_api->DisplayPlayerMenu(hVoteMenu, i, true, true);
    }

    if (g_iTotalVotersRequired == 0) {
        g_bVoteInProgress = false;
        return;
    }

    utils->CreateTimer(15.0f, []() {
        if (g_bVoteInProgress) {
            EndGlobalVoteban();
        }
        return -1.0f;
    });
}









void OpenReasonMenu(int iSlot, const char* szReason = "") {
    if (iSlot < 0 || iSlot > MAX_PLAYERS) return;

    Menu hMenu;
    menus_api->SetTitleMenu(hMenu,GetTranslation("Voteban_ReasonTitle"));

    if (!szReason || szReason[0] == '\0') {
        menus_api->AddItemMenu(hMenu,"",GetTranslation("Voteban_InputReasonInChat"),ITEM_DISABLED);
    } else {
        char reason[256];
        g_SMAPI->Format(reason,sizeof(reason),GetTranslation("VoteBan_InputedReason"),szReason);
        menus_api->AddItemMenu(hMenu,"",reason,ITEM_DISABLED);
        menus_api->AddItemMenu(hMenu,"confirm",GetTranslation("Voteban_ConfirmButton"),ITEM_DEFAULT);
    }
    menus_api->AddItemMenu(hMenu,"return",GetTranslation("Voteban_ReturnButton"),ITEM_DEFAULT);

    menus_api->SetBackMenu(hMenu,true);
    menus_api->SetExitMenu(hMenu,true);
    std::string sReason(szReason);
    menus_api->SetCallback(hMenu,[sReason](const char* szBack, const char* szFront, int iItem, int iSlot){
        if (!szBack || szBack[0] == '\0') return;
        if (strcmp(szBack,"exit") == 0 || 
            strcmp(szBack,"back") == 0 || 
            strcmp(szBack,"return") == 0) {
                menus_api->ClosePlayerMenu(iSlot);
                g_bPendingChatReason[iSlot] = false;
                return;
        }

        if (strcmp(szBack,"confirm") == 0) {
            g_PlayersVoteban[iSlot].sVoteReason = sReason;

            StartGlobalVoteban(iSlot);



        }

    });

    menus_api->DisplayPlayerMenu(hMenu,iSlot,true,true);
}

bool OnVotebanCommand(int iSlot, const char* szContent) {
    if (iSlot < 0 || iSlot > MAX_PLAYERS) return true;

    if (gpGlobals->curtime < g_flNextGlobalVotebanTime) {
        char msg[256];
        int iTimeLeft = (int)(g_flNextGlobalVotebanTime - gpGlobals->curtime);
        g_SMAPI->Format(msg, sizeof(msg), GetTranslation("Voteban_GlobalCooldown"), iTimeLeft);
        PrintSlotPrefixed(iSlot, msg);
        return true;
    }

    if (gpGlobals->curtime < g_PlayersVoteban[iSlot].flNextVotebanTime) {
        char msg[256];
        int iTimeLeft = (int)(g_PlayersVoteban[iSlot].flNextVotebanTime - gpGlobals->curtime);
        g_SMAPI->Format(msg, sizeof(msg), GetTranslation("Voteban_PersonalCooldown"), iTimeLeft);
        PrintSlotPrefixed(iSlot, msg);
        return true;
    }
    
    Menu hMenu;
    if (g_PlayersVoteban[iSlot].bHaveActiveVote) return true;
    g_PlayersVoteban[iSlot].Reset();

    menus_api->SetTitleMenu(hMenu,GetTranslation("Voteban_MainMenuTitle"));
    int iCount = 0;
    for (int i = 0; i < MAX_PLAYERS;i++) {
        auto pController = CCSPlayerController::FromSlot(i);
        if (!pController || !pController->IsConnected()) continue;

        auto pPawn = pController->GetPlayerPawn();
        if (!pPawn) continue;

        if (admin_api) {
            if (admin_api->IsAdmin(i) && admin_api->HasPermission(i,g_sAdminPermission.c_str())) continue;
        }

        menus_api->AddItemMenu(hMenu,std::to_string(i).c_str(),pController->GetPlayerName(),ITEM_DEFAULT);
        iCount++;


    }
    if (iCount == 0) {
        menus_api->AddItemMenu(hMenu,"",GetTranslation("Voteban_NoAvailablePlayers"));
    }

    menus_api->SetExitMenu(hMenu,true);
    menus_api->SetCallback(hMenu,[](const char* szBack, const char* szFront, int iItem, int iSlot){
        if (!szBack || szBack[0] == '\0') return;
        if (strcmp(szBack,"exit") == 0) {
            menus_api->ClosePlayerMenu(iSlot);
            g_PlayersVoteban[iSlot].Reset();
            return;
        }

        if (iItem < 7) {
            int iTarget = atoi(szBack);
            auto pController = CCSPlayerController::FromSlot(iTarget);
            if (!pController) return;
            uint64_t iSteamID64 = pController->m_steamID;
            std::string sName = pController->GetPlayerName();

            time_t now = std::time(nullptr);
            int64_t iCurrentTime = (int64_t)now;

            char query[512];
            g_SMAPI->Format(query, sizeof(query), 
                "SELECT adminsid, reason, expires_at FROM jb_punishments WHERE sid64 = %llu AND (expires_at = 0 OR expires_at > %lld);",
                iSteamID64, iCurrentTime);

            if (connection) {
                connection->Query(query, [iSlot, iTarget, sName](ISQLQuery* res) {
                    auto pInitiator = CCSPlayerController::FromSlot(iSlot);
                    auto pTargetEnt = CCSPlayerController::FromSlot(iTarget);

                    if (!pInitiator || !pInitiator->IsConnected() || !pTargetEnt || !pTargetEnt->IsConnected()) {
                        return; 
                    }

                    if (res) {
                        auto result = res->GetResultSet();
                        if (result && result->GetRowCount() > 0) {
                            PrintSlotPrefixed(iSlot, GetTranslation("Voteban_PlayerAlreadyBanned"));
                        } else {
                            g_PlayersVoteban[iSlot].iInitiator = iSlot;
                            g_PlayersVoteban[iSlot].iTarget = iTarget;
                            g_PlayersVoteban[iSlot].sTargetName = sName;

                            g_bPendingChatReason[iSlot] = true;
                            OpenReasonMenu(iSlot, "");
                        }
                    }
                });
            } else {
                PrintSlotPrefixed(iSlot, "Database connection error.");
            }
        }

    });

    menus_api->DisplayPlayerMenu(hMenu,iSlot,true,true);

    return true;
}


// =========================================
// OTHER
// =========================================


CGameEntitySystem* GameEntitySystem() {
    return utils ? utils->GetCGameEntitySystem() : nullptr;
}



void StartupServer() {
    g_pGameEntitySystem = GameEntitySystem();
    g_pEntitySystem = utils->GetCEntitySystem();
    gpGlobals = utils->GetCGlobalVars();

    for (int i = 0; i < 64; i ++) {
        g_PlayersVoteban[i].flNextVotebanTime = 0.0f;
    }
}

bool jb_votebanct::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late) {
    PLUGIN_SAVEVARS();

    GET_V_IFACE_CURRENT(GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION);
    GET_V_IFACE_ANY(GetEngineFactory, g_pSchemaSystem, ISchemaSystem, SCHEMASYSTEM_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetFileSystemFactory, g_pFullFileSystem, IFileSystem, FILESYSTEM_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, engine, IVEngineServer2, SOURCE2ENGINETOSERVER_INTERFACE_VERSION);
    GET_V_IFACE_ANY(GetServerFactory, g_pSource2Server, ISource2Server, SOURCE2SERVER_INTERFACE_VERSION);
    GET_V_IFACE_ANY(GetServerFactory, g_pSource2GameClients, IServerGameClients, SOURCE2GAMECLIENTS_INTERFACE_VERSION);
    GET_V_IFACE_ANY(GetServerFactory, g_pSource2GameEntities, ISource2GameEntities, SOURCE2GAMEENTITIES_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, g_pNetworkSystem, INetworkSystem, NETWORKSYSTEM_INTERFACE_VERSION);

    ConVar_Register(FCVAR_SERVER_CAN_EXECUTE | FCVAR_GAMEDLL);
    g_SMAPI->AddListener(this, this);

    return true;
}



void jb_votebanct::AllPluginsLoaded() {
    int ret;
    utils = (IUtilsApi*)g_SMAPI->MetaFactory(Utils_INTERFACE, &ret, nullptr);
    if (ret == META_IFACE_FAILED) {
        META_CONPRINTF("%s | Missing UTILS plugin.",g_PLAPI->GetLogTag());
        engine->ServerCommand(("meta unload " + std::to_string(g_PLID)).c_str());
        return;
    }


    menus_api = (IMenusApi*)g_SMAPI->MetaFactory(Menus_INTERFACE, &ret, nullptr);
    if (ret == META_IFACE_FAILED) {
        META_CONPRINTF("%s | Missing UTILS plugin.",g_PLAPI->GetLogTag());
        engine->ServerCommand(("meta unload " + std::to_string(g_PLID)).c_str());
        return;
    }

    players_api = (IPlayersApi*)g_SMAPI->MetaFactory(PLAYERS_INTERFACE, &ret, nullptr);
    if (ret == META_IFACE_FAILED) {
        META_CONPRINTF("%s | Missing UTILS plugin.",g_PLAPI->GetLogTag());
        engine->ServerCommand(("meta unload " + std::to_string(g_PLID)).c_str());
        return;
    }

    jailbreak_api =(IJailbreakApi*)g_SMAPI->MetaFactory(JAILBREAK_INTERFACE, &ret, nullptr);
    if (ret == META_IFACE_FAILED) {
        META_CONPRINTF("%s | Missing Jailbreak Core plugin.",g_PLAPI->GetLogTag());
        engine->ServerCommand(("meta unload " + std::to_string(g_PLID)).c_str());
        return;
    }

    ISQLInterface* sql_interface = (ISQLInterface*)g_SMAPI->MetaFactory(SQLMM_INTERFACE, &ret, nullptr);
    if (ret == META_IFACE_FAILED) {
        META_CONPRINTF("%s | Missing Mysql plugin.",g_PLAPI->GetLogTag());
        engine->ServerCommand(("meta unload " + std::to_string(g_PLID)).c_str());
        return;
    }

    admin_api = (IAdminApi*)g_SMAPI->MetaFactory(Admin_INTERFACE, &ret, nullptr);
    if (ret == META_IFACE_FAILED) {
        admin_api = nullptr;
        META_CONPRINTF("[%s] Admin System not found, no admin immunity.\n",g_PLAPI->GetLogTag());
    }

    mysql_client = sql_interface->GetMySQLClient();

    LoadConfig();
    LoadDatabase();
    LoadTranslations();

    utils->RegCommand(g_PLID,{"mm_votebanct"},{"!votebanct"},OnVotebanCommand);
    

    utils->StartupServer(g_PLID, StartupServer);

    utils->AddChatListenerPre(g_PLID,[](int iSlot, const char* szContent, bool bTeam){
        if (!g_bPendingChatReason[iSlot]) return true;

        std::string sInput = szContent;
        sInput.erase(std::remove(sInput.begin(), sInput.end(), '\"'), sInput.end());
        if (sInput.empty()) {
            PrintSlotPrefixed(iSlot, GetTranslation("Voteban_EmptyInputRetry"));
            return false;
        }

        OpenReasonMenu(iSlot,sInput.c_str());
        g_bPendingChatReason[iSlot] = false;
        return false;

    });

    utils->HookEvent(g_PLID,"player_disconnect",[](const char* szName, IGameEvent* pEvent, bool bDontBroadcast){
        int iSlot = pEvent->GetInt("userid");
        if (iSlot < 0 || iSlot >= 64) return;

        g_PlayersVoteban[iSlot].Reset();
        g_PlayersVoteban[iSlot].flNextVotebanTime = 0.0f;
        g_bPendingChatReason[iSlot] = false;
    });
    
}

bool jb_votebanct::Unload(char* error, size_t maxlen) {
    jailbreak_api->ClearAllPluginHooks(g_PLID);
    utils->ClearAllHooks(g_PLID);
    ConVar_Unregister();

   
    return true;
}

const char* jb_votebanct::GetAuthor() { return "niffox"; }
const char* jb_votebanct::GetDate() { return __DATE__; }
const char* jb_votebanct::GetDescription() { return "[JB] Voteban CT"; }
const char* jb_votebanct::GetLicense() { return "GPL"; }
const char* jb_votebanct::GetLogTag() { return "[JB] Voteban CT"; }
const char* jb_votebanct::GetName() { return "[JB] Voteban CT"; }
const char* jb_votebanct::GetURL() { return "https://t.me/niffox_2q"; }
const char* jb_votebanct::GetVersion() { return "1.0.1"; }
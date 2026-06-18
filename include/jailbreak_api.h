#pragma once
#include <functional>
#include <vector>
#include <string>

#define JAILBREAK_INTERFACE "IJailbreakApi"

class IJailbreakApi {
public:
    virtual ~IJailbreakApi() = default;

    virtual void SetWarden(int iSlot) = 0;
    virtual int GetWarden() = 0;
    virtual void ClearWarden() = 0;

    virtual void IncreaseDayCount() = 0;
    virtual void SetDayCount(int value) = 0;
    virtual int GetDayCount() = 0;

    virtual bool GetCellsValue() = 0; // return true on opened and false on closed
    virtual bool OpenCells() = 0; // return false on error
    virtual bool CloseCells() = 0; // return false on error

    virtual void MakeRebelPrisoner(int iSlot) = 0;
    virtual void RemoveRebelPrisoner(int iSlot) = 0;
    virtual bool IsPrisonerRebel(int iSlot) = 0;
    virtual std::vector<int> GetRebelPrisoners() = 0;
    virtual void ClearRebelPrisoners() = 0;

    virtual void GivePrisonerFreeday(int iSlot) = 0;
    virtual void RemovePrisonerFreeday(int iSlot) = 0;
    virtual bool IsPrisonerFreeday(int iSlot) = 0;
    virtual std::vector<int> GetFreedayPrisoners() = 0;
    virtual void ClearFreedayPrisoners() = 0;

    virtual void MutePrisoner(int iSlot) = 0;
    virtual void UnmutePrisoner(int iSlot) = 0;
    virtual bool IsPrisonerMuted(int iSlot) = 0;
    virtual void UnmuteAllPrisoners() = 0;

    virtual void GiveLR(int iSlot) = 0;     // After obtaining last request auto firing OnGiveLR
    virtual int  GetLRPrisoner() = 0;  // Return player slot
    virtual void ClearLR(int iSlot) = 0;    // -1 is default

    virtual void RegisterGameFeature(SourceMM::PluginId id,const std::string& keyName, const std::string& displayName, std::function<void(int)> onStart) = 0; // false is keyname already taken
    virtual void UnregisterGameFeature(SourceMM::PluginId id,const std::string& keyName) = 0;

    // Register and Unregister Last Request (last terrorist alive) features
    virtual void RegisterLRFeature(SourceMM::PluginId id,const std::string& keyName, const std::string& displayName, std::function<void(int)> onStart) = 0; // false is keyname already taken
    virtual void UnregisterLRFeature(SourceMM::PluginId id,const std::string& keyName) = 0;

    virtual void OnNewWardenListener(SourceMM::PluginId id,std::function<void(int)> listener) = 0;
    virtual void OnWardenDieListener(SourceMM::PluginId id,std::function<void(int)> listener) = 0;

    virtual void OnCellsOpenListener(SourceMM::PluginId id,std::function<void()> listener) = 0;
    virtual void OnCellsCloseListener(SourceMM::PluginId id,std::function<void()> listener) = 0;

    virtual void OnRebelPrisoner(SourceMM::PluginId id,std::function<void(int)> listener) = 0;
    virtual void OnRemoveRebelPrisoner(SourceMM::PluginId id,std::function<void(int)> listener) = 0;

    virtual void OnGiveFreedayPrisoner(SourceMM::PluginId id,std::function<void(int)> listener) = 0;
    virtual void OnRemoveFreedayPrisoner(SourceMM::PluginId id,std::function<void(int)> listener) = 0;

    virtual void OnGivePrisonerMute(SourceMM::PluginId id,std::function<void(int)> listener) = 0;
    virtual void OnRemovePrisonerMute(SourceMM::PluginId id,std::function<void(int)> listener) = 0;

    virtual void OnGiveLR(SourceMM::PluginId id,std::function<void(int)> listener) = 0;
    virtual void OnClearLR(SourceMM::PluginId id,std::function<void()> listener) = 0;

    virtual void ClearAllPluginHooks(SourceMM::PluginId id) = 0;

};
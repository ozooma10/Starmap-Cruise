#include "Settings.h"

#include "REX/REX.h"
#include "REX/FIniSettingStore.h"
#include "REX/TIniSetting.h"

namespace CFS::Settings
{
    namespace
    {
        REX::TIniSetting<bool> bVerboseLog{ "General", "bVerboseLog", true };
    }

    void Load()
    {
        const auto store = REX::FIniSettingStore::GetSingleton();
        store->Init("Data/SFSE/Plugins/CruiseFromStarmap.ini",
            "Data/SFSE/Plugins/CruiseFromStarmapCustom.ini");
        store->Load();

        REX::INFO("config: verbose={}", bVerboseLog.GetValue());
    }

    bool Verbose() { return bVerboseLog.GetValue(); }
}

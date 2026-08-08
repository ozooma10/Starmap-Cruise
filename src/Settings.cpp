#include "Settings.h"

#include "REX/REX.h"
#include "REX/FIniSettingStore.h"
#include "REX/TIniSetting.h"

namespace CFS::Settings
{
    namespace
    {
        REX::TIniSetting<bool> bVerboseLog{ "General", "bVerboseLog", true };
        // Campaign-only capture aid, deliberately absent from the shipped and
        // example INIs; set it in CruiseFromStarmapCustom.ini. 0 = off.
        // 1 = dump the galaxy focus diagnostics once in the proven-selection
        // state (the run continues normally). 2 = skip the native selection
        // rung so the existing one-shot diagnostic fires in the no-selection
        // state and the request fails closed as designed.
        REX::TIniSetting<std::int32_t> iGalaxyDiagnosticsMode{
            "Diagnostics", "iGalaxyDiagnosticsMode", 0
        };
    }

    void Load()
    {
        const auto store = REX::FIniSettingStore::GetSingleton();
        store->Init("Data/SFSE/Plugins/CruiseFromStarmap.ini",
            "Data/SFSE/Plugins/CruiseFromStarmapCustom.ini");
        store->Load();

        REX::INFO("config: verbose={} galaxyDiagnosticsMode={}",
            bVerboseLog.GetValue(), iGalaxyDiagnosticsMode.GetValue());
    }

    bool Verbose() { return bVerboseLog.GetValue(); }

    std::int32_t GalaxyDiagnosticsMode()
    {
        return iGalaxyDiagnosticsMode.GetValue();
    }
}

#include "Settings.h"

#include <vector>

#include "REX/REX.h"
#include "REX/FIniSettingStore.h"
#include "REX/TIniSetting.h"

#include <algorithm>
#include <cctype>

namespace CFS::Settings
{
    namespace
    {
        REX::TIniSetting<std::string> sMode{ "General", "sMode", "TapHoldCruise" };
        REX::TIniSetting<bool> bShowMarker{ "General", "bShowMarker", false };
        REX::TIniSetting<bool> bShowDestinationName{ "General", "bShowDestinationName", true };
        REX::TIniSetting<bool> bVerboseLog{ "General", "bVerboseLog", true };
    }

    void Load()
    {
        const auto store = REX::FIniSettingStore::GetSingleton();
        store->Init("Data/SFSE/Plugins/CruiseFromStarmap.ini",
            "Data/SFSE/Plugins/CruiseFromStarmapCustom.ini");
        store->Load();

        auto mode = sMode.GetValue();
        std::ranges::transform(mode, mode.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        if (mode != "tapholdcruise")
            REX::WARN("sMode={} is no longer exposed; using the supported TapHoldCruise flow",
                sMode.GetValue());

        REX::INFO("config: mode=TapHoldCruise marker={} destinationName={} verbose={}",
            bShowMarker.GetValue(), bShowDestinationName.GetValue(), bVerboseLog.GetValue());
    }

    bool ShowMarker() { return bShowMarker.GetValue(); }
    bool ShowDestinationName() { return bShowDestinationName.GetValue(); }
    bool Verbose() { return bVerboseLog.GetValue(); }
}

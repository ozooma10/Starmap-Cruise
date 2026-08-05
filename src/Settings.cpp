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
        REX::TIniSetting<std::string> sMode{ "General", "sMode", "SelectThenCruise" };
        REX::TIniSetting<bool> bShowMarker{ "General", "bShowMarker", false };
        REX::TIniSetting<bool> bShowDestinationName{ "General", "bShowDestinationName", true };
        REX::TIniSetting<bool> bVerboseLog{ "General", "bVerboseLog", true };
        Mode g_mode{ Mode::kSelectThenCruise };

        const char* ModeName(Mode a_mode)
        {
            switch (a_mode) {
            case Mode::kSelectThenCruise:
                return "SelectThenCruise";
            case Mode::kMarkOnly:
                return "MarkOnly";
            case Mode::kHoldToCruise:
                return "HoldToCruise";
            }
            return "SelectThenCruise";
        }
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
        if (mode == "selectthencruise") {
            g_mode = Mode::kSelectThenCruise;
        } else if (mode == "markonly") {
            g_mode = Mode::kMarkOnly;
        } else if (mode == "holdtocruise") {
            g_mode = Mode::kHoldToCruise;
        } else {
            g_mode = Mode::kSelectThenCruise;
            REX::WARN("Unknown sMode='{}'; using SelectThenCruise", sMode.GetValue());
        }

        REX::INFO("config: mode={} marker={} destinationName={} verbose={}",
            ModeName(g_mode),
            bShowMarker.GetValue(), bShowDestinationName.GetValue(), bVerboseLog.GetValue());
    }

    Mode GetMode() { return g_mode; }
    bool ShowMarker() { return bShowMarker.GetValue(); }
    bool ShowDestinationName() { return bShowDestinationName.GetValue(); }
    bool Verbose() { return bVerboseLog.GetValue(); }
}

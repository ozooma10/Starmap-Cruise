#include "Starfield/StarfieldCruiseAdapter.h"

#include "SFSE/SFSE.h"

namespace
{
    void OnMessage(SFSE::MessagingInterface::Message* a_message)
    {
        if (a_message->type != SFSE::MessagingInterface::kPostDataLoad)
            return;
        if (!StarfieldCruiseAdapter::GetSingleton().Initialize()) {
            REX::ERROR("Cruise From Starmap initialization failed; plugin remains disabled");
        }
    }
}

SFSE_PLUGIN_LOAD(const SFSE::LoadInterface* a_sfse)
{
    SFSE::Init(a_sfse, { .trampoline = true, .trampolineSize = 512 });
    const auto runtime = a_sfse->RuntimeVersion();
    REX::INFO("{} v{} loading; current runtime {}",
        SFSE::GetPluginName(), SFSE::GetPluginVersion().string(),
        runtime.string());
    const auto messaging = SFSE::GetMessagingInterface();
    if (!messaging || !messaging->RegisterListener(OnMessage)) {
        REX::ERROR("SFSE MessagingInterface unavailable; plugin disabled");
        return false;
    }
    REX::INFO("load complete");
    return true;
}

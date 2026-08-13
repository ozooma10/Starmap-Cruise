#pragma once

namespace CFS::MainThreadUiPump
{
    using PostAdvanceCallback = void (*)();

    // Installs hook around UI_AdvanceActiveMenus.
    // The callback runs after the original returns, on the Scaleform-owning game thread that advance's AS3 work. 
    [[nodiscard]] bool Install(PostAdvanceCallback a_callback);
}

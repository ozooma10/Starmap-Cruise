#pragma once

namespace CFS::MainThreadUiPump
{
    // Installs a byte-verified 1.16.244 hook around UI_AdvanceActiveMenus.
    // The callback runs after the original returns, on the Scaleform-owning
    // game thread with that advance's AS3 work complete.
    [[nodiscard]] bool Install();
}

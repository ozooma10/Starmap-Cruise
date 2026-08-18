#pragma once

namespace CFS::ScaleformPostAdvancePump
{
    using PostAdvanceCallback = void (*)();
    [[nodiscard]] bool Install(PostAdvanceCallback a_callback);
}

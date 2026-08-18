#pragma once

namespace CFS::UiPostAdvanceHook
{
    using PostAdvanceCallback = void (*)();
    [[nodiscard]] bool Install(PostAdvanceCallback a_callback);
}

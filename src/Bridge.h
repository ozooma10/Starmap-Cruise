#pragma once

namespace RE
{
    class IMenu;
}

namespace CFS::Bridge
{
    void Initialize();
    void OnFrame();
    void OnUiSafeFrame();
    void OnMovieCreated(RE::IMenu* a_menu);
}


#pragma once

#include "RE/Starfield.h"

namespace CFS::Bridge
{
    void Initialize();
    void OnFrame();
    void OnUiSafeFrame();
    void OnMovieCreated(RE::IMenu* a_menu);
}


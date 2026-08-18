#pragma once

#include "RE/Starfield.h"

namespace CFS::ScaleformEvents
{
    bool DispatchUiEvent(RE::Scaleform::GFx::ASMovieRootBase* a_root, const char* a_type, const RE::Scaleform::GFx::Value* a_params);
}

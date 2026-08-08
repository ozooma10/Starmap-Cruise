#pragma once

#include "RE/Starfield.h"

namespace CFS::ScaleformEvents
{
    // Dispatches a named BSUIDataManager event from a live movie root, using
    // the stock dispatchEvent path with a dispatchCustomEvent fallback. The
    // caller owns every movie-liveness, threading, and post-advance gate; this
    // helper performs only the invocation itself.
    bool DispatchUiEvent(RE::Scaleform::GFx::ASMovieRootBase* a_root,
        const char* a_type, const RE::Scaleform::GFx::Value* a_params);
}

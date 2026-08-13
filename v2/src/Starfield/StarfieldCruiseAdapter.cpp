#include "Starfield/StarfieldCruiseAdapter.h"

#include "MainThreadUiPump.h"

#include <cstring>
#include <limits>

namespace
{
    constexpr const char* MapMenuName = "GalaxyStarMapMenu";

    std::uint32_t ToMapGeneration(std::uint64_t sequence)
    {
        constexpr auto generationCount = static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max());
        return static_cast<std::uint32_t>(((sequence - 1) % generationCount) + 1);
    }
}

StarfieldCruiseAdapter& StarfieldCruiseAdapter::GetSingleton()
{
    static StarfieldCruiseAdapter singleton;
    return singleton;
}

StarfieldCruiseAdapter::StarfieldCruiseAdapter() : runtime_(bodySource_, commands_) {}

bool StarfieldCruiseAdapter::Initialize()
{
    if (initialized_) {
        return true;
    }

    const auto menus = SFSE::GetMenuInterface();
    if (!menus) {
        REX::ERROR("StarfieldCruiseAdapter: SFSE menu interface unavailable; v2 disabled");
        return false;
    }

    if (!CFS::MainThreadUiPump::Install(&OnUiSafeFrame)) {
        REX::ERROR("StarfieldCruiseAdapter: post-advance UI pump unavailable; v2 disabled");
        return false;
    }

    menus->Register(&OnMovieCreated);
    initialized_ = true;
    REX::INFO("StarfieldCruiseAdapter: initialized with copied map-movie observations");
    return true;
}

void StarfieldCruiseAdapter::OnMovieCreated(RE::IMenu* menu)
{
    if (!menu) {
        return;
    }

    const char* name = menu->menuName.c_str();
    if (!name || std::strcmp(name, MapMenuName) != 0) {
        return;
    }

    // This callback may run outside the game-thread pump. Publish only an owned sequence number; no menu or movie pointer crosses the boundary.
    GetSingleton().mapMovieSequence_.fetch_add(1, std::memory_order_release);
}

void StarfieldCruiseAdapter::OnUiSafeFrame()
{
    GetSingleton().DrainMapMovieObservation();
}

void StarfieldCruiseAdapter::DrainMapMovieObservation()
{
    const auto sequence = mapMovieSequence_.load(std::memory_order_acquire);
    if (sequence == 0 || sequence == consumedMapMovieSequence_) {
        return;
    }

    runtime_.OnMapMovieCreated(ToMapGeneration(sequence));
    presenter_.Invalidate();
    consumedMapMovieSequence_ = sequence;
}

bool StarfieldCruiseAdapter::Commands::CloseMap()
{
    return false;
}

bool StarfieldCruiseAdapter::Commands::PressCruise()
{
    return false;
}

bool StarfieldCruiseAdapter::Commands::RequestCourse(FormID)
{
    return false;
}

bool StarfieldCruiseAdapter::ActionView::Apply(const MapActionPresentation&)
{
    return false;
}

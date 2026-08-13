#include "Starfield/StarfieldCruiseAdapter.h"

#include "MainThreadUiPump.h"

#include "RE/Starfield.h"
#include "REX/REX.h"
#include "SFSE/SFSE.h"

#include <cstring>
#include <limits>
#include <utility>

namespace
{
    constexpr const char* MapMenuName = "GalaxyStarMapMenu";

    std::uint32_t ToIdentityComponent(std::uint64_t sequence)
    {
        if (sequence == 0) {
            return 0;
        }

        constexpr auto generationCount = static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max());
        return static_cast<std::uint32_t>(((sequence - 1) % generationCount) + 1);
    }

    bool IsPlayerFlying()
    {
        static_assert(RE::ID::TESObjectREFR::IsInSpace.id() == 63482);

        const auto player = RE::PlayerCharacter::GetSingleton();
        const auto ship = player ? player->GetSpaceship() : nullptr;
        return ship && ship->IsInSpace(false);
    }
}

class StarfieldCruiseAdapter::MapLifecycleSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
public:
    explicit MapLifecycleSink(StarfieldCruiseAdapter& owner) : owner_(owner) {}

    RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent& event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
    {
        const char* name = event.menuName.c_str();
        if (name && std::strcmp(name, MapMenuName) == 0) {
            owner_.RecordMapLifecycleObservation(event.opening);
        }

        return RE::BSEventNotifyControl::kContinue;
    }

private:
    StarfieldCruiseAdapter& owner_;
};

StarfieldCruiseAdapter& StarfieldCruiseAdapter::GetSingleton()
{
    static StarfieldCruiseAdapter singleton;
    return singleton;
}

StarfieldCruiseAdapter::StarfieldCruiseAdapter() : runtime_(bodySource_, commands_), mapLifecycleSink_(std::make_unique<MapLifecycleSink>(*this)) {}

StarfieldCruiseAdapter::~StarfieldCruiseAdapter() = default;

bool StarfieldCruiseAdapter::Initialize()
{
    if (initialized_) {
        return true;
    }

    const auto menus = SFSE::GetMenuInterface();
    const auto ui = RE::UI::GetSingleton();
    if (!menus || !ui) {
        REX::ERROR("StarfieldCruiseAdapter: required menu interface unavailable (sfse={} ui={}); v2 disabled", static_cast<bool>(menus), static_cast<bool>(ui));
        return false;
    }

    if (!CFS::MainThreadUiPump::Install(&OnUiSafeFrame)) {
        REX::ERROR("StarfieldCruiseAdapter: post-advance UI pump unavailable; v2 disabled");
        return false;
    }

    menus->Register(&OnMovieCreated);
    ui->RegisterSink<RE::MenuOpenCloseEvent>(mapLifecycleSink_.get());
    initialized_ = true;
    REX::INFO("StarfieldCruiseAdapter: initialized with copied map movie and lifecycle observations");
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
    auto& adapter = GetSingleton();
    adapter.DrainMapMovieObservation();
    adapter.DrainMapLifecycleObservations();
}

void StarfieldCruiseAdapter::RecordMapLifecycleObservation(bool opening)
{
    std::lock_guard lock {mapLifecycleMutex_};

    if (mapLifecycleOverflow_) {
        return;
    }
    if (pendingMapLifecycleCount_ == pendingMapLifecycle_.size()) {
        pendingMapLifecycleCount_ = 0;
        mapLifecycleOverflow_ = true;
        publishedMapIdentity_ = {};
        return;
    }

    MapSessionIdentity identity = publishedMapIdentity_;
    if (opening) {
        ++mapSessionSequence_;
        if (mapSessionSequence_ == 0) {
            ++mapSessionSequence_;
        }

        identity = {
            .session = ToIdentityComponent(mapSessionSequence_),
            .generation = ToIdentityComponent(mapMovieSequence_.load(std::memory_order_acquire)),
        };
        publishedMapIdentity_ = identity;
    } else {
        publishedMapIdentity_ = {};
    }

    pendingMapLifecycle_[pendingMapLifecycleCount_++] = {
        .opening = opening,
        .identity = identity,
    };
}

void StarfieldCruiseAdapter::DrainMapMovieObservation()
{
    const auto sequence = mapMovieSequence_.load(std::memory_order_acquire);
    if (sequence == 0 || sequence == consumedMapMovieSequence_) {
        return;
    }

    runtime_.OnMapMovieCreated(ToIdentityComponent(sequence));
    presenter_.Invalidate();
    consumedMapMovieSequence_ = sequence;
}

void StarfieldCruiseAdapter::DrainMapLifecycleObservations()
{
    std::array<MapLifecycleObservation, MaxPendingMapLifecycleObservations> observations;
    std::size_t observationCount = 0;
    bool overflowed = false;

    {
        std::lock_guard lock {mapLifecycleMutex_};
        observationCount = pendingMapLifecycleCount_;
        for (std::size_t index = 0; index < observationCount; ++index) {
            observations[index] = pendingMapLifecycle_[index];
        }
        pendingMapLifecycleCount_ = 0;
        overflowed = std::exchange(mapLifecycleOverflow_, false);
        if (overflowed) {
            publishedMapIdentity_ = {};
        }
    }

    if (overflowed) {
        const auto movieSequence = mapMovieSequence_.load(std::memory_order_acquire);
        if (movieSequence != 0) {
            runtime_.OnMapMovieCreated(ToIdentityComponent(movieSequence));
        }
        presenter_.Invalidate();
        REX::ERROR("StarfieldCruiseAdapter: map lifecycle observation queue overflowed; active map session invalidated");
        return;
    }

    for (std::size_t index = 0; index < observationCount; ++index) {
        const auto& observation = observations[index];
        if (observation.opening) {
            runtime_.OnMapOpened({
                .identity = observation.identity,
                .flying = IsPlayerFlying(),
                // Until the HUD source supplies exact Cruise state, treating  unknown as active prevents an unsafe hold-to-engage path.
                .cruiseWasActive = true,
                .currentSystemId = std::nullopt,
            });
        } else {
            runtime_.OnMapClosed(observation.identity);
        }
        presenter_.Invalidate();
    }
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

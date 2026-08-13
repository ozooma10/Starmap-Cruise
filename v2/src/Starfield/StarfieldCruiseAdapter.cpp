#include "Starfield/StarfieldCruiseAdapter.h"

#include "MainThreadUiPump.h"
#include "Scaleform/ValueAccess.h"

#include "RE/Starfield.h"
#include "REX/REX.h"
#include "SFSE/SFSE.h"

#include <chrono>
#include <cstring>
#include <limits>
#include <utility>

namespace
{
    using Clock = std::chrono::steady_clock;

    constexpr const char* MapMenuName = "GalaxyStarMapMenu";
    constexpr const char* MapDataFeed = "StarMapMenuData";
    constexpr auto MapMovieSettleTime = std::chrono::milliseconds(250);

    std::uint32_t ToIdentityComponent(std::uint64_t sequence)
    {
        if (sequence == 0) {
            return 0;
        }

        constexpr auto generationCount = static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max());
        return static_cast<std::uint32_t>(((sequence - 1) % generationCount) + 1);
    }

    std::uintptr_t PackIdentity(const MapSessionIdentity& identity)
    {
        static_assert(sizeof(std::uintptr_t) >= sizeof(std::uint64_t));
        return (static_cast<std::uintptr_t>(identity.session) << 32) |
            identity.generation;
    }

    MapSessionIdentity UnpackIdentity(const void* token)
    {
        const auto packed = reinterpret_cast<std::uintptr_t>(token);
        return {
            .session = static_cast<std::uint32_t>(packed >> 32),
            .generation = static_cast<std::uint32_t>(packed),
        };
    }

    bool IsPlayerFlying()
    {
        static_assert(RE::ID::TESObjectREFR::IsInSpace.id() == 63482);

        const auto player = RE::PlayerCharacter::GetSingleton();
        const auto ship = player ? player->GetSpaceship() : nullptr;
        return ship && ship->IsInSpace(false);
    }

    MapView ReadMapView(RE::Scaleform::GFx::Value& data)
    {
        RE::Scaleform::GFx::Value value;
        if (!data.GetMember("iCurrentMenuView", &value) ||
            !(value.IsNumber() || value.IsInt() || value.IsUInt())) {
            return MapView::Unknown;
        }

        const auto raw = CFS::ScaleformValue::AsNumber(value);
        if (raw == 0.0) {
            return MapView::Galaxy;
        }
        if (raw == 1.0) {
            return MapView::System;
        }
        return MapView::Other;
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

class StarfieldCruiseAdapter::MapDataHandler final : public RE::Scaleform::GFx::FunctionHandler
{
public:
    explicit MapDataHandler(StarfieldCruiseAdapter& owner) : owner_(owner) {}

    void Call(const Params& params) override
    {
        const auto identity = UnpackIdentity(params.userData);
        if (!identity.IsValid()) {
            return;
        }

        RE::Scaleform::GFx::Value data;
        const auto view = CFS::ScaleformValue::Payload(params, data) ?
            ReadMapView(data) : MapView::Unknown;
        owner_.RecordMapViewObservation(identity, view);
    }

private:
    StarfieldCruiseAdapter& owner_;
};

StarfieldCruiseAdapter& StarfieldCruiseAdapter::GetSingleton()
{
    static StarfieldCruiseAdapter singleton;
    return singleton;
}

StarfieldCruiseAdapter::StarfieldCruiseAdapter() :
    runtime_(bodySource_, commands_),
    mapLifecycleSink_(std::make_unique<MapLifecycleSink>(*this)),
    mapDataHandler_(std::make_unique<MapDataHandler>(*this))
{}

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
    REX::INFO("StarfieldCruiseAdapter: initialized with copied map movie, lifecycle, and view observations");
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

    // This callback may run outside the game-thread pump. Publish only owned
    // timing and sequence values; no menu or movie pointer crosses the boundary.
    auto& adapter = GetSingleton();
    adapter.mapMovieBornTicks_.store(
        Clock::now().time_since_epoch().count(), std::memory_order_relaxed);
    adapter.mapMovieSequence_.fetch_add(1, std::memory_order_release);
}

void StarfieldCruiseAdapter::OnUiSafeFrame()
{
    auto& adapter = GetSingleton();
    adapter.DrainMapMovieObservation();
    adapter.DrainMapLifecycleObservations();
    adapter.TrySubscribeMapView();
    adapter.DrainMapViewObservation();
}

void StarfieldCruiseAdapter::RecordMapLifecycleObservation(bool opening)
{
    std::lock_guard lock {mapObservationMutex_};

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

void StarfieldCruiseAdapter::RecordMapViewObservation(
    const MapSessionIdentity& identity, MapView view)
{
    std::lock_guard lock {mapObservationMutex_};

    const auto currentGeneration =
        ToIdentityComponent(mapMovieSequence_.load(std::memory_order_acquire));
    if (!identity.IsValid() || identity.generation != currentGeneration ||
        !activeMapIdentity_.IsValid() ||
        activeMapIdentity_ != identity) {
        return;
    }

    if (!pendingMapView_ || pendingMapView_->identity != activeMapIdentity_) {
        pendingMapView_ = MapViewObservation {
            .identity = activeMapIdentity_,
            .view = view,
        };
        return;
    }

    if (pendingMapView_->view != view) {
        // Losing a transition could preserve target evidence across views.
        // Ambiguity therefore degrades to Unknown, which clears it fail-closed.
        pendingMapView_->view = MapView::Unknown;
    }
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
    mapViewSubscriptionIdentity_ = {};

    std::lock_guard lock {mapObservationMutex_};
    activeMapIdentity_ = {};
    pendingMapView_.reset();
}

void StarfieldCruiseAdapter::DrainMapLifecycleObservations()
{
    std::array<MapLifecycleObservation, MaxPendingMapLifecycleObservations> observations;
    std::size_t observationCount = 0;
    bool overflowed = false;

    {
        std::lock_guard lock {mapObservationMutex_};
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
        {
            std::lock_guard lock {mapObservationMutex_};
            activeMapIdentity_ = {};
            pendingMapView_.reset();
        }
        presenter_.Invalidate();
        REX::ERROR("StarfieldCruiseAdapter: map lifecycle observation queue overflowed; active map session invalidated");
        return;
    }

    for (std::size_t index = 0; index < observationCount; ++index) {
        const auto& observation = observations[index];
        if (observation.opening) {
            const bool accepted = runtime_.OnMapOpened({
                .identity = observation.identity,
                .flying = IsPlayerFlying(),
                // Until the HUD source supplies exact Cruise state, treating
                // unknown as active prevents an unsafe hold-to-engage path.
                .cruiseWasActive = true,
                .currentSystemId = std::nullopt,
            });
            if (accepted) {
                std::lock_guard lock {mapObservationMutex_};
                activeMapIdentity_ = observation.identity;
                pendingMapView_.reset();
            }
        } else {
            runtime_.OnMapClosed(observation.identity);
            std::lock_guard lock {mapObservationMutex_};
            if (activeMapIdentity_ == observation.identity) {
                activeMapIdentity_ = {};
                pendingMapView_.reset();
            }
        }
        presenter_.Invalidate();
    }
}

bool StarfieldCruiseAdapter::IsCurrentMapMovie(
    const void* root, const MapSessionIdentity& identity)
{
    if (!root || !identity.IsValid() ||
        ToIdentityComponent(mapMovieSequence_.load(std::memory_order_acquire)) !=
            identity.generation) {
        return false;
    }

    {
        std::lock_guard lock {mapObservationMutex_};
        if (activeMapIdentity_ != identity) {
            return false;
        }
    }

    const auto ui = RE::UI::GetSingleton();
    const RE::BSFixedString mapName {MapMenuName};
    if (!ui || !ui->IsMenuOpen(mapName)) {
        return false;
    }

    const auto menu = ui->GetMenu(mapName);
    return menu && menu->uiMovie && menu->uiMovie->asMovieRoot &&
        static_cast<const void*>(menu->uiMovie->asMovieRoot.get()) == root;
}

void StarfieldCruiseAdapter::TrySubscribeMapView()
{
    MapSessionIdentity identity;
    {
        std::lock_guard lock {mapObservationMutex_};
        identity = activeMapIdentity_;
    }

    if (!identity.IsValid() || mapViewSubscriptionIdentity_ == identity) {
        return;
    }

    const auto bornTicks = mapMovieBornTicks_.load(std::memory_order_acquire);
    if (bornTicks == 0 ||
        Clock::now() - Clock::time_point {Clock::duration {bornTicks}} <
            MapMovieSettleTime) {
        return;
    }

    const auto ui = RE::UI::GetSingleton();
    const RE::BSFixedString mapName {MapMenuName};
    if (!ui || !ui->IsMenuOpen(mapName)) {
        return;
    }

    const auto menu = ui->GetMenu(mapName);
    if (!menu || !menu->uiMovie || !menu->uiMovie->asMovieRoot) {
        return;
    }

    auto* root = menu->uiMovie->asMovieRoot.get();
    const auto rootIdentity = static_cast<const void*>(root);
    if (!IsCurrentMapMovie(rootIdentity, identity)) {
        return;
    }

    RE::Scaleform::GFx::Value manager;
    if (!root->GetVariable(&manager, "Shared.AS3.Data.BSUIDataManager") ||
        !(manager.IsObject() || manager.IsDisplayObject()) ||
        !IsCurrentMapMovie(rootIdentity, identity)) {
        return;
    }

    RE::Scaleform::GFx::Value args[2];
    root->CreateString(&args[0], MapDataFeed);
    root->CreateFunction(
        &args[1], mapDataHandler_.get(),
        reinterpret_cast<void*>(PackIdentity(identity)));
    if (!IsCurrentMapMovie(rootIdentity, identity)) {
        return;
    }

    if (!manager.Invoke("Subscribe", nullptr, args, 2)) {
        return;
    }
    if (!IsCurrentMapMovie(rootIdentity, identity)) {
        std::lock_guard lock {mapObservationMutex_};
        if (pendingMapView_ && pendingMapView_->identity == identity) {
            pendingMapView_.reset();
        }
        return;
    }

    mapViewSubscriptionIdentity_ = identity;
    REX::INFO("StarfieldCruiseAdapter: subscribed {} -> {} session={} generation={}",
        MapMenuName, MapDataFeed, identity.session, identity.generation);
}

void StarfieldCruiseAdapter::DrainMapViewObservation()
{
    std::optional<MapViewObservation> observation;
    {
        std::lock_guard lock {mapObservationMutex_};
        observation = std::exchange(pendingMapView_, std::nullopt);
    }

    if (!observation) {
        return;
    }

    if (runtime_.OnMapViewChanged(observation->identity, observation->view)) {
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

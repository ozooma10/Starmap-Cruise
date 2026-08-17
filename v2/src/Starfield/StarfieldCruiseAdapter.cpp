#include "Starfield/StarfieldCruiseAdapter.h"

#include "MainThreadUiPump.h"
#include "Scaleform/ValueAccess.h"

#include "RE/Starfield.h"
#include "REX/REX.h"
#include "SFSE/SFSE.h"

#include <chrono>
#include <cstring>

namespace
{
    using Clock = std::chrono::steady_clock;

    constexpr const char* MapMenuName = "GalaxyStarMapMenu";
    constexpr const char* MapDataFeed = "StarMapMenuData";
    constexpr auto MapMovieSettleTime = std::chrono::milliseconds(250);

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
    explicit MapLifecycleSink(StarfieldCruiseAdapter& owner) : m_owner(owner) {}

    RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent& event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
    {
        const char* name = event.menuName.c_str();
        if (name && std::strcmp(name, MapMenuName) == 0) {
            m_owner.m_mapObservations.RecordLifecycle(event.opening);
        }

        return RE::BSEventNotifyControl::kContinue;
    }

private:
    StarfieldCruiseAdapter& m_owner;
};

class StarfieldCruiseAdapter::MapDataHandler final : public RE::Scaleform::GFx::FunctionHandler
{
public:
    explicit MapDataHandler(StarfieldCruiseAdapter& owner) : m_owner(owner) {}

    void Call(const Params& params) override
    {
        const auto identity = UnpackIdentity(params.userData);
        if (!identity.IsValid()) {
            return;
        }

        RE::Scaleform::GFx::Value data;
        const auto view = CFS::ScaleformValue::Payload(params, data) ? ReadMapView(data) : MapView::Unknown;
        m_owner.m_mapObservations.RecordView(identity, view);
    }

private:
    StarfieldCruiseAdapter& m_owner;
};

StarfieldCruiseAdapter& StarfieldCruiseAdapter::GetSingleton()
{
    static StarfieldCruiseAdapter singleton;
    return singleton;
}

StarfieldCruiseAdapter::StarfieldCruiseAdapter() :
    m_runtime(m_bodySource, m_commands),
    m_mapLifecycleSink(std::make_unique<MapLifecycleSink>(*this)),
    m_mapDataHandler(std::make_unique<MapDataHandler>(*this))
{}

StarfieldCruiseAdapter::~StarfieldCruiseAdapter() = default;

bool StarfieldCruiseAdapter::Initialize()
{
    if (m_initialized) {
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
    ui->RegisterSink<RE::MenuOpenCloseEvent>(m_mapLifecycleSink.get());
    m_initialized = true;
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

    GetSingleton().m_mapObservations.RecordMovieCreated(
        Clock::now().time_since_epoch().count());
}

void StarfieldCruiseAdapter::OnUiSafeFrame()
{
    auto& adapter = GetSingleton();
    adapter.DrainMapObservations();
    adapter.TrySubscribeMapView();
}

void StarfieldCruiseAdapter::DrainMapObservations()
{
    const auto observations = m_mapObservations.Drain();

    if (observations.movieCreated) {
        m_runtime.OnMapMovieCreated(observations.movieGeneration);
        m_mapMovieBornTicks = observations.movieBornTicks;
        m_activeMapIdentity = {};
        m_mapViewSubscriptionIdentity = {};
    }

    if (observations.lifecycleOverflowed) {
        if (!observations.movieCreated && observations.movieGeneration != 0) {
            m_runtime.OnMapMovieCreated(observations.movieGeneration);
        }
        m_activeMapIdentity = {};
        m_mapViewSubscriptionIdentity = {};
        REX::ERROR("StarfieldCruiseAdapter: map lifecycle observation queue overflowed; active map session invalidated");
        return;
    }

    for (std::size_t index = 0; index < observations.lifecycleCount; ++index) {
        const auto& observation = observations.lifecycle[index];

        if (observation.opening) {
            const bool accepted = m_runtime.OnMapOpened({
                .identity = observation.identity,
                .flying = IsPlayerFlying(),
                .cruiseState = ObservedCruiseState::Unknown,
                .currentSystemId = std::nullopt,
            });

            if (accepted) {
                m_activeMapIdentity = observation.identity;
            }
        } else {
            m_runtime.OnMapClosed(observation.identity);
            if (m_activeMapIdentity == observation.identity) {
                m_activeMapIdentity = {};
            }
        }
    }

    if (observations.view) {
        m_runtime.OnMapViewChanged(
            observations.view->identity,
            observations.view->view);
    }
}

bool StarfieldCruiseAdapter::IsCurrentMapMovie(
    const void* root, const MapSessionIdentity& identity)
{
    if (!root || identity != m_activeMapIdentity) {
        return false;
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
    const auto identity = m_activeMapIdentity;
    if (!identity.IsValid() || m_mapViewSubscriptionIdentity == identity) {
        return;
    }

    if (m_mapMovieBornTicks == 0 ||
        Clock::now() - Clock::time_point {Clock::duration {m_mapMovieBornTicks}} <
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
    root->CreateFunction(&args[1], m_mapDataHandler.get(), reinterpret_cast<void*>(PackIdentity(identity)));

    if (!IsCurrentMapMovie(rootIdentity, identity)) {
        return;
    }

    if (!manager.Invoke("Subscribe", nullptr, args, 2)) {
        return;
    }

    if (!IsCurrentMapMovie(rootIdentity, identity)) {
        return;
    }

    m_mapViewSubscriptionIdentity = identity;
    REX::INFO("StarfieldCruiseAdapter: subscribed {} -> {} session={} generation={}", MapMenuName, MapDataFeed, identity.session, identity.generation);
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

#include "Starfield/StarfieldCruiseAdapter.h"

#include "MainThreadUiPump.h"
#include "Scaleform/ValueAccess.h"

#include "RE/Starfield.h"
#include "REX/REX.h"
#include "SFSE/SFSE.h"

#include <chrono>
#include <cstring>
#include <utility>

namespace
{
    using Clock = std::chrono::steady_clock;

    constexpr const char* MapMenuName = "GalaxyStarMapMenu";
    constexpr const char* MapDataFeed = "StarMapMenuData";
    constexpr const char* MarkersFeed = "StarMapMenuMarkersData";
    constexpr const char* DossierFeed = "StarmapSystemBodyInfoProvider";
    constexpr auto MapMovieSettleTime = std::chrono::milliseconds(250);

    constexpr std::uint32_t PlanetType = 2;
    constexpr std::uint32_t MoonType = 3;

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

    ObservedTargetKind ReadTargetKind(std::uint32_t raw)
    {
        if (raw == PlanetType) {
            return ObservedTargetKind::Planet;
        }
        if (raw == MoonType) {
            return ObservedTargetKind::Moon;
        }
        return ObservedTargetKind::Unsupported;
    }

    const char* SelectionAvailabilityName(SelectionAvailability availability)
    {
        switch (availability) {
        case SelectionAvailability::Hidden:
            return "hidden";
        case SelectionAvailability::Disabled:
            return "disabled";
        case SelectionAvailability::Eligible:
            return "eligible";
        }

        return "unknown";
    }

    const char* SelectionReasonName(SelectionReason reason)
    {
        switch (reason) {
        case SelectionReason::InactiveContext:
            return "inactive-context";
        case SelectionReason::CurrentSystemUnavailable:
            return "current-system-unavailable";
        case SelectionReason::SelectDestination:
            return "select-destination";
        case SelectionReason::AmbiguousTarget:
            return "ambiguous-target";
        case SelectionReason::UnsupportedTarget:
            return "unsupported-target";
        case SelectionReason::TargetDataUpdating:
            return "target-data-updating";
        case SelectionReason::TargetSystemUnavailable:
            return "target-system-unavailable";
        case SelectionReason::RemoteSystem:
            return "remote-system";
        case SelectionReason::Eligible:
            return "eligible";
        }

        return "unknown";
    }

    class MarkerCollector final : public RE::Scaleform::GFx::Value::ArrayVisitor
    {
    public:
        void Visit(std::uint32_t, const RE::Scaleform::GFx::Value& value) override
        {
            auto entry = value;
            bool highlighted = false;
            if (!CFS::ScaleformValue::BooleanMember(entry, "bIsInHighlightRadius", highlighted) || !highlighted) {
                return;
            }

            highlightedCount++;
            highlightedTarget = {
                .id = CFS::ScaleformValue::UIntMember(entry, "uBodyID"),
                .kind = ReadTargetKind(CFS::ScaleformValue::UIntMember(entry, "uBodyType")),
                .displayName = CFS::ScaleformValue::StringMember(entry, "sMarkerText"),
            };
        }

        std::size_t highlightedCount {0};
        TargetObservation highlightedTarget;
    };
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

        MapView view = MapView::Unknown;
        FormID currentBodyId = 0;

        RE::Scaleform::GFx::Value data;
        if (CFS::ScaleformValue::Payload(params, data)) {
            view = ReadMapView(data);
            currentBodyId = CFS::ScaleformValue::UIntMember(data, "uBodyLocationID");
        }

        m_owner.m_mapObservations.RecordMapData(identity, view, currentBodyId);
    }

private:
    StarfieldCruiseAdapter& m_owner;
};

class StarfieldCruiseAdapter::MarkersHandler final : public RE::Scaleform::GFx::FunctionHandler
{
public:
    explicit MarkersHandler(StarfieldCruiseAdapter& owner) : m_owner(owner) {}

    void Call(const Params& params) override
    {
        const auto identity = UnpackIdentity(params.userData);
        if (!identity.IsValid()) {
            return;
        }

        MarkerUpdate update;
        RE::Scaleform::GFx::Value data;
        if (CFS::ScaleformValue::Payload(params, data)) {
            RE::Scaleform::GFx::Value markers;
            if (data.GetMember("aMarkersData", &markers)) {
                RE::Scaleform::GFx::Value inner;
                if (markers.GetMember("dataA", &inner) && inner.IsArray()) {
                    markers = inner;
                }

                if (markers.IsArray()) {
                    MarkerCollector collector;
                    markers.VisitElements(&collector);
                    update.highlightedCount = collector.highlightedCount;
                    if (collector.highlightedCount == 1) {
                        update.highlighted = std::move(collector.highlightedTarget);
                    }
                }
            }
        }

        m_owner.m_mapObservations.RecordMarkers(identity, std::move(update));
    }

private:
    StarfieldCruiseAdapter& m_owner;
};

class StarfieldCruiseAdapter::DossierHandler final : public RE::Scaleform::GFx::FunctionHandler
{
public:
    explicit DossierHandler(StarfieldCruiseAdapter& owner) : m_owner(owner) {}

    void Call(const Params& params) override
    {
        const auto identity = UnpackIdentity(params.userData);
        if (!identity.IsValid()) {
            return;
        }

        TargetObservation target;
        RE::Scaleform::GFx::Value data;
        if (CFS::ScaleformValue::Payload(params, data)) {
            target = {
                .id = CFS::ScaleformValue::UIntMember(data, "uBodyID"),
                .kind = ReadTargetKind(
                    CFS::ScaleformValue::UIntMember(data, "iType")),
                .displayName = CFS::ScaleformValue::StringMember(
                    data, "sBodyName"),
            };
        }

        m_owner.m_mapObservations.RecordDossier(identity, std::move(target));
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
    m_mapDataHandler(std::make_unique<MapDataHandler>(*this)),
    m_markersHandler(std::make_unique<MarkersHandler>(*this)),
    m_dossierHandler(std::make_unique<DossierHandler>(*this))
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
    REX::INFO("StarfieldCruiseAdapter: initialized with copied map lifecycle, location, marker, and dossier observations");
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
    adapter.TrySubscribeMapFeeds();
}

void StarfieldCruiseAdapter::DrainMapObservations()
{
    const auto observations = m_mapObservations.Drain();

    if (observations.movieCreated) {
        m_runtime.OnMapMovieCreated(observations.movieGeneration);
        m_mapMovieBornTicks = observations.movieBornTicks;
        m_activeMapIdentity = {};
        m_mapDataSubscriptionIdentity = {};
        m_markersSubscriptionIdentity = {};
        m_dossierSubscriptionIdentity = {};
    }

    if (observations.lifecycleOverflowed) {
        if (!observations.movieCreated && observations.movieGeneration != 0) {
            m_runtime.OnMapMovieCreated(observations.movieGeneration);
        }
        m_activeMapIdentity = {};
        m_mapDataSubscriptionIdentity = {};
        m_markersSubscriptionIdentity = {};
        m_dossierSubscriptionIdentity = {};
        REX::ERROR("StarfieldCruiseAdapter: map lifecycle observation queue overflowed; active map session invalidated");
        TraceCurrentSelection();
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

    if (observations.mapData &&
        observations.mapData->identity == m_activeMapIdentity) {
        const auto& mapData = *observations.mapData;

        if (mapData.currentBodyId != 0) {
            const auto currentBody = m_bodySource.ResolveBody(mapData.currentBodyId);
            if (currentBody && currentBody->id == mapData.currentBodyId) {
                m_runtime.OnCurrentSystemResolved(mapData.identity, currentBody->systemId);
            }
        }

        m_runtime.OnMapViewChanged(mapData.identity, mapData.view);
    }

    if (observations.markers &&
        observations.markers->identity == m_activeMapIdentity) {
        m_runtime.OnMarkersChanged(observations.markers->identity, std::move(observations.markers->update));
    }

    if (observations.dossier &&
        observations.dossier->identity == m_activeMapIdentity) {
        m_runtime.OnDossierChanged(observations.dossier->identity, observations.dossier->target);
    }

    TraceCurrentSelection();
}

void StarfieldCruiseAdapter::TraceCurrentSelection()
{
    const auto selection = m_runtime.CurrentSelection();

    SelectionTrace trace {
        .identity = m_activeMapIdentity,
        .availability = selection.availability,
        .reason = selection.reason,
    };

    if (selection.destination) {
        trace.targetId = selection.destination->targetId;
        trace.systemId = selection.destination->systemId;
        trace.displayName = selection.destination->displayName;
    }

    if (m_lastSelectionTrace && *m_lastSelectionTrace == trace) {
        return;
    }

    m_lastSelectionTrace = trace;

    if (trace.targetId != 0 && trace.systemId) {
        REX::INFO("StarfieldCruiseAdapter: selection session={} generation={} availability={} reason={} target={:08X} system={:08X} name='{}'", 
            trace.identity.session, trace.identity.generation, SelectionAvailabilityName(trace.availability), SelectionReasonName(trace.reason), trace.targetId, *trace.systemId, trace.displayName);
        return;
    }

    REX::INFO( "StarfieldCruiseAdapter: selection session={} generation={} availability={} reason={} target=none system=none name=''", 
        trace.identity.session, trace.identity.generation, SelectionAvailabilityName(trace.availability), SelectionReasonName(trace.reason));
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

void StarfieldCruiseAdapter::TrySubscribeMapFeeds()
{
    const auto identity = m_activeMapIdentity;
    if (!identity.IsValid()) {
        return;
    }

    const char* feed = nullptr;
    RE::Scaleform::GFx::FunctionHandler* handler = nullptr;
    MapSessionIdentity* subscriptionIdentity = nullptr;

    if (m_mapDataSubscriptionIdentity != identity) {
        feed = MapDataFeed;
        handler = m_mapDataHandler.get();
        subscriptionIdentity = &m_mapDataSubscriptionIdentity;
    } else if (m_markersSubscriptionIdentity != identity) {
        feed = MarkersFeed;
        handler = m_markersHandler.get();
        subscriptionIdentity = &m_markersSubscriptionIdentity;
    } else if (m_dossierSubscriptionIdentity != identity) {
        feed = DossierFeed;
        handler = m_dossierHandler.get();
        subscriptionIdentity = &m_dossierSubscriptionIdentity;
    } else {
        return;
    }

    if (m_mapMovieBornTicks == 0 || Clock::now() - Clock::time_point {Clock::duration {m_mapMovieBornTicks}} < MapMovieSettleTime) {
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
    if (!root->GetVariable(&manager, "Shared.AS3.Data.BSUIDataManager") || !(manager.IsObject() || manager.IsDisplayObject()) || !IsCurrentMapMovie(rootIdentity, identity)) {
        return;
    }

    RE::Scaleform::GFx::Value args[2];
    root->CreateString(&args[0], feed);
    root->CreateFunction(&args[1], handler, reinterpret_cast<void*>(PackIdentity(identity)));

    if (!IsCurrentMapMovie(rootIdentity, identity)) {
        return;
    }

    if (!manager.Invoke("Subscribe", nullptr, args, 2)) {
        return;
    }

    if (!IsCurrentMapMovie(rootIdentity, identity)) {
        return;
    }

    *subscriptionIdentity = identity;
    REX::INFO("StarfieldCruiseAdapter: subscribed {} -> {} session={} generation={}", MapMenuName, feed, identity.session, identity.generation);
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

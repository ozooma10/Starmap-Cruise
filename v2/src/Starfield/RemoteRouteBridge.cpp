#include "Starfield/RemoteRouteBridge.h"

#include "Scaleform/UiEventDispatch.h"
#include "Scaleform/ValueAccess.h"
#include "Starfield/RemoteRouteProtocol.h"
#include "Starfield/StarfieldBodyResolutionSource.h"

#include "RE/Starfield.h"
#include "REL/Pattern.h"
#include "REX/REX.h"

#include <Windows.h>
#undef ERROR

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <format>
#include <utility>

namespace
{
    using Clock = std::chrono::steady_clock;
    using Value = RE::Scaleform::GFx::Value;

    constexpr const char* MapMenuName = "GalaxyStarMapMenu";
    constexpr std::size_t MaxRoutePoints = 64;

    constexpr auto SelectGalaxySystemPattern = REL::Pattern<
        "48 89 5C 24 18 48 89 74 24 20 55 48 8D 6C 24 A9">();
    constexpr auto CloseGalaxyQuickSelectPattern = REL::Pattern<
        "48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20 57">();
    constexpr auto GlobalEventGetterPattern = REL::Pattern<
        "48 83 EC 28 65 48 8B 04 25 58 00 00 00 BA B8 00">();

    bool IsForegroundProcess()
    {
        const auto foreground = ::GetForegroundWindow();
        DWORD processId = 0;
        if (!foreground || ::GetWindowThreadProcessId(foreground, &processId) == 0) {
            return false;
        }
        return processId == ::GetCurrentProcessId();
    }

    std::int64_t NowMilliseconds()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now().time_since_epoch()).count();
    }

    template <class T>
    bool ReadScalar(std::uintptr_t address, T& value)
    {
        if (!address) {
            return false;
        }
        std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
        return true;
    }

    struct RouteSnapshot
    {
        bool readable {false};
        bool alternate {false};
        std::uint32_t pointCount {0};
        FormID endpoint {0};

        friend bool operator==(const RouteSnapshot&, const RouteSnapshot&) = default;
    };

    struct LiveMap
    {
        RE::Scaleform::GFx::ASMovieRootBase* movieRoot {nullptr};
        Value menuRoot;
        RE::StarMap::StarMapMenu* menu {nullptr};
        RE::StarMap::GalaxyState* galaxyState {nullptr};
    };
}

class RemoteRouteBridge::Impl final : public RE::BSTEventSink<RE::StarMapMenu_ExecuteRoute>
{
public:
    bool Initialize(const StarfieldBodyResolutionSource& bodySource)
    {
        if (available) {
            return true;
        }

        const auto& executeSourceBinding = RE::StarMapMenu_ExecuteRoute::EVENT_SOURCE_BINDING;
        const auto selectAddress = RE::ID::StarMap::SelectGalaxySystem.address();
        const auto closeAddress = RE::ID::StarMap::CloseGalaxyQuickSelect.address();
        const auto getterAddress = executeSourceBinding.GetGetterID().address();
        const auto menuVtable = RE::StarMap::StarMapMenu::PRIMARY_VTABLE.address();
        const auto galaxyVtable = RE::StarMap::GalaxyState::PRIMARY_VTABLE.address();
        if (!SelectGalaxySystemPattern.match(selectAddress) ||
            !CloseGalaxyQuickSelectPattern.match(closeAddress) ||
            !GlobalEventGetterPattern.match(getterAddress)) {
            REX::ERROR("RemoteRouteBridge: 1.16.244 binding fingerprint failed; remote routing disabled");
            return false;
        }

        auto* source = executeSourceBinding.Get();
        if (!executeSourceBinding.Matches(source)) {
            REX::ERROR("RemoteRouteBridge: native Execute event source proof failed; remote routing disabled");
            return false;
        }

        expectedMenuVtable = menuVtable;
        expectedGalaxyVtable = galaxyVtable;
        bodies = std::addressof(bodySource);
        source->RegisterSink(this);
        available = true;
        REX::INFO("RemoteRouteBridge: guarded Back -> system Set Course -> Execute bindings enabled");
        return true;
    }

    RE::BSEventNotifyControl ProcessEvent(const RE::StarMapMenu_ExecuteRoute&, RE::BSTEventSource<RE::StarMapMenu_ExecuteRoute>*) override
    {
        executeEvents.fetch_add(1, std::memory_order_release);
        return RE::BSEventNotifyControl::kContinue;
    }

    bool Begin(const BeginRemoteRoute& effect, const MapSessionIdentity& activeIdentity)
    {
        if (!available || protocol.Active() || effect.operationId == 0 ||
            effect.source.session != activeIdentity.session ||
            effect.source.movieGeneration != activeIdentity.generation ||
            !effect.destination.IsValid() || effect.destination.kind == DestinationKind::Station) {
            return false;
        }

        LiveMap live;
        std::string detail;
        // A remote action begins in System view, so the active state is intentionally SystemState until Back reaches Galaxy view.
        // Beginning the operation only needs the stable menu/movie and route; the stricter GalaxyState proof belongs to the later route stages.
        if (!ResolveLiveMenu(activeIdentity, live, detail)) {
            REX::WARN("RemoteRouteBridge: begin failed: {}", detail);
            return false;
        }

        const auto before = ReadStableRoute(live.menu);
        operation = effect;
        sourceIdentity = activeIdentity;
        observedMapIdentity = activeIdentity;
        observedView = MapView::Unknown;
        // StarMapMenuData reliably publishes the view change during Back, but it does not have to republish the system identity. 
        // The selected PNDT already proved this STDT before Begin, so carry that captured root across the transition. 
        // A later nonzero map value is accepted only when it names the same destination system.
        observedFocusedRoot = effect.destination.system.starFormId;
        preexistingEndpoint = before.readable ? before.endpoint : 0;
        protocol.Begin(effect.destination.system.starFormId, preexistingEndpoint, NowMilliseconds());
        if (!protocol.Active()) {
            Clear();
            return false;
        }

        if (!CFS::ScaleformEvents::DispatchUiEvent(live.movieRoot, "StarMapMenu_OnCancel", nullptr)) {
            Clear();
            return false;
        }
        REX::INFO("RemoteRouteBridge: operation={} dispatched stock Back session={} generation={} target={:08X} destination-STDT={:08X} numeric={:08X} preexisting-endpoint={:08X}",
            effect.operationId, activeIdentity.session, activeIdentity.generation, effect.destination.targetId, effect.destination.system.starFormId, effect.destination.system.numericId, preexistingEndpoint);
        return true;
    }

    void ObserveMapData(const MapSessionIdentity& identity, MapView view, FormID displayedSystemFormId)
    {
        if (!protocol.Active() || identity != sourceIdentity) {
            return;
        }
        observedMapIdentity = identity;
        observedView = view;
        if (displayedSystemFormId != 0 &&
            displayedSystemFormId == operation.destination.system.starFormId) {
            observedFocusedRoot = displayedSystemFormId;
        }
    }

    RemoteRouteResult Tick(const MapSessionIdentity& activeIdentity)
    {
        if (!protocol.Active()) {
            return {};
        }
        const auto nowMs = NowMilliseconds();
        LiveMap live;
        std::string detail;
        RemoteRouteProtocol::TickInput input {
            .foreground = IsForegroundProcess(),
            .sourceMatches = activeIdentity == sourceIdentity,
            .view = observedMapIdentity == sourceIdentity ? observedView : MapView::Unknown,
            .focusedRoot = observedMapIdentity == sourceIdentity ? observedFocusedRoot : 0,
        };

        if (protocol.CurrentPhase() != RemoteRouteProtocol::Phase::AwaitExecuteClose &&
            ResolveLiveGalaxyMap(activeIdentity, live, detail)) {
            input.mapReadable = true;
            if (protocol.CurrentPhase() == RemoteRouteProtocol::Phase::EstablishSelection) {
                input.selectedSystem = live.galaxyState->GetSelectedSystem();
                input.selectedReadable = true;
            } else if (protocol.CurrentPhase() == RemoteRouteProtocol::Phase::AwaitRoute) {
                const auto route = ReadStableRoute(live.menu);
                input.routeReadable = route.readable;
                input.routeEndpoint = route.endpoint;
                if (route.readable && route.endpoint != 0 &&
                    route.endpoint != preexistingEndpoint) {
                    if (const auto endpointSystem = bodies->ResolveSystemIdentity(route.endpoint)) {
                        input.endpointIdentity = *endpointSystem == operation.destination.system ?
                            RemoteRouteProtocol::EndpointIdentity::ExactDestination :
                            RemoteRouteProtocol::EndpointIdentity::WrongSystem;
                    }
                }
                input.executeGateResolved = ReadExecuteReady(
                    live.menuRoot, input.executeReady);
            }
        }

        const auto previousPhase = protocol.CurrentPhase();
        const auto decision = protocol.Tick(input, nowMs);
        if (decision.failed) {
            return Fail(std::string {decision.reason});
        }
        if (previousPhase == RemoteRouteProtocol::Phase::AwaitGalaxy &&
            protocol.CurrentPhase() == RemoteRouteProtocol::Phase::EstablishSelection) {
            REX::INFO("RemoteRouteBridge: operation={} reached GalaxyState for destination-STDT={:08X}", operation.operationId, operation.destination.system.starFormId);
        }

        if (decision.command == RemoteRouteProtocol::Command::InvokeSelector) {
            live.galaxyState->SelectSystem(
                operation.destination.system.starFormId, false);
            const bool exact = live.galaxyState->GetSelectedSystem() ==
                operation.destination.system.starFormId;
            if (!protocol.SelectorCompleted(true, exact, nowMs)) {
                return Fail(std::string {protocol.FailureReason()});
            }
            REX::INFO("RemoteRouteBridge: operation={} selected destination-STDT={:08X} through guarded native selector", operation.operationId, operation.destination.system.starFormId);
            return {};
        }

        if (decision.command == RemoteRouteProtocol::Command::DispatchSetCourse) {
            live.galaxyState->SetQuickSelectOpen(true);
            Value params;
            live.movieRoot->CreateObject(&params);
            const bool dispatched = params.IsObject() &&
                params.SetMember("buttonAction", Value {"SetRouteDestination"}) &&
                CFS::ScaleformEvents::DispatchUiEvent(
                    live.movieRoot, "StarMapMenu_OnHintButtonClicked", &params);
            const bool quickSelectOpen = live.galaxyState->IsQuickSelectOpen();
            const bool ownershipConsumed = dispatched && !quickSelectOpen;
            if ((!dispatched || !ownershipConsumed) && quickSelectOpen) {
                CloseQuickSelect(live);
            }
            if (!protocol.SetCourseCompleted(
                    dispatched, ownershipConsumed, nowMs)) {
                return Fail(std::string {protocol.FailureReason()});
            }
            REX::INFO("RemoteRouteBridge: operation={} dispatched stock system-level Set Course for destination-STDT={:08X}",
                operation.operationId, operation.destination.system.starFormId);
            return {};
        }

        if (decision.command == RemoteRouteProtocol::Command::InvokeExecute) {
            Value jumpData;
            if (!live.menuRoot.GetMember("JumpData_mc", &jumpData) ||
                !(jumpData.IsObject() || jumpData.IsDisplayObject())) {
                return Fail("vanilla JumpData panel disappeared before Execute");
            }
            const auto executeFloor = executeEvents.load(std::memory_order_acquire);
            if (!protocol.ExecuteStarted(executeFloor, nowMs) ||
                !jumpData.Invoke("SendExecuteEvent")) {
                return Fail("stock Execute Route handoff failed");
            }
            REX::INFO("RemoteRouteBridge: operation={} invoked stock Execute endpoint={:08X}; awaiting native event and matching close",
                operation.operationId, input.routeEndpoint);
        }
        return {};
    }

    RemoteRouteResult OnMapClosed(const MapSessionIdentity& identity)
    {
        if (!protocol.Active()) {
            return {};
        }
        const auto closed = protocol.MapClosed(
            identity == sourceIdentity,
            executeEvents.load(std::memory_order_acquire));
        if (closed == RemoteRouteProtocol::CloseResult::Committed) {
            auto result = MakeResult(RemoteRouteResult::Kind::Committed, "stock Execute event and matching Starmap close acknowledged");
            Clear();
            return result;
        }
        if (closed == RemoteRouteProtocol::CloseResult::Failed) {
            return Fail(std::string {protocol.FailureReason()});
        }
        return {};
    }

    RemoteRouteResult OnMovieCreated(std::uint32_t generation)
    {
        if (!protocol.Active() || generation == sourceIdentity.generation) {
            return {};
        }
        protocol.MovieReplaced();
        return Fail(std::string {protocol.FailureReason()});
    }

    RemoteRouteResult Fail(std::string reason)
    {
        const auto result = MakeResult(RemoteRouteResult::Kind::Failed, std::move(reason));
        REX::WARN("RemoteRouteBridge: operation={} failed: {}", result.operationId, result.reason);
        Clear();
        return result;
    }

    RemoteRouteResult MakeResult(RemoteRouteResult::Kind kind, std::string reason) const
    {
        return {
            .kind = kind,
            .operationId = operation.operationId,
            .source = sourceIdentity,
            .reason = std::move(reason),
        };
    }

    void Clear()
    {
        protocol.Reset();
        operation = {};
        sourceIdentity = {};
        observedMapIdentity = {};
        observedView = MapView::Unknown;
        observedFocusedRoot = 0;
        preexistingEndpoint = 0;
    }

    bool ResolveLiveMenu(const MapSessionIdentity& identity, LiveMap& live, std::string& detail) const
    {
        if (identity != sourceIdentity && protocol.Active()) {
            detail = "map identity no longer matches the remote operation";
            return false;
        }
        const auto* ui = RE::UI::GetSingleton();
        const RE::BSFixedString mapName {MapMenuName};
        if (!ui || !ui->IsMenuOpen(mapName)) {
            detail = "Starmap is not open";
            return false;
        }
        const auto menu = ui->GetMenu(mapName);
        if (!menu || !menu->uiMovie || !menu->uiMovie->asMovieRoot) {
            detail = "live Starmap movie is unavailable";
            return false;
        }

        live.menu = reinterpret_cast<RE::StarMap::StarMapMenu*>(menu.get());
        std::uintptr_t menuVtable = 0;
        if (!ReadScalar(reinterpret_cast<std::uintptr_t>(live.menu), menuVtable) ||
            menuVtable != expectedMenuVtable) {
            detail = "StarMapMenu primary vtable/layout guard failed";
            return false;
        }
        live.movieRoot = menu->uiMovie->asMovieRoot.get();
        const char* path = menu->GetRootPath();
        if (!live.movieRoot->GetVariable(&live.menuRoot, path && *path ? path : "root") ||
            !(live.menuRoot.IsObject() || live.menuRoot.IsDisplayObject()) ||
            !menu->uiMovie || !menu->uiMovie->asMovieRoot || menu->uiMovie->asMovieRoot.get() != live.movieRoot) {
            detail = "Starmap Scaleform root changed during guarded read";
            return false;
        }
        return true;
    }

    bool ResolveLiveGalaxyMap(const MapSessionIdentity& identity, LiveMap& live, std::string& detail) const
    {
        if (!ResolveLiveMenu(identity, live, detail)) {
            return false;
        }
        live.galaxyState = live.menu->GetGalaxyState();
        if (!live.galaxyState) {
            detail = "StarMapMenu has no active GalaxyState";
            return false;
        }
        std::uintptr_t galaxyVtable = 0;
        if (!ReadScalar(reinterpret_cast<std::uintptr_t>(live.galaxyState), galaxyVtable) ||
            galaxyVtable != expectedGalaxyVtable) {
            detail = "active Starmap state has not reached guarded GalaxyState";
            return false;
        }
        return true;
    }

    RouteSnapshot ReadRouteOnce(const RE::StarMap::StarMapMenu* menu) const
    {
        RouteSnapshot result;
        std::uintptr_t menuVtable = 0;
        if (!menu ||
            !ReadScalar(reinterpret_cast<std::uintptr_t>(menu), menuVtable) ||
            menuVtable != expectedMenuVtable) {
            return result;
        }
        const auto snapshot = menu->GetRoute()->GetSnapshot(MaxRoutePoints);
        if (!snapshot) {
            return result;
        }
        result.alternate = snapshot->alternate;
        result.pointCount = snapshot->pointCount;
        result.endpoint = snapshot->endpoint;
        result.readable = true;
        return result;
    }

    RouteSnapshot ReadStableRoute(const RE::StarMap::StarMapMenu* menu) const
    {
        const auto first = ReadRouteOnce(menu);
        const auto second = ReadRouteOnce(menu);
        return first == second ? second : RouteSnapshot {};
    }

    static bool ReadExecuteReady(Value& menuRoot, bool& ready)
    {
        Value jumpData;
        Value executeContainer;
        Value executeButton;
        bool panelVisible = false;
        bool executeVisible = false;
        if (!menuRoot.GetMember("JumpData_mc", &jumpData) ||
            !CFS::ScaleformValue::BooleanMember(jumpData, "visible", panelVisible) ||
            !jumpData.GetMember("ExecuteButton_mc", &executeContainer) ||
            !executeContainer.GetMember("ExecuteButtonHint_mc", &executeButton) ||
            !CFS::ScaleformValue::BooleanMember(executeButton, "Visible", executeVisible)) {
            return false;
        }
        ready = panelVisible && executeVisible;
        return true;
    }

    void CloseQuickSelect(const LiveMap& live) const
    {
        live.galaxyState->CloseQuickSelect(live.menu->GetDataModel());
    }

    bool available {false};
    const StarfieldBodyResolutionSource* bodies {nullptr};
    std::uintptr_t expectedMenuVtable {0};
    std::uintptr_t expectedGalaxyVtable {0};
    std::atomic<std::uint64_t> executeEvents {0};

    RemoteRouteProtocol protocol;
    BeginRemoteRoute operation;
    MapSessionIdentity sourceIdentity;
    MapSessionIdentity observedMapIdentity;
    MapView observedView {MapView::Unknown};
    FormID observedFocusedRoot {0};
    FormID preexistingEndpoint {0};
};

RemoteRouteBridge::RemoteRouteBridge() : m_impl(std::make_unique<Impl>()) {}
RemoteRouteBridge::~RemoteRouteBridge() = default;

bool RemoteRouteBridge::Initialize(const StarfieldBodyResolutionSource& bodySource)
{
    return m_impl->Initialize(bodySource);
}

bool RemoteRouteBridge::Available() const
{
    return m_impl->available;
}

bool RemoteRouteBridge::Begin(const BeginRemoteRoute& effect, const MapSessionIdentity& activeIdentity)
{
    return m_impl->Begin(effect, activeIdentity);
}

void RemoteRouteBridge::ObserveMapData(const MapSessionIdentity& identity, MapView view, FormID displayedSystemFormId)
{
    m_impl->ObserveMapData(identity, view, displayedSystemFormId);
}

RemoteRouteResult RemoteRouteBridge::Tick(const MapSessionIdentity& activeIdentity)
{
    return m_impl->Tick(activeIdentity);
}

RemoteRouteResult RemoteRouteBridge::OnMapClosed(const MapSessionIdentity& identity)
{
    return m_impl->OnMapClosed(identity);
}

RemoteRouteResult RemoteRouteBridge::OnMovieCreated(std::uint32_t generation)
{
    return m_impl->OnMovieCreated(generation);
}

void RemoteRouteBridge::Cancel()
{
    m_impl->Clear();
}

bool RemoteRouteBridge::Active() const
{
    return m_impl->protocol.Active();
}

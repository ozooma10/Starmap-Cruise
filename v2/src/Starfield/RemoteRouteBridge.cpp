#include "Starfield/RemoteRouteBridge.h"

#include "Scaleform/UiEventDispatch.h"
#include "Scaleform/ValueAccess.h"
#include "Starfield/RemoteRouteProtocol.h"
#include "Starfield/StarfieldBodyResolutionSource.h"
#include "Starfield/StarfieldNativeGuard.h"

#include "RE/Starfield.h"
#include "REX/REX.h"

#include <Windows.h>
#undef ERROR

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <format>
#include <limits>
#include <utility>

namespace
{
    using Clock = std::chrono::steady_clock;
    using Value = RE::Scaleform::GFx::Value;

    constexpr const char* MapMenuName = "GalaxyStarMapMenu";
    constexpr REL::ID SelectGalaxySystemId {94292};
    constexpr REL::ID CloseGalaxyQuickSelectId {94308};
    constexpr REL::ID StarMapMenuPrimaryVtableId {446845};
    constexpr REL::ID GalaxyStatePrimaryVtableId {446425};
    constexpr REL::ID ExecuteRouteGetEventSourceId {94774};
    constexpr REL::ID ExecuteRouteEventSourceAddressId {948974};
    constexpr REL::ID ExecuteRouteEventSourceVtableId {446781};

    constexpr std::size_t StarMapMenuDataModelOffset = 0x1B8;
    constexpr std::size_t StarMapMenuGalaxyStateOffset = 0x1240;
    constexpr std::size_t StarMapMenuRouteCountOffset = 0x1280;
    constexpr std::size_t StarMapMenuRouteDataOffset = 0x1288;
    constexpr std::size_t StarMapMenuAlternateEndpointOffset = 0x1294;
    constexpr std::size_t StarMapMenuAlternateModeOffset = 0x12B8;
    constexpr std::size_t GalaxyStateSelectedSystemOffset = 0x880;
    constexpr std::size_t GalaxyStateQuickSelectOpenOffset = 0x8F8;
    constexpr std::size_t RoutePointStride = 0x28;
    constexpr std::size_t RoutePointEndpointOffset = 0x04;
    constexpr std::size_t MaxRoutePoints = 64;

    constexpr std::array<std::uint8_t, 16> SelectGalaxySystemPrologue {
        0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x74,
        0x24, 0x20, 0x55, 0x48, 0x8D, 0x6C, 0x24, 0xA9,
    };
    constexpr std::array<std::uint8_t, 16> CloseGalaxyQuickSelectPrologue {
        0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C,
        0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57,
    };
    constexpr std::array<std::uint8_t, 16> GlobalEventGetterPrologue {
        0x48, 0x83, 0xEC, 0x28, 0x65, 0x48, 0x8B, 0x04,
        0x25, 0x58, 0x00, 0x00, 0x00, 0xBA, 0xB8, 0x00,
    };

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
        if (!CFS::V2::Native::IsReadable(address, sizeof(T))) {
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
        std::uintptr_t menuAddress {0};
        std::uintptr_t galaxyState {0};
    };
}

class RemoteRouteBridge::Impl final : public RE::BSTEventSink<RE::StarMapMenu_ExecuteRoute>
{
public:
    using SelectGalaxySystemFunction = void (*)(void*, FormID, bool);
    using CloseGalaxyQuickSelectFunction = void (*)(void*, void*);
    using ExecuteSourceGetter = RE::BSTEventSource<RE::StarMapMenu_ExecuteRoute>* (*)();

    bool Initialize(const StarfieldBodyResolutionSource& bodySource)
    {
        if (available) {
            return true;
        }

        const auto selectAddress = SelectGalaxySystemId.address();
        const auto closeAddress = CloseGalaxyQuickSelectId.address();
        const auto getterAddress = ExecuteRouteGetEventSourceId.address();
        const auto image = CFS::V2::Native::CurrentExecutable();
        const auto menuVtable = StarMapMenuPrimaryVtableId.address();
        const auto galaxyVtable = GalaxyStatePrimaryVtableId.address();
        if (!CFS::V2::Native::Matches(selectAddress, SelectGalaxySystemPrologue) ||
            !CFS::V2::Native::Matches(closeAddress, CloseGalaxyQuickSelectPrologue) ||
            !CFS::V2::Native::Matches(getterAddress, GlobalEventGetterPrologue) ||
            !image.Contains(menuVtable, sizeof(std::uintptr_t)) ||
            !image.Contains(galaxyVtable, sizeof(std::uintptr_t))) {
            REX::ERROR("RemoteRouteBridge: 1.16.244 binding fingerprint failed; remote routing disabled");
            return false;
        }

        auto* source = reinterpret_cast<ExecuteSourceGetter>(getterAddress)();
        std::uintptr_t sourceVtable = 0;
        if (!source || reinterpret_cast<std::uintptr_t>(source) != ExecuteRouteEventSourceAddressId.address() ||
            !ReadScalar(reinterpret_cast<std::uintptr_t>(source), sourceVtable) ||
            sourceVtable != ExecuteRouteEventSourceVtableId.address()) {
            REX::ERROR("RemoteRouteBridge: native Execute event source proof failed; remote routing disabled");
            return false;
        }

        selectGalaxySystem = reinterpret_cast<SelectGalaxySystemFunction>(selectAddress);
        closeQuickSelect = reinterpret_cast<CloseGalaxyQuickSelectFunction>(closeAddress);
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
        // A remote action begins in System view, so the active state at 0x1240
        // is intentionally SystemState until stock Back reaches Galaxy view.
        // Beginning the operation only needs the stable menu/movie and route;
        // the stricter GalaxyState proof belongs to the later route stages.
        if (!ResolveLiveMenu(activeIdentity, live, detail)) {
            REX::WARN("RemoteRouteBridge: begin failed: {}", detail);
            return false;
        }

        const auto before = ReadStableRoute(live.menuAddress);
        operation = effect;
        sourceIdentity = activeIdentity;
        observedMapIdentity = activeIdentity;
        observedView = MapView::Unknown;
        observedFocusedRoot = 0;
        preexistingEndpoint = before.readable ? before.endpoint : 0;
        protocol.Begin(effect.destination.system.starFormId,
            preexistingEndpoint, NowMilliseconds());
        if (!protocol.Active()) {
            Clear();
            return false;
        }

        if (!CFS::ScaleformEvents::DispatchUiEvent(live.movieRoot, "StarMapMenu_OnCancel", nullptr)) {
            Clear();
            return false;
        }
        REX::INFO("RemoteRouteBridge: operation={} dispatched stock Back session={} generation={} target={:08X} destination-STDT={:08X} numeric={:08X} preexisting-endpoint={:08X}",
            effect.operationId, activeIdentity.session, activeIdentity.generation,
            effect.destination.targetId, effect.destination.system.starFormId,
            effect.destination.system.numericId, preexistingEndpoint);
        return true;
    }

    void ObserveMapData(const MapSessionIdentity& identity, MapView view, FormID focusedRootId)
    {
        if (!protocol.Active() || identity != sourceIdentity) {
            return;
        }
        observedMapIdentity = identity;
        observedView = view;
        observedFocusedRoot = focusedRootId;
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
                input.selectedReadable = ReadScalar(
                    live.galaxyState + GalaxyStateSelectedSystemOffset,
                    input.selectedSystem);
                input.setCourseGateResolved = ReadSetCourseReady(
                    live.menuRoot, input.setCourseReady);
            } else if (protocol.CurrentPhase() == RemoteRouteProtocol::Phase::AwaitRoute) {
                const auto route = ReadStableRoute(live.menuAddress);
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

        const auto decision = protocol.Tick(input, nowMs);
        if (decision.failed) {
            return Fail(std::string {decision.reason});
        }

        if (decision.command == RemoteRouteProtocol::Command::InvokeSelector) {
            FormID selected = 0;
            selectGalaxySystem(reinterpret_cast<void*>(live.galaxyState),
                operation.destination.system.starFormId, false);
            const bool exact = ReadScalar(
                live.galaxyState + GalaxyStateSelectedSystemOffset, selected) &&
                selected == operation.destination.system.starFormId;
            if (!protocol.SelectorCompleted(true, exact, nowMs)) {
                return Fail(std::string {protocol.FailureReason()});
            }
            return {};
        }

        if (decision.command == RemoteRouteProtocol::Command::DispatchSetCourse) {
            const std::uint8_t open = 1;
            std::memcpy(reinterpret_cast<void*>(
                live.galaxyState + GalaxyStateQuickSelectOpenOffset),
                &open, sizeof(open));
            Value params;
            live.movieRoot->CreateObject(&params);
            const bool dispatched = params.IsObject() &&
                params.SetMember("buttonAction", Value {"SetRouteDestination"}) &&
                CFS::ScaleformEvents::DispatchUiEvent(
                    live.movieRoot, "StarMapMenu_OnHintButtonClicked", &params);
            std::uint8_t consumed = 1;
            const bool ownershipConsumed = dispatched && ReadScalar(
                live.galaxyState + GalaxyStateQuickSelectOpenOffset, consumed) &&
                consumed == 0;
            if ((!dispatched || !ownershipConsumed) && consumed != 0) {
                CloseQuickSelect(live);
            }
            if (!protocol.SetCourseCompleted(
                    dispatched, ownershipConsumed, nowMs)) {
                return Fail(std::string {protocol.FailureReason()});
            }
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

        live.menuAddress = reinterpret_cast<std::uintptr_t>(menu.get());
        std::uintptr_t menuVtable = 0;
        if (!ReadScalar(live.menuAddress, menuVtable) || menuVtable != expectedMenuVtable) {
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
        if (!ReadScalar(live.menuAddress + StarMapMenuGalaxyStateOffset, live.galaxyState) || !live.galaxyState) {
            detail = "StarMapMenu has no active GalaxyState";
            return false;
        }
        std::uintptr_t galaxyVtable = 0;
        if (!ReadScalar(live.galaxyState, galaxyVtable) || galaxyVtable != expectedGalaxyVtable) {
            detail = "active Starmap state has not reached guarded GalaxyState";
            return false;
        }
        return true;
    }

    RouteSnapshot ReadRouteOnce(std::uintptr_t menuAddress) const
    {
        RouteSnapshot result;
        std::uintptr_t menuVtable = 0;
        std::uint8_t alternate = 0;
        if (!ReadScalar(menuAddress, menuVtable) || menuVtable != expectedMenuVtable ||
            !ReadScalar(menuAddress + StarMapMenuAlternateModeOffset, alternate)) {
            return result;
        }
        result.alternate = alternate != 0;
        if (result.alternate) {
            if (!ReadScalar(menuAddress + StarMapMenuAlternateEndpointOffset, result.endpoint)) {
                return result;
            }
            result.readable = true;
            return result;
        }

        std::uintptr_t data = 0;
        if (!ReadScalar(menuAddress + StarMapMenuRouteCountOffset, result.pointCount) ||
            !ReadScalar(menuAddress + StarMapMenuRouteDataOffset, data) || result.pointCount > MaxRoutePoints) {
            return result;
        }
        if (result.pointCount != 0) {
            const auto span = static_cast<std::size_t>(result.pointCount) * RoutePointStride;
            if (!data || !CFS::V2::Native::IsReadable(data, span) ||
                !ReadScalar(data + (result.pointCount - 1) * RoutePointStride + RoutePointEndpointOffset, result.endpoint)) {
                return result;
            }
        }
        result.readable = true;
        return result;
    }

    RouteSnapshot ReadStableRoute(std::uintptr_t menuAddress) const
    {
        const auto first = ReadRouteOnce(menuAddress);
        const auto second = ReadRouteOnce(menuAddress);
        return first == second ? second : RouteSnapshot {};
    }

    static bool ReadSetCourseReady(Value& menuRoot, bool& ready)
    {
        Value hintBar;
        Value button;
        bool enabled = false;
        bool visible = false;
        if (!menuRoot.GetMember("ButtonHintBar_mc", &hintBar) ||
            !hintBar.GetMember("SetRouteDestinationButtonData", &button) ||
            !CFS::ScaleformValue::BooleanMember(button, "bEnabled", enabled) ||
            !CFS::ScaleformValue::BooleanMember(button, "bVisible", visible)) {
            return false;
        }
        ready = enabled && visible;
        return true;
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
        closeQuickSelect(reinterpret_cast<void*>(live.galaxyState),
            reinterpret_cast<void*>(live.menuAddress + StarMapMenuDataModelOffset));
    }

    bool available {false};
    const StarfieldBodyResolutionSource* bodies {nullptr};
    SelectGalaxySystemFunction selectGalaxySystem {nullptr};
    CloseGalaxyQuickSelectFunction closeQuickSelect {nullptr};
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

void RemoteRouteBridge::ObserveMapData(const MapSessionIdentity& identity, MapView view, FormID focusedRootId)
{
    m_impl->ObserveMapData(identity, view, focusedRootId);
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

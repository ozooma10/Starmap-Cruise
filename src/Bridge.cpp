#include "Bridge.h"

#include "BodyIndex.h"
#include "Settings.h"
#include "Types.h"

#include "RE/B/BSInputEventUser.h"
#include "RE/U/UI.h"
#include "SFSE/SFSE.h"

#include <Windows.h>
#undef ERROR

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <vector>

namespace CFS::Bridge
{
    namespace
    {
        using Clock = std::chrono::steady_clock;
        using V = RE::Scaleform::GFx::Value;

        constexpr const char* kMapMenu = "GalaxyStarMapMenu";
        constexpr const char* kHudMenu = "SpaceshipHudMenu";
        constexpr std::int32_t kSystemView = 1;
        constexpr std::uint32_t kPlanetType = 2;
        constexpr std::uint32_t kMoonType = 3;
        constexpr double kArrivalDistanceLightSeconds = 0.05;
        constexpr std::uint32_t kNavigationColor = 0x66CCFF;
        constexpr std::uint32_t kCruiseColor = 0xF5A04E;
        constexpr std::array<std::uint8_t, 16> kIsInSpace116244Prologue{
            0x48, 0x89, 0x5C, 0x24, 0x10, 0x55, 0x56, 0x57,
            0x48, 0x83, 0xEC, 0x40, 0x40, 0x32, 0xF6, 0x48,
        };

        using IsInSpace_t = bool (*)(RE::TESObjectREFR*, bool);
        std::atomic<IsInSpace_t> g_isInSpace{ nullptr };

        struct MovieState
        {
            std::atomic<std::uint32_t> generation{ 0 };
            std::atomic<std::uint32_t> subscriptions{ 0 };
            std::atomic<std::int64_t> bornTicks{ 0 };
        };

        MovieState g_mapMovie;
        MovieState g_hudMovie;
        std::atomic<bool> g_subscribeInFlight{ false };

        struct MapSnapshot
        {
            std::uint32_t session{ 0 };
            std::uint32_t generation{ 0 };
            bool openedWhileFlying{ false };
            bool wasCruising{ false };
            bool haveCapturedSystem{ false };
            std::uint32_t capturedSystem{ 0 };
            std::int32_t view{ -1 };
            std::uint32_t systemLocationID{ 0 };
            std::uint32_t bodyLocationID{ 0 };
            std::uint32_t treeBodyID{ 0 };
            std::uint32_t treeBodyType{ 0 };
            std::size_t highlightedMarkerCount{ 0 };
            std::uint32_t markerBodyID{ 0 };
            std::uint32_t markerBodyType{ 0 };
            std::string markerName;
            std::uint32_t dossierBodyID{ 0 };
            std::uint32_t dossierBodyType{ 0 };
            std::string dossierName;
        };

        std::mutex g_mapMutex;
        MapSnapshot g_map;
        std::atomic<std::uint32_t> g_mapSession{ 0 };
        std::atomic<bool> g_mapOpen{ false };
        std::atomic<bool> g_closeRequested{ false };
        std::atomic<bool> g_selectionAcceptedThisOpen{ false };

        std::mutex g_destinationMutex;
        std::optional<BodyDestination> g_destination;
        std::atomic<NavState> g_state{ NavState::kIdle };

        std::atomic<bool> g_haveCurrentSystem{ false };
        std::atomic<std::uint32_t> g_currentSystem{ 0 };
        std::atomic<bool> g_cruiseActive{ false };
        std::atomic<std::uint32_t> g_confirmedCourseID{ 0 };

        struct PhysicalHold
        {
            bool active{ false };
            RE::InputEvent::DeviceType device{ RE::InputEvent::DeviceType::kNone };
            std::int32_t idCode{ 0 };
            std::uint32_t session{ 0 };
            bool sawCockpitContext{ false };
            bool timeoutLogged{ false };
            bool suppressUntilRelease{ false };
            Clock::time_point started{};
        };

        std::mutex g_holdMutex;
        PhysicalHold g_hold;
        bool g_claimMapKey{ false };

        struct CourseRequest
        {
            std::uint32_t id{ 0 };
            bool clearing{ false };
            Clock::time_point queued{};
        };
        std::mutex g_courseMutex;
        CourseRequest g_courseRequest;
        std::atomic<std::uint32_t> g_courseAskedID{ 0 };
        std::atomic<std::int64_t> g_courseAskedTicks{ 0 };
        std::atomic<bool> g_courseAskedClearing{ false };

        struct HudRow
        {
            std::uint32_t id{ 0 };
            std::uint32_t type{ 0 };
            std::string name;
            bool courseLocked{ false };
        };
        std::mutex g_hudRowsMutex;
        std::vector<HudRow> g_hudRows;
        std::atomic<double> g_markedDistance{ -1.0 };
        std::atomic<bool> g_courseWasLocked{ false };
        std::atomic<std::uint32_t> g_arrivalCheckID{ 0 };
        std::atomic<std::int64_t> g_arrivalCheckTicks{ 0 };

        std::atomic<std::int64_t> g_lastUnsettledTicks{ 0 };

        std::atomic<bool> g_markerReady{ false };
        std::atomic<bool> g_markerFailed{ false };
        std::atomic<bool> g_markerBuildInFlight{ false };
        V g_marker;
        V g_navGlyph;
        V g_cruiseGlyph;
        V g_markerLabel;
        V g_markerFormat;

        using ProcessInput_t = void (*)(RE::BSInputEventReceiver*, const RE::InputEvent*);
        std::atomic<ProcessInput_t> g_originalInput{ nullptr };
        std::atomic<bool> g_inputInstalled{ false };

        void ResolveCurrentSystem(const std::vector<HudRow>& a_rows)
        {
            if (!BodyIndex::Ready())
                return;
            std::unordered_map<std::uint32_t, std::size_t> systems;
            for (const auto& row : a_rows)
                if (const auto body = BodyIndex::Lookup(row.id))
                    ++systems[body->galaxy.system];
            if (systems.empty())
                return;

            auto best = systems.begin();
            bool unique = true;
            for (auto it = std::next(systems.begin()); it != systems.end(); ++it) {
                if (it->second > best->second) {
                    best = it;
                    unique = true;
                } else if (it->second == best->second) {
                    unique = false;
                }
            }
            if (!unique) {
                g_haveCurrentSystem.store(false, std::memory_order_release);
                return;
            }
            const auto old = g_currentSystem.exchange(best->first, std::memory_order_acq_rel);
            const bool had = g_haveCurrentSystem.exchange(true, std::memory_order_acq_rel);
            if ((!had || old != best->first) && Settings::Verbose())
                REX::INFO("[system] cockpit feed resolves current system {}", best->first);
        }

        double AsNumber(const V& a_value)
        {
            if (a_value.IsUInt())
                return a_value.GetUInt();
            if (a_value.IsInt())
                return a_value.GetInt();
            if (a_value.IsNumber())
                return a_value.GetNumber();
            return 0.0;
        }

        std::uint32_t UIntMember(V& a_object, const char* a_name)
        {
            V member;
            return a_object.GetMember(a_name, &member) ? static_cast<std::uint32_t>(AsNumber(member)) : 0;
        }

        std::string StringMember(V& a_object, const char* a_name)
        {
            V member;
            if (!a_object.GetMember(a_name, &member) || !member.IsString())
                return {};
            const char* text = member.GetString();
            return text ? text : "";
        }

        bool Payload(const RE::Scaleform::GFx::FunctionHandler::Params& a_params, V& a_data)
        {
            if (!a_params.args || a_params.argCount == 0)
                return false;
            a_data = a_params.args[0];
            V inner;
            if (a_data.IsObject() && a_data.GetMember("data", &inner))
                a_data = inner;
            return a_data.IsObject() || a_data.IsArray();
        }

        bool ValidateIsInSpaceBinding()
        {
            static_assert(RE::ID::TESObjectREFR::IsInSpace.id() == 63482);

            REL::Relocation<std::uintptr_t> target{ RE::ID::TESObjectREFR::IsInSpace };
            const auto address = target.address();
            const auto module = reinterpret_cast<std::uintptr_t>(::GetModuleHandleW(nullptr));
            if (!address || !module) {
                REX::ERROR("[space] IsInSpace binding unavailable; bridge disabled before hooks");
                return false;
            }

            const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
                REX::ERROR("[space] Starfield module has no valid DOS header; bridge disabled");
                return false;
            }
            const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(module + dos->e_lfanew);
            if (nt->Signature != IMAGE_NT_SIGNATURE) {
                REX::ERROR("[space] Starfield module has no valid NT header; bridge disabled");
                return false;
            }
            const auto imageEnd = module + nt->OptionalHeader.SizeOfImage;
            if (address < module || address > imageEnd ||
                kIsInSpace116244Prologue.size() > imageEnd - address) {
                REX::ERROR("[space] Address Library ID 63482 resolved outside Starfield.exe: {:016X}; bridge disabled",
                    address);
                return false;
            }
            if (std::memcmp(reinterpret_cast<const void*>(address),
                    kIsInSpace116244Prologue.data(), kIsInSpace116244Prologue.size()) != 0) {
                REX::ERROR("[space] Address Library ID 63482 failed the Starfield 1.16.244 prologue fingerprint at {:016X}; bridge disabled",
                    address);
                return false;
            }

            g_isInSpace.store(reinterpret_cast<IsInSpace_t>(address), std::memory_order_release);
            REX::INFO("[space] IsInSpace(false) binding validated: Address Library ID 63482, RVA=0x{:X}, fingerprint={} bytes",
                address - module, kIsInSpace116244Prologue.size());
            return true;
        }

        bool IsShipInSpace(RE::TESObjectREFR* a_ship)
        {
            const auto predicate = g_isInSpace.load(std::memory_order_acquire);
            return a_ship && predicate && predicate(a_ship, false);
        }

        bool IsFlying()
        {
            const auto player = RE::PlayerCharacter::GetSingleton();
            const auto ship = player ? player->GetSpaceship() : nullptr;
            return IsShipInSpace(ship);
        }

        std::optional<BodyDestination> Destination()
        {
            std::lock_guard lock{ g_destinationMutex };
            return g_destination;
        }

        void HideMarker()
        {
            if (g_markerReady.load(std::memory_order_acquire))
                g_marker.SetMember("visible", V{ false });
        }

        void ResetHold(const char* a_reason)
        {
            bool changed = false;
            {
                std::lock_guard lock{ g_holdMutex };
                changed = g_hold.active || g_claimMapKey;
                g_hold = {};
                g_claimMapKey = false;
            }
            const auto state = g_state.load(std::memory_order_acquire);
            if (state == NavState::kAwaitingCruise || state == NavState::kMapSelection)
                g_state.store(Destination() ? NavState::kMarked : NavState::kIdle,
                    std::memory_order_release);
            if (changed && Settings::Verbose())
                REX::INFO("[input] pending physical hold reset: {}", a_reason);
        }

        void ClearDestination(const char* a_reason)
        {
            std::optional<BodyDestination> old;
            {
                std::lock_guard lock{ g_destinationMutex };
                old = std::move(g_destination);
                g_destination.reset();
            }
            {
                std::lock_guard lock{ g_courseMutex };
                g_courseRequest = {};
            }
            g_courseAskedID.store(0, std::memory_order_release);
            g_courseAskedClearing.store(false, std::memory_order_release);
            g_state.store(NavState::kIdle, std::memory_order_release);
            g_markedDistance.store(-1.0, std::memory_order_release);
            g_courseWasLocked.store(false, std::memory_order_release);
            g_arrivalCheckID.store(0, std::memory_order_release);
            if (old)
                REX::INFO("[destination] cleared {:08X} '{}': {}", old->formID,
                    old->localizedName, a_reason);
        }

        void StoreDestination(BodyDestination a_destination)
        {
            std::optional<BodyDestination> old;
            {
                std::lock_guard lock{ g_destinationMutex };
                old = g_destination;
                g_destination = a_destination;
            }
            {
                std::lock_guard lock{ g_courseMutex };
                g_courseRequest = {};
            }
            g_courseAskedID.store(0, std::memory_order_release);
            g_courseAskedClearing.store(false, std::memory_order_release);
            g_state.store(NavState::kMapSelection, std::memory_order_release);
            g_markedDistance.store(-1.0, std::memory_order_release);
            g_courseWasLocked.store(false, std::memory_order_release);
            g_arrivalCheckID.store(0, std::memory_order_release);
            if (old && old->formID != a_destination.formID)
                REX::INFO("[destination] replaced {:08X} '{}' with {:08X} '{}'",
                    old->formID, old->localizedName, a_destination.formID,
                    a_destination.localizedName);
            else
                REX::INFO("[destination] marked {:08X} '{}' (system={} parent={} planet={} kind={})",
                    a_destination.formID, a_destination.localizedName,
                    a_destination.galaxy.system, a_destination.galaxy.parent,
                    a_destination.galaxy.planet,
                    a_destination.kind == BodyKind::kMoon ? "moon" : "planet");
        }

        std::optional<BodyDestination> ResolveMapSelection(std::string& a_reason)
        {
            if (!BodyIndex::Ready()) {
                a_reason = "PNDT/GNAM index is not ready";
                return std::nullopt;
            }

            MapSnapshot snapshot;
            {
                std::lock_guard lock{ g_mapMutex };
                snapshot = g_map;
            }

            if (!snapshot.openedWhileFlying) {
                a_reason = "map was not opened during active flight";
                return std::nullopt;
            }
            if (snapshot.session == 0 ||
                snapshot.session != g_mapSession.load(std::memory_order_acquire)) {
                a_reason = "map session was replaced";
                return std::nullopt;
            }
            if (snapshot.generation != g_mapMovie.generation.load(std::memory_order_acquire)) {
                a_reason = "map movie was replaced";
                return std::nullopt;
            }
            if (snapshot.view != kSystemView) {
                a_reason = std::format("view {} is not system view {}", snapshot.view, kSystemView);
                return std::nullopt;
            }
            if (!snapshot.haveCapturedSystem) {
                a_reason = "cockpit current system was not resolved before map open";
                return std::nullopt;
            }
            if (snapshot.highlightedMarkerCount != 1) {
                a_reason = std::format(
                    "system view has {} highlight-radius marker candidates; exactly one is required",
                    snapshot.highlightedMarkerCount);
                return std::nullopt;
            }
            if (snapshot.markerBodyID == 0 ||
                (snapshot.markerBodyType != kPlanetType && snapshot.markerBodyType != kMoonType)) {
                a_reason = std::format(
                    "highlight-radius marker is not a planet/moon candidate ({:08X}/{})",
                    snapshot.markerBodyID, snapshot.markerBodyType);
                return std::nullopt;
            }
            if (snapshot.dossierBodyID == 0 ||
                (snapshot.dossierBodyType != kPlanetType && snapshot.dossierBodyType != kMoonType)) {
                a_reason = std::format(
                    "system-view dossier has no planet/moon PNDT candidate ({:08X}/{})",
                    snapshot.dossierBodyID, snapshot.dossierBodyType);
                return std::nullopt;
            }
            if (snapshot.markerBodyID != snapshot.dossierBodyID ||
                snapshot.markerBodyType != snapshot.dossierBodyType) {
                a_reason = std::format(
                    "highlight-radius marker {:08X}/{} differs from dossier {:08X}/{}",
                    snapshot.markerBodyID, snapshot.markerBodyType,
                    snapshot.dossierBodyID, snapshot.dossierBodyType);
                return std::nullopt;
            }

            // Live 1.16.244 proof identifies the selected system-view body as
            // the one StarMapMenuMarkersData row with bIsInHighlightRadius.
            // Tree focus remains the system/star and does not join identity.
            const auto form = RE::TESForm::LookupByID(snapshot.dossierBodyID);
            if (!form || form->GetFormType() != RE::FormType::kPNDT) {
                a_reason = std::format("system-view dossier {:08X} is not a live PNDT form",
                    snapshot.dossierBodyID);
                return std::nullopt;
            }
            const auto body = BodyIndex::Lookup(snapshot.dossierBodyID);
            if (!body) {
                a_reason = std::format("system-view dossier PNDT {:08X} has no parsed GNAM identity",
                    snapshot.dossierBodyID);
                return std::nullopt;
            }
            if (body->galaxy.system != snapshot.capturedSystem) {
                a_reason = std::format("dossier PNDT {:08X} system {} differs from captured cockpit system {}",
                    snapshot.dossierBodyID, body->galaxy.system, snapshot.capturedSystem);
                return std::nullopt;
            }

            return BodyDestination{
                .kind = snapshot.dossierBodyType == kMoonType ? BodyKind::kMoon : BodyKind::kPlanet,
                .formID = snapshot.dossierBodyID,
                .galaxy = body->galaxy,
                .localizedName = snapshot.dossierName.empty() ? snapshot.markerName : snapshot.dossierName,
                .menuGeneration = snapshot.generation,
            };
        }

        void QueueCourse(std::uint32_t a_id, bool a_clearing)
        {
            std::lock_guard lock{ g_courseMutex };
            g_courseRequest = { a_id, a_clearing, Clock::now() };
            if (Settings::Verbose())
                REX::INFO("[course] queued {} for {:08X}", a_clearing ? "clear" : "lock", a_id);
        }

        bool DispatchHudEvent(RE::Scaleform::GFx::ASMovieRootBase* a_root, const char* a_type,
            const V* a_params)
        {
            V manager;
            if (!a_root->GetVariable(&manager, "Shared.AS3.Data.BSUIDataManager") ||
                !(manager.IsObject() || manager.IsDisplayObject())) {
                REX::WARN("[course] BSUIDataManager unavailable; '{}' not dispatched", a_type);
                return false;
            }

            V type;
            a_root->CreateString(&type, a_type);
            V args[2]{ type, a_params ? *a_params : V{} };
            V event;
            if (a_params)
                a_root->CreateObject(&event, "Shared.AS3.Events.CustomEvent", args, 2);
            else
                a_root->CreateObject(&event, "flash.events.Event", args, 1);
            if (event.IsObject() && manager.Invoke("dispatchEvent", nullptr, &event, 1))
                return true;
            return manager.Invoke("dispatchCustomEvent", nullptr, args, a_params ? 2 : 1);
        }

        void RunCourseRequest(RE::Scaleform::GFx::ASMovieRootBase* a_root)
        {
            CourseRequest request;
            {
                std::lock_guard lock{ g_courseMutex };
                request = g_courseRequest;
                if (!request.id)
                    return;
                if (!g_cruiseActive.load(std::memory_order_acquire))
                    return;
                g_courseRequest = {};
            }

            V params;
            a_root->CreateObject(&params);
            if (!params.IsObject()) {
                REX::WARN("[course] could not create event payload for {:08X}", request.id);
                g_state.store(Destination() ? NavState::kMarked : NavState::kIdle,
                    std::memory_order_release);
                return;
            }
            params.SetMember("uBodyID", V{ static_cast<double>(request.id) });
            if (DispatchHudEvent(a_root, "Reticle_OnCruiseLockCourse", &params)) {
                g_courseAskedID.store(request.id, std::memory_order_release);
                g_courseAskedClearing.store(request.clearing, std::memory_order_release);
                g_courseAskedTicks.store(Clock::now().time_since_epoch().count(), std::memory_order_release);
                REX::INFO("[course] dispatched {} uBodyID={:08X}",
                    request.clearing ? "clear/toggle" : "lock", request.id);
            } else {
                REX::WARN("[course] HUD rejected {} dispatch for {:08X}; mark preserved",
                    request.clearing ? "clear" : "lock", request.id);
                g_state.store(Destination() ? NavState::kMarked : NavState::kIdle,
                    std::memory_order_release);
            }
        }

        void AcceptSelection(const RE::ButtonEvent* a_button)
        {
            std::string reason;
            const auto selected = ResolveMapSelection(reason);
            if (!selected) {
                if (Settings::Verbose())
                    REX::INFO("[map] SetRouteDestination left to vanilla: {}", reason);
                return;
            }

            const auto existing = Destination();
            const bool same = existing && existing->formID == selected->formID;
            if (same) {
                const bool courseMatches = g_confirmedCourseID.load(std::memory_order_acquire) == selected->formID;
                ClearDestination("explicit same-body toggle");
                if (courseMatches && g_cruiseActive.load(std::memory_order_acquire))
                    QueueCourse(selected->formID, true);
            } else {
                StoreDestination(*selected);
            }

            {
                std::lock_guard lock{ g_holdMutex };
                g_claimMapKey = true;
                g_hold = {
                    .active = true,
                    .device = a_button->deviceType,
                    .idCode = a_button->idCode,
                    .session = g_mapSession.load(std::memory_order_acquire),
                    .sawCockpitContext = false,
                    .timeoutLogged = false,
                    .suppressUntilRelease = false,
                    .started = Clock::now(),
                };
            }
            g_selectionAcceptedThisOpen.store(true, std::memory_order_release);
            g_closeRequested.store(true, std::memory_order_release);
            REX::INFO("[map] accepted {:08X}; consumed SetRouteDestination and requested normal menu hide",
                selected->formID);
        }

        bool ObserveButton(const RE::ButtonEvent* a_button)
        {
            const bool down = a_button->value != 0.0f;
            const bool first = down && a_button->heldDownSecs == 0.0f;
            const char* raw = a_button->strUserEvent.c_str();
            const std::string_view name = raw ? raw : "";

            if (g_mapOpen.load(std::memory_order_acquire) && first && name == "SetRouteDestination") {
                if (a_button->disabled)
                    return false;
                const auto before = g_selectionAcceptedThisOpen.load(std::memory_order_acquire);
                AcceptSelection(a_button);
                return !before && g_selectionAcceptedThisOpen.load(std::memory_order_acquire);
            }

            std::lock_guard lock{ g_holdMutex };
            if (!g_hold.active || g_hold.device != a_button->deviceType || g_hold.idCode != a_button->idCode)
                return false;

            if (g_mapOpen.load(std::memory_order_acquire) && g_claimMapKey) {
                if (!down)
                    g_hold.active = false;
                return true;
            }

            if (g_hold.suppressUntilRelease) {
                if (!down)
                    g_hold.active = false;
                return true;
            }

            if (name == "Cruise" || name == "LockCourse") {
                if (!g_hold.sawCockpitContext) {
                    g_hold.sawCockpitContext = true;
                    REX::INFO("[input] natural context handoff: device={} id={} now reports '{}' "
                              "value={:.2f} held={:.3f}",
                        static_cast<std::uint32_t>(a_button->deviceType), a_button->idCode,
                        name, a_button->value, a_button->heldDownSecs);
                }
            }
            if (!down) {
                g_hold.active = false;
                if (g_state.load(std::memory_order_acquire) == NavState::kAwaitingCruise &&
                    !g_cruiseActive.load(std::memory_order_acquire))
                    g_state.store(Destination() ? NavState::kMarked : NavState::kIdle,
                        std::memory_order_release);
                if (Settings::Verbose())
                    REX::INFO("[input] physical hold released (device={} id={} event='{}')",
                        static_cast<std::uint32_t>(a_button->deviceType), a_button->idCode, name);
            }
            return false;
        }

        void ProcessInputHook(RE::BSInputEventReceiver* a_receiver, const RE::InputEvent* a_head)
        {
            struct Fix
            {
                RE::InputEvent* node{ nullptr };
                RE::InputEvent* next{ nullptr };
            };
            std::array<Fix, 16> fixes{};
            std::size_t fixCount = 0;
            const RE::InputEvent* head = a_head;
            RE::InputEvent* previous = nullptr;

            for (auto* event = a_head; event;) {
                auto* next = event->next;
                bool drop = false;
                if (event->eventType == RE::InputEvent::EventType::kButton)
                    drop = ObserveButton(static_cast<const RE::ButtonEvent*>(event));
                if (drop && fixCount < fixes.size()) {
                    if (previous) {
                        fixes[fixCount++] = { previous, previous->next };
                        previous->next = next;
                    } else {
                        head = next;
                    }
                } else {
                    previous = const_cast<RE::InputEvent*>(event);
                }
                event = next;
            }

            if (const auto original = g_originalInput.load(std::memory_order_acquire))
                original(a_receiver, head);
            for (std::size_t i = fixCount; i-- > 0;)
                fixes[i].node->next = fixes[i].next;
        }

        void TryInstallInputHook()
        {
            if (g_inputInstalled.load(std::memory_order_acquire))
                return;
            const auto ui = RE::UI::GetSingleton();
            if (!ui)
                return;
            bool expected = false;
            if (!g_inputInstalled.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                return;
            auto* receiver = static_cast<RE::BSInputEventReceiver*>(ui);
            const auto vtable = *reinterpret_cast<std::uintptr_t*>(receiver);
            const auto original = *reinterpret_cast<std::uintptr_t*>(vtable + sizeof(void*));
            g_originalInput.store(reinterpret_cast<ProcessInput_t>(original), std::memory_order_release);
            REL::Relocation<std::uintptr_t> relocation{ vtable };
            relocation.write_vfunc(1, &ProcessInputHook);
            REX::INFO("[input] UI::PerformInputProcessing hook installed (physical device/id tracking)");
        }

        class MapDataHandler : public RE::Scaleform::GFx::FunctionHandler
        {
        public:
            void Call(const Params& a_params) override
            {
                V data;
                if (!Payload(a_params, data))
                    return;
                V value;
                std::lock_guard lock{ g_mapMutex };
                if (data.GetMember("iCurrentMenuView", &value)) {
                    const auto view = static_cast<std::int32_t>(AsNumber(value));
                    if (view != g_map.view) {
                        g_map.treeBodyID = 0;
                        g_map.treeBodyType = 0;
                        g_map.highlightedMarkerCount = 0;
                        g_map.markerBodyID = 0;
                        g_map.markerBodyType = 0;
                        g_map.markerName.clear();
                        g_map.dossierBodyID = 0;
                        g_map.dossierBodyType = 0;
                        g_map.dossierName.clear();
                    }
                    g_map.view = view;
                }
                g_map.systemLocationID = UIntMember(data, "uSystemLocationID");
                g_map.bodyLocationID = UIntMember(data, "uBodyLocationID");
            }
        } g_mapDataHandler;

        class BodyInfoHandler : public RE::Scaleform::GFx::FunctionHandler
        {
        public:
            void Call(const Params& a_params) override
            {
                V data;
                if (!Payload(a_params, data))
                    return;
                const auto bodyID = UIntMember(data, "focusedBodyID");
                const auto bodyType = UIntMember(data, "focusedBodyType");
                std::lock_guard lock{ g_mapMutex };
                if (bodyID != g_map.treeBodyID || bodyType != g_map.treeBodyType) {
                    // Provider callbacks are asynchronous. Invalidate the copied
                    // selection join when the tree snapshot changes; current
                    // marker/dossier callbacks must repopulate it.
                    g_map.highlightedMarkerCount = 0;
                    g_map.markerBodyID = 0;
                    g_map.markerBodyType = 0;
                    g_map.markerName.clear();
                    g_map.dossierBodyID = 0;
                    g_map.dossierBodyType = 0;
                    g_map.dossierName.clear();
                }
                g_map.treeBodyID = bodyID;
                g_map.treeBodyType = bodyType;
            }
        } g_bodyInfoHandler;

        class MarkerCollector : public V::ArrayVisitor
        {
        public:
            std::uint32_t bodyID{ 0 };
            std::uint32_t bodyType{ 0 };
            std::string name;
            std::size_t highlightedCount{ 0 };

            void Visit(std::uint32_t, const V& a_value) override
            {
                V entry = a_value;
                V highlighted;
                if (!entry.GetMember("bIsInHighlightRadius", &highlighted) ||
                    !highlighted.IsBoolean() || !highlighted.GetBoolean())
                    return;
                ++highlightedCount;
                bodyID = UIntMember(entry, "uBodyID");
                bodyType = UIntMember(entry, "uBodyType");
                name = StringMember(entry, "sMarkerText");
            }
        };

        class MarkersHandler : public RE::Scaleform::GFx::FunctionHandler
        {
        public:
            void Call(const Params& a_params) override
            {
                V data;
                if (!Payload(a_params, data))
                    return;
                MarkerCollector visitor;
                V markers;
                if (data.GetMember("aMarkersData", &markers)) {
                    V inner;
                    if (markers.GetMember("dataA", &inner) && inner.IsArray())
                        markers = inner;
                    if (markers.IsArray())
                        markers.VisitElements(&visitor);
                }
                std::lock_guard lock{ g_mapMutex };
                g_map.highlightedMarkerCount = visitor.highlightedCount;
                if (visitor.highlightedCount == 1) {
                    g_map.markerBodyID = visitor.bodyID;
                    g_map.markerBodyType = visitor.bodyType;
                    g_map.markerName = std::move(visitor.name);
                } else {
                    g_map.markerBodyID = 0;
                    g_map.markerBodyType = 0;
                    g_map.markerName.clear();
                }
            }
        } g_markersHandler;

        class DossierHandler : public RE::Scaleform::GFx::FunctionHandler
        {
        public:
            void Call(const Params& a_params) override
            {
                V data;
                if (!Payload(a_params, data))
                    return;
                std::lock_guard lock{ g_mapMutex };
                g_map.dossierBodyID = UIntMember(data, "uBodyID");
                g_map.dossierBodyType = UIntMember(data, "iType");
                g_map.dossierName = StringMember(data, "sBodyName");
            }
        } g_dossierHandler;

        class LowCollector : public V::ArrayVisitor
        {
        public:
            std::vector<HudRow> rows;

            void Visit(std::uint32_t a_index, const V& a_value) override
            {
                V entry = a_value;
                HudRow row;
                row.id = UIntMember(entry, "uniqueID");
                row.type = UIntMember(entry, "uTargetType");
                row.name = StringMember(entry, "name");
                V lock;
                row.courseLocked = entry.GetMember("bIsCruiseTargetLock", &lock) &&
                    lock.IsBoolean() && lock.GetBoolean();
                if (rows.size() <= a_index)
                    rows.resize(a_index + 1);
                rows[a_index] = std::move(row);
            }
        };

        class LowHandler : public RE::Scaleform::GFx::FunctionHandler
        {
        public:
            void Call(const Params& a_params) override
            {
                V data;
                if (!Payload(a_params, data))
                    return;
                V array;
                if (!data.GetMember("targetArray", &array))
                    return;
                V inner;
                if (array.GetMember("dataA", &inner) && inner.IsArray())
                    array = inner;
                if (!array.IsArray())
                    return;

                LowCollector collector;
                array.VisitElements(&collector);

                ResolveCurrentSystem(collector.rows);

                std::uint32_t course = 0;
                for (const auto& row : collector.rows)
                    if (row.courseLocked) {
                        course = row.id;
                        break;
                    }
                const auto previousCourse = g_confirmedCourseID.exchange(course, std::memory_order_acq_rel);
                const auto asked = g_courseAskedID.load(std::memory_order_acquire);
                if (asked && g_courseAskedClearing.load(std::memory_order_acquire) && course != asked) {
                    g_courseAskedID.store(0, std::memory_order_release);
                    g_courseAskedClearing.store(false, std::memory_order_release);
                    REX::INFO("[course] engine confirmed clear of {:08X}", asked);
                }

                {
                    std::lock_guard lock{ g_hudRowsMutex };
                    g_hudRows = collector.rows;
                }

                const auto destination = Destination();
                if (!destination)
                    return;

                if (course == destination->formID) {
                    g_courseWasLocked.store(true, std::memory_order_release);
                    g_courseAskedID.store(0, std::memory_order_release);
                    g_courseAskedClearing.store(false, std::memory_order_release);
                    g_state.store(NavState::kAutopilotLocked, std::memory_order_release);
                    if (previousCourse != course)
                        REX::INFO("[course] engine confirmed lock on {:08X} '{}'",
                            destination->formID, destination->localizedName);
                } else if (previousCourse == destination->formID &&
                    g_courseWasLocked.exchange(false, std::memory_order_acq_rel)) {
                    g_arrivalCheckID.store(destination->formID, std::memory_order_release);
                    g_arrivalCheckTicks.store(Clock::now().time_since_epoch().count(), std::memory_order_release);
                    g_state.store(NavState::kMarked, std::memory_order_release);
                    if (Settings::Verbose())
                        REX::INFO("[arrival] Cruise lock left {:08X}; waiting for arrival evidence",
                            destination->formID);
                }
            }
        } g_lowHandler;

        struct Bearing
        {
            bool valid{ false };
            double angle{ 0.0 };
            double distance{ -1.0 };
        };

        class HighCollector : public V::ArrayVisitor
        {
        public:
            std::vector<Bearing> rows;

            void Visit(std::uint32_t a_index, const V& a_value) override
            {
                V entry = a_value;
                V angle;
                Bearing row;
                if (entry.GetMember("angleToCrosshair", &angle) &&
                    (angle.IsNumber() || angle.IsInt() || angle.IsUInt())) {
                    row.valid = true;
                    row.angle = AsNumber(angle);
                }
                V distance;
                if (entry.GetMember("distance", &distance))
                    row.distance = AsNumber(distance);
                if (rows.size() <= a_index)
                    rows.resize(a_index + 1);
                rows[a_index] = row;
            }
        };

        bool WorldSettled()
        {
            const auto last = Clock::time_point{ Clock::duration{
                g_lastUnsettledTicks.load(std::memory_order_acquire) } };
            return Clock::now() - last > std::chrono::milliseconds(2500);
        }

        bool AddSprite(RE::Scaleform::GFx::ASMovieRootBase* a_root, V& a_parent,
            V& a_out, const char* a_name)
        {
            if (a_parent.CreateEmptyMovieClip(&a_out, a_name, 21000))
                return true;
            a_root->CreateObject(&a_out, "flash.display.Sprite");
            if (!(a_out.IsObject() || a_out.IsDisplayObject()))
                return false;
            V added;
            return a_parent.Invoke("addChild", &added, &a_out, 1);
        }

        bool BorrowTextFormat(RE::Scaleform::GFx::ASMovieRootBase* a_root,
            const std::string& a_base)
        {
            const char* donors[]{
                ".Reticle_mc.ShipReticle_mc.LockOn_mc.LockText_tf",
                ".Reticle_mc.ShipReticle_mc.Distance_tf",
                ".DebugText_tf",
            };
            for (const auto* suffix : donors) {
                V donor;
                if (a_root->GetVariable(&donor, (a_base + suffix).c_str()) &&
                    donor.Invoke("getTextFormat", &g_markerFormat) && g_markerFormat.IsObject())
                    return true;
            }
            return false;
        }

        bool DrawDiamond(V& a_clip, std::uint32_t a_color)
        {
            V graphics;
            if (!a_clip.GetMember("graphics", &graphics))
                return false;
            constexpr double half = 12.0;
            V fill[]{ V{ a_color }, V{ 1.0 } };
            graphics.Invoke("beginFill", nullptr, fill, 2);
            V p0[]{ V{ 0.0 }, V{ -half } };
            V p1[]{ V{ half * 0.62 }, V{ 0.0 } };
            V p2[]{ V{ 0.0 }, V{ half } };
            V p3[]{ V{ -half * 0.62 }, V{ 0.0 } };
            graphics.Invoke("moveTo", nullptr, p0, 2);
            graphics.Invoke("lineTo", nullptr, p1, 2);
            graphics.Invoke("lineTo", nullptr, p2, 2);
            graphics.Invoke("lineTo", nullptr, p3, 2);
            graphics.Invoke("lineTo", nullptr, p0, 2);
            graphics.Invoke("endFill", nullptr, nullptr, 0);
            return true;
        }

        void TryCreateMarker(RE::Scaleform::GFx::ASMovieRootBase* a_root, const char* a_rootPath)
        {
            if (!Settings::ShowMarker() || g_markerReady.load(std::memory_order_acquire) ||
                g_markerFailed.load(std::memory_order_acquire) || !WorldSettled())
                return;
            if (g_markerBuildInFlight.exchange(true, std::memory_order_acq_rel))
                return;
            struct Release { ~Release() { g_markerBuildInFlight.store(false, std::memory_order_release); } } release;

            const std::string base{ a_rootPath ? a_rootPath : "root" };
            V reticle;
            if (!a_root->GetVariable(&reticle, (base + ".Reticle_mc").c_str()))
                return;
            if (!AddSprite(a_root, reticle, g_marker, "CruiseFromStarmapMarker") ||
                !AddSprite(a_root, g_marker, g_navGlyph, "NavigationGlyph") ||
                !AddSprite(a_root, g_marker, g_cruiseGlyph, "CruiseGlyph") ||
                !DrawDiamond(g_navGlyph, kNavigationColor) ||
                !DrawDiamond(g_cruiseGlyph, kCruiseColor)) {
                g_markerFailed.store(true, std::memory_order_release);
                REX::WARN("[marker] runtime marker construction failed");
                return;
            }

            a_root->CreateObject(&g_markerLabel, "flash.text.TextField");
            if (g_markerLabel.IsObject() || g_markerLabel.IsDisplayObject()) {
                V added;
                g_marker.Invoke("addChild", &added, &g_markerLabel, 1);
                g_markerLabel.SetMember("selectable", V{ false });
                g_markerLabel.SetMember("mouseEnabled", V{ false });
                g_markerLabel.SetMember("width", V{ 360.0 });
                g_markerLabel.SetMember("height", V{ 28.0 });
                g_markerLabel.SetMember("x", V{ 18.0 });
                g_markerLabel.SetMember("y", V{ -14.0 });
                if (BorrowTextFormat(a_root, base)) {
                    g_markerFormat.SetMember("size", V{ 18.0 });
                    g_markerLabel.SetMember("defaultTextFormat", g_markerFormat);
                }
            }

            g_marker.SetMember("visible", V{ false });
            g_navGlyph.SetMember("visible", V{ true });
            g_cruiseGlyph.SetMember("visible", V{ false });
            g_markerReady.store(true, std::memory_order_release);
            REX::INFO("[marker] runtime navigation/cruise marker ready");
        }

        void UpdateMarker(RE::Scaleform::GFx::ASMovieRootBase* a_root, const char* a_rootPath,
            const std::vector<Bearing>& a_bearings)
        {
            const auto destination = Destination();
            if (!destination || !Settings::ShowMarker()) {
                HideMarker();
                return;
            }

            std::size_t index = static_cast<std::size_t>(-1);
            bool courseLocked = false;
            {
                std::lock_guard lock{ g_hudRowsMutex };
                const auto count = std::min(g_hudRows.size(), a_bearings.size());
                for (std::size_t i = 0; i < count; ++i)
                    if (g_hudRows[i].id == destination->formID) {
                        index = i;
                        courseLocked = g_hudRows[i].courseLocked;
                        break;
                    }
            }
            if (index == static_cast<std::size_t>(-1) || !a_bearings[index].valid) {
                HideMarker();
                return;
            }

            g_markedDistance.store(a_bearings[index].distance, std::memory_order_release);
            TryCreateMarker(a_root, a_rootPath);
            if (!g_markerReady.load(std::memory_order_acquire))
                return;

            const double radians = a_bearings[index].angle * 3.14159265358979323846 / 180.0;
            constexpr double radius = 150.0;
            g_marker.SetMember("x", V{ radius * std::sin(radians) });
            g_marker.SetMember("y", V{ -radius * std::cos(radians) });
            g_marker.SetMember("visible", V{ true });
            g_navGlyph.SetMember("visible", V{ !courseLocked });
            g_cruiseGlyph.SetMember("visible", V{ courseLocked });
            if (g_markerLabel.IsObject() || g_markerLabel.IsDisplayObject()) {
                g_markerLabel.SetMember("visible", V{ Settings::ShowDestinationName() });
                if (Settings::ShowDestinationName()) {
                    g_markerLabel.SetMember("text", V{ destination->localizedName.c_str() });
                    g_markerLabel.SetMember("textColor", V{ courseLocked ? kCruiseColor : kNavigationColor });
                    if (g_markerFormat.IsObject()) {
                        g_markerFormat.SetMember("color", V{ courseLocked ? kCruiseColor : kNavigationColor });
                        g_markerLabel.Invoke("setTextFormat", nullptr, &g_markerFormat, 1);
                    }
                }
            }
        }

        class HighHandler : public RE::Scaleform::GFx::FunctionHandler
        {
        public:
            void Call(const Params& a_params) override
            {
                V data;
                if (!Payload(a_params, data))
                    return;
                V array;
                if (!data.GetMember("targetArray", &array))
                    return;
                V inner;
                if (array.GetMember("dataA", &inner) && inner.IsArray())
                    array = inner;
                if (!array.IsArray())
                    return;

                HighCollector collector;
                array.VisitElements(&collector);

                const auto ui = RE::UI::GetSingleton();
                const RE::BSFixedString hudName{ kHudMenu };
                const auto menu = ui ? ui->GetMenu(hudName) : nullptr;
                if (!menu || !menu->uiMovie || !menu->uiMovie->asMovieRoot)
                    return;
                auto* root = menu->uiMovie->asMovieRoot.get();
                const char* rootPath = menu->GetRootPath();
                const std::string base{ rootPath ? rootPath : "root" };
                V cruise;
                const bool active = root->GetVariable(&cruise,
                    (base + ".Reticle_mc.CruiseModeHUDActive").c_str()) &&
                    cruise.IsBoolean() && cruise.GetBoolean();
                const bool wasActive = g_cruiseActive.exchange(active, std::memory_order_acq_rel);

                if (active && !wasActive) {
                    const auto state = g_state.load(std::memory_order_acquire);
                    const auto destination = Destination();
                    if (destination &&
                        (state == NavState::kAwaitingCruise ||
                            (state == NavState::kMarked &&
                                Settings::GetMode() == Mode::kSelectThenCruise))) {
                        g_state.store(NavState::kAwaitingCruise, std::memory_order_release);
                        if (state == NavState::kMarked)
                            REX::INFO("[course] vanilla Cruise activation detected; locking marked destination {:08X}",
                                destination->formID);
                        QueueCourse(destination->formID, false);
                    }
                }
                if (!active && wasActive && g_state.load(std::memory_order_acquire) == NavState::kAutopilotLocked)
                    g_state.store(NavState::kMarked, std::memory_order_release);

                UpdateMarker(root, rootPath, collector.rows);
                RunCourseRequest(root);
            }
        } g_highHandler;

        struct Subscription
        {
            MovieState* movie;
            const char* menu;
            const char* feed;
            RE::Scaleform::GFx::FunctionHandler* handler;
            std::uint32_t bit;
        };

        Subscription g_subscriptions[]{
            { &g_mapMovie, kMapMenu, "StarMapMenuData", &g_mapDataHandler, 1u << 0 },
            { &g_mapMovie, kMapMenu, "StarMapMenuSystemBodyInfoData", &g_bodyInfoHandler, 1u << 1 },
            { &g_mapMovie, kMapMenu, "StarMapMenuMarkersData", &g_markersHandler, 1u << 2 },
            { &g_mapMovie, kMapMenu, "StarmapSystemBodyInfoProvider", &g_dossierHandler, 1u << 3 },
            { &g_hudMovie, kHudMenu, "TargetLowFrequencyProvider", &g_lowHandler, 1u << 0 },
            { &g_hudMovie, kHudMenu, "TargetHighFrequencyProvider", &g_highHandler, 1u << 1 },
        };

        bool StillSameMovie(const Subscription& a_sub, const void* a_root, std::uint32_t a_generation)
        {
            if (a_sub.movie->generation.load(std::memory_order_acquire) != a_generation)
                return false;
            const auto ui = RE::UI::GetSingleton();
            if (!ui)
                return false;
            const RE::BSFixedString name{ a_sub.menu };
            if (!ui->IsMenuOpen(name))
                return false;
            const auto menu = ui->GetMenu(name);
            return menu && menu->uiMovie && menu->uiMovie->asMovieRoot &&
                static_cast<const void*>(menu->uiMovie->asMovieRoot.get()) == a_root;
        }

        void TrySubscribe()
        {
            if (g_subscribeInFlight.exchange(true, std::memory_order_acq_rel))
                return;
            struct Release { ~Release() { g_subscribeInFlight.store(false, std::memory_order_release); } } release;

            const auto ui = RE::UI::GetSingleton();
            if (!ui)
                return;
            for (const auto& sub : g_subscriptions) {
                if ((sub.movie->subscriptions.load(std::memory_order_acquire) & sub.bit) != 0)
                    continue;
                // The Starmap can be registered as open while its background
                // AS3 movie is still being constructed after a load. Only enter
                // its VM after the visible MenuOpenCloseEvent for this session.
                if (sub.movie == &g_mapMovie && !g_mapOpen.load(std::memory_order_acquire))
                    continue;
                if (sub.movie == &g_hudMovie && (!WorldSettled() || !IsFlying()))
                    continue;
                const RE::BSFixedString menuName{ sub.menu };
                if (!ui->IsMenuOpen(menuName))
                    continue;
                const auto born = Clock::time_point{ Clock::duration{
                    sub.movie->bornTicks.load(std::memory_order_acquire) } };
                if (Clock::now() - born < std::chrono::milliseconds(250))
                    continue;
                const auto menu = ui->GetMenu(menuName);
                if (!menu || !menu->uiMovie || !menu->uiMovie->asMovieRoot)
                    continue;
                auto* root = menu->uiMovie->asMovieRoot.get();
                const auto generation = sub.movie->generation.load(std::memory_order_acquire);
                const auto rootID = static_cast<const void*>(root);
                if (!StillSameMovie(sub, rootID, generation))
                    return;
                V manager;
                if (!root->GetVariable(&manager, "Shared.AS3.Data.BSUIDataManager") ||
                    !(manager.IsObject() || manager.IsDisplayObject()))
                    continue;
                if (!StillSameMovie(sub, rootID, generation))
                    return;
                V args[2];
                root->CreateString(&args[0], sub.feed);
                root->CreateFunction(&args[1], sub.handler);
                if (manager.Invoke("Subscribe", nullptr, args, 2)) {
                    sub.movie->subscriptions.fetch_or(sub.bit, std::memory_order_acq_rel);
                    REX::INFO("[ui] subscribed {} -> {}", sub.menu, sub.feed);
                }
                return;  // one AS3 subscription per frame
            }
        }

        class MenuSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
        {
        public:
            RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent& a_event,
                RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
            {
                const char* name = a_event.menuName.c_str();
                if (!name)
                    return RE::BSEventNotifyControl::kContinue;

                if (std::strcmp(name, "LoadingMenu") == 0) {
                    g_lastUnsettledTicks.store(Clock::now().time_since_epoch().count(), std::memory_order_release);
                    if (a_event.opening) {
                        ResetHold("loading transition");
                        ClearDestination("loading transition");
                    }
                    return RE::BSEventNotifyControl::kContinue;
                }
                if (std::strcmp(name, kMapMenu) != 0)
                    return RE::BSEventNotifyControl::kContinue;

                g_mapOpen.store(a_event.opening, std::memory_order_release);
                if (a_event.opening) {
                    const auto session = g_mapSession.fetch_add(1, std::memory_order_acq_rel) + 1;
                    const bool haveSystem = g_haveCurrentSystem.load(std::memory_order_acquire);
                    std::lock_guard lock{ g_mapMutex };
                    g_map = {};
                    g_map.session = session;
                    g_map.generation = g_mapMovie.generation.load(std::memory_order_acquire);
                    g_map.openedWhileFlying = IsFlying();
                    g_map.wasCruising = g_cruiseActive.load(std::memory_order_acquire);
                    g_map.haveCapturedSystem = haveSystem;
                    g_map.capturedSystem = g_currentSystem.load(std::memory_order_acquire);
                    g_selectionAcceptedThisOpen.store(false, std::memory_order_release);
                    if (Settings::Verbose())
                        REX::INFO("[map] open session={} generation={} flying={} cruise={} currentSystem={}",
                            session, g_map.generation, g_map.openedWhileFlying, g_map.wasCruising,
                            haveSystem ? std::format("{}", g_map.capturedSystem) : "unresolved");
                } else {
                    bool accepted = g_selectionAcceptedThisOpen.exchange(false, std::memory_order_acq_rel);
                    bool wasCruising = false;
                    {
                        std::lock_guard lock{ g_mapMutex };
                        wasCruising = g_map.wasCruising;
                    }
                    bool held = false;
                    {
                        std::lock_guard lock{ g_holdMutex };
                        held = g_hold.active;
                        g_claimMapKey = false;
                        // Keyboard proof shows the remapped cockpit event is a
                        // continued hold (first=false), not a usable Cruise
                        // press. Suppress the carried event in every mode until
                        // release; upstream synthetic replay remains prohibited.
                        g_hold.suppressUntilRelease = true;
                    }
                    if (accepted) {
                        const auto mode = Settings::GetMode();
                        if (wasCruising &&
                            (mode == Mode::kHoldToCruise || mode == Mode::kSelectThenCruise)) {
                            if (const auto destination = Destination())
                                QueueCourse(destination->formID, false);
                            g_state.store(Destination() ? NavState::kAwaitingCruise : NavState::kIdle,
                                std::memory_order_release);
                        } else {
                            g_state.store(Destination() ? NavState::kMarked : NavState::kIdle,
                                std::memory_order_release);
                            if (held && mode == Mode::kHoldToCruise && Destination())
                                REX::WARN("[input] one-action HoldToCruise is unavailable: the continued cockpit hold is suppressed until release and synthetic replay is disabled");
                        }
                    }
                    if (Settings::Verbose())
                        REX::INFO("[map] close accepted={} physicalHeld={} state={}",
                            accepted, held, static_cast<std::uint32_t>(g_state.load(std::memory_order_acquire)));
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        } g_menuSink;

        void StartFocusWatcher()
        {
            std::thread{ [] {
                bool wasForeground = true;
                while (true) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    DWORD foregroundPID = 0;
                    if (const auto window = ::GetForegroundWindow())
                        ::GetWindowThreadProcessId(window, &foregroundPID);
                    const bool foreground = foregroundPID == ::GetCurrentProcessId();
                    if (wasForeground && !foreground)
                        ResetHold("application focus loss");
                    wasForeground = foreground;
                }
            } }.detach();
        }

        void CheckArrival()
        {
            const auto id = g_arrivalCheckID.load(std::memory_order_acquire);
            if (!id)
                return;
            const auto since = Clock::time_point{ Clock::duration{
                g_arrivalCheckTicks.load(std::memory_order_acquire) } };
            const auto age = Clock::now() - since;
            const double distance = g_markedDistance.load(std::memory_order_acquire);
            const bool evidence = distance >= 0.0 && distance <= kArrivalDistanceLightSeconds;
            if (evidence) {
                g_arrivalCheckID.store(0, std::memory_order_release);
                ClearDestination("confirmed arrival (course transition plus close distance)");
            } else if (age > std::chrono::seconds(2)) {
                g_arrivalCheckID.store(0, std::memory_order_release);
                if (Settings::Verbose())
                    REX::INFO("[arrival] no arrival evidence after lock transition; preserving mark {:08X}", id);
            }
        }

        void CheckCourseTimeout()
        {
            bool queuedExpired = false;
            {
                std::lock_guard lock{ g_courseMutex };
                if (g_courseRequest.id &&
                    Clock::now() - g_courseRequest.queued > std::chrono::seconds(2)) {
                    REX::WARN("[course] queued {} for {:08X} expired before Cruise HUD became ready; mark preserved",
                        g_courseRequest.clearing ? "clear" : "lock", g_courseRequest.id);
                    g_courseRequest = {};
                    queuedExpired = true;
                }
            }
            if (queuedExpired)
                g_state.store(Destination() ? NavState::kMarked : NavState::kIdle,
                    std::memory_order_release);

            const auto asked = g_courseAskedID.load(std::memory_order_acquire);
            if (asked) {
                const auto at = Clock::time_point{ Clock::duration{
                    g_courseAskedTicks.load(std::memory_order_acquire) } };
                if (Clock::now() - at > std::chrono::milliseconds(1500)) {
                    g_courseAskedID.store(0, std::memory_order_release);
                    g_courseAskedClearing.store(false, std::memory_order_release);
                    REX::WARN("[course] no bIsCruiseTargetLock confirmation for {:08X} after 1.5 seconds; mark preserved",
                        asked);
                    g_state.store(Destination() ? NavState::kMarked : NavState::kIdle,
                        std::memory_order_release);
                }
            }

            std::lock_guard lock{ g_holdMutex };
            if (g_hold.active && !g_hold.timeoutLogged &&
                g_state.load(std::memory_order_acquire) == NavState::kAwaitingCruise &&
                !g_cruiseActive.load(std::memory_order_acquire) &&
                Clock::now() - g_hold.started > std::chrono::seconds(2)) {
                g_hold.timeoutLogged = true;
                REX::WARN("[input] held key did not make vanilla CruiseModeHUDActive within 2 seconds. "
                          "The destination remains marked; synthetic input is disabled until the RE probe proves a complete queue route.");
            }
        }
    }

    void OnMovieCreated(RE::IMenu* a_menu)
    {
        if (!a_menu)
            return;
        const char* name = a_menu->menuName.c_str();
        if (!name)
            return;
        MovieState* state = nullptr;
        if (std::strcmp(name, kMapMenu) == 0)
            state = &g_mapMovie;
        else if (std::strcmp(name, kHudMenu) == 0)
            state = &g_hudMovie;
        else
            return;

        state->generation.fetch_add(1, std::memory_order_acq_rel);
        state->subscriptions.store(0, std::memory_order_release);
        state->bornTicks.store(Clock::now().time_since_epoch().count(), std::memory_order_release);
        if (state == &g_mapMovie) {
            ResetHold("Starmap movie replacement");
            g_selectionAcceptedThisOpen.store(false, std::memory_order_release);
        } else {
            ResetHold("Spaceship HUD movie replacement");
            {
                std::lock_guard lock{ g_courseMutex };
                g_courseRequest = {};
            }
            g_courseAskedID.store(0, std::memory_order_release);
            g_courseAskedClearing.store(false, std::memory_order_release);
            g_confirmedCourseID.store(0, std::memory_order_release);
            g_haveCurrentSystem.store(false, std::memory_order_release);
            {
                std::lock_guard lock{ g_hudRowsMutex };
                g_hudRows.clear();
            }
            g_markerReady.store(false, std::memory_order_release);
            g_markerFailed.store(false, std::memory_order_release);
            g_marker = V{};
            g_navGlyph = V{};
            g_cruiseGlyph = V{};
            g_markerLabel = V{};
            g_markerFormat = V{};
            g_cruiseActive.store(false, std::memory_order_release);
        }
        REX::INFO("[ui] movie created: {} generation={}", name,
            state->generation.load(std::memory_order_acquire));
    }

    void OnFrame()
    {
        TryInstallInputHook();
        TrySubscribe();

        if (BodyIndex::Ready() && !g_haveCurrentSystem.load(std::memory_order_acquire)) {
            std::vector<HudRow> rows;
            {
                std::lock_guard lock{ g_hudRowsMutex };
                rows = g_hudRows;
            }
            if (!rows.empty())
                ResolveCurrentSystem(rows);
        }

        if (g_closeRequested.exchange(false, std::memory_order_acq_rel)) {
            if (const auto queue = RE::UIMessageQueue::GetSingleton())
                queue->AddMessage(RE::BSFixedString{ kMapMenu }, RE::UI_MESSAGE_TYPE::kHide);
        }

        const auto destination = Destination();
        if (destination) {
            const auto player = RE::PlayerCharacter::GetSingleton();
            const auto ship = player ? player->GetSpaceship() : nullptr;
            if (!IsShipInSpace(ship)) {
                ClearDestination("landing, docking, or leaving the pilot seat");
            } else if (g_haveCurrentSystem.load(std::memory_order_acquire) &&
                g_currentSystem.load(std::memory_order_acquire) != destination->galaxy.system) {
                ClearDestination("system change");
            }
        }

        CheckArrival();
        CheckCourseTimeout();
    }

    void Initialize()
    {
        const auto menus = SFSE::GetMenuInterface();
        const auto tasks = SFSE::GetTaskInterface();
        const auto ui = RE::UI::GetSingleton();
        if (!menus || !tasks || !ui) {
            REX::ERROR("required SFSE/UI interface unavailable (menu={} task={} ui={}); bridge disabled",
                static_cast<bool>(menus), static_cast<bool>(tasks), static_cast<bool>(ui));
            return;
        }

        if (!ValidateIsInSpaceBinding())
            return;

        BodyIndex::StartLoad();
        menus->Register(&OnMovieCreated);
        ui->RegisterSink<RE::MenuOpenCloseEvent>(&g_menuSink);
        TryInstallInputHook();
        StartFocusWatcher();
        g_lastUnsettledTicks.store(Clock::now().time_since_epoch().count(), std::memory_order_release);
        tasks->AddPermanentTask(&OnFrame);
        REX::INFO("bridge initialized: bodies-first, current-system only, no serialization/API/SWF replacement");
    }
}

#include "Bridge.h"

#include "BodyIndex.h"
#include "MainThreadUiPump.h"
#include "Settings.h"
#include "Types.h"

#include "RE/B/BSInputEventUser.h"
#include "RE/B/BSService.h"
#include "RE/U/UI.h"
#include "SFSE/SFSE.h"

#include <Windows.h>
#undef ERROR

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <exception>
#include <format>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
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
        constexpr std::int32_t kGalaxyView = 0;
        constexpr std::int32_t kSystemView = 1;
        constexpr std::uint32_t kPlanetType = 2;
        constexpr std::uint32_t kMoonType = 3;
        constexpr double kArrivalDistanceLightSeconds = 0.05;
        constexpr std::uint32_t kNavigationColor = 0x66CCFF;
        constexpr std::uint32_t kCruiseColor = 0xF5A04E;
        constexpr const char* kCruiseMapActionLabel = "SET CRUISE TARGET";
        constexpr const char* kRemoteCruiseMapActionLabel = "JUMP THEN CRUISE";
        constexpr const char* kCruiseMapActionHoldLabel = "HOLD TO CRUISE";
        constexpr const char* kCruiseMapUserEvent = "Cruise";
        constexpr const char* kCruiseMapGamepadUserEvent = "SHMonocle";
        constexpr auto kHudMovieSettleTime = std::chrono::milliseconds(1500);
        constexpr auto kMapRoutePollTime = std::chrono::milliseconds(250);
        constexpr auto kRemoteRouteExecuteSettleTime = std::chrono::milliseconds(500);
        constexpr auto kRemoteRouteTimeout = std::chrono::seconds(5);
        // Post-advance passes a focus rung keeps before the next one may run.
        // The unit is completed AS3 advances, not wall clock: each pass means
        // native finished one advance with the previous rung already applied.
        constexpr std::uint32_t kGalaxyFocusRungPasses = 10;
        constexpr REL::ID kControlMapSingletonPtr{ 938003 };
        constexpr REL::ID kSetShipHudTarget{ 97892 };
        constexpr REL::ID kCurrentShipHudTarget{ 883585 };
        constexpr REL::ID kLoadGameGetEventSource{ 64149 };
        constexpr REL::ID kLoadGameSourceStatic{ 838425 };
        constexpr REL::ID kLoadGameSourceVtable{ 413741 };
        constexpr REL::ID kGravJumpGetEventSource{ 93876 };
        constexpr REL::ID kGravJumpSourceVtable{ 445846 };
        constexpr std::size_t kControlMapSize = 0x3A0;
        constexpr std::size_t kControlMapContextSlotsOffset = 0x10;
        constexpr std::size_t kControlMapMappingStride = 0x28;
        constexpr std::size_t kMaxControlMappings = 4096;
        constexpr std::array<std::uint8_t, 2> kCruiseControlContexts{ 0x21, 0x4D };
        constexpr std::array<std::uint8_t, 16> kIsInSpace116244Prologue{
            0x48, 0x89, 0x5C, 0x24, 0x10, 0x55, 0x56, 0x57,
            0x48, 0x83, 0xEC, 0x40, 0x40, 0x32, 0xF6, 0x48,
        };
        constexpr std::array<std::uint8_t, 6> kSetShipHudTarget116244Prefix{
            0x48, 0x83, 0xEC, 0x48, 0x89, 0x0D,
        };
        constexpr std::array<std::uint8_t, 16> kGlobalEventGetEventSource116244Prologue{
            0x48, 0x83, 0xEC, 0x28, 0x65, 0x48, 0x8B, 0x04,
            0x25, 0x58, 0x00, 0x00, 0x00, 0xBA, 0xB8, 0x00,
        };

        using IsInSpace_t = bool (*)(RE::TESObjectREFR*, bool);
        using SetShipHudTarget_t = void (*)(std::uint32_t);
        std::atomic<IsInSpace_t> g_isInSpace{ nullptr };
        std::atomic<SetShipHudTarget_t> g_setShipHudTarget{ nullptr };

        struct MovieState
        {
            std::atomic<std::uint32_t> generation{ 0 };
            std::atomic<std::uint32_t> subscriptions{ 0 };
            std::atomic<std::int64_t> bornTicks{ 0 };
        };

        MovieState g_mapMovie;
        MovieState g_hudMovie;
        std::atomic<bool> g_subscribeInFlight{ false };
        std::atomic<bool> g_mainFramePending{ false };
        std::atomic<std::uint32_t> g_uiResetMask{ 0 };
        constexpr std::uint32_t kResetMapUi = 1u << 0;
        constexpr std::uint32_t kResetHudUi = 1u << 1;

        struct MapSnapshot
        {
            std::uint32_t session{ 0 };
            std::uint32_t generation{ 0 };
            bool openedWhileFlying{ false };
            bool wasCruising{ false };
            bool cruiseEngageAvailable{ false };
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
            // Native Quick Select readback. This is the cursor-independent
            // statement of which galaxy system vanilla currently considers
            // selected; it is read only and never written back.
            bool quickSelectPublished{ false };
            std::uint32_t quickSelectCount{ 0 };
            std::int32_t quickSelectCursorIndex{ -1 };
            std::uint32_t quickSelectCursorBodyID{ 0 };
        };

        enum class EligibilityCode : std::uint8_t
        {
            kHidden,
            kTargetDataLoading,
            kCruiseControlUnbound,
            kCurrentSystemUnavailable,
            kSelectBody,
            kAmbiguousTarget,
            kTargetTypeUnsupported,
            kTargetDataUpdating,
            kTargetNotIndexed,
            kRemoteSafetyUnavailable,
            kRemoteCourseUnavailable,
            kRemoteCourseMismatch,
            kEligible,
        };

        struct MapEligibility
        {
            EligibilityCode code{ EligibilityCode::kHidden };
            bool show{ false };
            bool enabled{ false };
            std::string label;
            std::string detail;
            std::optional<BodyDestination> destination;
        };

        std::mutex g_mapMutex;
        MapSnapshot g_map;
        std::atomic<std::uint32_t> g_mapSession{ 0 };
        std::atomic<bool> g_mapOpen{ false };
        std::atomic<bool> g_closeRequested{ false };
        std::atomic<bool> g_selectionAcceptedThisOpen{ false };
        std::atomic<std::uint64_t> g_mapActionHintSignature{ 0 };
        std::atomic<bool> g_mapUiDirty{ false };
        std::atomic<bool> g_applicationForeground{ true };

        enum class RemoteRoutePhase : std::uint8_t
        {
            kNone,
            kAwaitGalaxy,
            kEstablishSelection,
            kAwaitRoute,
        };

        // Ordered, cursor-independent attempts at establishing the vanilla
        // galaxy marker context. Each rung is attempted at most once and only
        // on a post-advance pass that already failed the proof test, so the
        // ladder advances on observed state rather than on elapsed time.
        enum class GalaxyFocusRung : std::uint8_t
        {
            kQuickSelectChange = 0,
            kHoverSetter = 1,
            kExhausted = 2,
        };

        struct RemoteRouteRequest
        {
            RemoteRoutePhase phase{ RemoteRoutePhase::kNone };
            std::uint32_t session{ 0 };
            std::uint32_t generation{ 0 };
            std::uint32_t targetFormID{ 0 };
            std::uint32_t systemBodyID{ 0 };
            GalaxyFocusRung nextFocusRung{ GalaxyFocusRung::kQuickSelectChange };
            std::uint32_t focusRungCooldown{ 0 };
            bool focusDiagnosticsLogged{ false };
            std::string expectedSystemName;
            std::string targetName;
            Clock::time_point started{};
            Clock::time_point executeReadySince{};
        };
        std::mutex g_remoteRouteMutex;
        RemoteRouteRequest g_remoteRouteRequest;

        struct MapActionHintState
        {
            std::uint32_t generation{ 0 };
            bool comboReady{ false };
            bool tapReady{ false };
            bool installed{ false };
            V comboButton;
            V comboMkbButtonData;
            V comboGamepadButtonData;
            V tapButton;
            V tapMkbButtonData;
            V tapGamepadButtonData;
        } g_mapActionHint;
        std::atomic<bool> g_mapActionInteractive{ false };
        std::atomic<bool> g_mapActionTapOnly{ false };
        std::atomic<bool> g_lastInputWasGamepad{ false };
        std::atomic<bool> g_mapHintUsesGamepad{ false };

        std::mutex g_destinationMutex;
        std::optional<BodyDestination> g_destination;
        std::atomic<NavState> g_state{ NavState::kIdle };

        std::atomic<bool> g_haveCurrentSystem{ false };
        std::atomic<std::uint32_t> g_currentSystem{ 0 };
        std::atomic<bool> g_cruiseActive{ false };
        std::atomic<bool> g_cruiseEngageAvailable{ false };
        std::atomic<std::uint32_t> g_confirmedCourseID{ 0 };
        std::atomic<RE::InputEvent::DeviceType> g_pendingJumpDevice{
            RE::InputEvent::DeviceType::kNone
        };
        std::atomic<bool> g_loadGameSinkAttempted{ false };
        std::atomic<bool> g_loadGameSinkReady{ false };
        std::atomic<bool> g_loadClearPending{ false };
        std::atomic<bool> g_gravJumpSinkAttempted{ false };

        struct PhysicalHold
        {
            bool active{ false };
            RE::InputEvent::DeviceType device{ RE::InputEvent::DeviceType::kNone };
            std::int32_t idCode{ 0 };
            std::uint32_t session{ 0 };
            bool completed{ false };
            bool sawCockpitContext{ false };
            bool timeoutLogged{ false };
            bool suppressUntilRelease{ false };
            Clock::time_point started{};
        };

        std::mutex g_holdMutex;
        PhysicalHold g_hold;
        bool g_claimMapKey{ false };

        enum class HudCruiseInputPhase : std::uint8_t
        {
            kIdle,
            kPressPending,
            kPressed,
            kReleasePending,
        };

        std::mutex g_hudCruiseInputMutex;
        HudCruiseInputPhase g_hudCruiseInputPhase{ HudCruiseInputPhase::kIdle };
        const char* g_hudCruiseUserEvent{ "Cruise" };
        bool g_hudCruiseInputLatched{ false };
        Clock::time_point g_hudCruiseInputStarted{};

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

        std::atomic<bool> g_hudLowDirty{ false };

        struct Bearing
        {
            bool valid{ false };
            double angle{ 0.0 };
            double distance{ -1.0 };
        };
        std::mutex g_hudBearingsMutex;
        std::vector<Bearing> g_hudBearings;
        std::atomic<bool> g_hudUiDirty{ false };
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

        std::atomic<bool> g_targetStatusReady{ false };
        std::atomic<bool> g_targetStatusFailed{ false };
        std::atomic<bool> g_targetStatusBuildInFlight{ false };
        V g_targetStatus;
        V g_targetStatusLabel;
        V g_targetStatusFormat;

        using ProcessInput_t = void (*)(RE::BSInputEventReceiver*, const RE::InputEvent*);
        std::atomic<ProcessInput_t> g_originalInput{ nullptr };
        std::atomic<bool> g_inputInstalled{ false };
        std::atomic<std::int32_t> g_cruiseMapKey{ -1 };
        std::atomic<std::int32_t> g_cruiseMapModifier{ -1 };
        std::atomic<std::int32_t> g_cruiseMapMouseButton{ -1 };
        std::atomic<std::int32_t> g_cruiseMapGamepadButton{ -1 };

        const char* DestinationKindName(BodyKind a_kind)
        {
            switch (a_kind) {
            case BodyKind::kPlanet:
                return "planet";
            case BodyKind::kMoon:
                return "moon";
            case BodyKind::kStation:
                return "station";
            case BodyKind::kShip:
                return "ship";
            default:
                return "non-planet target";
            }
        }

        bool IsPlanetary(const BodyDestination& a_destination)
        {
            return a_destination.kind == BodyKind::kPlanet ||
                   a_destination.kind == BodyKind::kMoon;
        }

        std::optional<std::uint32_t> MapTreeSystemID(std::uint32_t a_formID)
        {
            return BodyIndex::LookupSystemRoot(a_formID);
        }

        struct ControlMapArray
        {
            std::uint32_t size;
            std::uint32_t capacity;
            std::uintptr_t data;
        };
        static_assert(sizeof(ControlMapArray) == 0x10);

        struct ControlMapMapping
        {
            std::uintptr_t eventEntry;
            std::uint32_t keyCode;
            std::uint32_t modifierCode;
            std::uint8_t bindingSlot;
            std::uint8_t unk11;
            std::uint16_t unk12;
            std::uint8_t sortIndex;
            std::uint8_t unk15[3];
            std::uint32_t contextMask;
            std::uint8_t bindingMeta;
            std::uint8_t visibleInControls;
            std::uint8_t defaultWasUnbound;
            std::uint8_t unk1F;
            std::uint8_t required;
            std::uint8_t pad21[7];
        };
        static_assert(sizeof(ControlMapMapping) == kControlMapMappingStride);

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

        bool ObjectMember(V& a_object, const char* a_name, V& a_member)
        {
            return a_object.GetMember(a_name, &a_member) &&
                   (a_member.IsObject() || a_member.IsDisplayObject());
        }

        bool BooleanMember(V& a_object, const char* a_name, bool& a_value)
        {
            V member;
            if (!a_object.GetMember(a_name, &member) || !member.IsBoolean())
                return false;
            a_value = member.GetBoolean();
            return true;
        }

        bool IsReadableRange(std::uintptr_t a_address, std::size_t a_size)
        {
            if (!a_address || !a_size || a_address > UINTPTR_MAX - a_size)
                return false;

            const auto end = a_address + a_size;
            while (a_address < end) {
                MEMORY_BASIC_INFORMATION memory{};
                if (::VirtualQuery(reinterpret_cast<const void*>(a_address),
                        &memory, sizeof(memory)) != sizeof(memory) ||
                    memory.State != MEM_COMMIT ||
                    (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
                    return false;

                const auto regionEnd = reinterpret_cast<std::uintptr_t>(memory.BaseAddress) +
                                       memory.RegionSize;
                if (regionEnd <= a_address)
                    return false;
                a_address = std::min(end, regionEnd);
            }
            return true;
        }

        template <class T>
        bool ReadMemory(std::uintptr_t a_address, T& a_value)
        {
            if (!IsReadableRange(a_address, sizeof(T)))
                return false;
            std::memcpy(&a_value, reinterpret_cast<const void*>(a_address), sizeof(T));
            return true;
        }

        template <std::size_t N>
        std::string HexBytes(const std::array<std::uint8_t, N>& a_bytes)
        {
            std::string result;
            for (const auto byte : a_bytes)
                result += std::format("{}{:02X}", result.empty() ? "" : " ", byte);
            return result;
        }

        std::string ReadControlMapEvent(std::uintptr_t a_entry)
        {
            for (std::size_t depth = 0; a_entry && depth < 8; ++depth) {
                std::uint8_t flags = 0;
                if (!ReadMemory(a_entry + 0x14, flags))
                    return {};
                if ((flags & 0x02) == 0) {
                    std::uint32_t length = 0;
                    if (!ReadMemory(a_entry + 0x08, length) || length > 128 ||
                        !IsReadableRange(a_entry + 0x18, length))
                        return {};
                    return std::string{ reinterpret_cast<const char*>(a_entry + 0x18), length };
                }
                if (!ReadMemory(a_entry + 0x08, a_entry))
                    return {};
            }
            return {};
        }

        bool FindCruiseBinding(std::uintptr_t a_controlMap, std::uint8_t a_context,
            std::uint32_t a_deviceIndex, const char* a_userEvent,
            std::int32_t& a_key, std::int32_t& a_modifier)
        {
            if (a_deviceIndex > 2)
                return false;

            std::uintptr_t context = 0;
            if (!ReadMemory(a_controlMap + kControlMapContextSlotsOffset +
                    static_cast<std::size_t>(a_context) * sizeof(std::uintptr_t), context) ||
                !context)
                return false;

            // A context begins with keyboard, mouse, and gamepad array headers.
            ControlMapArray mappings{};
            if (!ReadMemory(context +
                    static_cast<std::size_t>(a_deviceIndex) * sizeof(ControlMapArray),
                    mappings) ||
                mappings.size > mappings.capacity ||
                mappings.size > kMaxControlMappings ||
                (mappings.size && !IsReadableRange(mappings.data,
                    static_cast<std::size_t>(mappings.size) * kControlMapMappingStride)))
                return false;

            for (const auto desiredSlot : { std::uint8_t{ 0 }, std::uint8_t{ 1 } }) {
                for (std::uint32_t i = 0; i < mappings.size; ++i) {
                    ControlMapMapping mapping{};
                    std::memcpy(&mapping,
                        reinterpret_cast<const void*>(mappings.data +
                            static_cast<std::size_t>(i) * kControlMapMappingStride),
                        sizeof(mapping));
                    if (mapping.bindingSlot != desiredSlot ||
                        ReadControlMapEvent(mapping.eventEntry) != a_userEvent ||
                        mapping.keyCode == 0xFF || mapping.keyCode == 0x7FFFFFFF ||
                        (a_deviceIndex == 0 && mapping.keyCode > 0xFE))
                        continue;

                    a_key = static_cast<std::int32_t>(mapping.keyCode);
                    a_modifier = mapping.modifierCode == 0xFF ||
                                         mapping.modifierCode == 0x7FFFFFFF ?
                                     -1 :
                                     static_cast<std::int32_t>(mapping.modifierCode);
                    return a_deviceIndex != 0 || a_modifier <= 0xFE;
                }
            }
            return false;
        }

        void ResolveCruiseMapBinding()
        {
            REL::Relocation<std::uintptr_t*> singleton{ kControlMapSingletonPtr };
            std::uintptr_t controlMap = 0;
            std::uintptr_t vtable = 0;
            if (!ReadMemory(singleton.address(), controlMap) ||
                !IsReadableRange(controlMap, kControlMapSize) ||
                !ReadMemory(controlMap, vtable) ||
                vtable != RE::VTABLE::ControlMap[0].address()) {
                g_cruiseMapKey.store(-1, std::memory_order_release);
                g_cruiseMapModifier.store(-1, std::memory_order_release);
                g_cruiseMapMouseButton.store(-1, std::memory_order_release);
                g_cruiseMapGamepadButton.store(-1, std::memory_order_release);
                REX::WARN("[input] live Cruise bindings unavailable: ControlMap validation failed");
                return;
            }

            std::int32_t key = -1;
            std::int32_t modifier = -1;
            std::int32_t mouseButton = -1;
            std::int32_t mouseModifier = -1;
            std::int32_t gamepadButton = -1;
            std::int32_t gamepadModifier = -1;
            for (const auto context : kCruiseControlContexts)
                if (FindCruiseBinding(controlMap, context, 0, kCruiseMapUserEvent,
                        key, modifier))
                    break;
            for (const auto context : kCruiseControlContexts)
                if (FindCruiseBinding(controlMap, context, 1, kCruiseMapUserEvent,
                        mouseButton, mouseModifier))
                    break;
            for (const auto context : kCruiseControlContexts)
                if (FindCruiseBinding(controlMap, context, 2,
                        kCruiseMapGamepadUserEvent, gamepadButton, gamepadModifier))
                    break;

            // The UI hook can identify one physical ButtonEvent at a time. Do
            // not claim a mouse/gamepad chord unless its second edge can also
            // be proven; the shipped SHMonocle binding is a single button.
            if (mouseButton >= 0 && mouseModifier >= 0) {
                REX::WARN("[input] mouse Cruise chord is unsupported; mouse routing disabled");
                mouseButton = -1;
            }
            if (gamepadButton >= 0 && gamepadModifier >= 0) {
                REX::WARN("[input] controller '{}' chord is unsupported; controller routing disabled",
                    kCruiseMapGamepadUserEvent);
                gamepadButton = -1;
            }

            const auto oldKey = g_cruiseMapKey.exchange(key, std::memory_order_acq_rel);
            const auto oldModifier = g_cruiseMapModifier.exchange(modifier, std::memory_order_acq_rel);
            const auto oldMouse = g_cruiseMapMouseButton.exchange(mouseButton,
                std::memory_order_acq_rel);
            const auto oldGamepad = g_cruiseMapGamepadButton.exchange(gamepadButton,
                std::memory_order_acq_rel);
            if (key >= 0 && (oldKey != key || oldModifier != modifier)) {
                REX::INFO("[input] Starmap Cruise action follows live Cruise binding: VK=0x{:02X} modifier={}",
                    key, modifier < 0 ? "none" : std::format("0x{:02X}", modifier));
            }
            if (mouseButton >= 0 && oldMouse != mouseButton)
                REX::INFO("[input] Starmap Cruise action follows live mouse Cruise binding: id={}",
                    mouseButton);
            if (gamepadButton >= 0 && oldGamepad != gamepadButton)
                REX::INFO("[input] Starmap Cruise action follows live controller '{}' binding: id={} modifier={}",
                    kCruiseMapGamepadUserEvent, gamepadButton,
                    gamepadModifier < 0 ? "none" : std::format("{}", gamepadModifier));
            if (key < 0 && mouseButton < 0 && gamepadButton < 0)
                REX::WARN("[input] Cruise has no keyboard, mouse, or controller binding; Starmap Cruise action disabled");
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

        bool ValidateShipTargetBinding()
        {
            REL::Relocation<std::uintptr_t> target{ kSetShipHudTarget };
            const auto address = target.address();
            const auto module = reinterpret_cast<std::uintptr_t>(::GetModuleHandleW(nullptr));
            if (!address || !module) {
                REX::ERROR("[target] native ship-target binding unavailable; bridge disabled before hooks");
                return false;
            }

            const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
                REX::ERROR("[target] Starfield module has no valid DOS header; bridge disabled");
                return false;
            }
            const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(module + dos->e_lfanew);
            if (nt->Signature != IMAGE_NT_SIGNATURE) {
                REX::ERROR("[target] Starfield module has no valid NT header; bridge disabled");
                return false;
            }
            const auto imageEnd = module + nt->OptionalHeader.SizeOfImage;
            constexpr std::size_t kFingerprintSpan = 12;
            if (address < module || address >= imageEnd ||
                kFingerprintSpan > imageEnd - address) {
                REX::ERROR("[target] Address Library ID 97892 resolved outside Starfield.exe: {:016X}; bridge disabled",
                    address);
                return false;
            }
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(address);
            if (std::memcmp(bytes, kSetShipHudTarget116244Prefix.data(),
                    kSetShipHudTarget116244Prefix.size()) != 0 ||
                bytes[10] != 0x85 || bytes[11] != 0xC9) {
                REX::ERROR("[target] Address Library ID 97892 failed the Starfield 1.16.244 fingerprint at {:016X}; bridge disabled",
                    address);
                return false;
            }

            g_setShipHudTarget.store(reinterpret_cast<SetShipHudTarget_t>(address),
                std::memory_order_release);
            REX::INFO("[target] native ship-target setter validated: Address Library ID 97892, RVA=0x{:X}",
                address - module);
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

        struct LiveReferenceTarget
        {
            std::uint32_t referenceFormID{ 0 };
            std::uint32_t baseFormID{ 0 };
        };

        std::vector<LiveReferenceTarget> ResolveStationTargets(std::uint32_t a_mapFormID)
        {
            std::vector<LiveReferenceTarget> resolved;
            const auto appendLive = [&resolved](LiveReferenceTarget a_candidate) {
                const auto form = RE::TESForm::LookupByID(a_candidate.referenceFormID);
                const auto reference = form ? form->As<RE::TESObjectREFR>() : nullptr;
                const auto base = reference ? reference->GetBaseObject() : nullptr;
                if (!base || !BodyIndex::IsStationBase(base->GetFormID()))
                    return;
                a_candidate.baseFormID = base->GetFormID();
                resolved.push_back(std::move(a_candidate));
            };

            // Dynamic map markers may already be the live station reference.
            if (const auto form = RE::TESForm::LookupByID(a_mapFormID)) {
                if (const auto reference = form->As<RE::TESObjectREFR>()) {
                    const auto base = reference->GetBaseObject();
                    if (base && BodyIndex::IsStationBase(base->GetFormID())) {
                        appendLive({
                            .referenceFormID = a_mapFormID,
                            .baseFormID = base->GetFormID(),
                        });
                    }
                }
            }
            for (const auto& candidate : BodyIndex::StationTargets(a_mapFormID))
                appendLive({ candidate.referenceFormID, candidate.baseFormID });

            std::ranges::sort(resolved, {}, &LiveReferenceTarget::referenceFormID);
            resolved.erase(std::unique(resolved.begin(), resolved.end(),
                [](const LiveReferenceTarget& a_left,
                    const LiveReferenceTarget& a_right) {
                    return a_left.referenceFormID == a_right.referenceFormID;
                }), resolved.end());
            return resolved;
        }

        std::vector<LiveReferenceTarget> ResolveShipTargets(std::uint32_t a_mapFormID)
        {
            std::vector<LiveReferenceTarget> resolved;
            const auto form = RE::TESForm::LookupByID(a_mapFormID);
            const auto cell = form ? form->As<RE::TESObjectCELL>() : nullptr;
            if (!cell)
                return resolved;

            const auto player = RE::PlayerCharacter::GetSingleton();
            const auto playerShip = player ? player->GetSpaceship() : nullptr;
            const auto playerShipID = playerShip ? playerShip->GetFormID() : 0;
            cell->ForEachReference([&](const RE::NiPointer<RE::TESObjectREFR>& a_reference) {
                const auto base = a_reference ? a_reference->GetBaseObject() : nullptr;
                const auto referenceID = a_reference ? a_reference->GetFormID() : 0;
                if (referenceID && referenceID != playerShipID && base &&
                    IsShipInSpace(a_reference.get()) &&
                    base->GetFormType() == RE::FormType::kGBFM &&
                    !BodyIndex::IsStationBase(base->GetFormID())) {
                    resolved.push_back({ referenceID, base->GetFormID() });
                }
                return RE::BSContainer::ForEachResult::kContinue;
            });
            std::ranges::sort(resolved, {}, &LiveReferenceTarget::referenceFormID);
            resolved.erase(std::unique(resolved.begin(), resolved.end(),
                [](const LiveReferenceTarget& a_left,
                    const LiveReferenceTarget& a_right) {
                    return a_left.referenceFormID == a_right.referenceFormID;
                }), resolved.end());
            return resolved;
        }

        std::vector<HudRow> CurrentHudTargets(std::uint32_t a_formID)
        {
            std::vector<HudRow> matches;
            std::lock_guard lock{ g_hudRowsMutex };
            for (const auto& row : g_hudRows) {
                if (row.id == a_formID)
                    matches.push_back(row);
            }
            return matches;
        }

        bool AssignNativeShipTarget(const BodyDestination& a_destination)
        {
            if (a_destination.kind != BodyKind::kStation &&
                a_destination.kind != BodyKind::kShip)
                return true;

            const auto form = RE::TESForm::LookupByID(a_destination.formID);
            const auto reference = form ? form->As<RE::TESObjectREFR>() : nullptr;
            const auto base = reference ? reference->GetBaseObject() : nullptr;
            const auto setter = g_setShipHudTarget.load(std::memory_order_acquire);
            const auto player = RE::PlayerCharacter::GetSingleton();
            const auto playerShip = player ? player->GetSpaceship() : nullptr;
            const auto playerShipID = playerShip ? playerShip->GetFormID() : 0;
            const bool validStation = base && a_destination.kind == BodyKind::kStation &&
                BodyIndex::IsStationBase(base->GetFormID());
            const bool validShip = base && reference &&
                reference->GetFormID() != playerShipID && IsShipInSpace(reference) &&
                a_destination.kind == BodyKind::kShip &&
                base->GetFormType() == RE::FormType::kGBFM &&
                !BodyIndex::IsStationBase(base->GetFormID());
            if (!setter || (!validStation && !validShip)) {
                REX::ERROR("[target] refusing native assignment for {:08X}: live {} REFR validation failed",
                    a_destination.formID, DestinationKindName(a_destination.kind));
                return false;
            }

            setter(a_destination.formID);
            REL::Relocation<std::uint32_t*> current{ kCurrentShipHudTarget };
            std::uint32_t observed = 0;
            if (!ReadMemory(current.address(), observed) || observed != a_destination.formID) {
                REX::ERROR("[target] native assignment of {:08X} did not commit (observed {:08X})",
                    a_destination.formID, observed);
                return false;
            }

            REX::INFO("[target] native cockpit target assigned: map={:08X}/{} ref={:08X} base={:08X}",
                a_destination.mapFormID, a_destination.mapType, a_destination.formID,
                base->GetFormID());
            return true;
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

        void CancelOrReleaseHudCruiseInput(const char* a_reason)
        {
            const char* action = nullptr;
            {
                std::lock_guard lock{ g_hudCruiseInputMutex };
                g_hudCruiseInputLatched = false;
                g_hudCruiseInputStarted = {};
                if (g_hudCruiseInputPhase == HudCruiseInputPhase::kPressPending) {
                    g_hudCruiseInputPhase = HudCruiseInputPhase::kIdle;
                    g_hudCruiseUserEvent = "Cruise";
                    action = "cancelled pending press";
                } else if (g_hudCruiseInputPhase == HudCruiseInputPhase::kPressed) {
                    g_hudCruiseInputPhase = HudCruiseInputPhase::kReleasePending;
                    action = "queued release";
                }
            }
            g_hudUiDirty.store(true, std::memory_order_release);
            if (action && Settings::Verbose())
                REX::INFO("[input] HUD Cruise {}: {}", action, a_reason);
        }

        bool QueueHudCruisePress(RE::InputEvent::DeviceType a_device)
        {
            std::lock_guard lock{ g_hudCruiseInputMutex };
            if (g_hudCruiseInputPhase != HudCruiseInputPhase::kIdle)
                return false;
            // ShipReticle installs a different quick/hold combo for controller
            // mode. Both combos reach the same stock Cruise hold callback.
            g_hudCruiseUserEvent = a_device == RE::InputEvent::DeviceType::kGamepad ?
                                       kCruiseMapGamepadUserEvent :
                                       "Cruise";
            g_hudCruiseInputPhase = HudCruiseInputPhase::kPressPending;
            // The Starmap's completed fill is the user's confirmation. Keep
            // the separate cockpit hold pressed even if the physical key is
            // released, then release on Cruise activation or the safety limit.
            g_hudCruiseInputLatched = true;
            g_hudCruiseInputStarted = Clock::now();
            g_hudUiDirty.store(true, std::memory_order_release);
            return true;
        }

        bool HudCruiseInputLatched()
        {
            std::lock_guard lock{ g_hudCruiseInputMutex };
            return g_hudCruiseInputLatched;
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
            CancelOrReleaseHudCruiseInput(a_reason);
            const auto state = g_state.load(std::memory_order_acquire);
            if (state == NavState::kAwaitingCruise)
                g_state.store(Destination() ? NavState::kMarked : NavState::kIdle,
                    std::memory_order_release);
            else if (state == NavState::kMapSelection && Settings::Verbose())
                REX::INFO("[input] active Starmap selection preserved across hold reset: {}",
                    a_reason);
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
            {
                std::lock_guard lock{ g_remoteRouteMutex };
                g_remoteRouteRequest = {};
            }
            g_courseAskedID.store(0, std::memory_order_release);
            g_courseAskedClearing.store(false, std::memory_order_release);
            g_state.store(NavState::kIdle, std::memory_order_release);
            g_markedDistance.store(-1.0, std::memory_order_release);
            g_courseWasLocked.store(false, std::memory_order_release);
            g_arrivalCheckID.store(0, std::memory_order_release);
            g_pendingJumpDevice.store(RE::InputEvent::DeviceType::kNone,
                std::memory_order_release);
            g_hudUiDirty.store(true, std::memory_order_release);
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
            g_pendingJumpDevice.store(RE::InputEvent::DeviceType::kNone,
                std::memory_order_release);
            g_hudUiDirty.store(true, std::memory_order_release);
            if (old && old->formID != a_destination.formID)
                REX::INFO("[destination] replaced {:08X} '{}' with {:08X} '{}'",
                    old->formID, old->localizedName, a_destination.formID,
                    a_destination.localizedName);
            else
                REX::INFO("[destination] marked {:08X} '{}' (system={} parent={} planet={} kind={})",
                    a_destination.formID, a_destination.localizedName,
                    a_destination.galaxy.system, a_destination.galaxy.parent,
                    a_destination.galaxy.planet,
                    DestinationKindName(a_destination.kind));
        }

        class LoadGameSink final : public RE::BSTEventSink<RE::TESLoadGameEvent>
        {
        public:
            RE::BSEventNotifyControl ProcessEvent(const RE::TESLoadGameEvent&,
                RE::BSTEventSource<RE::TESLoadGameEvent>*) override
            {
                // Event delivery is not assumed to be the main game thread.
                // Publish only a value signal; the verified BSService frame
                // owns the actual navigation/input reset.
                g_loadClearPending.store(true, std::memory_order_release);
                REX::INFO("[safety] TESLoadGameEvent received; queued destination clear");
                return RE::BSEventNotifyControl::kContinue;
            }
        } g_loadGameSink;

        void TryInstallLoadGameSink()
        {
            if (g_loadGameSinkAttempted.exchange(true, std::memory_order_acq_rel))
                return;

            const auto function = kLoadGameGetEventSource.address();
            std::array<std::uint8_t, kGlobalEventGetEventSource116244Prologue.size()> prologue{};
            const bool readable = ReadMemory(function, prologue);
            const bool prologueMatches = readable &&
                prologue == kGlobalEventGetEventSource116244Prologue;
            if (!prologueMatches) {
                REX::ERROR("[safety] TESLoadGameEvent ID 64149 fingerprint failed at {:016X}: [{}]; remote targets disabled",
                    function, readable ? HexBytes(prologue) : "unreadable");
                return;
            }

            const auto source = RE::TESLoadGameEvent::GetEventSource();
            std::uintptr_t vtable = 0;
            const bool sourceReadable = source &&
                ReadMemory(reinterpret_cast<std::uintptr_t>(source), vtable);
            const auto expectedSource = kLoadGameSourceStatic.address();
            const auto expectedVtable = kLoadGameSourceVtable.address();
            const bool sourceMatches = reinterpret_cast<std::uintptr_t>(source) == expectedSource;
            const bool vtableMatches = sourceReadable && vtable == expectedVtable;
            REX::INFO("[safety] TESLoadGameEvent guard prologue=[{}] source={:016X}/{:016X} match={} vtable={:016X}/{:016X} match={}",
                HexBytes(prologue), reinterpret_cast<std::uintptr_t>(source), expectedSource,
                sourceMatches, vtable, expectedVtable, vtableMatches);
            if (!source || !sourceMatches || !vtableMatches) {
                REX::ERROR("[safety] TESLoadGameEvent identity guard failed; remote targets disabled");
                return;
            }

            source->RegisterSink(&g_loadGameSink);
            g_loadGameSinkReady.store(true, std::memory_order_release);
            REX::INFO("[safety] TESLoadGameEvent sink registered; jump-persistent remote targets enabled");
        }

        class GravJumpSink final : public RE::BSTEventSink<RE::Spaceship::GravJumpEvent>
        {
        public:
            RE::BSEventNotifyControl ProcessEvent(const RE::Spaceship::GravJumpEvent& a_event,
                RE::BSTEventSource<RE::Spaceship::GravJumpEvent>*) override
            {
                const auto player = RE::PlayerCharacter::GetSingleton();
                if (!player || !a_event.ship || a_event.ship.get() != player->GetSpaceship())
                    return RE::BSEventNotifyControl::kContinue;

                const auto retained = Destination();
                REX::INFO("[jump] player grav-jump state={} destination={:08X} navState={} retainedTarget={:08X}",
                    a_event.state,
                    a_event.destination ? a_event.destination->GetFormID() : 0,
                    static_cast<std::uint32_t>(g_state.load(std::memory_order_acquire)),
                    retained ? retained->formID : 0);
                return RE::BSEventNotifyControl::kContinue;
            }
        } g_gravJumpSink;

        void TryInstallGravJumpSink()
        {
            if (g_gravJumpSinkAttempted.exchange(true, std::memory_order_acq_rel))
                return;

            const auto function = kGravJumpGetEventSource.address();
            std::array<std::uint8_t, kGlobalEventGetEventSource116244Prologue.size()> prologue{};
            const bool readable = ReadMemory(function, prologue);
            const bool prologueMatches = readable &&
                prologue == kGlobalEventGetEventSource116244Prologue;
            const auto source = prologueMatches ?
                RE::Spaceship::GravJumpEvent::GetEventSource() : nullptr;
            std::uintptr_t vtable = 0;
            const bool sourceReadable = source &&
                ReadMemory(reinterpret_cast<std::uintptr_t>(source), vtable);
            const auto expectedVtable = kGravJumpSourceVtable.address();
            const bool vtableMatches = sourceReadable && vtable == expectedVtable;
            REX::INFO("[jump] GravJumpEvent guard prologue=[{}] source={:016X} vtable={:016X}/{:016X} match={}",
                readable ? HexBytes(prologue) : "unreadable",
                reinterpret_cast<std::uintptr_t>(source), vtable, expectedVtable,
                prologueMatches && vtableMatches);
            if (!source || !prologueMatches || !vtableMatches) {
                REX::WARN("[jump] GravJumpEvent identity guard failed; jump acknowledgement diagnostics unavailable");
                return;
            }

            source->RegisterSink(&g_gravJumpSink);
            REX::INFO("[jump] player-filtered GravJumpEvent acknowledgement sink registered");
        }

        MapEligibility EvaluateMapSelection(MapSnapshot a_snapshot)
        {
            const auto unavailable = [](EligibilityCode a_code, std::string a_label,
                                         std::string a_detail) {
                return MapEligibility{
                    .code = a_code,
                    .show = true,
                    .enabled = false,
                    .label = std::move(a_label),
                    .detail = std::move(a_detail),
                };
            };

            if (!a_snapshot.openedWhileFlying || a_snapshot.view != kSystemView ||
                a_snapshot.session == 0 ||
                a_snapshot.session != g_mapSession.load(std::memory_order_acquire) ||
                a_snapshot.generation != g_mapMovie.generation.load(std::memory_order_acquire)) {
                return {
                    .code = EligibilityCode::kHidden,
                    .detail = "not an active-flight system-view map session",
                };
            }
            const bool usingGamepad = g_lastInputWasGamepad.load(std::memory_order_acquire);
            const bool cruiseControlBound = usingGamepad ?
                g_cruiseMapGamepadButton.load(std::memory_order_acquire) >= 0 :
                g_cruiseMapKey.load(std::memory_order_acquire) >= 0 ||
                    g_cruiseMapMouseButton.load(std::memory_order_acquire) >= 0;
            if (!cruiseControlBound)
                return unavailable(EligibilityCode::kCruiseControlUnbound,
                    "CRUISE CONTROL IS NOT BOUND",
                    usingGamepad ? "SHMonocle has no controller binding" :
                                   "Cruise has no keyboard or mouse binding");
            if (!a_snapshot.haveCapturedSystem)
                return unavailable(EligibilityCode::kCurrentSystemUnavailable,
                    "CURRENT SYSTEM UNAVAILABLE",
                    "cockpit current system was not resolved before map open");
            if (a_snapshot.highlightedMarkerCount == 0)
                return unavailable(EligibilityCode::kSelectBody,
                    "HIGHLIGHT A DESTINATION",
                    "system view has no highlight-radius target marker");
            if (a_snapshot.highlightedMarkerCount != 1)
                return unavailable(EligibilityCode::kAmbiguousTarget,
                    "TARGET IS AMBIGUOUS",
                    std::format("system view has {} highlight-radius marker candidates",
                        a_snapshot.highlightedMarkerCount));
            if (a_snapshot.markerBodyID == 0) {
                return unavailable(EligibilityCode::kTargetTypeUnsupported,
                    "TARGET HAS NO CRUISE ID",
                    std::format("highlight-radius marker has type {} but no id",
                        a_snapshot.markerBodyType));
            }

            const bool planetary = a_snapshot.markerBodyType == kPlanetType ||
                a_snapshot.markerBodyType == kMoonType;
            if (!BodyIndex::Ready())
                return unavailable(EligibilityCode::kTargetDataLoading,
                    "CRUISE TARGET DATA LOADING",
                    "PNDT/GNAM and starstation reference index is not ready");
            if (!planetary) {
                const auto browsedSystemID = MapTreeSystemID(a_snapshot.treeBodyID);
                if (browsedSystemID && *browsedSystemID != a_snapshot.capturedSystem) {
                    return {
                        .code = EligibilityCode::kHidden,
                        .detail = std::format("remote non-planet marker {:08X}/{} is hidden until its system is loaded",
                            a_snapshot.markerBodyID, a_snapshot.markerBodyType),
                    };
                }

                const auto stationTargets = ResolveStationTargets(a_snapshot.markerBodyID);
                if (stationTargets.size() > 1)
                    return unavailable(EligibilityCode::kAmbiguousTarget,
                        "STATION TARGET IS AMBIGUOUS",
                        std::format("non-planet marker {:08X}/{} resolves to {} live starstation references",
                            a_snapshot.markerBodyID, a_snapshot.markerBodyType,
                            stationTargets.size()));

                if (!stationTargets.empty()) {
                    const auto& station = stationTargets.front();
                    auto destination = BodyDestination{
                        .kind = BodyKind::kStation,
                        .formID = station.referenceFormID,
                        .mapFormID = a_snapshot.markerBodyID,
                        .mapType = a_snapshot.markerBodyType,
                        .galaxy = { .system = a_snapshot.capturedSystem },
                        .localizedName = a_snapshot.markerName.empty() ?
                            std::format("STATION {:08X}", station.referenceFormID) :
                            a_snapshot.markerName,
                        .menuGeneration = a_snapshot.generation,
                    };
                    return {
                        .code = EligibilityCode::kEligible,
                        .show = true,
                        .enabled = true,
                        .label = kCruiseMapActionLabel,
                        .detail = std::format("eligible station map={:08X}/{} ref={:08X} base={:08X} '{}'",
                            destination.mapFormID, destination.mapType,
                            destination.formID, station.baseFormID,
                            destination.localizedName),
                        .destination = std::move(destination),
                    };
                }

                const auto shipTargets = ResolveShipTargets(a_snapshot.markerBodyID);
                if (shipTargets.size() > 1)
                    return unavailable(EligibilityCode::kAmbiguousTarget,
                        "SHIP TARGET IS AMBIGUOUS",
                        std::format("non-planet CELL {:08X}/{} contains {} live non-station ship references",
                            a_snapshot.markerBodyID, a_snapshot.markerBodyType,
                            shipTargets.size()));

                if (!shipTargets.empty()) {
                    const auto& ship = shipTargets.front();
                    auto localizedName = a_snapshot.markerName.empty() ?
                        std::format("SHIP {:08X}", ship.referenceFormID) :
                        a_snapshot.markerName;
                    const auto shipHudTargets = CurrentHudTargets(ship.referenceFormID);
                    if (shipHudTargets.size() == 1 && !shipHudTargets.front().name.empty())
                        localizedName = shipHudTargets.front().name;
                    auto destination = BodyDestination{
                        .kind = BodyKind::kShip,
                        .formID = ship.referenceFormID,
                        .mapFormID = a_snapshot.markerBodyID,
                        .mapType = a_snapshot.markerBodyType,
                        .galaxy = { .system = a_snapshot.capturedSystem },
                        .localizedName = std::move(localizedName),
                        .menuGeneration = a_snapshot.generation,
                    };
                    return {
                        .code = EligibilityCode::kEligible,
                        .show = true,
                        .enabled = true,
                        .label = kCruiseMapActionLabel,
                        .detail = std::format("eligible ship CELL={:08X}/{} ref={:08X} base={:08X} '{}'",
                            destination.mapFormID, destination.mapType,
                            destination.formID, ship.baseFormID,
                            destination.localizedName),
                        .destination = std::move(destination),
                    };
                }

                const auto hudTargets = CurrentHudTargets(a_snapshot.markerBodyID);
                if (hudTargets.empty())
                    return unavailable(EligibilityCode::kTargetNotIndexed,
                        "TARGET IS NOT AVAILABLE TO CRUISE",
                        std::format("non-planet marker {:08X}/{} has neither a station/ship reference nor a current HUD target row",
                            a_snapshot.markerBodyID, a_snapshot.markerBodyType));
                if (hudTargets.size() != 1)
                    return unavailable(EligibilityCode::kAmbiguousTarget,
                        "TARGET IS AMBIGUOUS",
                        std::format("non-planet marker {:08X}/{} matches {} current HUD target rows",
                            a_snapshot.markerBodyID, a_snapshot.markerBodyType,
                            hudTargets.size()));

                const auto& hudTarget = hudTargets.front();
                auto destination = BodyDestination{
                    .kind = BodyKind::kOther,
                    .formID = hudTarget.id,
                    .mapFormID = a_snapshot.markerBodyID,
                    .mapType = a_snapshot.markerBodyType,
                    .galaxy = { .system = a_snapshot.capturedSystem },
                    .localizedName = a_snapshot.markerName.empty() ?
                        hudTarget.name : a_snapshot.markerName,
                    .menuGeneration = a_snapshot.generation,
                };
                return {
                    .code = EligibilityCode::kEligible,
                    .show = true,
                    .enabled = true,
                    .label = kCruiseMapActionLabel,
                    .detail = std::format("eligible current-feed non-planet map={:08X}/{} target={:08X} '{}'",
                        destination.mapFormID, destination.mapType,
                        destination.formID, destination.localizedName),
                    .destination = std::move(destination),
                };
            }

            if (a_snapshot.dossierBodyID == 0 ||
                (a_snapshot.dossierBodyType != kPlanetType &&
                    a_snapshot.dossierBodyType != kMoonType) ||
                a_snapshot.markerBodyID != a_snapshot.dossierBodyID ||
                a_snapshot.markerBodyType != a_snapshot.dossierBodyType) {
                return unavailable(EligibilityCode::kTargetDataUpdating,
                    "TARGET DATA IS UPDATING",
                    std::format("marker {:08X}/{} differs from dossier {:08X}/{}",
                        a_snapshot.markerBodyID, a_snapshot.markerBodyType,
                        a_snapshot.dossierBodyID, a_snapshot.dossierBodyType));
            }

            // Live 1.16.244 proof identifies the selected system-view body as
            // the one StarMapMenuMarkersData row with bIsInHighlightRadius.
            // Tree focus remains the system/star and does not join identity.
            const auto form = RE::TESForm::LookupByID(a_snapshot.dossierBodyID);
            if (!form || form->GetFormType() != RE::FormType::kPNDT) {
                return unavailable(EligibilityCode::kTargetTypeUnsupported,
                    "TARGET TYPE IS NOT SUPPORTED",
                    std::format("dossier {:08X} is not a live PNDT form",
                        a_snapshot.dossierBodyID));
            }
            const auto body = BodyIndex::Lookup(a_snapshot.dossierBodyID);
            if (!body) {
                return unavailable(EligibilityCode::kTargetNotIndexed,
                    "TARGET DATA IS NOT AVAILABLE",
                    std::format("dossier PNDT {:08X} has no parsed GNAM identity",
                        a_snapshot.dossierBodyID));
            }
            const bool remote = body->galaxy.system != a_snapshot.capturedSystem;
            if (remote && !g_loadGameSinkReady.load(std::memory_order_acquire))
                return unavailable(EligibilityCode::kRemoteSafetyUnavailable,
                    "REMOTE CRUISE SAFETY UNAVAILABLE",
                    "guarded TESLoadGameEvent sink is unavailable; refusing a mark that could survive a save load");

            auto destination = BodyDestination{
                .kind = a_snapshot.dossierBodyType == kMoonType ? BodyKind::kMoon : BodyKind::kPlanet,
                .formID = a_snapshot.dossierBodyID,
                .mapFormID = a_snapshot.markerBodyID,
                .mapType = a_snapshot.dossierBodyType,
                .galaxy = body->galaxy,
                .localizedName = a_snapshot.dossierName.empty() ?
                    a_snapshot.markerName : a_snapshot.dossierName,
                .menuGeneration = a_snapshot.generation,
            };
            return {
                .code = EligibilityCode::kEligible,
                .show = true,
                .enabled = true,
                .label = remote ? kRemoteCruiseMapActionLabel : kCruiseMapActionLabel,
                .detail = std::format("eligible {}{} {:08X} '{}' system={}",
                    remote ? "remote " : "",
                    DestinationKindName(destination.kind),
                    destination.formID, destination.localizedName,
                    destination.galaxy.system),
                .destination = std::move(destination),
            };
        }

        struct RemoteRouteGate
        {
            bool ready{ false };
            EligibilityCode code{ EligibilityCode::kRemoteCourseUnavailable };
            std::string detail{ "vanilla travel data is unavailable" };
            std::string destinationBodyName;
        };

        bool GetLiveMapMenuRoot(const MapSnapshot& a_snapshot,
            RE::Scaleform::GFx::ASMovieRootBase*& a_root, V& a_menuRoot)
        {
            const auto ui = RE::UI::GetSingleton();
            const RE::BSFixedString mapName{ kMapMenu };
            const auto menu = ui ? ui->GetMenu(mapName) : nullptr;
            if (!menu || !menu->uiMovie || !menu->uiMovie->asMovieRoot ||
                a_snapshot.generation !=
                    g_mapMovie.generation.load(std::memory_order_acquire))
                return false;

            a_root = menu->uiMovie->asMovieRoot.get();
            const char* path = menu->GetRootPath();
            const std::string rootPath = path && *path ? path : "root";
            return a_root->GetVariable(&a_menuRoot, rootPath.c_str()) &&
                   (a_menuRoot.IsObject() || a_menuRoot.IsDisplayObject()) &&
                   menu->uiMovie && menu->uiMovie->asMovieRoot &&
                   menu->uiMovie->asMovieRoot.get() == a_root &&
                   a_snapshot.generation ==
                       g_mapMovie.generation.load(std::memory_order_acquire);
        }

        struct SetCourseButtonState
        {
            bool resolved{ false };
            bool enabled{ false };
            bool visible{ false };
            std::string detail{ "vanilla Set Course button data is unavailable" };

            [[nodiscard]] bool Ready() const noexcept
            {
                return resolved && enabled && visible;
            }
        };

        SetCourseButtonState ReadVanillaSetCourseButton(V& a_menuRoot,
            V& a_hintBar, V& a_buttonData)
        {
            SetCourseButtonState state;
            if (!ObjectMember(a_menuRoot, "ButtonHintBar_mc", a_hintBar) ||
                !ObjectMember(a_hintBar, "SetRouteDestinationButtonData",
                    a_buttonData))
                return state;

            if (!BooleanMember(a_buttonData, "bEnabled", state.enabled) ||
                !BooleanMember(a_buttonData, "bVisible", state.visible)) {
                state.detail = "vanilla Set Course button state is unresolved";
                return state;
            }
            state.resolved = true;
            state.detail = state.Ready() ?
                "vanilla Set Course is enabled and visible" :
                std::format("vanilla Set Course is disabled or hidden (enabled={} visible={})",
                    state.enabled, state.visible);
            return state;
        }

        bool GetVanillaSetCourseData(V& a_menuRoot, V& a_hintBar,
            V& a_buttonData, std::string& a_detail)
        {
            const auto state = ReadVanillaSetCourseButton(a_menuRoot, a_hintBar,
                a_buttonData);
            a_detail = state.detail;
            return state.Ready();
        }

        std::string BrowsedSystemName(V& a_menuRoot)
        {
            V systemHeader;
            V header;
            V textField;
            if (!ObjectMember(a_menuRoot, "SystemNameHeader_mc", systemHeader) ||
                !ObjectMember(systemHeader, "Header_mc", header) ||
                !ObjectMember(header, "text_tf", textField))
                return {};
            return StringMember(textField, "text");
        }

        // Bounded, read-only member enumeration. It copies names and a type tag
        // only: no GFx handle is retained past the visit, so nothing can outlive
        // the movie generation that produced it.
        class MemberNameCollector : public V::ObjectVisitor
        {
        public:
            explicit MemberNameCollector(std::size_t a_limit) : limit(a_limit) {}

            bool IncludeAS3PublicMembers() const override { return true; }

            void Visit(const char* a_name, const V& a_value) override
            {
                ++seen;
                if (!a_name || names.size() >= limit)
                    return;
                const char* kind = "value";
                if (a_value.IsArray())
                    kind = "array";
                else if (a_value.IsDisplayObject())
                    kind = "displayobject";
                else if (a_value.IsObject())
                    kind = "object";
                else if (a_value.IsBoolean())
                    kind = "bool";
                else if (a_value.IsString() || a_value.IsStringW())
                    kind = "string";
                else if (a_value.IsNumber() || a_value.IsInt() || a_value.IsUInt())
                    kind = "number";
                names.emplace_back(std::format("{}:{}", a_name, kind));
            }

            std::vector<std::string> names;
            std::size_t seen{ 0 };
            std::size_t limit{ 0 };
        };

        std::string JoinMemberNames(V& a_object, std::size_t a_limit)
        {
            if (!a_object.IsObject())
                return "<not an object>";
            MemberNameCollector collector{ a_limit };
            a_object.VisitMembers(&collector);
            std::string joined;
            for (const auto& name : collector.names) {
                if (!joined.empty())
                    joined += ", ";
                joined += name;
            }
            if (collector.seen > collector.names.size())
                joined += std::format(", ...(+{} more)",
                    collector.seen - collector.names.size());
            return joined.empty() ? "<none>" : joined;
        }

        struct GalaxySelectionProof
        {
            bool proven{ false };
            const char* authority{ "none" };
            SetCourseButtonState button;
            bool quickSelectMatch{ false };
            bool markerMatch{ false };

            [[nodiscard]] std::string Describe(const MapSnapshot& a_snapshot,
                std::uint32_t a_root) const
            {
                return std::format(
                    "root={:08X} setCourse(resolved={} enabled={} visible={}) quickSelect(published={} count={} cursor={} bodyID={:08X}) marker(count={} bodyID={:08X})",
                    a_root, button.resolved, button.enabled, button.visible,
                    a_snapshot.quickSelectPublished, a_snapshot.quickSelectCount,
                    a_snapshot.quickSelectCursorIndex,
                    a_snapshot.quickSelectCursorBodyID,
                    a_snapshot.highlightedMarkerCount, a_snapshot.markerBodyID);
            }
        };

        // A galaxy selection counts as established only when native itself says
        // so. The vanilla Set Course button is the strongest statement; the
        // Quick Select cursor and the unique galaxy highlight marker are the two
        // other native-published readbacks that name a system directly. Nothing
        // here forces, writes, or infers the button state.
        GalaxySelectionProof EvaluateGalaxySelection(V& a_menuRoot,
            const MapSnapshot& a_snapshot, std::uint32_t a_systemBodyID)
        {
            GalaxySelectionProof proof;
            V hintBar;
            V buttonData;
            proof.button = ReadVanillaSetCourseButton(a_menuRoot, hintBar,
                buttonData);
            proof.quickSelectMatch = a_snapshot.quickSelectPublished &&
                a_snapshot.quickSelectCursorIndex >= 0 && a_systemBodyID != 0 &&
                a_snapshot.quickSelectCursorBodyID == a_systemBodyID;
            proof.markerMatch = a_snapshot.highlightedMarkerCount == 1 &&
                a_systemBodyID != 0 &&
                a_snapshot.markerBodyID == a_systemBodyID;

            if (proof.button.Ready()) {
                proof.proven = true;
                proof.authority = "vanilla Set Course button";
            } else if (proof.button.resolved && proof.button.visible &&
                       proof.quickSelectMatch) {
                // QuickSystemSelect.OnItemPress plots from the list cursor
                // without consulting the hint bar. Mirror that seam only when
                // native published the cursor on the captured root.
                proof.proven = true;
                proof.authority = "native Quick Select cursor";
            } else if (proof.button.resolved && proof.button.visible &&
                       proof.markerMatch) {
                proof.proven = true;
                proof.authority = "unique galaxy highlight marker";
            }
            return proof;
        }

        void LogGalaxyFocusDiagnostics(V& a_menuRoot,
            const MapSnapshot& a_snapshot, const GalaxySelectionProof& a_proof,
            std::uint32_t a_systemBodyID)
        {
            REX::WARN("[jump] galaxy marker context not established: {}",
                a_proof.Describe(a_snapshot, a_systemBodyID));
            REX::INFO("[jump] galaxy diagnostics root members: {}",
                JoinMemberNames(a_menuRoot, 96));

            V hintBar;
            if (!ObjectMember(a_menuRoot, "ButtonHintBar_mc", hintBar)) {
                REX::INFO("[jump] galaxy diagnostics: ButtonHintBar_mc is unavailable");
                return;
            }
            MemberNameCollector collector{ 96 };
            hintBar.VisitMembers(&collector);
            std::string joined;
            for (const auto& entry : collector.names) {
                if (!joined.empty())
                    joined += ", ";
                joined += entry;
            }
            REX::INFO("[jump] galaxy diagnostics hint bar members: {}",
                joined.empty() ? "<none>" : joined);
            for (const auto& entry : collector.names) {
                const auto colon = entry.rfind(':');
                const auto name = entry.substr(0, colon);
                if (name.size() < 10 ||
                    name.compare(name.size() - 10, 10, "ButtonData") != 0)
                    continue;
                V data;
                if (!ObjectMember(hintBar, name.c_str(), data))
                    continue;
                bool enabled = false;
                bool visible = false;
                BooleanMember(data, "bEnabled", enabled);
                BooleanMember(data, "bVisible", visible);
                auto text = StringMember(data, "sText");
                if (text.empty())
                    text = StringMember(data, "text");
                auto action = StringMember(data, "sButtonAction");
                if (action.empty())
                    action = StringMember(data, "buttonAction");
                REX::INFO("[jump] galaxy diagnostics hint '{}' enabled={} visible={} text='{}' action='{}'",
                    name, enabled, visible, text, action);
            }
        }

        RemoteRouteGate InspectRemoteRoute(V& a_menuRoot,
            const std::string& a_expectedSystemName, V* a_jumpDataOut = nullptr)
        {
            RemoteRouteGate gate;
            V jumpData;
            bool panelVisible = false;
            if (!ObjectMember(a_menuRoot, "JumpData_mc", jumpData) ||
                !BooleanMember(jumpData, "visible", panelVisible) ||
                !panelVisible) {
                gate.detail = "vanilla travel panel is not showing a plotted route";
                return gate;
            }
            V routeEnd;
            V routeSystemField;
            if (!ObjectMember(jumpData, "PlotPointDisplayEnd_mc", routeEnd) ||
                !ObjectMember(routeEnd, "systemName_tf", routeSystemField)) {
                gate.detail = "vanilla route destination identity is unavailable";
                return gate;
            }
            const auto routeSystem = StringMember(routeSystemField, "text");
            V routeBodyField;
            if (ObjectMember(routeEnd, "bodyName_tf", routeBodyField))
                gate.destinationBodyName = StringMember(routeBodyField, "text");
            if (routeSystem.empty()) {
                gate.detail = "vanilla route destination system is empty";
                return gate;
            }
            if (a_expectedSystemName.empty() ||
                routeSystem != a_expectedSystemName) {
                gate.code = EligibilityCode::kRemoteCourseMismatch;
                gate.detail = std::format("vanilla route ends in '{}' but marked system is '{}'",
                    routeSystem, a_expectedSystemName);
                return gate;
            }

            V executeContainer;
            V executeButton;
            bool executeVisible = false;
            if (!ObjectMember(jumpData, "ExecuteButton_mc", executeContainer) ||
                !ObjectMember(executeContainer, "ExecuteButtonHint_mc", executeButton) ||
                !BooleanMember(executeButton, "Visible", executeVisible) ||
                !executeVisible) {
                gate.detail = "vanilla bCanExecuteRoute gate is false";
                return gate;
            }

            gate.ready = true;
            gate.code = EligibilityCode::kEligible;
            gate.detail = std::format("vanilla executable route ends in '{}'",
                routeSystem);
            if (a_jumpDataOut)
                *a_jumpDataOut = std::move(jumpData);
            return gate;
        }

        void QueueCourse(std::uint32_t a_id, bool a_clearing)
        {
            std::lock_guard lock{ g_courseMutex };
            g_courseRequest = { a_id, a_clearing, Clock::now() };
            g_hudUiDirty.store(true, std::memory_order_release);
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

        bool DispatchVanillaSetCourse(RE::Scaleform::GFx::ASMovieRootBase* a_root)
        {
            V params;
            a_root->CreateObject(&params);
            if (!params.IsObject() ||
                !params.SetMember("buttonAction", V{ "SetRouteDestination" }))
                return false;
            return DispatchHudEvent(a_root,
                "StarMapMenu_OnHintButtonClicked", &params);
        }

        bool DispatchVanillaGalaxySelection(RE::Scaleform::GFx::ASMovieRootBase* a_root,
            std::uint32_t a_systemBodyID)
        {
            // QuickSystemSelect.OnSelectionChange emits this exact payload
            // before its OpenForPlot item sends SetRouteDestination. Mouse
            // hover normally establishes the selection; reproduce that stock
            // handoff so a remote route never depends on cursor movement.
            V params;
            a_root->CreateObject(&params);
            if (!params.IsObject() ||
                !params.SetMember("bodyID", V{ static_cast<double>(a_systemBodyID) }))
                return false;
            return DispatchHudEvent(a_root,
                "StarMapMenu_QuickSelectChange", &params);
        }

        constexpr const char* kGalaxyHoverSetter = "SetHoveredSystem";

        bool ContainsNoCase(const std::string& a_haystack, std::string_view a_needle)
        {
            const auto it = std::search(a_haystack.begin(), a_haystack.end(),
                a_needle.begin(), a_needle.end(),
                [](char a_lhs, char a_rhs) {
                    return std::tolower(static_cast<unsigned char>(a_lhs)) ==
                           std::tolower(static_cast<unsigned char>(a_rhs));
                });
            return it != a_haystack.end();
        }

        // Second rung: invoke the shipped public hover setter with the captured
        // root instead of relying on the physical cursor. Enumeration is
        // read-only and the only mutation is one call to a method vanilla
        // already exposes; a miss leaves the map untouched.
        bool InvokeGalaxyHoverSetter(V& a_menuRoot, std::uint32_t a_systemBodyID,
            std::string& a_detail)
        {
            MemberNameCollector collector{ 64 };
            if (!a_menuRoot.IsObject()) {
                a_detail = "menu root is not an object";
                return false;
            }
            a_menuRoot.VisitMembers(&collector);

            auto tryInvoke = [&](V& a_owner, const std::string& a_path) {
                if (!a_owner.HasMember(kGalaxyHoverSetter))
                    return false;
                V argument{ static_cast<double>(a_systemBodyID) };
                V result;
                const bool invoked = a_owner.Invoke(kGalaxyHoverSetter, &result,
                    &argument, 1);
                a_detail = std::format("{}.{}({:08X}) invoked={}", a_path,
                    kGalaxyHoverSetter, a_systemBodyID, invoked);
                return invoked;
            };

            for (const auto& entry : collector.names) {
                const auto colon = entry.rfind(':');
                if (colon == std::string::npos ||
                    (entry.compare(colon, std::string::npos, ":object") != 0 &&
                        entry.compare(colon, std::string::npos, ":displayobject") != 0))
                    continue;
                const auto name = entry.substr(0, colon);
                V member;
                if (!ObjectMember(a_menuRoot, name.c_str(), member))
                    continue;
                if (tryInvoke(member, name))
                    return true;
                if (!ContainsNoCase(name, "galaxy"))
                    continue;
                // The shipped Galaxy2DMap can sit one level inside its
                // container. Only galaxy-named containers are descended into so
                // this stays a bounded, targeted probe.
                MemberNameCollector children{ 64 };
                member.VisitMembers(&children);
                for (const auto& childEntry : children.names) {
                    const auto childColon = childEntry.rfind(':');
                    if (childColon == std::string::npos ||
                        (childEntry.compare(childColon, std::string::npos, ":object") != 0 &&
                            childEntry.compare(childColon, std::string::npos, ":displayobject") != 0))
                        continue;
                    const auto childName = childEntry.substr(0, childColon);
                    V child;
                    if (!ObjectMember(member, childName.c_str(), child))
                        continue;
                    if (tryInvoke(child, std::format("{}.{}", name, childName)))
                        return true;
                }
            }

            if (a_detail.empty())
                a_detail = std::format("no reachable public '{}' method", kGalaxyHoverSetter);
            return false;
        }

        bool DispatchVanillaMapCancel(RE::Scaleform::GFx::ASMovieRootBase* a_root)
        {
            // This is the exact Event emitted by GalaxyStarMapMenu's visible
            // Back button. From system view, native returns to galaxy view with
            // that system focused; it does not close the Starmap.
            return DispatchHudEvent(a_root, "StarMapMenu_OnCancel", nullptr);
        }

        bool DispatchVanillaCloseAllMenus(RE::Scaleform::GFx::ASMovieRootBase* a_root)
        {
            // StarMapButtonHintBar.onCloseSubMenuToGame emits these events in
            // this order. The first keeps DataMenu quick-entry state coherent;
            // the second closes the entire menu stack and returns to gameplay.
            const bool quickEntrySet = DispatchHudEvent(
                a_root, "DataMenu_SetMenuForQuickEntry", nullptr);
            const bool closeAll = DispatchHudEvent(
                a_root, "GlobalFunc_CloseAllMenus", nullptr);
            if (!quickEntrySet)
                REX::WARN("[map] stock DataMenu quick-entry dispatch failed before close-all");
            return closeAll;
        }

        bool InvokeHudCruiseUserEvent(RE::Scaleform::GFx::ASMovieRootBase* a_root,
            const char* a_rootPath, const char* a_userEvent, bool a_down)
        {
            V menu;
            const char* path = a_rootPath && *a_rootPath ? a_rootPath : "root";
            if (!a_root->GetVariable(&menu, path) ||
                !(menu.IsObject() || menu.IsDisplayObject())) {
                REX::WARN("[input] HUD root '{}' unavailable for Cruise {}",
                    path, a_down ? "press" : "release");
                return false;
            }

            V eventName;
            a_root->CreateString(&eventName, a_userEvent);
            V args[2]{ eventName, V{ a_down } };
            V handled;
            const bool invoked = menu.Invoke("ProcessUserEvent", &handled, args, 2);
            REX::INFO("[input] forwarded stock HUD '{}' {} invoked={} handled={}",
                a_userEvent, a_down ? "press" : "release", invoked,
                handled.IsBoolean() ? handled.GetBoolean() : false);
            return invoked;
        }

        void DriveHudCruiseInput(RE::Scaleform::GFx::ASMovieRootBase* a_root,
            const char* a_rootPath)
        {
            bool press = false;
            bool release = false;
            const char* userEvent = "Cruise";
            {
                std::lock_guard lock{ g_hudCruiseInputMutex };
                userEvent = g_hudCruiseUserEvent;
                if (g_hudCruiseInputPhase == HudCruiseInputPhase::kPressPending) {
                    // Publish the new phase before calling ActionScript. This
                    // prevents a synchronous callback from repeating the edge.
                    g_hudCruiseInputPhase = HudCruiseInputPhase::kPressed;
                    press = true;
                } else if (g_hudCruiseInputPhase == HudCruiseInputPhase::kReleasePending) {
                    g_hudCruiseInputPhase = HudCruiseInputPhase::kIdle;
                    g_hudCruiseUserEvent = "Cruise";
                    release = true;
                }
            }

            if (press && !InvokeHudCruiseUserEvent(a_root, a_rootPath, userEvent, true)) {
                {
                    std::lock_guard lock{ g_hudCruiseInputMutex };
                    g_hudCruiseInputPhase = HudCruiseInputPhase::kIdle;
                    g_hudCruiseUserEvent = "Cruise";
                    g_hudCruiseInputLatched = false;
                    g_hudCruiseInputStarted = {};
                }
                g_state.store(Destination() ? NavState::kMarked : NavState::kIdle,
                    std::memory_order_release);
            } else if (release) {
                InvokeHudCruiseUserEvent(a_root, a_rootPath, userEvent, false);
            }
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

        enum class MapAction : std::uint8_t
        {
            kNone,
            kTap,
            kHold,
        };
        std::atomic<MapAction> g_pendingMapAction{ MapAction::kNone };

        void AcceptSelection(MapAction a_action)
        {
            if (a_action == MapAction::kNone)
                return;
            MapSnapshot snapshot;
            {
                std::lock_guard lock{ g_mapMutex };
                snapshot = g_map;
            }
            const auto eligibility = EvaluateMapSelection(snapshot);
            if (!eligibility.enabled || !eligibility.destination) {
                if (Settings::Verbose())
                    REX::INFO("[map] Cruise action rejected: {}", eligibility.detail);
                return;
            }
            const auto& selected = *eligibility.destination;
            const bool remotePlanetary = IsPlanetary(selected) &&
                g_haveCurrentSystem.load(std::memory_order_acquire) &&
                selected.galaxy.system != g_currentSystem.load(std::memory_order_acquire);

            RE::Scaleform::GFx::ASMovieRootBase* mapRoot = nullptr;
            std::string expectedSystemName;
            std::uint32_t expectedSystemBodyID = 0;
            if (remotePlanetary) {
                V menuRoot;
                if (!GetLiveMapMenuRoot(snapshot, mapRoot, menuRoot)) {
                    REX::WARN("[jump] remote action rejected: live Starmap root is unavailable");
                    return;
                }
                V hintBar;
                V setCourseData;
                std::string setCourseDetail;
                if (!GetVanillaSetCourseData(menuRoot, hintBar,
                        setCourseData, setCourseDetail)) {
                    g_mapUiDirty.store(true, std::memory_order_release);
                    REX::INFO("[jump] remote action rejected: {}", setCourseDetail);
                    return;
                }
                expectedSystemName = BrowsedSystemName(menuRoot);
                if (expectedSystemName.empty()) {
                    g_mapUiDirty.store(true, std::memory_order_release);
                    REX::WARN("[jump] remote action rejected: browsed system identity is unavailable");
                    return;
                }
                const auto treeSystemID = MapTreeSystemID(snapshot.treeBodyID);
                if (!treeSystemID || *treeSystemID != selected.galaxy.system) {
                    g_mapUiDirty.store(true, std::memory_order_release);
                    REX::WARN("[jump] remote action rejected: focused STDT root {:08X} resolves to {}, expected system {}",
                        snapshot.treeBodyID,
                        treeSystemID ? std::format("system {}", *treeSystemID) : "no live star",
                        selected.galaxy.system);
                    return;
                }
                expectedSystemBodyID = snapshot.treeBodyID;
            }

            const auto existing = Destination();
            const bool same = existing && existing->mapFormID == selected.mapFormID &&
                existing->mapType == selected.mapType &&
                existing->formID == selected.formID;
            if (same && a_action == MapAction::kTap && !remotePlanetary) {
                const bool courseMatches = g_confirmedCourseID.load(std::memory_order_acquire) == selected.formID;
                ClearDestination("explicit same-target toggle");
                if (courseMatches && g_cruiseActive.load(std::memory_order_acquire))
                    QueueCourse(selected.formID, true);
            } else if (!same) {
                StoreDestination(selected);
            }

            auto selectionDevice = RE::InputEvent::DeviceType::kNone;
            {
                std::lock_guard lock{ g_holdMutex };
                selectionDevice = g_hold.device;
                const bool liveHold = a_action == MapAction::kHold && g_hold.active &&
                    g_hold.session == g_mapSession.load(std::memory_order_acquire);
                if (a_action == MapAction::kHold && !liveHold) {
                    REX::WARN("[map] completed UI hold had no matching physical control; preserving target without starting Cruise");
                }
                g_hold.completed = liveHold;
                g_claimMapKey = liveHold;
            }
            if (remotePlanetary && Destination()) {
                if (selectionDevice == RE::InputEvent::DeviceType::kNone)
                    selectionDevice = RE::InputEvent::DeviceType::kKeyboard;
                g_pendingJumpDevice.store(selectionDevice, std::memory_order_release);
            }
            g_selectionAcceptedThisOpen.store(true, std::memory_order_release);
            if (remotePlanetary) {
                // Preserve the body only as our Cruise destination. First emit
                // the exact stock Back event so native returns from the body
                // system view to the focused system node in galaxy view. A
                // later post-advance pass proves that focus before asking
                // vanilla to Set Course at system scope.
                g_state.store(NavState::kMapSelection, std::memory_order_release);
                {
                    std::lock_guard lock{ g_remoteRouteMutex };
                    g_remoteRouteRequest = {
                        .phase = RemoteRoutePhase::kAwaitGalaxy,
                        .session = snapshot.session,
                        .generation = snapshot.generation,
                        .targetFormID = selected.formID,
                        .systemBodyID = expectedSystemBodyID,
                        .expectedSystemName = expectedSystemName,
                        .targetName = selected.localizedName,
                        .started = Clock::now(),
                    };
                }
                if (!DispatchVanillaMapCancel(mapRoot)) {
                    g_selectionAcceptedThisOpen.store(false, std::memory_order_release);
                    ClearDestination("vanilla system-view Back handoff failed");
                    g_mapUiDirty.store(true, std::memory_order_release);
                    REX::WARN("[jump] stock StarMapMenu_OnCancel dispatch failed; remote mark cleared");
                    return;
                }
                REX::INFO("[jump] accepted tap map={:08X}/{} target={:08X} systemRoot={:08X} system='{}'; dispatched stock Back and awaiting matching galaxy focus",
                    selected.mapFormID, selected.mapType, selected.formID,
                    expectedSystemBodyID, expectedSystemName);
                return;
            }

            V menuRoot;
            if (!GetLiveMapMenuRoot(snapshot, mapRoot, menuRoot) ||
                !DispatchVanillaCloseAllMenus(mapRoot)) {
                // Preserve the previous safe behavior if the stock AS3 event
                // path is unexpectedly unavailable. This may reveal a parent
                // DataMenu, but never leaves the accepted Starmap stuck open.
                g_closeRequested.store(true, std::memory_order_release);
                REX::WARN("[map] stock close-all dispatch failed; queued Starmap hide fallback");
            }
            REX::INFO("[map] accepted {} map={:08X}/{} target={:08X}; requested stock return-to-game close",
                a_action == MapAction::kHold ? "hold" : "tap", selected.mapFormID,
                selected.mapType, selected.formID);
        }

        bool RemoteRouteRequestActive()
        {
            std::lock_guard lock{ g_remoteRouteMutex };
            return g_remoteRouteRequest.phase != RemoteRoutePhase::kNone;
        }

        void DriveRemoteRouteRequest()
        {
            if (!g_applicationForeground.load(std::memory_order_acquire))
                return;

            RemoteRouteRequest request;
            {
                std::lock_guard lock{ g_remoteRouteMutex };
                request = g_remoteRouteRequest;
            }
            if (request.phase == RemoteRoutePhase::kNone)
                return;

            MapSnapshot snapshot;
            {
                std::lock_guard lock{ g_mapMutex };
                snapshot = g_map;
            }
            const auto age = Clock::now() - request.started;
            const auto destination = Destination();
            if (!g_mapOpen.load(std::memory_order_acquire) ||
                !destination || destination->formID != request.targetFormID ||
                request.session != snapshot.session ||
                request.generation != snapshot.generation) {
                if (!g_mapOpen.load(std::memory_order_acquire))
                    return;  // The menu-close sink owns cancellation or success.
                g_selectionAcceptedThisOpen.store(false, std::memory_order_release);
                ClearDestination("remote Set Course session or destination changed");
                REX::WARN("[jump] remote route request lost its guarded map identity; mark cleared");
                return;
            }

            RE::Scaleform::GFx::ASMovieRootBase* root = nullptr;
            V menuRoot;
            if (!GetLiveMapMenuRoot(snapshot, root, menuRoot))
                return;

            if (request.phase == RemoteRoutePhase::kAwaitGalaxy) {
                std::string gateDetail;
                if (snapshot.view != kGalaxyView) {
                    gateDetail = std::format("Starmap view is {} rather than galaxy view",
                        snapshot.view);
                } else {
                    const auto focusedSystemID = MapTreeSystemID(snapshot.treeBodyID);
                    if (!focusedSystemID) {
                        gateDetail = "focused galaxy STDT/DNAM identity is unavailable";
                    } else if (snapshot.treeBodyID != request.systemBodyID ||
                               *focusedSystemID != destination->galaxy.system) {
                        gateDetail = std::format("galaxy root {:08X}/system {} differs from marked root {:08X}/system {}",
                            snapshot.treeBodyID, *focusedSystemID,
                            request.systemBodyID, destination->galaxy.system);
                    } else {
                        // Galaxy view now carries the exact captured root. The
                        // remaining work is native galaxy-marker selection, which
                        // gets its own phase and its own full timeout so a slow
                        // Back transition cannot consume the focus budget.
                        {
                            std::lock_guard lock{ g_remoteRouteMutex };
                            if (g_remoteRouteRequest.phase != RemoteRoutePhase::kAwaitGalaxy ||
                                g_remoteRouteRequest.targetFormID != request.targetFormID ||
                                g_remoteRouteRequest.generation != request.generation)
                                return;
                            g_remoteRouteRequest.phase = RemoteRoutePhase::kEstablishSelection;
                            g_remoteRouteRequest.started = Clock::now();
                            g_remoteRouteRequest.executeReadySince = {};
                        }
                        REX::INFO("[jump] matching galaxy STDT/DNAM root {:08X}/system {} reached after {} ms; establishing cursor-independent marker context for '{}'",
                            snapshot.treeBodyID, *focusedSystemID,
                            std::chrono::duration_cast<std::chrono::milliseconds>(age).count(),
                            request.expectedSystemName);
                        return;
                    }
                }

                if (age < kRemoteRouteTimeout)
                    return;
                g_selectionAcceptedThisOpen.store(false, std::memory_order_release);
                const auto reason = std::format("vanilla Back did not produce a matching galaxy focus: {}",
                    gateDetail);
                ClearDestination(reason.c_str());
                g_mapUiDirty.store(true, std::memory_order_release);
                REX::WARN("[jump] {}; remote mark cleared", reason);
                return;
            }

            if (request.phase == RemoteRoutePhase::kEstablishSelection) {
                if (snapshot.view != kGalaxyView) {
                    if (age < kRemoteRouteTimeout)
                        return;
                    g_selectionAcceptedThisOpen.store(false, std::memory_order_release);
                    ClearDestination("galaxy marker context left galaxy view before Set Course");
                    g_mapUiDirty.store(true, std::memory_order_release);
                    REX::WARN("[jump] marker-context gate timed out outside galaxy view; remote mark cleared");
                    return;
                }

                const auto proof = EvaluateGalaxySelection(menuRoot, snapshot,
                    request.systemBodyID);
                if (proof.proven) {
                    {
                        std::lock_guard lock{ g_remoteRouteMutex };
                        if (g_remoteRouteRequest.phase != RemoteRoutePhase::kEstablishSelection ||
                            g_remoteRouteRequest.targetFormID != request.targetFormID ||
                            g_remoteRouteRequest.generation != request.generation)
                            return;
                        g_remoteRouteRequest.phase = RemoteRoutePhase::kAwaitRoute;
                        g_remoteRouteRequest.started = Clock::now();
                        g_remoteRouteRequest.executeReadySince = {};
                    }
                    REX::INFO("[jump] marker context established by {} after {} ms; {}",
                        proof.authority,
                        std::chrono::duration_cast<std::chrono::milliseconds>(age).count(),
                        proof.Describe(snapshot, request.systemBodyID));
                    if (proof.button.Ready())
                        REX::INFO("[jump] Set Course enabled for '{}' root={:08X}",
                            request.expectedSystemName, request.systemBodyID);
                    else
                        REX::INFO("[jump] Set Course still reports enabled={} visible={} while native selection is proven by {}; the vanilla button is never written to",
                            proof.button.enabled, proof.button.visible,
                            proof.authority);
                    if (!DispatchVanillaSetCourse(root)) {
                        g_selectionAcceptedThisOpen.store(false, std::memory_order_release);
                        ClearDestination("vanilla system-level Set Course handoff failed");
                        g_mapUiDirty.store(true, std::memory_order_release);
                        REX::WARN("[jump] stock system-level SetRouteDestination dispatch failed; remote mark cleared");
                        return;
                    }
                    REX::INFO("[jump] Set Course dispatched at system scope for '{}' root={:08X} (authority={})",
                        request.expectedSystemName, request.systemBodyID,
                        proof.authority);
                    return;  // Never consume a route that predates this dispatch.
                }

                // No proof yet. A rung that has just run keeps the ladder for a
                // fixed number of completed advances so native can publish its
                // result before the next rung is allowed to change the same
                // state.
                if (request.focusRungCooldown != 0) {
                    std::lock_guard lock{ g_remoteRouteMutex };
                    if (g_remoteRouteRequest.phase == RemoteRoutePhase::kEstablishSelection &&
                        g_remoteRouteRequest.targetFormID == request.targetFormID &&
                        g_remoteRouteRequest.generation == request.generation &&
                        g_remoteRouteRequest.focusRungCooldown != 0)
                        --g_remoteRouteRequest.focusRungCooldown;
                    return;
                }

                // Advance the focus ladder by exactly one rung on this pass.
                // Each rung is a stock, cursor-independent seam.
                if (request.nextFocusRung != GalaxyFocusRung::kExhausted) {
                    auto rung = request.nextFocusRung;
                    {
                        std::lock_guard lock{ g_remoteRouteMutex };
                        if (g_remoteRouteRequest.phase != RemoteRoutePhase::kEstablishSelection ||
                            g_remoteRouteRequest.targetFormID != request.targetFormID ||
                            g_remoteRouteRequest.generation != request.generation)
                            return;
                        g_remoteRouteRequest.nextFocusRung =
                            rung == GalaxyFocusRung::kQuickSelectChange ?
                            GalaxyFocusRung::kHoverSetter :
                            GalaxyFocusRung::kExhausted;
                        g_remoteRouteRequest.focusRungCooldown = kGalaxyFocusRungPasses;
                    }
                    if (rung == GalaxyFocusRung::kQuickSelectChange) {
                        if (!DispatchVanillaGalaxySelection(root, request.systemBodyID)) {
                            g_selectionAcceptedThisOpen.store(false, std::memory_order_release);
                            ClearDestination("vanilla galaxy system selection handoff failed");
                            g_mapUiDirty.store(true, std::memory_order_release);
                            REX::WARN("[jump] stock StarMapMenu_QuickSelectChange dispatch failed; remote mark cleared");
                            return;
                        }
                        REX::INFO("[jump] focus rung 1: primed stock Quick Select system bodyID={:08X} for '{}' without cursor input",
                            request.systemBodyID, request.expectedSystemName);
                    } else {
                        std::string detail;
                        const bool invoked = InvokeGalaxyHoverSetter(menuRoot,
                            request.systemBodyID, detail);
                        // A miss here is not fatal: rung 1 may still settle, and
                        // the vanilla route/warning state is untouched either way.
                        REX::INFO("[jump] focus rung 2: public galaxy hover setter {} ({})",
                            invoked ? "invoked" : "unavailable", detail);
                    }
                    return;  // Re-test native state on the next advance.
                }

                if (!request.focusDiagnosticsLogged) {
                    {
                        std::lock_guard lock{ g_remoteRouteMutex };
                        if (g_remoteRouteRequest.phase != RemoteRoutePhase::kEstablishSelection ||
                            g_remoteRouteRequest.targetFormID != request.targetFormID ||
                            g_remoteRouteRequest.generation != request.generation)
                            return;
                        g_remoteRouteRequest.focusDiagnosticsLogged = true;
                    }
                    LogGalaxyFocusDiagnostics(menuRoot, snapshot, proof,
                        request.systemBodyID);
                    return;
                }

                if (age < kRemoteRouteTimeout)
                    return;
                g_selectionAcceptedThisOpen.store(false, std::memory_order_release);
                const auto reason = std::format("no cursor-independent galaxy marker context was established: {}",
                    proof.Describe(snapshot, request.systemBodyID));
                ClearDestination(reason.c_str());
                g_mapUiDirty.store(true, std::memory_order_release);
                REX::WARN("[jump] {}; remote mark cleared and vanilla route state preserved",
                    reason);
                return;
            }

            if (snapshot.view != kGalaxyView) {
                if (age < kRemoteRouteTimeout)
                    return;
                g_selectionAcceptedThisOpen.store(false, std::memory_order_release);
                ClearDestination("system-level Set Course left galaxy view before producing a route");
                g_mapUiDirty.store(true, std::memory_order_release);
                REX::WARN("[jump] system-level route gate timed out outside galaxy view; remote mark cleared");
                return;
            }

            V jumpData;
            auto gate = InspectRemoteRoute(menuRoot,
                request.expectedSystemName, &jumpData);

            if (!gate.ready) {
                if (request.executeReadySince != Clock::time_point{}) {
                    std::lock_guard lock{ g_remoteRouteMutex };
                    if (g_remoteRouteRequest.phase == RemoteRoutePhase::kAwaitRoute &&
                        g_remoteRouteRequest.targetFormID == request.targetFormID &&
                        g_remoteRouteRequest.generation == request.generation) {
                        g_remoteRouteRequest.executeReadySince = {};
                    }
                }
                if (age < kRemoteRouteTimeout)
                    return;

                g_selectionAcceptedThisOpen.store(false, std::memory_order_release);
                const auto reason = std::format("vanilla Set Course did not produce an executable matching route: {}",
                    gate.detail);
                ClearDestination(reason.c_str());
                g_mapUiDirty.store(true, std::memory_order_release);
                REX::WARN("[jump] {}; remote mark cleared and vanilla route state preserved",
                    reason);
                return;
            }

            const auto now = Clock::now();
            if (request.executeReadySince == Clock::time_point{}) {
                {
                    std::lock_guard lock{ g_remoteRouteMutex };
                    if (g_remoteRouteRequest.phase != RemoteRoutePhase::kAwaitRoute ||
                        g_remoteRouteRequest.targetFormID != request.targetFormID ||
                        g_remoteRouteRequest.generation != request.generation)
                        return;
                    g_remoteRouteRequest.executeReadySince = now;
                }
                REX::INFO("[jump] route identity confirmed: vanilla route ends in '{}' body='{}'; requiring {} ms continuous readiness before stock Execute Route",
                    request.expectedSystemName, gate.destinationBodyName,
                    kRemoteRouteExecuteSettleTime.count());
                return;
            }
            const auto readyAge = now - request.executeReadySince;
            if (readyAge < kRemoteRouteExecuteSettleTime)
                return;

            {
                std::lock_guard lock{ g_remoteRouteMutex };
                if (g_remoteRouteRequest.phase != RemoteRoutePhase::kAwaitRoute ||
                    g_remoteRouteRequest.targetFormID != request.targetFormID ||
                    g_remoteRouteRequest.generation != request.generation)
                    return;
                g_remoteRouteRequest.phase = RemoteRoutePhase::kNone;
            }

            // SendExecuteEvent is the callback behind the visible vanilla
            // Execute hold. It rechecks ExecuteButtonHint.Visible before
            // dispatching StarMapMenu_ExecuteRoute.
            g_state.store(NavState::kPendingJump, std::memory_order_release);
            if (!jumpData.Invoke("SendExecuteEvent")) {
                g_selectionAcceptedThisOpen.store(false, std::memory_order_release);
                ClearDestination("vanilla Execute Route handoff failed");
                g_mapUiDirty.store(true, std::memory_order_release);
                REX::WARN("[jump] JumpDataPanel.SendExecuteEvent invocation failed; remote mark cleared");
                return;
            }
            REX::INFO("[jump] Execute dispatched: vanilla matching-system route remained executable for {} ms for '{}' body='{}' (route age {} ms); stock StarMapMenu_ExecuteRoute sent while retaining Cruise target {:08X}",
                std::chrono::duration_cast<std::chrono::milliseconds>(readyAge).count(),
                request.expectedSystemName,
                gate.destinationBodyName,
                std::chrono::duration_cast<std::chrono::milliseconds>(age).count(),
                request.targetFormID);
        }

        class MapActionHandler : public RE::Scaleform::GFx::FunctionHandler
        {
        public:
            explicit MapActionHandler(MapAction a_action) : action(a_action) {}

            void Call(const Params&) override
            {
                // This callback is executing inside the AS3 VM. Record only a
                // value signal; selection evaluation and engine/UI side effects
                // run from the post-advance pump after the VM unwinds.
                const auto pending = action == MapAction::kHold &&
                                             g_mapActionTapOnly.load(std::memory_order_acquire) ?
                                         MapAction::kTap :
                                         action;
                g_pendingMapAction.store(pending, std::memory_order_release);
            }

        private:
            MapAction action;
        };

        MapActionHandler g_mapTapActionHandler{ MapAction::kTap };
        MapActionHandler g_mapHoldActionHandler{ MapAction::kHold };

        void TrackActiveInputDevice(const RE::ButtonEvent* a_button)
        {
            if (!a_button || a_button->value == 0.0f || a_button->heldDownSecs != 0.0f)
                return;
            if (a_button->deviceType != RE::InputEvent::DeviceType::kKeyboard &&
                a_button->deviceType != RE::InputEvent::DeviceType::kMouse &&
                a_button->deviceType != RE::InputEvent::DeviceType::kGamepad)
                return;

            const bool gamepad = a_button->deviceType == RE::InputEvent::DeviceType::kGamepad;
            if (g_lastInputWasGamepad.exchange(gamepad, std::memory_order_acq_rel) != gamepad &&
                g_mapOpen.load(std::memory_order_acquire)) {
                g_mapUiDirty.store(true, std::memory_order_release);
                if (Settings::Verbose())
                    REX::INFO("[input] Starmap action hint input mode -> {}",
                        gamepad ? "controller" : "keyboard/mouse");
            }
        }

        bool ObserveButton(const RE::ButtonEvent* a_button)
        {
            const bool down = a_button->value != 0.0f;
            const bool first = down && a_button->heldDownSecs == 0.0f;
            const char* raw = a_button->strUserEvent.c_str();
            const std::string_view name = raw ? raw : "";

            if (g_mapOpen.load(std::memory_order_acquire) && first &&
                (name == kCruiseMapUserEvent || name == kCruiseMapGamepadUserEvent)) {
                if (a_button->disabled)
                    return false;
                // The runtime-installed ReleaseHoldComboButton must receive the
                // Cruise down/up stream so its stock hold timer and fill
                // animation can distinguish tap from completed hold. Capture
                // only the physical identity here; its callbacks accept the
                // action after the UI gesture finishes.
                if (g_mapActionInteractive.load(std::memory_order_acquire)) {
                    std::lock_guard lock{ g_holdMutex };
                    g_claimMapKey = false;
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
                return false;
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
                if (!down) {
                    g_hold.active = false;
                    if (HudCruiseInputLatched()) {
                        if (Settings::Verbose())
                            REX::INFO("[input] physical control released after completed Starmap hold; HUD Cruise remains pressed until activation");
                    } else {
                        CancelOrReleaseHudCruiseInput("physical control released");
                    }
                    if (g_state.load(std::memory_order_acquire) == NavState::kAwaitingCruise &&
                        !g_cruiseActive.load(std::memory_order_acquire))
                        g_state.store(Destination() ? NavState::kMarked : NavState::kIdle,
                            std::memory_order_release);
                }
                return true;
            }

            if (name == kCruiseMapUserEvent || name == kCruiseMapGamepadUserEvent ||
                name == "LockCourse") {
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

        bool RouteCruiseMapControl(RE::ButtonEvent* a_button)
        {
            if (!a_button || !g_mapOpen.load(std::memory_order_acquire) ||
                !g_mapActionInteractive.load(std::memory_order_acquire) || a_button->disabled)
                return false;

            std::int32_t binding = -1;
            std::int32_t modifier = -1;
            switch (a_button->deviceType) {
            case RE::InputEvent::DeviceType::kKeyboard:
                binding = g_cruiseMapKey.load(std::memory_order_acquire);
                modifier = g_cruiseMapModifier.load(std::memory_order_acquire);
                break;
            case RE::InputEvent::DeviceType::kMouse:
                binding = g_cruiseMapMouseButton.load(std::memory_order_acquire);
                break;
            case RE::InputEvent::DeviceType::kGamepad:
                binding = g_cruiseMapGamepadButton.load(std::memory_order_acquire);
                break;
            default:
                return false;
            }
            if (binding < 0 || a_button->idCode != binding)
                return false;

            // The engine's StarMap context normally names this physical key as
            // a map action rather than Cruise. Route the down edge only when a
            // configured chord modifier is held; always route release so the
            // active stock button cannot be left in its down state. Both
            // variants expose the real Cruise event so Starfield can resolve
            // the player's current binding and glyph; the inactive control is
            // disabled and hidden.
            if (a_button->deviceType == RE::InputEvent::DeviceType::kKeyboard &&
                a_button->value != 0.0f && modifier >= 0 &&
                (::GetAsyncKeyState(modifier) & 0x8000) == 0)
                return false;

            // Route to the data object currently installed on the Scaleform
            // button. If this edge also changed device mode, the next safe UI
            // pass swaps the data object; this first edge still reaches the old
            // object instead of being lost.
            a_button->strUserEvent = RE::BSFixedString{
                g_mapHintUsesGamepad.load(std::memory_order_acquire) ?
                    kCruiseMapGamepadUserEvent : kCruiseMapUserEvent
            };
            return true;
        }

        bool SuppressRemoteRouteControl(const RE::ButtonEvent* a_button)
        {
            if (!a_button || !g_mapOpen.load(std::memory_order_acquire) ||
                !RemoteRouteRequestActive())
                return false;

            std::int32_t binding = -1;
            switch (a_button->deviceType) {
            case RE::InputEvent::DeviceType::kKeyboard:
                binding = g_cruiseMapKey.load(std::memory_order_acquire);
                break;
            case RE::InputEvent::DeviceType::kMouse:
                binding = g_cruiseMapMouseButton.load(std::memory_order_acquire);
                break;
            case RE::InputEvent::DeviceType::kGamepad:
                binding = g_cruiseMapGamepadButton.load(std::memory_order_acquire);
                break;
            default:
                return false;
            }
            if (binding < 0 || a_button->idCode != binding)
                return false;
            if (a_button->value != 0.0f && a_button->heldDownSecs == 0.0f)
                REX::INFO("[input] suppressed repeated Starmap Cruise control while remote route handoff is active");
            return true;
        }

        void ProcessInputHook(RE::BSInputEventReceiver* a_receiver, const RE::InputEvent* a_head)
        {
            struct Fix
            {
                RE::InputEvent* node{ nullptr };
                RE::InputEvent* next{ nullptr };
            };
            struct RoutedEvent
            {
                RE::ButtonEvent* event{ nullptr };
                RE::BSFixedString originalName;
            };
            std::array<Fix, 16> fixes{};
            std::array<RoutedEvent, 16> routedEvents{};
            std::size_t fixCount = 0;
            std::size_t routedCount = 0;
            const RE::InputEvent* head = a_head;
            RE::InputEvent* previous = nullptr;

            for (auto* event = a_head; event;) {
                auto* next = event->next;
                bool drop = false;
                if (event->eventType == RE::InputEvent::EventType::kButton) {
                    auto* button = const_cast<RE::ButtonEvent*>(
                        static_cast<const RE::ButtonEvent*>(event));
                    TrackActiveInputDevice(button);
                    drop = SuppressRemoteRouteControl(button);
                    if (!drop && routedCount < routedEvents.size()) {
                        const auto originalName = button->strUserEvent;
                        if (RouteCruiseMapControl(button))
                            routedEvents[routedCount++] = { button, originalName };
                    }
                    if (!drop)
                        drop = ObserveButton(button);
                }
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
            for (std::size_t i = routedCount; i-- > 0;)
                routedEvents[i].event->strUserEvent = routedEvents[i].originalName;
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

        std::uint64_t EligibilitySignature(const MapSnapshot& a_snapshot,
            const MapEligibility& a_eligibility)
        {
            std::uint64_t signature = 1469598103934665603ull;
            const auto mix = [&signature](std::uint64_t a_value) {
                signature ^= a_value;
                signature *= 1099511628211ull;
            };
            mix(a_snapshot.generation);
            mix(a_snapshot.session);
            mix(a_snapshot.wasCruising);
            mix(a_snapshot.cruiseEngageAvailable);
            mix(g_lastInputWasGamepad.load(std::memory_order_acquire));
            mix(static_cast<std::uint64_t>(a_eligibility.code));
            mix(a_snapshot.markerBodyID);
            mix(a_snapshot.markerBodyType);
            mix(a_snapshot.dossierBodyID);
            mix(a_snapshot.dossierBodyType);
            return signature;
        }

        bool GetMapButtonBar(V& a_hintBar, V& a_buttonBar)
        {
            return a_hintBar.GetMember("HintBar_mc", &a_buttonBar) &&
                   (a_buttonBar.IsObject() || a_buttonBar.IsDisplayObject());
        }

        bool BuildCruiseComboButton(RE::Scaleform::GFx::ASMovieRootBase* a_root,
            V& a_buttonBar, V& a_vanillaData, V& a_button,
            V& a_mkbButtonData, V& a_gamepadButtonData)
        {
            V tapCallback;
            a_root->CreateFunction(&tapCallback, &g_mapTapActionHandler);
            V holdCallback;
            a_root->CreateFunction(&holdCallback, &g_mapHoldActionHandler);
            const auto buildData = [&](const char* a_userEvent, V& a_buttonData) {
                V pressEventName;
                a_root->CreateString(&pressEventName, a_userEvent);
                V pressArgs[2]{ pressEventName, tapCallback };
                V pressEvent;
                a_root->CreateObject(&pressEvent,
                    "Shared.Components.ButtonControls.ButtonData.UserEventData", pressArgs, 2);

                V emptyName;
                a_root->CreateString(&emptyName, "");
                V holdArgs[2]{ emptyName, holdCallback };
                V holdEvent;
                a_root->CreateObject(&holdEvent,
                    "Shared.Components.ButtonControls.ButtonData.UserEventData", holdArgs, 2);
                if (!(pressEvent.IsObject() || pressEvent.IsDisplayObject()) ||
                    !(holdEvent.IsObject() || holdEvent.IsDisplayObject())) {
                    REX::WARN("[map] stacked '{}' action hint unavailable: UserEventData construction failed",
                        a_userEvent);
                    return false;
                }

                V events;
                a_root->CreateArray(&events);
                if (!events.IsArray() || !events.PushBack(pressEvent) ||
                    !events.PushBack(holdEvent)) {
                    REX::WARN("[map] stacked '{}' action hint unavailable: combo event array construction failed",
                        a_userEvent);
                    return false;
                }

                V pressLabel;
                V holdLabel;
                a_root->CreateString(&pressLabel, kCruiseMapActionLabel);
                a_root->CreateString(&holdLabel, kCruiseMapActionHoldLabel);
                V dataArgs[3]{ pressLabel, holdLabel, events };
                a_root->CreateObject(&a_buttonData,
                    "Shared.Components.ButtonControls.ButtonData.ReleaseHoldComboButtonData",
                    dataArgs, 3);
                if (!(a_buttonData.IsObject() || a_buttonData.IsDisplayObject())) {
                    REX::WARN("[map] stacked '{}' action hint unavailable: ReleaseHoldComboButtonData construction failed",
                        a_userEvent);
                    return false;
                }

                for (const char* member : { "bEnabled", "bVisible" }) {
                    V value;
                    if (a_vanillaData.GetMember(member, &value))
                        a_buttonData.SetMember(member, value);
                }
                return true;
            };
            if (!buildData(kCruiseMapUserEvent, a_mkbButtonData) ||
                !buildData(kCruiseMapGamepadUserEvent, a_gamepadButtonData))
                return false;

            // ReleaseHoldComboButton is an imported library symbol. Let the
            // movie instantiate it through the same factory used by
            // StarMapButtonHintBar.PopulateButtons; creating the AS3 class
            // directly does not attach the exported display asset.
            V factory;
            if (!a_root->GetVariable(&factory,
                    "Shared.Components.ButtonControls.ButtonFactory.ButtonFactory") ||
                !(factory.IsObject() || factory.IsDisplayObject())) {
                REX::WARN("[map] stacked action hint unavailable: stock ButtonFactory is inaccessible");
                return false;
            }
            V buttonType;
            a_root->CreateString(&buttonType, "ReleaseHoldComboButton");
            V& initialData = g_lastInputWasGamepad.load(std::memory_order_acquire) ?
                                 a_gamepadButtonData : a_mkbButtonData;
            V factoryArgs[3]{ buttonType, initialData, a_buttonBar };
            if (!factory.Invoke("AddToButtonBar", &a_button, factoryArgs, 3) ||
                !(a_button.IsObject() || a_button.IsDisplayObject())) {
                REX::WARN("[map] stacked action hint unavailable: stock ButtonFactory rejected ReleaseHoldComboButton");
                return false;
            }

            return true;
        }

        bool BuildCruiseTapButton(RE::Scaleform::GFx::ASMovieRootBase* a_root,
            V& a_buttonBar, V& a_vanillaData, V& a_button,
            V& a_mkbButtonData, V& a_gamepadButtonData)
        {
            V tapCallback;
            a_root->CreateFunction(&tapCallback, &g_mapTapActionHandler);
            // ShipReticle swaps distinct data objects on input-device changes:
            // Cruise for MKB and SHMonocle for gamepad. The Starmap mirrors
            // that stock pattern so ButtonKeyHelper can resolve a real glyph.
            const auto buildData = [&](const char* a_userEvent, V& a_buttonData) {
                V eventName;
                a_root->CreateString(&eventName, a_userEvent);
                V eventArgs[2]{ eventName, tapCallback };
                V tapEvent;
                a_root->CreateObject(&tapEvent,
                    "Shared.Components.ButtonControls.ButtonData.UserEventData", eventArgs, 2);
                if (!(tapEvent.IsObject() || tapEvent.IsDisplayObject())) {
                    REX::WARN("[map] tap-only '{}' action hint unavailable: UserEventData construction failed",
                        a_userEvent);
                    return false;
                }

                V events;
                a_root->CreateArray(&events);
                if (!events.IsArray() || !events.PushBack(tapEvent)) {
                    REX::WARN("[map] tap-only '{}' action hint unavailable: event array construction failed",
                        a_userEvent);
                    return false;
                }

                V label;
                a_root->CreateString(&label, kCruiseMapActionLabel);
                V dataArgs[2]{ label, events };
                a_root->CreateObject(&a_buttonData,
                    "Shared.Components.ButtonControls.ButtonData.ButtonBaseData",
                    dataArgs, 2);
                if (!(a_buttonData.IsObject() || a_buttonData.IsDisplayObject())) {
                    REX::WARN("[map] tap-only '{}' action hint unavailable: ButtonBaseData construction failed",
                        a_userEvent);
                    return false;
                }

                for (const char* member : { "bEnabled", "bVisible" }) {
                    V value;
                    if (a_vanillaData.GetMember(member, &value))
                        a_buttonData.SetMember(member, value);
                }
                return true;
            };
            if (!buildData(kCruiseMapUserEvent, a_mkbButtonData) ||
                !buildData(kCruiseMapGamepadUserEvent, a_gamepadButtonData))
                return false;

            V factory;
            if (!a_root->GetVariable(&factory,
                    "Shared.Components.ButtonControls.ButtonFactory.ButtonFactory") ||
                !(factory.IsObject() || factory.IsDisplayObject())) {
                REX::WARN("[map] tap-only action hint unavailable: stock ButtonFactory is inaccessible");
                return false;
            }
            V buttonType;
            a_root->CreateString(&buttonType, "BasicButton");
            V& initialData = g_lastInputWasGamepad.load(std::memory_order_acquire) ?
                                 a_gamepadButtonData : a_mkbButtonData;
            V factoryArgs[3]{ buttonType, initialData, a_buttonBar };
            if (!factory.Invoke("AddToButtonBar", &a_button, factoryArgs, 3) ||
                !(a_button.IsObject() || a_button.IsDisplayObject())) {
                REX::WARN("[map] tap-only action hint unavailable: stock ButtonFactory rejected BasicButton");
                return false;
            }

            return true;
        }

        void SyncCruiseMapButtons(V& a_vanillaData, V& a_buttonBar,
            const MapEligibility& a_eligibility, bool a_tapOnly)
        {
            bool enabled = a_eligibility.enabled;
            V vanillaEnabled;
            if (enabled && a_vanillaData.GetMember("bEnabled", &vanillaEnabled) &&
                vanillaEnabled.IsBoolean()) {
                enabled = vanillaEnabled.GetBoolean();
            }

            const bool comboVisible = a_eligibility.show && !a_tapOnly &&
                                      g_mapActionHint.comboReady;
            const bool tapVisible = a_eligibility.show && a_tapOnly &&
                                    g_mapActionHint.tapReady;
            const bool useGamepad = g_lastInputWasGamepad.load(std::memory_order_acquire);
            bool labelSet = true;
            bool holdLabelSet = true;
            bool comboDataSet = true;
            bool tapDataSet = true;

            if (g_mapActionHint.comboReady) {
                for (V* data : { &g_mapActionHint.comboMkbButtonData,
                         &g_mapActionHint.comboGamepadButtonData }) {
                    data->SetMember("bEnabled", V{ enabled && comboVisible });
                    data->SetMember("bVisible", V{ comboVisible });
                    labelSet = data->SetMember("sButtonText",
                        V{ a_eligibility.label.c_str() }) && labelSet;
                    holdLabelSet = data->SetMember("sHoldText",
                        V{ enabled && comboVisible ? kCruiseMapActionHoldLabel : "" }) &&
                        holdLabelSet;
                }
                V& activeData = useGamepad ? g_mapActionHint.comboGamepadButtonData :
                                             g_mapActionHint.comboMkbButtonData;
                comboDataSet = g_mapActionHint.comboButton.Invoke(
                    "SetButtonData", nullptr, &activeData, 1);
                g_mapActionHint.comboButton.Invoke("RefreshButtonData");
            }
            if (g_mapActionHint.tapReady) {
                for (V* data : { &g_mapActionHint.tapMkbButtonData,
                         &g_mapActionHint.tapGamepadButtonData }) {
                    data->SetMember("bEnabled", V{ enabled && tapVisible });
                    data->SetMember("bVisible", V{ tapVisible });
                    labelSet = data->SetMember("sButtonText",
                        V{ a_eligibility.label.c_str() }) && labelSet;
                }
                V& activeData = useGamepad ? g_mapActionHint.tapGamepadButtonData :
                                             g_mapActionHint.tapMkbButtonData;
                tapDataSet = g_mapActionHint.tapButton.Invoke(
                    "SetButtonData", nullptr, &activeData, 1);
                g_mapActionHint.tapButton.Invoke("RefreshButtonData");
            }
            if ((!labelSet || !holdLabelSet) && Settings::Verbose())
                REX::WARN("[map] action data rejected dynamic labels (tap={} hold={})",
                    labelSet, holdLabelSet);
            const bool desiredReady = a_tapOnly ? g_mapActionHint.tapReady :
                                                  g_mapActionHint.comboReady;
            const bool desiredDataSet = a_tapOnly ? tapDataSet : comboDataSet;
            if (desiredReady && desiredDataSet)
                g_mapHintUsesGamepad.store(useGamepad, std::memory_order_release);
            if (desiredReady && !desiredDataSet)
                REX::WARN("[map] action hint rejected {} input data",
                    useGamepad ? "controller" : "keyboard/mouse");
            g_mapActionHint.installed = a_eligibility.show && desiredReady && desiredDataSet;
            g_mapActionInteractive.store(enabled && desiredReady && desiredDataSet,
                std::memory_order_release);
            g_mapActionTapOnly.store(enabled && desiredReady && desiredDataSet && a_tapOnly,
                std::memory_order_release);
            a_buttonBar.Invoke("RefreshButtons");
        }

        bool InstallCruiseMapButton(RE::Scaleform::GFx::ASMovieRootBase* a_root,
            V& a_buttonBar, V& a_vanillaData,
            std::uint32_t a_generation, const MapEligibility& a_eligibility,
            bool a_tapOnly)
        {
            g_mapActionHint.generation = a_generation;
            if (a_tapOnly && !g_mapActionHint.tapReady) {
                V tapButton;
                V tapMkbButtonData;
                V tapGamepadButtonData;
                if (!BuildCruiseTapButton(a_root, a_buttonBar, a_vanillaData,
                        tapButton, tapMkbButtonData, tapGamepadButtonData)) {
                    SyncCruiseMapButtons(a_vanillaData, a_buttonBar, {}, true);
                    REX::WARN("[map] tap-only action hint installation failed; preserving vanilla route button");
                    return false;
                }
                g_mapActionHint.tapReady = true;
                g_mapActionHint.tapButton = std::move(tapButton);
                g_mapActionHint.tapMkbButtonData = std::move(tapMkbButtonData);
                g_mapActionHint.tapGamepadButtonData = std::move(tapGamepadButtonData);
            } else if (!a_tapOnly && !g_mapActionHint.comboReady) {
                V comboButton;
                V comboMkbButtonData;
                V comboGamepadButtonData;
                if (!BuildCruiseComboButton(a_root, a_buttonBar, a_vanillaData,
                        comboButton, comboMkbButtonData, comboGamepadButtonData)) {
                    SyncCruiseMapButtons(a_vanillaData, a_buttonBar, {}, false);
                    REX::WARN("[map] stacked action hint installation failed; preserving vanilla route button");
                    return false;
                }
                g_mapActionHint.comboReady = true;
                g_mapActionHint.comboButton = std::move(comboButton);
                g_mapActionHint.comboMkbButtonData = std::move(comboMkbButtonData);
                g_mapActionHint.comboGamepadButtonData = std::move(comboGamepadButtonData);
            }

            SyncCruiseMapButtons(a_vanillaData, a_buttonBar, a_eligibility, a_tapOnly);
            return true;
        }

        void HideCruiseMapButton(V& a_vanillaData, V& a_buttonBar)
        {
            SyncCruiseMapButtons(a_vanillaData, a_buttonBar, {}, false);
        }

        void UpdateMapActionHint()
        {
            MapSnapshot snapshot;
            {
                std::lock_guard lock{ g_mapMutex };
                snapshot = g_map;
            }
            auto eligibility = EvaluateMapSelection(snapshot);
            const bool remotePlanetary = eligibility.destination &&
                IsPlanetary(*eligibility.destination) &&
                eligibility.destination->galaxy.system != snapshot.capturedSystem;
            const bool tapOnly = remotePlanetary || snapshot.wasCruising ||
                                 !snapshot.cruiseEngageAvailable;

            RE::Scaleform::GFx::ASMovieRootBase* root = nullptr;
            V menuRoot;
            if (!GetLiveMapMenuRoot(snapshot, root, menuRoot))
                return;

            V hintBar;
            V buttonData;
            std::string setCourseDetail;
            const bool setCourseReady = GetVanillaSetCourseData(menuRoot,
                hintBar, buttonData, setCourseDetail);
            if (!(hintBar.IsObject() || hintBar.IsDisplayObject()) ||
                !(buttonData.IsObject() || buttonData.IsDisplayObject()))
                return;
            if (remotePlanetary && eligibility.enabled) {
                const auto treeSystemID = MapTreeSystemID(snapshot.treeBodyID);
                const bool systemRootReady = treeSystemID && eligibility.destination &&
                    *treeSystemID == eligibility.destination->galaxy.system;
                if (!systemRootReady) {
                    eligibility.code = EligibilityCode::kRemoteCourseUnavailable;
                    eligibility.enabled = false;
                    eligibility.label = "SYSTEM ROUTE IDENTITY UNAVAILABLE";
                    eligibility.detail = std::format("focused STDT root {:08X} resolves to {}, expected system {}",
                        snapshot.treeBodyID,
                        treeSystemID ? std::format("system {}", *treeSystemID) : "no live star",
                        eligibility.destination->galaxy.system);
                } else if (!setCourseReady) {
                    eligibility.code = EligibilityCode::kRemoteCourseUnavailable;
                    eligibility.enabled = false;
                    eligibility.label = "VANILLA SET COURSE UNAVAILABLE";
                    eligibility.detail = setCourseDetail;
                }
            }

            const auto signature = EligibilitySignature(snapshot, eligibility);
            const bool signatureChanged =
                g_mapActionHintSignature.load(std::memory_order_acquire) != signature;
            if (!signatureChanged)
                return;

            V buttonBar;
            if (!GetMapButtonBar(hintBar, buttonBar))
                return;

            if ((g_mapActionHint.comboReady || g_mapActionHint.tapReady) &&
                g_mapActionHint.generation != snapshot.generation)
            {
                g_mapActionHint = {};
                g_mapActionInteractive.store(false, std::memory_order_release);
                g_mapActionTapOnly.store(false, std::memory_order_release);
            }

            bool updated = false;
            if (eligibility.show) {
                const bool desiredReady = tapOnly ?
                                              g_mapActionHint.tapReady :
                                              g_mapActionHint.comboReady;
                if (!g_mapActionHint.installed || !desiredReady) {
                    updated = InstallCruiseMapButton(root, buttonBar, buttonData,
                        snapshot.generation, eligibility, tapOnly);
                } else {
                    SyncCruiseMapButtons(buttonData, buttonBar, eligibility,
                        tapOnly);
                    updated = true;
                }
            } else if (g_mapActionHint.installed) {
                HideCruiseMapButton(buttonData, buttonBar);
                updated = true;
            } else {
                updated = true;
            }
            if (!updated)
                return;

            g_mapActionHintSignature.store(signature, std::memory_order_release);
            if (Settings::Verbose() && signatureChanged)
                REX::INFO("[map] action hint -> {} {} '{}' ({}, session={} generation={})",
                    eligibility.show ? (eligibility.enabled ? "ENABLED" : "DISABLED") : "HIDDEN",
                    remotePlanetary ? "TAP/REMOTE" :
                        snapshot.wasCruising ? "TAP/ACTIVE" :
                        (snapshot.cruiseEngageAvailable ? "TAP/HOLD" : "TAP/UNAVAILABLE"),
                    eligibility.show ? eligibility.label : "",
                    eligibility.detail, snapshot.session, snapshot.generation);
        }

        class MapDataHandler : public RE::Scaleform::GFx::FunctionHandler
        {
        public:
            void Call(const Params& a_params) override
            {
                V data;
                if (!Payload(a_params, data))
                    return;
                const bool preserveRemoteRoot = RemoteRouteRequestActive();
                V value;
                {
                    std::lock_guard lock{ g_mapMutex };
                    if (data.GetMember("iCurrentMenuView", &value)) {
                        const auto view = static_cast<std::int32_t>(AsNumber(value));
                        if (view != g_map.view) {
                            // Ordinarily galaxy view invalidates the prior root.
                            // A guarded remote Back carries its already-proven
                            // STDT root through this transition so Set Course is
                            // not delayed by an optional republish. The later
                            // route-display identity gate still fails closed.
                            if (view == kGalaxyView && !preserveRemoteRoot) {
                                g_map.treeBodyID = 0;
                                g_map.treeBodyType = 0;
                            }
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
                g_mapUiDirty.store(true, std::memory_order_release);
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
                const auto systemID = MapTreeSystemID(bodyID);
                std::optional<std::uint32_t> expectedRemoteRoot;
                {
                    std::lock_guard lock{ g_remoteRouteMutex };
                    if (g_remoteRouteRequest.phase == RemoteRoutePhase::kAwaitGalaxy ||
                        g_remoteRouteRequest.phase == RemoteRoutePhase::kEstablishSelection)
                        expectedRemoteRoot = g_remoteRouteRequest.systemBodyID;
                }
                const bool acceptedRoot = systemID &&
                    (!expectedRemoteRoot || bodyID == *expectedRemoteRoot);
                bool changed = false;
                {
                    std::lock_guard lock{ g_mapMutex };
                    // Live evidence shows this provider identifies the system/star,
                    // not the highlighted body. Keep it diagnostic-only: clearing
                    // the marker/dossier join here made eligibility depend on
                    // asynchronous callback order. Zero/transient rows must not
                    // erase the last live star. A stale star remains fail-closed
                    // because the remote gate compares its indexed DNAM system ID with the
                    // selected PNDT system.
                    if (acceptedRoot && (g_map.treeBodyID != bodyID ||
                            g_map.treeBodyType != bodyType)) {
                        g_map.treeBodyID = bodyID;
                        g_map.treeBodyType = bodyType;
                        changed = true;
                    }
                }
                if (changed && Settings::Verbose())
                    REX::INFO("[map] tree root -> STDT {:08X}/{} system={}",
                        bodyID, bodyType, *systemID);
                else if (systemID && expectedRemoteRoot &&
                    bodyID != *expectedRemoteRoot && Settings::Verbose())
                    REX::INFO("[jump] ignored transient galaxy STDT root {:08X}/system {} while awaiting captured root {:08X}",
                        bodyID, *systemID, *expectedRemoteRoot);
                g_mapUiDirty.store(true, std::memory_order_release);
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
                {
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
                g_mapUiDirty.store(true, std::memory_order_release);
            }
        } g_markersHandler;

        class QuickSelectCollector : public V::ArrayVisitor
        {
        public:
            explicit QuickSelectCollector(std::int32_t a_cursor) : cursor(a_cursor) {}

            void Visit(std::uint32_t a_index, const V& a_value) override
            {
                ++count;
                if (cursor < 0 || static_cast<std::uint32_t>(cursor) != a_index)
                    return;
                V entry = a_value;
                cursorBodyID = UIntMember(entry, "uBodyID");
            }

            std::int32_t cursor{ -1 };
            std::uint32_t count{ 0 };
            std::uint32_t cursorBodyID{ 0 };
        };

        // Read-only mirror of vanilla's Quick Select state. It is the one native
        // statement of galaxy system selection that does not depend on the
        // physical cursor, so the remote driver can prove a selection without
        // touching it.
        class QuickSelectHandler : public RE::Scaleform::GFx::FunctionHandler
        {
        public:
            void Call(const Params& a_params) override
            {
                V data;
                if (!Payload(a_params, data))
                    return;

                if (!shapeLogged.exchange(true, std::memory_order_acq_rel))
                    REX::INFO("[ui] StarMapMenuQuickSelectData members: {}",
                        JoinMemberNames(data, 48));

                V entries;
                if (!data.GetMember("entryList", &entries))
                    data.GetMember("aEntryList", &entries);
                if (entries.IsObject() && !entries.IsArray()) {
                    V inner;
                    if (entries.GetMember("dataA", &inner) && inner.IsArray())
                        entries = inner;
                }

                std::int32_t cursor = -1;
                V cursorValue;
                if (data.GetMember("uCursorSelectionIndex", &cursorValue) &&
                    !cursorValue.IsUndefined())
                    cursor = static_cast<std::int32_t>(AsNumber(cursorValue));

                QuickSelectCollector visitor{ cursor };
                if (entries.IsArray())
                    entries.VisitElements(&visitor);

                {
                    std::lock_guard lock{ g_mapMutex };
                    g_map.quickSelectPublished = true;
                    g_map.quickSelectCount = visitor.count;
                    g_map.quickSelectCursorIndex = cursor;
                    g_map.quickSelectCursorBodyID = visitor.cursorBodyID;
                }
                if (Settings::Verbose())
                    REX::INFO("[ui] quick select cursor={} of {} entries bodyID={:08X}",
                        cursor, visitor.count, visitor.cursorBodyID);
                g_mapUiDirty.store(true, std::memory_order_release);
            }

        private:
            std::atomic<bool> shapeLogged{ false };
        } g_quickSelectHandler;

        class DossierHandler : public RE::Scaleform::GFx::FunctionHandler
        {
        public:
            void Call(const Params& a_params) override
            {
                V data;
                if (!Payload(a_params, data))
                    return;
                {
                    std::lock_guard lock{ g_mapMutex };
                    g_map.dossierBodyID = UIntMember(data, "uBodyID");
                    g_map.dossierBodyType = UIntMember(data, "iType");
                    g_map.dossierName = StringMember(data, "sBodyName");
                }
                g_mapUiDirty.store(true, std::memory_order_release);
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
                {
                    std::lock_guard lock{ g_hudRowsMutex };
                    g_hudRows = std::move(collector.rows);
                }
                g_hudLowDirty.store(true, std::memory_order_release);
                g_hudUiDirty.store(true, std::memory_order_release);
            }
        } g_lowHandler;

        void ProcessLowSnapshot()
        {
            if (!g_hudLowDirty.exchange(false, std::memory_order_acq_rel))
                return;

            std::vector<HudRow> rows;
            {
                std::lock_guard lock{ g_hudRowsMutex };
                rows = g_hudRows;
            }
            ResolveCurrentSystem(rows);

            std::uint32_t course = 0;
            for (const auto& row : rows)
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

        bool HudMovieSettled(std::uint32_t a_generation)
        {
            if (a_generation == 0 ||
                g_hudMovie.generation.load(std::memory_order_acquire) != a_generation)
                return false;
            const auto born = Clock::time_point{ Clock::duration{
                g_hudMovie.bornTicks.load(std::memory_order_acquire) } };
            return Clock::now() - born >= kHudMovieSettleTime;
        }

        bool AddSprite(RE::Scaleform::GFx::ASMovieRootBase* a_root, V& a_parent,
            V& a_out, const char* a_name, std::int32_t a_depth = 21000)
        {
            if (a_parent.CreateEmptyMovieClip(&a_out, a_name, a_depth))
                return true;
            a_root->CreateObject(&a_out, "flash.display.Sprite");
            if (!(a_out.IsObject() || a_out.IsDisplayObject()))
                return false;
            V added;
            return a_parent.Invoke("addChild", &added, &a_out, 1);
        }

        bool BorrowTextFormat(RE::Scaleform::GFx::ASMovieRootBase* a_root,
            const std::string& a_base, V& a_format)
        {
            const char* donors[]{
                ".Reticle_mc.ShipReticle_mc.LockOn_mc.LockText_tf",
                ".Reticle_mc.ShipReticle_mc.Distance_tf",
                ".DebugText_tf",
            };
            for (const auto* suffix : donors) {
                V donor;
                if (a_root->GetVariable(&donor, (a_base + suffix).c_str()) &&
                    donor.Invoke("getTextFormat", &a_format) && a_format.IsObject())
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

        void TryCreateTargetStatus(RE::Scaleform::GFx::ASMovieRootBase* a_root,
            const char* a_rootPath)
        {
            if (!Settings::ShowTargetStatus() ||
                g_targetStatusReady.load(std::memory_order_acquire) ||
                g_targetStatusFailed.load(std::memory_order_acquire) || !WorldSettled())
                return;
            if (g_targetStatusBuildInFlight.exchange(true, std::memory_order_acq_rel))
                return;
            struct Release
            {
                ~Release()
                {
                    g_targetStatusBuildInFlight.store(false, std::memory_order_release);
                }
            } release;

            const std::string base{ a_rootPath ? a_rootPath : "root" };
            V reticle;
            if (!a_root->GetVariable(&reticle, (base + ".Reticle_mc").c_str()))
                return;
            if (!AddSprite(a_root, reticle, g_targetStatus,
                    "CruiseFromStarmapTargetStatus", 21001)) {
                g_targetStatusFailed.store(true, std::memory_order_release);
                REX::WARN("[status] cockpit target-status construction failed");
                return;
            }

            a_root->CreateObject(&g_targetStatusLabel, "flash.text.TextField");
            if (!(g_targetStatusLabel.IsObject() || g_targetStatusLabel.IsDisplayObject())) {
                g_targetStatusFailed.store(true, std::memory_order_release);
                REX::WARN("[status] cockpit target-status text construction failed");
                return;
            }
            V added;
            g_targetStatus.Invoke("addChild", &added, &g_targetStatusLabel, 1);
            g_targetStatusLabel.SetMember("selectable", V{ false });
            g_targetStatusLabel.SetMember("mouseEnabled", V{ false });
            g_targetStatusLabel.SetMember("width", V{ 520.0 });
            g_targetStatusLabel.SetMember("height", V{ 30.0 });
            g_targetStatusLabel.SetMember("x", V{ -260.0 });
            g_targetStatusLabel.SetMember("y", V{ 185.0 });
            if (BorrowTextFormat(a_root, base, g_targetStatusFormat)) {
                g_targetStatusFormat.SetMember("size", V{ 18.0 });
                g_targetStatusFormat.SetMember("color", V{ kNavigationColor });
                g_targetStatusFormat.SetMember("align", V{ "center" });
                g_targetStatusLabel.SetMember("defaultTextFormat", g_targetStatusFormat);
            }
            g_targetStatus.SetMember("visible", V{ false });
            g_targetStatusReady.store(true, std::memory_order_release);
            REX::INFO("[status] cockpit Cruise target confirmation ready");
        }

        void UpdateTargetStatus(RE::Scaleform::GFx::ASMovieRootBase* a_root,
            const char* a_rootPath)
        {
            const auto destination = Destination();
            if (!destination || !Settings::ShowTargetStatus()) {
                if (g_targetStatusReady.load(std::memory_order_acquire))
                    g_targetStatus.SetMember("visible", V{ false });
                return;
            }

            TryCreateTargetStatus(a_root, a_rootPath);
            if (!g_targetStatusReady.load(std::memory_order_acquire))
                return;

            const auto state = g_state.load(std::memory_order_acquire);
            const bool locked = state == NavState::kAutopilotLocked;
            const bool awaiting = state == NavState::kAwaitingCruise;
            const auto text = std::format("{}: {}",
                locked ? "CRUISE LOCK" : (awaiting ? "LOCKING CRUISE TARGET" : "CRUISE TARGET"),
                destination->localizedName);
            const auto color = locked ? kCruiseColor : kNavigationColor;
            g_targetStatusLabel.SetMember("text", V{ text.c_str() });
            g_targetStatusLabel.SetMember("textColor", V{ color });
            if (g_targetStatusFormat.IsObject()) {
                g_targetStatusFormat.SetMember("color", V{ color });
                g_targetStatusLabel.Invoke("setTextFormat", nullptr,
                    &g_targetStatusFormat, 1);
            }
            g_targetStatus.SetMember("visible", V{ true });
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
                if (BorrowTextFormat(a_root, base, g_markerFormat)) {
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
                {
                    std::lock_guard lock{ g_hudBearingsMutex };
                    g_hudBearings = std::move(collector.rows);
                }
                // Passed GFx values die with this callback. The post-advance
                // pump consumes only copied C++ rows before entering AS3.
                g_hudUiDirty.store(true, std::memory_order_release);
            }
        } g_highHandler;

        void ReleaseStaleUiState()
        {
            const auto reset = g_uiResetMask.exchange(0, std::memory_order_acq_rel);
            if ((reset & kResetMapUi) != 0) {
                g_mapActionHint = {};
                g_mapActionInteractive.store(false, std::memory_order_release);
                g_mapActionTapOnly.store(false, std::memory_order_release);
                g_pendingMapAction.store(MapAction::kNone, std::memory_order_release);
            }
            if ((reset & kResetHudUi) != 0) {
                g_markerReady.store(false, std::memory_order_release);
                g_markerFailed.store(false, std::memory_order_release);
                g_marker = V{};
                g_navGlyph = V{};
                g_cruiseGlyph = V{};
                g_markerLabel = V{};
                g_markerFormat = V{};
                g_targetStatusReady.store(false, std::memory_order_release);
                g_targetStatusFailed.store(false, std::memory_order_release);
                g_targetStatus = V{};
                g_targetStatusLabel = V{};
                g_targetStatusFormat = V{};
                {
                    std::lock_guard lock{ g_hudBearingsMutex };
                    g_hudBearings.clear();
                }
            }
        }

        void ReconcileHudUi()
        {
            const bool dirty = g_hudUiDirty.load(std::memory_order_acquire);
            const bool mapOpen = g_mapOpen.load(std::memory_order_acquire);
            // A settled HUD may be sampled while the map is open so a short
            // stock Cruise cooldown can expire without a close/reopen.
            if ((!dirty && !mapOpen) || !WorldSettled() || !IsFlying())
                return;

            const auto ui = RE::UI::GetSingleton();
            const RE::BSFixedString hudName{ kHudMenu };
            if (!ui || !ui->IsMenuOpen(hudName))
                return;
            const auto menu = ui->GetMenu(hudName);
            if (!menu || !menu->uiMovie || !menu->uiMovie->asMovieRoot)
                return;

            auto* root = menu->uiMovie->asMovieRoot.get();
            const auto generation = g_hudMovie.generation.load(std::memory_order_acquire);
            if (!HudMovieSettled(generation))
                return;

            const char* rootPath = menu->GetRootPath();
            const std::string base{ rootPath ? rootPath : "root" };
            V reticle;
            if (!root->GetVariable(&reticle, (base + ".Reticle_mc").c_str()) ||
                !reticle.IsObject() ||
                g_hudMovie.generation.load(std::memory_order_acquire) != generation ||
                !menu->uiMovie || !menu->uiMovie->asMovieRoot ||
                menu->uiMovie->asMovieRoot.get() != root)
                return;

            g_hudUiDirty.store(false, std::memory_order_release);

            DriveHudCruiseInput(root, rootPath);
            UpdateTargetStatus(root, rootPath);

            std::vector<Bearing> bearings;
            {
                std::lock_guard lock{ g_hudBearingsMutex };
                bearings = g_hudBearings;
            }

            V cruise;
            const bool activeResolved = reticle.GetMember("CruiseModeHUDActive", &cruise) &&
                                        cruise.IsBoolean();
            const bool active = activeResolved ? cruise.GetBoolean() :
                g_cruiseActive.load(std::memory_order_acquire);
            const bool wasActive = g_cruiseActive.exchange(active, std::memory_order_acq_rel);

            // ShipReticle.UpdateCruiseButton enables the stock hold event only
            // when CanActivateCruiseMode is true and neither Monocle nor Cruise
            // mode is active. Mirror those public getters rather than guessing
            // at cooldown timing in native code.
            V canActivateValue;
            V monocleValue;
            const bool canActivateResolved = reticle.GetMember(
                "CanActivateCruiseMode", &canActivateValue) &&
                canActivateValue.IsBoolean();
            const bool monocleResolved = reticle.GetMember(
                "MonocleModeActive", &monocleValue) &&
                monocleValue.IsBoolean();
            const bool engageAvailable = activeResolved && canActivateResolved &&
                                         monocleResolved && canActivateValue.GetBoolean() &&
                                         !monocleValue.GetBoolean() && !active;
            const bool wasEngageAvailable = g_cruiseEngageAvailable.exchange(
                engageAvailable, std::memory_order_acq_rel);
            if (engageAvailable != wasEngageAvailable) {
                if (g_mapOpen.load(std::memory_order_acquire)) {
                    {
                        std::lock_guard lock{ g_mapMutex };
                        g_map.cruiseEngageAvailable = engageAvailable;
                    }
                    g_mapUiDirty.store(true, std::memory_order_release);
                }
                if (Settings::Verbose())
                    REX::INFO("[hud] stock Cruise engage availability -> {} (activeResolved={} resolved={} monocleResolved={} active={})",
                        engageAvailable, activeResolved, canActivateResolved,
                        monocleResolved, active);
            }

            if (active && !wasActive) {
                CancelOrReleaseHudCruiseInput("CruiseModeHUDActive confirmed");
                const auto state = g_state.load(std::memory_order_acquire);
                const auto destination = Destination();
                if (destination &&
                    (state == NavState::kAwaitingCruise || state == NavState::kMarked)) {
                    g_state.store(NavState::kAwaitingCruise, std::memory_order_release);
                    if (state == NavState::kMarked)
                        REX::INFO("[course] vanilla Cruise activation detected; locking marked destination {:08X}",
                            destination->formID);
                    QueueCourse(destination->formID, false);
                }
            }
            if (!active && wasActive &&
                g_state.load(std::memory_order_acquire) == NavState::kAutopilotLocked)
                g_state.store(NavState::kMarked, std::memory_order_release);

            UpdateMarker(root, rootPath, bearings);
            RunCourseRequest(root);
        }

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
            { &g_mapMovie, kMapMenu, "StarMapMenuQuickSelectData", &g_quickSelectHandler, 1u << 4 },
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
                        if (g_state.load(std::memory_order_acquire) != NavState::kPendingJump)
                            ClearDestination("loading transition");
                        else if (Settings::Verbose())
                            REX::INFO("[destination] preserving pending remote mark across LoadingMenu");
                    }
                    return RE::BSEventNotifyControl::kContinue;
                }
                if (std::strcmp(name, kMapMenu) != 0)
                    return RE::BSEventNotifyControl::kContinue;

                g_mapOpen.store(a_event.opening, std::memory_order_release);
                if (a_event.opening) {
                    g_mapUiDirty.store(true, std::memory_order_release);
                    const auto session = g_mapSession.fetch_add(1, std::memory_order_acq_rel) + 1;
                    const bool haveSystem = g_haveCurrentSystem.load(std::memory_order_acquire);
                    std::lock_guard lock{ g_mapMutex };
                    g_map = {};
                    g_map.session = session;
                    g_map.generation = g_mapMovie.generation.load(std::memory_order_acquire);
                    g_map.openedWhileFlying = IsFlying();
                    g_map.wasCruising = g_cruiseActive.load(std::memory_order_acquire);
                    g_map.cruiseEngageAvailable =
                        g_cruiseEngageAvailable.load(std::memory_order_acquire);
                    g_map.haveCapturedSystem = haveSystem;
                    g_map.capturedSystem = g_currentSystem.load(std::memory_order_acquire);
                    g_selectionAcceptedThisOpen.store(false, std::memory_order_release);
                    if (Settings::Verbose())
                        REX::INFO("[map] open session={} generation={} flying={} cruise={} cruiseAvailable={} currentSystem={}",
                            session, g_map.generation, g_map.openedWhileFlying, g_map.wasCruising,
                            g_map.cruiseEngageAvailable,
                            haveSystem ? std::format("{}", g_map.capturedSystem) : "unresolved");
                } else {
                    g_mapActionInteractive.store(false, std::memory_order_release);
                    g_mapActionTapOnly.store(false, std::memory_order_release);
                    bool accepted = g_selectionAcceptedThisOpen.exchange(false, std::memory_order_acq_rel);
                    bool wasCruising = false;
                    {
                        std::lock_guard lock{ g_mapMutex };
                        wasCruising = g_map.wasCruising;
                    }
                    if (accepted && RemoteRouteRequestActive()) {
                        ClearDestination("Starmap closed before vanilla route became executable");
                        accepted = false;
                        REX::WARN("[jump] Starmap closed during remote Set Course handoff; mark cleared");
                    }
                    if (accepted) {
                        const auto destination = Destination();
                        if (destination && (destination->kind == BodyKind::kStation ||
                                destination->kind == BodyKind::kShip) &&
                            !AssignNativeShipTarget(*destination)) {
                            ClearDestination("native station/ship target assignment failed");
                            accepted = false;
                        }
                    }
                    const auto acceptedDestination = accepted ? Destination() : std::nullopt;
                    const bool pendingJump = acceptedDestination &&
                        IsPlanetary(*acceptedDestination) &&
                        g_haveCurrentSystem.load(std::memory_order_acquire) &&
                        acceptedDestination->galaxy.system !=
                            g_currentSystem.load(std::memory_order_acquire);
                    bool held = false;
                    bool holdCompleted = false;
                    auto heldDevice = RE::InputEvent::DeviceType::kNone;
                    {
                        std::lock_guard lock{ g_holdMutex };
                        held = g_hold.active;
                        holdCompleted = g_hold.completed;
                        heldDevice = g_hold.device;
                        g_claimMapKey = false;
                        // A completed pre-Cruise fill or an already-cruising
                        // BasicButton press can close the map while the physical
                        // key is still down. Suppress that carried cockpit input
                        // until release. A remote completed hold is deferred to
                        // arrival rather than replayed in the origin system.
                        g_hold.suppressUntilRelease = accepted && held &&
                                                      (holdCompleted || wasCruising);
                    }
                    if (accepted) {
                        if (pendingJump) {
                            g_state.store(NavState::kPendingJump, std::memory_order_release);
                            REX::INFO("[destination] pending grav-jump arrival for {:08X} '{}' system={}",
                                acceptedDestination->formID,
                                acceptedDestination->localizedName,
                                acceptedDestination->galaxy.system);
                        } else if (wasCruising) {
                            if (const auto destination = Destination())
                                QueueCourse(destination->formID, false);
                            g_state.store(Destination() ? NavState::kAwaitingCruise : NavState::kIdle,
                                std::memory_order_release);
                        } else {
                            g_state.store(Destination() ? NavState::kMarked : NavState::kIdle,
                                std::memory_order_release);
                            if (holdCompleted && Destination()) {
                                if (QueueHudCruisePress(heldDevice)) {
                                    g_state.store(NavState::kAwaitingCruise, std::memory_order_release);
                                    REX::INFO("[input] completed Starmap hold queued latched stock HUD Cruise press");
                                } else {
                                    REX::WARN("[input] stock HUD Cruise press was already pending; destination remains marked");
                                }
                            }
                        }
                    }
                    if (Settings::Verbose())
                        REX::INFO("[map] close accepted={} holdCompleted={} physicalHeld={} state={}",
                            accepted, holdCompleted, held,
                            static_cast<std::uint32_t>(g_state.load(std::memory_order_acquire)));
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
                    if (wasForeground && !foreground) {
                        g_applicationForeground.store(false, std::memory_order_release);
                        ResetHold("application focus loss");
                    } else if (!wasForeground && foreground) {
                        bool routeRestarted = false;
                        {
                            std::lock_guard lock{ g_remoteRouteMutex };
                            if (g_remoteRouteRequest.phase != RemoteRoutePhase::kNone) {
                                g_remoteRouteRequest.started = Clock::now();
                                g_remoteRouteRequest.executeReadySince = {};
                                routeRestarted = true;
                            }
                        }
                        g_applicationForeground.store(true, std::memory_order_release);
                        if (routeRestarted)
                            REX::INFO("[jump] active remote route timeout restarted after application focus returned");
                    }
                    wasForeground = foreground;
                }
            } }.detach();
        }

        void ReconcilePendingJump(const BodyDestination& a_destination)
        {
            if (!IsPlanetary(a_destination)) {
                ClearDestination("invalid non-planet pending-jump state");
                return;
            }

            const auto player = RE::PlayerCharacter::GetSingleton();
            const auto ship = player ? player->GetSpaceship() : nullptr;
            if (!IsShipInSpace(ship)) {
                // Do not sample transient travel/load state. Once the world is
                // settled, leaving space is a real cancellation boundary.
                if (WorldSettled())
                    ClearDestination("landing, docking, or leaving the pilot seat during pending jump");
                return;
            }
            if (!g_haveCurrentSystem.load(std::memory_order_acquire) ||
                g_currentSystem.load(std::memory_order_acquire) != a_destination.galaxy.system ||
                g_mapOpen.load(std::memory_order_acquire) || !WorldSettled())
                return;

            // Resolver agreement alone is not enough: require the marked body
            // to be present exactly once in the settled cockpit target feed.
            // This also keeps remote station/POI identities out of this path.
            if (CurrentHudTargets(a_destination.formID).size() != 1)
                return;

            if (g_cruiseActive.load(std::memory_order_acquire)) {
                auto expected = NavState::kPendingJump;
                if (g_state.compare_exchange_strong(expected, NavState::kAwaitingCruise,
                        std::memory_order_acq_rel)) {
                    QueueCourse(a_destination.formID, false);
                    REX::INFO("[destination] pending jump arrived in system {}; Cruise already active, queued course {:08X}",
                        a_destination.galaxy.system, a_destination.formID);
                }
                return;
            }
            if (!g_cruiseEngageAvailable.load(std::memory_order_acquire))
                return;

            auto expected = NavState::kPendingJump;
            if (!g_state.compare_exchange_strong(expected, NavState::kAwaitingCruise,
                    std::memory_order_acq_rel))
                return;
            auto device = g_pendingJumpDevice.load(std::memory_order_acquire);
            if (device == RE::InputEvent::DeviceType::kNone)
                device = RE::InputEvent::DeviceType::kKeyboard;
            if (!QueueHudCruisePress(device)) {
                g_state.store(NavState::kPendingJump, std::memory_order_release);
                return;
            }
            REX::INFO("[destination] pending jump arrived in system {}; queued latched stock HUD Cruise press for {:08X} '{}'",
                a_destination.galaxy.system, a_destination.formID,
                a_destination.localizedName);
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

            bool hudHoldExpired = false;
            {
                std::lock_guard lock{ g_hudCruiseInputMutex };
                hudHoldExpired = g_hudCruiseInputLatched &&
                    g_hudCruiseInputStarted != Clock::time_point{} &&
                    Clock::now() - g_hudCruiseInputStarted > std::chrono::seconds(4);
            }
            if (hudHoldExpired) {
                CancelOrReleaseHudCruiseInput("four-second activation safety limit");
                REX::WARN("[input] latched HUD Cruise hold did not make CruiseModeHUDActive within 4 seconds; released automatically and preserved destination");
                g_state.store(Destination() ? NavState::kMarked : NavState::kIdle,
                    std::memory_order_release);
            }
        }

        void RunMainThreadFrame()
        {
            if (g_loadClearPending.exchange(false, std::memory_order_acq_rel)) {
                ResetHold("save load");
                ClearDestination("save load");
                return;
            }

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
                const auto state = g_state.load(std::memory_order_acquire);
                const bool remoteMapSelection =
                    (state == NavState::kMapSelection || RemoteRouteRequestActive()) &&
                    IsPlanetary(*destination) &&
                    g_haveCurrentSystem.load(std::memory_order_acquire) &&
                    g_currentSystem.load(std::memory_order_acquire) !=
                        destination->galaxy.system;
                if (state == NavState::kPendingJump) {
                    ReconcilePendingJump(*destination);
                } else if (!remoteMapSelection) {
                    const auto player = RE::PlayerCharacter::GetSingleton();
                    const auto ship = player ? player->GetSpaceship() : nullptr;
                    if (!IsShipInSpace(ship)) {
                        ClearDestination("landing, docking, or leaving the pilot seat");
                    } else if (g_haveCurrentSystem.load(std::memory_order_acquire) &&
                        g_currentSystem.load(std::memory_order_acquire) != destination->galaxy.system) {
                        ClearDestination("system change");
                    }
                }
            }

            CheckArrival();
            CheckCourseTimeout();
        }

        class MainFrameTask final : public RE::BSService::QueuedDelegate
        {
        public:
            void Run() override
            {
                struct Release
                {
                    ~Release()
                    {
                        g_mainFramePending.store(false, std::memory_order_release);
                    }
                } release;

                try {
                    RunMainThreadFrame();
                } catch (const std::exception& e) {
                    REX::ERROR("main-thread frame threw '{}'; frame dropped", e.what());
                } catch (...) {
                    REX::ERROR("main-thread frame threw an unknown exception; frame dropped");
                }
            }
        };

        bool QueueMainThreadFrame()
        {
            auto* queue = RE::BSService::TaskQueue::GetSingleton();
            if (!queue)
                return false;

            RE::BSService::QueuedDelegate* task = new MainFrameTask();
            queue->QueueTask(task);
            if (task) {
                // Queueing is disabled or tried to use an inline fallback.
                // Never run engine work on the SFSE worker; retry next frame.
                delete task;
                return false;
            }
            return true;
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
            g_mapActionHintSignature.store(0, std::memory_order_release);
            g_mapActionInteractive.store(false, std::memory_order_release);
            g_mapActionTapOnly.store(false, std::memory_order_release);
            g_mapUiDirty.store(true, std::memory_order_release);
            g_uiResetMask.fetch_or(kResetMapUi, std::memory_order_acq_rel);
        } else {
            ResetHold("Spaceship HUD movie replacement");
            {
                std::lock_guard lock{ g_hudCruiseInputMutex };
                g_hudCruiseInputPhase = HudCruiseInputPhase::kIdle;
                g_hudCruiseUserEvent = "Cruise";
                g_hudCruiseInputLatched = false;
                g_hudCruiseInputStarted = {};
            }
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
            g_hudLowDirty.store(false, std::memory_order_release);
            g_markerReady.store(false, std::memory_order_release);
            g_markerFailed.store(false, std::memory_order_release);
            g_targetStatusReady.store(false, std::memory_order_release);
            g_targetStatusFailed.store(false, std::memory_order_release);
            g_cruiseActive.store(false, std::memory_order_release);
            g_cruiseEngageAvailable.store(false, std::memory_order_release);
            g_hudUiDirty.store(true, std::memory_order_release);
            g_uiResetMask.fetch_or(kResetHudUi, std::memory_order_acq_rel);
        }
        REX::INFO("[ui] movie created: {} generation={}", name,
            state->generation.load(std::memory_order_acquire));
    }

    void OnFrame()
    {
        // SFSE permanent tasks run on rotating render-graph workers. This is a
        // coalesced producer only; engine work drains through BSService on the
        // game main thread.
        if (g_mainFramePending.exchange(true, std::memory_order_acq_rel))
            return;
        if (!QueueMainThreadFrame())
            g_mainFramePending.store(false, std::memory_order_release);
    }

    void OnUiSafeFrame()
    {
        static std::atomic<bool> faulted{ false };
        static auto nextMapRoutePoll = Clock::time_point{};
        if (faulted.load(std::memory_order_acquire))
            return;

        try {
            ReleaseStaleUiState();
            TrySubscribe();
            ProcessLowSnapshot();

            const auto action = g_pendingMapAction.exchange(
                MapAction::kNone, std::memory_order_acq_rel);
            if (action != MapAction::kNone)
                AcceptSelection(action);

            DriveRemoteRouteRequest();

            const auto now = Clock::now();
            if (g_mapOpen.load(std::memory_order_acquire) &&
                now >= nextMapRoutePoll) {
                // Plot-route state is native-pushed directly into JumpData_mc
                // and is not guaranteed to publish one of our subscribed feeds.
                // Poll only in the verified post-advance window; signatures
                // keep unchanged button state mutation-free.
                nextMapRoutePoll = now + kMapRoutePollTime;
                g_mapUiDirty.store(true, std::memory_order_release);
            }

            if (g_mapUiDirty.load(std::memory_order_acquire)) {
                const auto ui = RE::UI::GetSingleton();
                const RE::BSFixedString mapName{ kMapMenu };
                if (ui && ui->IsMenuOpen(mapName)) {
                    g_mapUiDirty.store(false, std::memory_order_release);
                    UpdateMapActionHint();
                }
            }
            ReconcileHudUi();
        } catch (const std::exception& e) {
            faulted.store(true, std::memory_order_release);
            REX::ERROR("post-advance UI pump threw '{}'; disabling further Scaleform work", e.what());
        } catch (...) {
            faulted.store(true, std::memory_order_release);
            REX::ERROR("post-advance UI pump threw an unknown exception; disabling further Scaleform work");
        }
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

        if (!ValidateIsInSpaceBinding() || !ValidateShipTargetBinding())
            return;
        TryInstallLoadGameSink();
        TryInstallGravJumpSink();
        if (!MainThreadUiPump::Install()) {
            REX::ERROR("post-advance UI pump unavailable; bridge disabled to prevent off-thread Scaleform access");
            return;
        }

        ResolveCruiseMapBinding();
        BodyIndex::StartLoad();
        menus->Register(&OnMovieCreated);
        ui->RegisterSink<RE::MenuOpenCloseEvent>(&g_menuSink);
        TryInstallInputHook();
        StartFocusWatcher();
        g_lastUnsettledTicks.store(Clock::now().time_since_epoch().count(), std::memory_order_release);
        tasks->AddPermanentTask(&OnFrame);
        REX::INFO("bridge initialized: current-system Cruise plus guarded remote stock Back/system-SetCourse/ExecuteRoute handoff, no serialization or public API");
    }
}

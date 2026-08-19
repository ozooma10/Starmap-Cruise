#include "RemoteSystemNativeProbe.h"

#include "Scaleform/ValueAccess.h"

#include "REL/Trampoline.h"
#include "REX/REX.h"

#include <Windows.h>
#undef ERROR
#undef max
#undef min

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace CFS::RemoteSystemNativeProbe
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        constexpr const char* MapMenuName = "GalaxyStarMapMenu";
        constexpr const char* HudMenuName = "SpaceshipHudMenu";
        constexpr const char* LoadingMenuName = "LoadingMenu";
        constexpr const char* SetRouteDestinationEvent =
            "SetRouteDestination";

        constexpr FormID IsStarstationKeywordId = 0x003402A3;
        constexpr FormID MissingFormId =
            std::numeric_limits<FormID>::max();

        constexpr REL::ID ResolveCurrentBodyId {97914};
        constexpr REL::ID ResolveBodySystemFormId {124608};
        constexpr REL::ID ResolveBodyNumericSystemId {124767};
        constexpr REL::ID ResolveBodyNumericSystemInnerId {124766};
        constexpr REL::ID ResolveBodyBySystemOrdinalId {124772};
        constexpr REL::ID ResolvePlanetFromRefId {52188};
        constexpr REL::ID LookupSatelliteRowId {124799};
        constexpr REL::ID LookupComponentTreeId {126806};
        constexpr REL::ID CellDataTagId {938337};
        constexpr REL::ID LookupFormByIdId {47401};
        constexpr REL::ID AllFormsMapAddressId {883341};

        constexpr REL::ID GravJumpGetEventSourceId {93876};
        constexpr REL::ID GravJumpEventSourceVtableId {445846};
        constexpr REL::ID LoadGameGetEventSourceId {64149};
        constexpr REL::ID LoadGameEventSourceAddressId {838425};
        constexpr REL::ID LoadGameEventSourceVtableId {413741};
        constexpr REL::ID ExecuteRouteGetEventSourceId {94774};
        constexpr REL::ID ExecuteRouteEventSourceAddressId {948974};
        constexpr REL::ID ExecuteRouteEventSourceVtableId {446781};

        constexpr REL::ID StarMapMenuPrimaryVtableId {446845};
        constexpr REL::ID GalaxyStatePrimaryVtableId {446425};
        constexpr REL::ID SystemStatePrimaryVtableId {447180};
        constexpr REL::ID BodyInspectStatePrimaryVtableId {446055};
        constexpr REL::ID SurfaceMapStatePrimaryVtableId {447074};

        constexpr std::size_t NumericOuterInnerCallOffset = 0x6F;
        constexpr std::size_t LookupFormAllFormsInstructionOffset = 0x0B;
        constexpr std::size_t LookupFormAllFormsDisplacementOffset = 0x03;
        constexpr std::size_t LookupFormAllFormsInstructionSize = 0x07;
        constexpr std::size_t StarMapMenuActiveStateOffset = 0x1240;
        constexpr std::size_t StarMapMenuRouteCountOffset = 0x1280;
        constexpr std::size_t StarMapMenuRouteDataOffset = 0x1288;
        constexpr std::size_t StarMapMenuAlternateEndpointOffset = 0x1294;
        constexpr std::size_t StarMapMenuAlternateModeOffset = 0x12B8;
        constexpr std::size_t GalaxyStateSelectedSystemOffset = 0x880;
        constexpr std::size_t GalaxyStateQuickSelectOpenOffset = 0x8F8;
        constexpr std::size_t SystemStateDisplayedSystemOffset = 0xA10;
        constexpr std::size_t SystemStateDisplayRootOffset = 0xA18;
        constexpr std::size_t SystemStateSelectedBodyOffset = 0xA1C;
        constexpr std::size_t BodyInspectStateSelectedBodyOffset = 0xC90;
        constexpr std::size_t RoutePointStride = 0x28;
        constexpr std::size_t RoutePointEndpointOffset = 0x04;

        constexpr std::size_t MaxText = 96;
        constexpr std::size_t MaxInbox = 128;
        constexpr std::size_t MaxHudRows = 32;
        constexpr std::size_t MaxRoutePoints = 64;
        constexpr std::size_t MaxStationCellReferences = 4096;
        constexpr std::size_t MaxPndtForms = 8192;
        constexpr std::uint64_t MinAllFormsCapacity = 0x40000;
        constexpr std::uint64_t MaxAllFormsCapacity = 0x400000;
        constexpr std::size_t AllFormsTableOffset = 0x10;
        constexpr std::size_t AllFormsCapacityOffset = 0x18;
        constexpr std::size_t AllFormsFreeOffset = 0x20;
        constexpr std::size_t AllFormsLastFreeOffset = 0x28;
        constexpr FormID WrongOrdinalControl = 0x7FFFFFFE;
        constexpr std::size_t PndtFormsPerDrain = 32;
        constexpr std::size_t MaxStationBaselines = 16;
        constexpr std::size_t MaxAncestryDepth = 8;
        constexpr std::size_t MaxLoggedCandidates = 8;
        constexpr std::size_t MaxPndtFailureSamples = 16;
        constexpr auto CurrentPairPollInterval =
            std::chrono::milliseconds(250);
        constexpr auto ExecuteReadyDwell =
            std::chrono::milliseconds(500);
        constexpr auto ExecuteCloseTimeout =
            std::chrono::seconds(2);
        constexpr auto WorldSettleTime =
            std::chrono::milliseconds(2500);

        constexpr std::array<std::uint8_t, 16>
            GlobalEventGetterPrologue {
                0x48, 0x83, 0xEC, 0x28, 0x65, 0x48, 0x8B, 0x04,
                0x25, 0x58, 0x00, 0x00, 0x00, 0xBA, 0xB8, 0x00,
            };
        constexpr std::array<std::uint8_t, 16> CurrentBodyPrologue {
            0x40, 0x53, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x8B,
            0xD9, 0x48, 0x8B, 0x0D, 0x10, 0x0B, 0x76, 0x04,
        };
        constexpr std::array<std::uint8_t, 24> AcquiringWrapperPrologue {
            0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C,
            0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57,
            0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x30,
        };
        constexpr std::array<std::uint8_t, 16> NumericInnerPrologue {
            0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74,
            0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x48,
        };
        constexpr std::array<std::uint8_t, 5> NumericInnerCallBytes {
            0xE8, 0xDC, 0xFE, 0xFF, 0xFF,
        };
        constexpr std::array<std::uint8_t, 24> SatelliteLookupPrologue {
            0x48, 0x83, 0xEC, 0x48, 0x44, 0x0F, 0xB7, 0x05,
            0xF4, 0x86, 0xEA, 0x03, 0x48, 0x8B, 0x09, 0x49,
            0xC1, 0xE0, 0x20, 0x48, 0x81, 0xC1, 0x68, 0x02,
        };
        constexpr std::array<std::uint8_t, 24> ComponentTreeLookupPrologue {
            0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
            0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x33,
            0xF6, 0x48, 0xC7, 0x42, 0x08, 0xFC, 0x00, 0x00,
        };
        constexpr std::array<std::uint8_t, 16>
            ResolveBodyBySystemOrdinalPrologue {
                0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C,
                0x24, 0x18, 0x56, 0x57, 0x41, 0x55, 0x41, 0x56,
            };
        constexpr std::array<std::uint8_t, 16>
            ResolvePlanetFromRefPrologue {
                0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x6C,
                0x24, 0x20, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56,
            };
        constexpr std::array<std::uint8_t, 24> LookupFormByIdPrologue {
            0x89, 0x4C, 0x24, 0x08, 0x53, 0x48, 0x83, 0xEC,
            0x20, 0x33, 0xDB, 0x48, 0x39, 0x1D, 0xDE, 0x2A,
            0x88, 0x05, 0x74, 0x7B, 0x85, 0xC9, 0x74, 0x77,
        };

        enum class ObservationKind : std::uint8_t
        {
            Movie,
            Menu,
            MapData,
            Markers,
            Dossier,
            ResolvedMarker,
            Hud,
            HudMovie,
            Input,
            ExecuteRoute,
            GravJump,
            LoadGame,
            Component,
        };

        struct TargetCopy
        {
            FormID id {0};
            FormID resolvedTargetId {0};
            FormID resolvedCourseId {0};
            FormID displayedSystemFormId {0};
            std::uint8_t kind {0};
            bool hasDisplayedSystemFormId {false};
            std::array<char, MaxText> name {};
        };

        struct HudRowCopy
        {
            FormID courseId {0};
            bool locked {false};
        };

        enum class CellDataCaptureState : std::uint8_t
        {
            Absent,
            Present,
            Invalid,
        };

        struct ComponentCopy
        {
            FormID bodyId {0};
            FormID numericSystemId {0};
            FormID parentOrdinal {0};
            FormID planetOrdinal {0};
            bool satellitePresent {false};
            CellDataCaptureState cellDataState {
                CellDataCaptureState::Invalid};
            bool cellDataFlag {false};
            bool cellTextTruncated {false};
            std::array<char, MaxText> cellEditorId {};

            friend bool operator==(const ComponentCopy&,
                const ComponentCopy&) = default;
        };

        struct Observation
        {
            ObservationKind kind {ObservationKind::Movie};
            std::uint64_t sequence {0};
            MapSessionIdentity identity;
            TargetCopy target;
            ComponentCopy component;
            std::array<HudRowCopy, MaxHudRows> hudRows {};
            std::array<char, MaxText> text {};
            std::int64_t ticks {0};
            FormID id1 {0};
            FormID id2 {0};
            std::uint32_t generation {0};
            std::uint32_t count {0};
            std::uint32_t value1 {0};
            std::int32_t value2 {0};
            float float1 {0.0F};
            float float2 {0.0F};
            std::uint8_t small1 {0};
            bool flag1 {false};
            bool flag2 {false};
            bool overflowed {false};
        };
        static_assert(std::is_trivially_copyable_v<Observation>);

        struct Inbox
        {
            std::mutex mutex;
            std::array<Observation, MaxInbox> entries {};
            std::size_t count {0};
            bool overflowed {false};
        } g_inbox;

        struct InboxBatch
        {
            std::array<Observation, MaxInbox> entries {};
            std::size_t count {0};
            bool overflowed {false};
        } g_drainBatch;

        struct SystemPair
        {
            FormID bodyId {0};
            FormID starFormId {0};
            FormID numericId {0};
            FormID satelliteNumericId {0};
            bool valid {false};

            friend bool operator==(const SystemPair&, const SystemPair&) =
                default;
        };

        struct SystemIdentity
        {
            FormID starFormId {0};
            FormID numericId {0};
            bool valid {false};

            friend bool operator==(const SystemIdentity&,
                const SystemIdentity&) = default;
        };

        enum class NativeMapView : std::uint8_t
        {
            None,
            Galaxy,
            System,
            BodyInspect,
            Surface,
            Unknown,
        };

        struct NativeMapState
        {
            NativeMapView view {NativeMapView::None};
            FormID displayedSystem {0};
            FormID displayRoot {0};
            FormID selectedIdentity {0};
            FormID routeEndpoint {0};
            std::uint32_t routePointCount {0};
            bool menuOpen {false};
            bool layoutValid {false};
            bool routeReadable {false};
            bool routeAlternate {false};
            bool routeEndpointIsStdt {false};
            bool quickSelectOpen {false};
            bool stable {false};

            friend bool operator==(const NativeMapState&,
                const NativeMapState&) = default;
        };

        struct ProbeState
        {
            MapSessionIdentity selectedIdentity;
            TargetCopy selectedTarget;
            bool hasSelectedTarget {false};
            bool selectedFromResolvedMarker {false};

            std::optional<SystemPair> lastSystemPair;
            Clock::time_point nextSystemPairPoll {};
            bool forceSystemPairPoll {true};
            bool currentSystemSampled {false};

            std::optional<NativeMapState> lastNativeMap;
            MapSessionIdentity observedMapIdentity;
            std::uint32_t observedMapMovieGeneration {0};
            std::uint64_t lastSetRouteSequence {0};
            MapSessionIdentity pendingSetRouteIdentity;
            SystemIdentity pendingSetRouteExpected;
            FormID pendingPreexistingEndpoint {0};
            bool pendingSetRoute {false};
            bool pendingRouteSampleNeeded {false};
            FormID provenRouteEndpoint {0};
            SystemIdentity provenRouteEndpointSystem;
            bool routeEndpointProven {false};
            Clock::time_point executeReadySince {};
            bool executeReadyDwellProven {false};
            bool focusSuspended {false};
            Clock::time_point focusLostAt {};
            std::uint64_t executeSequence {0};
            Clock::time_point executeObservedAt {};
            bool awaitingExecuteClose {false};
            bool mapOpen {false};
            bool loadingOpen {false};
            Clock::time_point lastUnsettledAt {};
            SystemIdentity committedDestination;
            std::uint64_t routeCommitSequence {0};
            bool pendingArrival {false};
            bool completedJumpAfterCommit {false};
            bool arrivalLogged {false};
            std::uint64_t lastCurrentPollTravelSequence {0};
            FormID lastNativeTarget {0};
            std::uint64_t lastTravelSequence {0};
            std::uint64_t completedTravelSequence {0};
            std::uint64_t lastHudSequence {0};
            std::uint8_t gravJumpProgress {0};
            std::uint32_t hudGeneration {0};
            std::array<HudRowCopy, MaxHudRows> lastHudRows {};
            std::size_t lastHudRowCount {0};
            bool lastHudOverflowed {false};
            bool hasHudSnapshot {false};

        } g_state;

        struct StationTuple
        {
            FormID mapId {0};
            FormID cellId {0};
            FormID targetRefId {0};
            FormID baseId {0};
            FormID courseMarkerId {0};
            FormID displayedSystemFormId {0};
            FormID physicalOrbitalPndtId {0};
            FormID physicalSecondaryId {0};
            FormID courseOrbitalPndtId {0};
            FormID courseSecondaryId {0};
            FormID nativeOrbitalPndtId {0};
            FormID nativeSecondaryId {0};
            std::uint32_t referenceCount {0};
            std::uint32_t stationCount {0};
            std::uint32_t markerCount {0};
            bool valid {false};
            bool physicalOrbitalResolved {false};
            bool courseOrbitalResolved {false};
            bool nativeOrbitalResolved {false};
            bool nativeOrbitalFromCourseMarker {false};
            bool truncatedEditorId {false};
            std::array<char, MaxText> cellEditorId {};

            friend bool operator==(const StationTuple&,
                const StationTuple&) = default;
        };

        struct BodyFacts
        {
            FormID starFormId {0};
            ComponentCopy component;
        };

        enum class PndtFailureReason : std::uint8_t
        {
            PreCaptureStale,
            SystemResolverFailure,
            InvalidSystemForm,
            SystemDrift,
            TlsCaptureFailure,
            NumericWrapperFailure,
            CellDataInvalid,
            SatelliteMissing,
            WrapperSatelliteDisagreement,
            SelectedNumericDisagreement,
            RetainedCapacity,
            StationCtAmbiguity,
            StationCtWithoutRetainedBody,
            StationCtOutsideSelectedSystem,
            StationNumericDisagreement,
            StationNativeOrbitalMissing,
            StationNativeReverseDisagreement,
            SelectedTargetMissing,
            SnapshotDrift,
            CounterMismatch,
        };

        struct PndtFailureSample
        {
            PndtFailureReason reason {PndtFailureReason::PreCaptureStale};
            FormID bodyId {0};
            FormID starFormId {0};
            FormID value1 {0};
            FormID value2 {0};
        };

        struct CellDataTextSample
        {
            FormID bodyId {0};
            FormID numericWrapperId {MissingFormId};
            FormID numericSystemId {0};
            FormID parentOrdinal {0};
            FormID planetOrdinal {0};
            bool numericRead {false};
            bool satellitePresent {false};
            std::array<char, MaxText> editorId {};
        };

        struct PndtScanCounters
        {
            std::size_t processed {0};
            std::size_t classificationFailed {0};
            std::size_t unrooted {0};
            std::size_t unrelated {0};
            std::size_t selectedSystem {0};
            std::size_t selectedRejected {0};
            std::size_t selectedCandidates {0};
            std::size_t tlsCaptured {0};
            std::size_t satellitePresent {0};
            std::size_t satelliteAbsent {0};
            std::size_t cellDataAbsent {0};
            std::size_t cellDataPresent {0};
            std::size_t cellDataInvalid {0};
            std::size_t cellDataEmpty {0};
            std::size_t cellDataTextRows {0};
            std::size_t stationCtExact {0};
            std::size_t stationGlobalTlsCaptured {0};
            std::size_t stationGlobalCtAbsent {0};
            std::size_t stationGlobalCtPresent {0};
            std::size_t stationGlobalCtInvalid {0};
            std::size_t stationGlobalCtEmpty {0};
            std::size_t stationGlobalCtTextRows {0};
            std::size_t stationGlobalCtExact {0};
            std::size_t fullIdentityRetained {0};
            std::size_t numericDisagreement {0};
            std::size_t sampleCount {0};
            std::array<PndtFailureSample, MaxPndtFailureSamples> samples {};
            std::size_t cellDataTextSampleCount {0};
            std::array<CellDataTextSample, MaxLoggedCandidates>
                cellDataTextSamples {};
            bool hasStationGlobalCtExactSample {false};
            CellDataTextSample stationGlobalCtExactSample;
        };

        struct StationBaseline
        {
            FormID mapId {0};
            MapSessionIdentity firstIdentity;
            std::uint64_t completedTravelFloor {0};
            StationTuple tuple;
        };

        struct AncestryState
        {
            std::vector<FormID> ids;
            std::vector<BodyFacts> bodies;
            MapSessionIdentity selectionIdentity;
            TargetCopy target;
            std::optional<StationTuple> station;
            std::size_t expectedForms {0};
            std::size_t cursor {0};
            std::size_t failedRows {0};
            FormID selectedStarFormId {0};
            FormID selectedNumericSystemId {0};
            bool hasSelectedStarFormId {false};
            bool hasSelectedNumericSystemId {false};
            PndtScanCounters scan;
            bool hasSelection {false};
            bool enumerationActive {false};
            bool enumerationComplete {false};
            bool analysisLogged {false};
            bool failed {false};
            std::array<StationBaseline, MaxStationBaselines> baselines {};
            std::size_t baselineCount {0};
        } g_ancestry;

        using ResolveCurrentBodyFunction = FormID* (*)(FormID*);
        using ResolveBodyIdentityFunction = FormID* (*)(FormID*, FormID);
        using ResolveBodyBySystemOrdinalFunction = FormID* (*)(FormID*,
            FormID, FormID);
        using ResolveNumericInnerFunction = FormID* (*)(FormID*,
            std::uintptr_t*, FormID);
        using LookupSatelliteFunction = const void* (*)(std::uintptr_t*, FormID);

        struct ComponentTreeCursor
        {
            std::uint64_t unknown00 {0};
            std::uint64_t unknown08 {0};
            std::uintptr_t node {0};
            std::uint64_t slot {0};
        };
        static_assert(sizeof(ComponentTreeCursor) == 0x20);
        static_assert(offsetof(ComponentTreeCursor, node) == 0x10);
        static_assert(offsetof(ComponentTreeCursor, slot) == 0x18);

        using LookupComponentTreeFunction = ComponentTreeCursor* (*)(
            std::uintptr_t, ComponentTreeCursor*, const std::uint64_t*);
        using ResolvePlanetFromRefFunction = std::uint64_t (*)(
            RE::TESObjectREFR*, std::int32_t*, std::int32_t*);

        using AllFormsMap = RE::BSTHashMap2<FormID, RE::TESForm*>;
        using GuardedAllFormsMap =
            RE::BSGuarded<AllFormsMap, RE::BSReadWriteLock>;
        static_assert(sizeof(AllFormsMap) == 0x30);
        static_assert(sizeof(AllFormsMap::entry_type) == 0x18);
        static_assert(sizeof(GuardedAllFormsMap) == 0x38);

        ResolveCurrentBodyFunction g_resolveCurrentBody {nullptr};
        ResolveBodyIdentityFunction g_resolveBodySystemForm {nullptr};
        ResolveBodyIdentityFunction g_resolveBodyNumericSystem {nullptr};
        ResolveBodyBySystemOrdinalFunction
            g_resolveBodyBySystemOrdinal {nullptr};
        ResolvePlanetFromRefFunction g_resolvePlanetFromRef {nullptr};
        ResolveNumericInnerFunction g_originalResolveNumericInner {nullptr};
        LookupSatelliteFunction g_lookupSatellite {nullptr};
        LookupComponentTreeFunction g_lookupComponentTree {nullptr};
        std::uintptr_t g_cellDataTagAddress {0};
        std::uintptr_t g_allFormsMapAddress {0};
        std::atomic<std::uint64_t> g_nextSequence {1};
        std::atomic_bool g_producerFaulted {false};
        std::atomic_bool g_initialized {false};

        struct CaptureTls
        {
            bool armed {false};
            bool reentrant {false};
            bool captured {false};
            FormID expectedBodyId {0};
            ComponentCopy copy;
        };
        thread_local CaptureTls g_captureTls;

        template <std::size_t N>
        void CopyText(std::array<char, N>& destination, const char* source,
            bool* truncated = nullptr) noexcept
        {
            destination.fill('\0');
            if (truncated) {
                *truncated = false;
            }
            if (!source) {
                return;
            }

            std::size_t index = 0;
            while (index + 1 < destination.size() && source[index] != '\0') {
                const auto byte = static_cast<unsigned char>(source[index]);
                destination[index] = byte < 0x20 ? ' ' : source[index];
                ++index;
            }
            if (source[index] != '\0' && truncated) {
                *truncated = true;
            }
        }

        TargetCopy CopyTarget(const TargetObservation& target)
        {
            TargetCopy copy {
                .id = target.id,
                .resolvedTargetId = target.resolvedTargetId,
                .resolvedCourseId = target.resolvedCourseId,
                .displayedSystemFormId =
                    target.displayedSystemFormId.value_or(0),
                .kind = static_cast<std::uint8_t>(target.kind),
                .hasDisplayedSystemFormId =
                    target.displayedSystemFormId.has_value(),
            };
            CopyText(copy.name, target.displayName.c_str());
            return copy;
        }

        bool SameComponent(const ComponentCopy& left,
            const ComponentCopy& right)
        {
            return left == right;
        }

        void MarkProducerFault() noexcept
        {
            if (g_initialized.load(std::memory_order_acquire)) {
                g_producerFaulted.store(true, std::memory_order_release);
            }
        }

        void PushObservation(Observation observation)
        {
            if (!g_initialized.load(std::memory_order_acquire)) {
                return;
            }
            std::lock_guard lock {g_inbox.mutex};
            if (observation.kind == ObservationKind::Component &&
                g_inbox.count != 0) {
                const auto& previous = g_inbox.entries[g_inbox.count - 1];
                if (previous.kind == ObservationKind::Component &&
                    SameComponent(previous.component, observation.component)) {
                    return;
                }
            }
            if (g_inbox.overflowed) {
                return;
            }
            if (g_inbox.count == g_inbox.entries.size()) {
                g_inbox.count = 0;
                g_inbox.overflowed = true;
                return;
            }
            // Sequence is assigned under the same mutex as insertion so its
            // order is exactly the reducer order across all producer threads.
            observation.sequence =
                g_nextSequence.fetch_add(1, std::memory_order_relaxed);
            if (observation.sequence == 0) {
                observation.sequence =
                    g_nextSequence.fetch_add(1, std::memory_order_relaxed);
            }
            g_inbox.entries[g_inbox.count++] = observation;
        }

        void TakeInbox(InboxBatch& batch)
        {
            std::lock_guard lock {g_inbox.mutex};
            batch.count = g_inbox.count;
            batch.overflowed = g_inbox.overflowed;
            std::copy_n(g_inbox.entries.begin(), g_inbox.count,
                batch.entries.begin());
            g_inbox.count = 0;
            g_inbox.overflowed = false;
        }

        bool IsReadableRange(std::uintptr_t address, std::size_t size) noexcept
        {
            if (address == 0 || size == 0 ||
                address > std::numeric_limits<std::uintptr_t>::max() - size) {
                return false;
            }
            MEMORY_BASIC_INFORMATION information {};
            if (VirtualQuery(reinterpret_cast<const void*>(address),
                    &information, sizeof(information)) != sizeof(information) ||
                information.State != MEM_COMMIT ||
                (information.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
                return false;
            }
            const auto regionEnd = reinterpret_cast<std::uintptr_t>(
                information.BaseAddress) + information.RegionSize;
            return address + size <= regionEnd;
        }

        bool IsReadableSpan(std::uintptr_t address, std::size_t size) noexcept
        {
            if (address == 0 || size == 0 ||
                address > std::numeric_limits<std::uintptr_t>::max() - size) {
                return false;
            }
            const auto end = address + size;
            auto cursor = address;
            while (cursor < end) {
                MEMORY_BASIC_INFORMATION information {};
                if (VirtualQuery(reinterpret_cast<const void*>(cursor),
                        &information, sizeof(information)) !=
                        sizeof(information) ||
                    information.State != MEM_COMMIT ||
                    (information.Protect &
                        (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
                    return false;
                }
                const auto regionStart = reinterpret_cast<std::uintptr_t>(
                    information.BaseAddress);
                if (cursor < regionStart ||
                    information.RegionSize >
                        std::numeric_limits<std::uintptr_t>::max() -
                            regionStart) {
                    return false;
                }
                const auto regionEnd = regionStart + information.RegionSize;
                if (regionEnd <= cursor) {
                    return false;
                }
                cursor = std::min(end, regionEnd);
            }
            return true;
        }

        bool IsExecutingImageRange(std::uintptr_t address,
            std::size_t size) noexcept
        {
            const auto base =
                REX::FModule::GetExecutingModule().GetBaseAddress();
            if (!IsReadableRange(base, sizeof(IMAGE_DOS_HEADER))) {
                return false;
            }
            const auto* dos =
                reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 ||
                !IsReadableRange(base + dos->e_lfanew,
                    sizeof(IMAGE_NT_HEADERS64))) {
                return false;
            }
            const auto* headers = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
                base + dos->e_lfanew);
            if (headers->Signature != IMAGE_NT_SIGNATURE ||
                headers->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
                return false;
            }
            const auto imageSize = headers->OptionalHeader.SizeOfImage;
            return address >= base && size <= imageSize &&
                address - base <= imageSize - size;
        }

        template <std::size_t N>
        bool MatchesBytes(std::uintptr_t address,
            const std::array<std::uint8_t, N>& expected)
        {
            if (!IsExecutingImageRange(address, expected.size()) ||
                !IsReadableRange(address, expected.size())) {
                return false;
            }
            std::array<std::uint8_t, N> observed {};
            std::memcpy(observed.data(), reinterpret_cast<const void*>(address),
                observed.size());
            return observed == expected;
        }

        std::uintptr_t DecodeRelativeCall(std::uintptr_t site)
        {
            if (*reinterpret_cast<const std::uint8_t*>(site) != 0xE8) {
                return 0;
            }
            std::int32_t displacement = 0;
            std::memcpy(&displacement,
                reinterpret_cast<const void*>(site + 1),
                sizeof(displacement));
            return site + 5 + displacement;
        }

        std::uintptr_t DecodeRipRelativeAddress(std::uintptr_t instruction,
            std::size_t displacementOffset,
            std::size_t instructionSize) noexcept
        {
            if (displacementOffset > instructionSize ||
                sizeof(std::int32_t) > instructionSize - displacementOffset ||
                !IsReadableRange(instruction + displacementOffset,
                    sizeof(std::int32_t))) {
                return 0;
            }
            std::int32_t displacement = 0;
            std::memcpy(&displacement,
                reinterpret_cast<const void*>(
                    instruction + displacementOffset),
                sizeof(displacement));
            return instruction + instructionSize + displacement;
        }

        struct CellDataLookup
        {
            CellDataCaptureState state {CellDataCaptureState::Invalid};
            const std::byte* payload {nullptr};
        };

        CellDataLookup LookupCellData(std::uintptr_t* databaseContext,
            FormID bodyId) noexcept
        {
            if (!databaseContext || !*databaseContext ||
                !g_lookupComponentTree || !g_cellDataTagAddress) {
                return {};
            }

            std::uint16_t tag = 0;
            std::memcpy(&tag,
                reinterpret_cast<const void*>(g_cellDataTagAddress),
                sizeof(tag));
            if (tag == 0) {
                return {};
            }

            const auto key = (static_cast<std::uint64_t>(tag) << 48) |
                (static_cast<std::uint64_t>(bodyId) << 16);
            ComponentTreeCursor cursor {};
            if (g_lookupComponentTree(*databaseContext + 0x268, &cursor,
                    &key) != std::addressof(cursor)) {
                return {};
            }
            if (cursor.node == 0 && cursor.slot == 0xFE0) {
                return {.state = CellDataCaptureState::Absent};
            }
            if (!cursor.node || cursor.slot >= 0xFE0 ||
                cursor.slot >
                    (std::numeric_limits<std::uintptr_t>::max() - 0x12) /
                        4 ||
                cursor.node > std::numeric_limits<std::uintptr_t>::max() -
                        0x12 - cursor.slot * 4) {
                return {};
            }

            const auto offsetAddress = cursor.node + 0x12 + cursor.slot * 4;
            if (!IsReadableRange(offsetAddress, sizeof(std::uint16_t))) {
                return {};
            }

            std::uint16_t offset = 0;
            std::memcpy(&offset,
                reinterpret_cast<const void*>(offsetAddress),
                sizeof(offset));
            if (cursor.node > std::numeric_limits<std::uintptr_t>::max() -
                    offset - 0x20) {
                return {};
            }
            const auto payload = cursor.node + offset + 0x20;
            if (!IsReadableRange(payload, 9)) {
                return {};
            }
            return {
                .state = CellDataCaptureState::Present,
                .payload = reinterpret_cast<const std::byte*>(payload),
            };
        }

        template <std::size_t N>
        bool CopyRawFixedString(std::array<char, N>& destination,
            const std::byte* componentPayload, bool& truncated) noexcept
        {
            static_assert(N > 1);
            constexpr std::size_t EntrySize = 0x18;
            constexpr std::size_t EntryValueOffset = 0x08;
            constexpr std::size_t EntryFlagsOffset = 0x14;
            constexpr std::uint8_t ExternalFlag = 0x02;
            constexpr std::size_t MaxExternalHops = 8;

            destination.fill('\0');
            truncated = false;
            if (!componentPayload ||
                !IsReadableRange(reinterpret_cast<std::uintptr_t>(
                    componentPayload), sizeof(std::uintptr_t))) {
                return false;
            }

            std::uintptr_t entry = 0;
            std::memcpy(&entry, componentPayload, sizeof(entry));
            if (entry == 0) {
                return true;
            }

            std::array<std::uintptr_t, MaxExternalHops + 1> visited {};
            std::size_t visitedCount = 0;
            for (std::size_t hop = 0; hop <= MaxExternalHops; ++hop) {
                if (!IsReadableRange(entry, EntrySize)) {
                    return false;
                }
                if (std::find(visited.begin(),
                        visited.begin() + visitedCount, entry) !=
                    visited.begin() + visitedCount) {
                    return false;
                }
                visited[visitedCount++] = entry;

                std::uint8_t flags = 0;
                std::memcpy(&flags,
                    reinterpret_cast<const void*>(entry + EntryFlagsOffset),
                    sizeof(flags));
                if ((flags & ExternalFlag) != 0) {
                    if (hop == MaxExternalHops) {
                        return false;
                    }
                    std::uintptr_t right = 0;
                    std::memcpy(&right,
                        reinterpret_cast<const void*>(
                            entry + EntryValueOffset),
                        sizeof(right));
                    if (!right) {
                        return false;
                    }
                    entry = right;
                    continue;
                }

                std::uint32_t length = 0;
                std::memcpy(&length,
                    reinterpret_cast<const void*>(entry + EntryValueOffset),
                    sizeof(length));
                if (length >= destination.size()) {
                    truncated = true;
                    return false;
                }
                const auto data = entry + EntrySize;
                const auto readableSize = static_cast<std::size_t>(length) + 1;
                if (!IsReadableRange(data, readableSize)) {
                    return false;
                }
                const auto* source = reinterpret_cast<const char*>(data);
                if (source[length] != '\0') {
                    return false;
                }
                for (std::size_t index = 0; index < length; ++index) {
                    const auto byte =
                        static_cast<unsigned char>(source[index]);
                    destination[index] = byte < 0x20 ? ' ' : source[index];
                }
                return true;
            }
            return false;
        }

        FormID* ResolveNumericInnerThunk(FormID* output,
            std::uintptr_t* databaseContext, FormID bodyId) noexcept
        {
            const bool capture = g_captureTls.armed &&
                !g_captureTls.reentrant &&
                g_captureTls.expectedBodyId == bodyId;
            if (capture) {
                g_captureTls.reentrant = true;
            }

            // The stock inner is always invoked exactly once. The TLS arm is
            // private to our post-advance wrapper; unrelated engine callers
            // pass through without locks, allocation, logging, or publication.
            auto* result = g_originalResolveNumericInner(output,
                databaseContext, bodyId);
            if (!capture) {
                return result;
            }

            ComponentCopy copy {.bodyId = bodyId};
            if (databaseContext && *databaseContext) {
                if (const auto* row = static_cast<const std::byte*>(
                        g_lookupSatellite(databaseContext, bodyId));
                    row && IsReadableRange(
                        reinterpret_cast<std::uintptr_t>(row), 12)) {
                    std::memcpy(&copy.numericSystemId, row,
                        sizeof(copy.numericSystemId));
                    std::memcpy(&copy.parentOrdinal,
                        row + sizeof(FormID), sizeof(copy.parentOrdinal));
                    std::memcpy(&copy.planetOrdinal,
                        row + sizeof(FormID) * 2, sizeof(copy.planetOrdinal));
                    copy.satellitePresent = true;
                }

                const auto cellData = LookupCellData(databaseContext, bodyId);
                copy.cellDataState = cellData.state;
                if (cellData.state == CellDataCaptureState::Present) {
                    std::memcpy(&copy.cellDataFlag, cellData.payload + 8,
                        sizeof(copy.cellDataFlag));
                    if (!CopyRawFixedString(copy.cellEditorId,
                            cellData.payload, copy.cellTextTruncated)) {
                        copy.cellDataState = CellDataCaptureState::Invalid;
                    }
                }
            }

            g_captureTls.copy = copy;
            g_captureTls.captured = true;
            g_captureTls.reentrant = false;
            return result;
        }

        bool ResolveNumericWithCapture(FormID bodyId, FormID& numericId,
            ComponentCopy* capturedCopy = nullptr, bool publish = true)
        {
            if (capturedCopy) {
                *capturedCopy = {};
            }
            if (!g_resolveBodyNumericSystem || bodyId == 0 ||
                g_captureTls.armed) {
                return false;
            }

            g_captureTls = {
                .armed = true,
                .expectedBodyId = bodyId,
            };
            numericId = MissingFormId;
            const auto* wrapperReturn =
                g_resolveBodyNumericSystem(&numericId, bodyId);
            const auto captured = g_captureTls.captured;
            const auto copy = g_captureTls.copy;
            g_captureTls = {};

            // Publication occurs only after the acquiring outer wrapper has
            // released its ComponentDB context.
            if (captured) {
                if (capturedCopy) {
                    *capturedCopy = copy;
                }
                if (publish) {
                    Observation observation {
                        .kind = ObservationKind::Component,
                        .component = copy,
                    };
                    PushObservation(observation);
                }
            }
            return wrapperReturn == std::addressof(numericId) &&
                numericId != MissingFormId;
        }

        class GravJumpSink final :
            public RE::BSTEventSink<RE::Spaceship::GravJumpEvent>
        {
        public:
            RE::BSEventNotifyControl ProcessEvent(
                const RE::Spaceship::GravJumpEvent& event,
                RE::BSTEventSource<RE::Spaceship::GravJumpEvent>*) override
            {
                try {
                    auto* player = RE::PlayerCharacter::GetSingleton();
                    if (!player || !event.ship ||
                        event.ship.get() != player->GetSpaceship()) {
                        return RE::BSEventNotifyControl::kContinue;
                    }
                    Observation observation {
                        .kind = ObservationKind::GravJump,
                        .id2 = event.destination ?
                            event.destination->GetFormID() : 0,
                        .value1 = event.state,
                    };
                    PushObservation(observation);
                } catch (...) {
                    MarkProducerFault();
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        } g_gravJumpSink;

        class LoadGameSink final :
            public RE::BSTEventSink<RE::TESLoadGameEvent>
        {
        public:
            RE::BSEventNotifyControl ProcessEvent(const RE::TESLoadGameEvent&,
                RE::BSTEventSource<RE::TESLoadGameEvent>*) override
            {
                try {
                    PushObservation(Observation {
                        .kind = ObservationKind::LoadGame,
                    });
                } catch (...) {
                    MarkProducerFault();
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        } g_loadGameSink;

        class ExecuteRouteSink final :
            public RE::BSTEventSink<RE::StarMapMenu_ExecuteRoute>
        {
        public:
            RE::BSEventNotifyControl ProcessEvent(
                const RE::StarMapMenu_ExecuteRoute&,
                RE::BSTEventSource<RE::StarMapMenu_ExecuteRoute>*) override
            {
                try {
                    PushObservation(Observation {
                        .kind = ObservationKind::ExecuteRoute,
                    });
                } catch (...) {
                    MarkProducerFault();
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        } g_executeRouteSink;

        template <class Event>
        RE::BSTEventSource<Event>* ProveEventSource(
            const REL::ID& getterId, const REL::ID& vtableId,
            std::optional<REL::ID> exactSourceId)
        {
            if (!MatchesBytes(getterId.address(), GlobalEventGetterPrologue)) {
                return nullptr;
            }
            auto* source = Event::GetEventSource();
            if (!source || (exactSourceId &&
                    reinterpret_cast<std::uintptr_t>(source) !=
                        exactSourceId->address())) {
                return nullptr;
            }
            std::uintptr_t vtable = 0;
            std::memcpy(&vtable, source, sizeof(vtable));
            return vtable == vtableId.address() ? source : nullptr;
        }

        RE::BSTEventSource<RE::StarMapMenu_ExecuteRoute>*
        ProveExecuteRouteEventSource()
        {
            const auto getterAddress =
                ExecuteRouteGetEventSourceId.address();
            if (!MatchesBytes(getterAddress,
                    GlobalEventGetterPrologue)) {
                return nullptr;
            }
            using Getter = RE::BSTEventSource<
                RE::StarMapMenu_ExecuteRoute>* (*)();
            const auto getter = reinterpret_cast<Getter>(getterAddress);
            auto* source = getter();
            if (!source || reinterpret_cast<std::uintptr_t>(source) !=
                    ExecuteRouteEventSourceAddressId.address()) {
                return nullptr;
            }
            std::uintptr_t vtable = 0;
            std::memcpy(&vtable, source, sizeof(vtable));
            return vtable == ExecuteRouteEventSourceVtableId.address() ?
                source : nullptr;
        }

        const char* NativeViewName(NativeMapView view)
        {
            switch (view) {
            case NativeMapView::None:
                return "none";
            case NativeMapView::Galaxy:
                return "galaxy";
            case NativeMapView::System:
                return "system";
            case NativeMapView::BodyInspect:
                return "body-inspect";
            case NativeMapView::Surface:
                return "surface";
            case NativeMapView::Unknown:
                return "unknown";
            }
            return "unknown";
        }
    }

    bool Initialize() noexcept
    {
        try {
        if (g_initialized.load(std::memory_order_acquire)) {
            return true;
        }

        const auto currentBodyAddress = ResolveCurrentBodyId.address();
        const auto systemFormAddress = ResolveBodySystemFormId.address();
        const auto numericOuterAddress =
            ResolveBodyNumericSystemId.address();
        const auto numericInnerAddress =
            ResolveBodyNumericSystemInnerId.address();
        const auto bodyBySystemOrdinalAddress =
            ResolveBodyBySystemOrdinalId.address();
        const auto planetFromRefAddress = ResolvePlanetFromRefId.address();
        const auto satelliteLookupAddress = LookupSatelliteRowId.address();
        const auto componentLookupAddress = LookupComponentTreeId.address();
        const auto lookupFormAddress = LookupFormByIdId.address();
        const auto allFormsMapAddress = AllFormsMapAddressId.address();
        const auto innerCallSite =
            numericOuterAddress + NumericOuterInnerCallOffset;
        const auto lookupFormAllFormsInstruction = lookupFormAddress +
            LookupFormAllFormsInstructionOffset;

        if (!MatchesBytes(currentBodyAddress, CurrentBodyPrologue) ||
            !MatchesBytes(systemFormAddress, AcquiringWrapperPrologue) ||
            !MatchesBytes(numericOuterAddress, AcquiringWrapperPrologue) ||
            !MatchesBytes(numericInnerAddress, NumericInnerPrologue) ||
            !MatchesBytes(bodyBySystemOrdinalAddress,
                ResolveBodyBySystemOrdinalPrologue) ||
            !MatchesBytes(planetFromRefAddress,
                ResolvePlanetFromRefPrologue) ||
            !MatchesBytes(satelliteLookupAddress, SatelliteLookupPrologue) ||
            !MatchesBytes(componentLookupAddress,
                ComponentTreeLookupPrologue) ||
            !MatchesBytes(lookupFormAddress, LookupFormByIdPrologue) ||
            !MatchesBytes(innerCallSite, NumericInnerCallBytes) ||
            !IsExecutingImageRange(innerCallSite, 5) ||
            !IsReadableRange(innerCallSite, 5) ||
            !IsExecutingImageRange(CellDataTagId.address(),
                sizeof(std::uint16_t)) ||
            !IsReadableRange(CellDataTagId.address(),
                sizeof(std::uint16_t)) ||
            !IsExecutingImageRange(allFormsMapAddress,
                sizeof(std::uintptr_t)) ||
            !IsReadableRange(allFormsMapAddress,
                sizeof(std::uintptr_t)) ||
            DecodeRipRelativeAddress(lookupFormAllFormsInstruction,
                LookupFormAllFormsDisplacementOffset,
                LookupFormAllFormsInstructionSize) != allFormsMapAddress ||
            DecodeRelativeCall(innerCallSite) != numericInnerAddress) {
            REX::ERROR("[remote-native-probe] 1.16.244 native function/call-site fingerprint failed; probe disabled before hooks");
            return false;
        }

        auto* gravSource = ProveEventSource<RE::Spaceship::GravJumpEvent>(
            GravJumpGetEventSourceId, GravJumpEventSourceVtableId,
            std::nullopt);
        auto* loadSource = ProveEventSource<RE::TESLoadGameEvent>(
            LoadGameGetEventSourceId, LoadGameEventSourceVtableId,
            LoadGameEventSourceAddressId);
        auto* executeRouteSource = ProveExecuteRouteEventSource();
        if (!gravSource || !loadSource || !executeRouteSource) {
            REX::ERROR("[remote-native-probe] guarded GravJump/TESLoadGame/ExecuteRoute source proof failed; probe disabled before hooks");
            return false;
        }

        g_resolveCurrentBody =
            reinterpret_cast<ResolveCurrentBodyFunction>(currentBodyAddress);
        g_resolveBodySystemForm =
            reinterpret_cast<ResolveBodyIdentityFunction>(systemFormAddress);
        g_resolveBodyNumericSystem =
            reinterpret_cast<ResolveBodyIdentityFunction>(numericOuterAddress);
        g_resolveBodyBySystemOrdinal =
            reinterpret_cast<ResolveBodyBySystemOrdinalFunction>(
                bodyBySystemOrdinalAddress);
        g_resolvePlanetFromRef =
            reinterpret_cast<ResolvePlanetFromRefFunction>(
                planetFromRefAddress);
        g_originalResolveNumericInner =
            reinterpret_cast<ResolveNumericInnerFunction>(numericInnerAddress);
        g_lookupSatellite =
            reinterpret_cast<LookupSatelliteFunction>(satelliteLookupAddress);
        g_lookupComponentTree =
            reinterpret_cast<LookupComponentTreeFunction>(
                componentLookupAddress);
        g_cellDataTagAddress = CellDataTagId.address();
        g_allFormsMapAddress = allFormsMapAddress;

        const auto replaced = REL::GetTrampoline().write_call<5>(
            innerCallSite, &ResolveNumericInnerThunk);
        // The exact five bytes and decoded target were proven immediately
        // before patching. Chain the target returned by the trampoline; do
        // not perform a fallible post-write rejection after the site changed.
        g_originalResolveNumericInner =
            reinterpret_cast<ResolveNumericInnerFunction>(replaced);

        gravSource->RegisterSink(&g_gravJumpSink);
        loadSource->RegisterSink(&g_loadGameSink);
        executeRouteSource->RegisterSink(&g_executeRouteSink);
        g_initialized.store(true, std::memory_order_release);
        REX::WARN("[remote-native-probe] READY: PASSIVE 1.16.244 fingerprints exact; IDs current=97914 system-form=124608 numeric-outer=124767 numeric-inner=124766 body-by-system-ordinal=124772 planet-from-ref=52188 Satellite=124799 CT=126806 CellDataTag=938337 LookupForm=47401 AllForms=883341 Grav=93876 Load=64149 ExecuteRoute-getter=94774 ExecuteRoute-source=948974; no input is consumed and no stock action is dispatched");
        return true;
        } catch (const std::exception& error) {
            REX::ERROR("[remote-native-probe] initialization exception: {}",
                error.what());
            return false;
        } catch (...) {
            REX::ERROR("[remote-native-probe] unknown initialization exception");
            return false;
        }
    }

    void RecordMapMovie(const MapSessionIdentity& identity,
        std::int64_t bornTicks) noexcept
    {
        try {
            Observation observation {
                .kind = ObservationKind::Movie,
                .identity = identity,
                .ticks = bornTicks,
            };
            CopyText(observation.text, MapMenuName);
            PushObservation(observation);
        } catch (...) {
            MarkProducerFault();
        }
    }

    void RecordMenuLifecycle(const char* menuName, bool opening) noexcept
    {
        try {
            if (!menuName ||
                std::strcmp(menuName, LoadingMenuName) != 0) {
                return;
            }
            Observation observation {
                .kind = ObservationKind::Menu,
                .flag1 = opening,
            };
            CopyText(observation.text, menuName);
            PushObservation(observation);
        } catch (...) {
            MarkProducerFault();
        }
    }

    void RecordMapLifecycle(const MapSessionIdentity& identity,
        bool opening) noexcept
    {
        try {
            Observation observation {
                .kind = ObservationKind::Menu,
                .identity = identity,
                .flag1 = opening,
            };
            CopyText(observation.text, MapMenuName);
            PushObservation(observation);
        } catch (...) {
            MarkProducerFault();
        }
    }

    void RecordMapData(const MapSessionIdentity& identity, MapView view,
        FormID currentBodyId, FormID currentSystemFormId) noexcept
    {
        try {
            PushObservation(Observation {
                .kind = ObservationKind::MapData,
                .identity = identity,
                .id1 = currentBodyId,
                .id2 = currentSystemFormId,
                .small1 = static_cast<std::uint8_t>(view),
            });
        } catch (...) {
            MarkProducerFault();
        }
    }

    void RecordMarkers(const MapSessionIdentity& identity,
        const MarkerUpdate& update) noexcept
    {
        try {
            Observation observation {
                .kind = ObservationKind::Markers,
                .identity = identity,
                .target = CopyTarget(update.highlighted),
                .count = static_cast<std::uint32_t>(std::min<std::size_t>(
                    update.highlightedCount,
                    std::numeric_limits<std::uint32_t>::max())),
            };
            PushObservation(observation);
        } catch (...) {
            MarkProducerFault();
        }
    }

    void RecordDossier(const MapSessionIdentity& identity,
        const TargetObservation& target) noexcept
    {
        try {
            Observation observation {
                .kind = ObservationKind::Dossier,
                .identity = identity,
                .target = CopyTarget(target),
            };
            PushObservation(observation);
        } catch (...) {
            MarkProducerFault();
        }
    }

    void RecordResolvedMarker(const MapSessionIdentity& identity,
        const TargetObservation& target) noexcept
    {
        try {
            Observation observation {
                .kind = ObservationKind::ResolvedMarker,
                .identity = identity,
                .target = CopyTarget(target),
            };
            PushObservation(observation);
        } catch (...) {
            MarkProducerFault();
        }
    }

    namespace
    {
        class HudRowCollector final :
            public RE::Scaleform::GFx::Value::ArrayVisitor
        {
        public:
            void Visit(std::uint32_t,
                const RE::Scaleform::GFx::Value& value) override
            {
                try {
                    if (count == rows.size()) {
                        overflowed = true;
                        return;
                    }
                    auto entry = value;
                    bool locked = false;
                    (void) CFS::ScaleformValue::BooleanMember(
                        entry, "bIsCruiseTargetLock", locked);
                    rows[count++] = {
                        .courseId = CFS::ScaleformValue::UIntMember(
                            entry, "uniqueID"),
                        .locked = locked,
                    };
                } catch (...) {
                    overflowed = true;
                    MarkProducerFault();
                }
            }

            std::array<HudRowCopy, MaxHudRows> rows {};
            std::size_t count {0};
            bool overflowed {false};
        };
    }

    void RecordHudCourse(std::uint32_t generation,
        RE::Scaleform::GFx::Value& payload) noexcept
    {
        try {
            RE::Scaleform::GFx::Value targets;
            if (!payload.GetMember("targetArray", &targets)) {
                return;
            }
            RE::Scaleform::GFx::Value inner;
            if (targets.GetMember("dataA", &inner) && inner.IsArray()) {
                targets = inner;
            }
            if (!targets.IsArray()) {
                return;
            }

            HudRowCollector collector;
            targets.VisitElements(&collector);
            Observation observation {
                .kind = ObservationKind::Hud,
                .generation = generation,
                .count = static_cast<std::uint32_t>(collector.count),
                .overflowed = collector.overflowed,
            };
            observation.hudRows = collector.rows;
            PushObservation(observation);
        } catch (...) {
            MarkProducerFault();
        }
    }

    void RecordHudMovieCreated(std::uint32_t generation) noexcept
    {
        try {
            PushObservation(Observation {
                .kind = ObservationKind::HudMovie,
                .generation = generation,
            });
        } catch (...) {
            MarkProducerFault();
        }
    }

    void RecordInput(const RE::ButtonEvent& event) noexcept
    {
        try {
            const auto* name = event.strUserEvent.c_str();
            const bool firstDown = event.value != 0.0F &&
                event.heldDownSecs == 0.0F;
            if (!firstDown || !name ||
                std::strcmp(name, SetRouteDestinationEvent) != 0) {
                return;
            }

            Observation observation {
                .kind = ObservationKind::Input,
                .value1 = static_cast<std::uint32_t>(event.deviceType),
                .value2 = event.idCode,
                .float1 = event.value,
                .float2 = event.heldDownSecs,
                .flag1 = event.disabled,
            };
            CopyText(observation.text, name);
            PushObservation(observation);
        } catch (...) {
            MarkProducerFault();
        }
    }

    namespace
    {
        template <class T>
        bool ReadScalar(std::uintptr_t address, T& value) noexcept
        {
            static_assert(std::is_trivially_copyable_v<T>);
            if (!IsReadableRange(address, sizeof(T))) {
                return false;
            }
            std::memcpy(&value, reinterpret_cast<const void*>(address),
                sizeof(T));
            return true;
        }

        bool IsLiveFormType(FormID id, RE::FormType type) noexcept
        {
            if (id == 0 || id == MissingFormId) {
                return false;
            }
            const auto* form = RE::TESForm::LookupByID(id);
            return form && !form->IsDeleted() &&
                form->GetFormID() == id &&
                form->GetFormType() == type;
        }

        NativeMapState ReadNativeMapStateOnce() noexcept
        {
            NativeMapState result;
            const auto* ui = RE::UI::GetSingleton();
            const RE::BSFixedString mapName {MapMenuName};
            if (!ui || !ui->IsMenuOpen(mapName)) {
                return result;
            }
            result.menuOpen = true;

            const auto menu = ui->GetMenu(mapName);
            if (!menu || !menu->uiMovie ||
                !menu->uiMovie->asMovieRoot) {
                return result;
            }

            const auto menuAddress =
                reinterpret_cast<std::uintptr_t>(menu.get());
            std::uintptr_t menuVtable = 0;
            if (!ReadScalar(menuAddress, menuVtable) ||
                menuVtable != StarMapMenuPrimaryVtableId.address()) {
                return result;
            }

            std::uintptr_t activeState = 0;
            if (!ReadScalar(menuAddress + StarMapMenuActiveStateOffset,
                    activeState) || !activeState) {
                return result;
            }
            std::uintptr_t stateVtable = 0;
            if (!ReadScalar(activeState, stateVtable)) {
                return result;
            }

            if (stateVtable == GalaxyStatePrimaryVtableId.address()) {
                result.view = NativeMapView::Galaxy;
                if (!ReadScalar(activeState +
                        GalaxyStateSelectedSystemOffset,
                        result.selectedIdentity) ||
                    !ReadScalar(activeState +
                        GalaxyStateQuickSelectOpenOffset,
                        result.quickSelectOpen)) {
                    return result;
                }
            } else if (stateVtable == SystemStatePrimaryVtableId.address()) {
                result.view = NativeMapView::System;
                if (!ReadScalar(activeState +
                        SystemStateDisplayedSystemOffset,
                        result.displayedSystem) ||
                    !ReadScalar(activeState + SystemStateDisplayRootOffset,
                        result.displayRoot) ||
                    !ReadScalar(activeState + SystemStateSelectedBodyOffset,
                        result.selectedIdentity)) {
                    return result;
                }
            } else if (
                stateVtable == BodyInspectStatePrimaryVtableId.address()) {
                result.view = NativeMapView::BodyInspect;
                if (!ReadScalar(activeState +
                        BodyInspectStateSelectedBodyOffset,
                        result.selectedIdentity)) {
                    return result;
                }
            } else if (
                stateVtable == SurfaceMapStatePrimaryVtableId.address()) {
                result.view = NativeMapView::Surface;
            } else {
                result.view = NativeMapView::Unknown;
                return result;
            }
            result.layoutValid = true;

            std::uint8_t alternate = 0;
            if (!ReadScalar(menuAddress +
                    StarMapMenuAlternateModeOffset, alternate)) {
                return result;
            }
            result.routeAlternate = alternate != 0;
            if (result.routeAlternate) {
                if (!ReadScalar(menuAddress +
                        StarMapMenuAlternateEndpointOffset,
                        result.routeEndpoint)) {
                    return result;
                }
                result.routePointCount = result.routeEndpoint == 0 ? 0 : 1;
                result.routeReadable = true;
            } else {
                std::uintptr_t routeData = 0;
                if (!ReadScalar(menuAddress + StarMapMenuRouteCountOffset,
                        result.routePointCount) ||
                    !ReadScalar(menuAddress + StarMapMenuRouteDataOffset,
                        routeData) ||
                    result.routePointCount > MaxRoutePoints) {
                    return result;
                }
                if (result.routePointCount != 0) {
                    if (!routeData || routeData >
                            (std::numeric_limits<std::uintptr_t>::max)() -
                                RoutePointEndpointOffset ||
                        result.routePointCount - 1 >
                            (std::numeric_limits<std::uintptr_t>::max() -
                                routeData - RoutePointEndpointOffset) /
                                RoutePointStride) {
                        return result;
                    }
                    const auto endpointAddress = routeData +
                        (result.routePointCount - 1) * RoutePointStride +
                        RoutePointEndpointOffset;
                    if (!ReadScalar(endpointAddress, result.routeEndpoint)) {
                        return result;
                    }
                }
                result.routeReadable = true;
            }
            result.routeEndpointIsStdt = result.routeEndpoint != 0 &&
                IsLiveFormType(result.routeEndpoint, RE::FormType::kSTDT);
            return result;
        }

        NativeMapState ReadNativeMapState() noexcept
        {
            const auto first = ReadNativeMapStateOnce();
            auto second = ReadNativeMapStateOnce();
            if (first == second) {
                second.stable = true;
            } else {
                second.routeReadable = false;
            }
            return second;
        }

        std::optional<SystemPair> ReadCurrentSystemPair()
        {
            if (!g_resolveCurrentBody || !g_resolveBodySystemForm ||
                !g_resolveBodyNumericSystem) {
                return std::nullopt;
            }

            SystemPair result;
            result.bodyId = MissingFormId;
            if (g_resolveCurrentBody(&result.bodyId) !=
                    std::addressof(result.bodyId) ||
                !IsLiveFormType(result.bodyId, RE::FormType::kPNDT)) {
                return std::nullopt;
            }

            result.starFormId = MissingFormId;
            if (g_resolveBodySystemForm(
                    &result.starFormId, result.bodyId) !=
                    std::addressof(result.starFormId) ||
                !IsLiveFormType(result.starFormId, RE::FormType::kSTDT)) {
                return std::nullopt;
            }

            result.numericId = MissingFormId;
            ComponentCopy component;
            if (!ResolveNumericWithCapture(result.bodyId, result.numericId,
                    &component, false)) {
                return std::nullopt;
            }
            if (!component.satellitePresent ||
                component.numericSystemId != result.numericId) {
                REX::ERROR("[remote-native-probe] FAIL: current body {:08X} numeric wrapper={:08X} Satellite-present={} Satellite-system={:08X}; identities disagree",
                    result.bodyId, result.numericId,
                    component.satellitePresent,
                    component.numericSystemId);
                return std::nullopt;
            }
            result.satelliteNumericId = component.numericSystemId;
            // Numeric zero is Sol and is deliberately accepted.
            result.valid = true;
            return result;
        }

        bool ResolveSystemIdentity(FormID formId,
            SystemIdentity& identity)
        {
            identity = {};
            const auto* form = RE::TESForm::LookupByID(formId);
            if (!form || form->IsDeleted() ||
                form->GetFormID() != formId) {
                return false;
            }

            const auto type = form->GetFormType();
            if (type == RE::FormType::kSTDT) {
                identity.starFormId = formId;
            } else if (type == RE::FormType::kPNDT) {
                identity.starFormId = MissingFormId;
                if (!g_resolveBodySystemForm ||
                    g_resolveBodySystemForm(&identity.starFormId,
                        formId) !=
                        std::addressof(identity.starFormId) ||
                    !IsLiveFormType(identity.starFormId,
                        RE::FormType::kSTDT)) {
                    return false;
                }
            } else {
                return false;
            }

            ComponentCopy component;
            if (!ResolveNumericWithCapture(formId, identity.numericId,
                    std::addressof(component), false)) {
                return false;
            }
            if (type == RE::FormType::kPNDT &&
                (!component.satellitePresent ||
                    component.bodyId != formId ||
                    component.numericSystemId != identity.numericId)) {
                return false;
            }
            if (!IsLiveFormType(identity.starFormId,
                    RE::FormType::kSTDT)) {
                return false;
            }
            const auto* postForm = RE::TESForm::LookupByID(formId);
            if (!postForm || postForm->IsDeleted() ||
                postForm->GetFormID() != formId ||
                postForm->GetFormType() != type) {
                return false;
            }
            identity.valid = true;
            return true;
        }

        SystemIdentity ExpectedRouteSystem(
            const NativeMapState& map)
        {
            FormID candidate = 0;
            if (map.view == NativeMapView::Galaxy &&
                IsLiveFormType(map.selectedIdentity,
                    RE::FormType::kSTDT)) {
                candidate = map.selectedIdentity;
            } else if (map.view == NativeMapView::System &&
                IsLiveFormType(map.displayedSystem,
                    RE::FormType::kSTDT)) {
                candidate = map.displayedSystem;
            }
            SystemIdentity identity;
            if (candidate != 0 &&
                ResolveSystemIdentity(candidate, identity)) {
                return identity;
            }
            return {};
        }

        struct ExecuteGate
        {
            bool resolved {false};
            bool ready {false};
        };

        ExecuteGate ReadExecuteGate() noexcept
        {
            ExecuteGate result;
            try {
                const auto* ui = RE::UI::GetSingleton();
                const RE::BSFixedString mapName {MapMenuName};
                if (!ui || !ui->IsMenuOpen(mapName)) {
                    return result;
                }
                const auto menu = ui->GetMenu(mapName);
                if (!menu || !menu->uiMovie ||
                    !menu->uiMovie->asMovieRoot) {
                    return result;
                }
                auto* root = menu->uiMovie->asMovieRoot.get();
                const char* path = menu->GetRootPath();
                const std::string rootPath = path && *path ? path : "root";
                RE::Scaleform::GFx::Value menuRoot;
                RE::Scaleform::GFx::Value jumpData;
                RE::Scaleform::GFx::Value executeContainer;
                RE::Scaleform::GFx::Value executeButton;
                bool panelVisible = false;
                bool executeVisible = false;
                if (!root->GetVariable(&menuRoot, rootPath.c_str()) ||
                    !(menuRoot.IsObject() ||
                        menuRoot.IsDisplayObject()) ||
                    !CFS::ScaleformValue::ObjectMember(menuRoot,
                        "JumpData_mc", jumpData) ||
                    !CFS::ScaleformValue::BooleanMember(jumpData,
                        "visible", panelVisible) ||
                    !CFS::ScaleformValue::ObjectMember(jumpData,
                        "ExecuteButton_mc", executeContainer) ||
                    !CFS::ScaleformValue::ObjectMember(executeContainer,
                        "ExecuteButtonHint_mc", executeButton) ||
                    !CFS::ScaleformValue::BooleanMember(executeButton,
                        "Visible", executeVisible)) {
                    return result;
                }
                const auto current = ui->GetMenu(mapName);
                if (!ui->IsMenuOpen(mapName) || !current ||
                    !current->uiMovie ||
                    !current->uiMovie->asMovieRoot ||
                    current->uiMovie->asMovieRoot.get() != root) {
                    return result;
                }
                result.resolved = true;
                result.ready = panelVisible && executeVisible;
                return result;
            } catch (...) {
                return {};
            }
        }

        bool IsApplicationForeground() noexcept
        {
            const auto foreground = GetForegroundWindow();
            if (!foreground) {
                return false;
            }
            DWORD processId = 0;
            (void) GetWindowThreadProcessId(foreground, &processId);
            return processId == GetCurrentProcessId();
        }

        bool IsPlayerFlying() noexcept
        {
            static_assert(RE::ID::TESObjectREFR::IsInSpace.id() == 63482);
            const auto player = RE::PlayerCharacter::GetSingleton();
            const auto ship = player ? player->GetSpaceship() : nullptr;
            return ship && ship->IsInSpace(false);
        }

        using Reference = RE::NiPointer<RE::TESObjectREFR>;
        using Cell = RE::NiPointer<RE::TESObjectCELL>;

        bool IsLiveStationReference(const Reference& reference,
            RE::BGSKeyword* keyword, const RE::TESObjectCELL* cell)
        {
            const auto base = reference ?
                reference->GetBaseObject() :
                RE::NiPointer<RE::TESBoundObject> {};
            return reference && !reference->IsDeleted() && base &&
                !base->IsDeleted() && keyword &&
                reference->parentCell == cell &&
                reference->HasKeyword(keyword);
        }

        bool IsLiveCourseMarker(const Reference& reference,
            const RE::TESObjectCELL* cell)
        {
            if (!reference || reference->IsDeleted() ||
                reference->parentCell != cell) {
                return false;
            }
            const auto* extra = reference->extraDataList.get();
            return extra && extra->HasType(RE::ExtraDataType::kMapMarker);
        }

        std::optional<std::vector<Reference>> SnapshotCellReferences(
            const Cell& cell)
        {
            if (!cell) {
                return std::nullopt;
            }
            std::size_t expected = 0;
            {
                RE::BSAutoReadLock lock {cell->lock};
                expected = cell->references.size();
            }
            if (expected > MaxStationCellReferences) {
                return std::nullopt;
            }

            std::vector<Reference> result;
            result.reserve(expected);
            {
                RE::BSAutoReadLock lock {cell->lock};
                if (cell->references.size() != expected) {
                    return std::nullopt;
                }
                for (const auto& reference : cell->references) {
                    if (reference) {
                        result.push_back(reference);
                    }
                }
            }
            return result;
        }

        StationTuple ScanStationTuple(const TargetCopy& target,
            const NativeMapState& map)
        {
            StationTuple tuple {.mapId = target.id};
            auto* keyword = RE::TESForm::LookupByID<RE::BGSKeyword>(
                IsStarstationKeywordId);
            if (!keyword || !map.stable || !map.layoutValid ||
                map.view != NativeMapView::System) {
                return tuple;
            }

            Cell cell {RE::TESForm::LookupByID<RE::TESObjectCELL>(
                target.id)};
            Reference directStation;
            if (!cell) {
                directStation.reset(
                    RE::TESForm::LookupByID<RE::TESObjectREFR>(target.id));
                if (!directStation || !directStation->parentCell) {
                    return tuple;
                }
                cell.reset(directStation->parentCell);
                if (!IsLiveStationReference(
                        directStation, keyword, cell.get())) {
                    return tuple;
                }
            }

            tuple.cellId = cell->GetFormID();
            CopyText(tuple.cellEditorId, cell->GetFormEditorID(),
                &tuple.truncatedEditorId);
            const auto references = SnapshotCellReferences(cell);
            if (!references) {
                return tuple;
            }
            tuple.referenceCount = static_cast<std::uint32_t>(
                references->size());

            std::vector<std::pair<FormID, FormID>> stations;
            std::vector<FormID> markers;
            Reference stationReference;
            Reference courseReference;
            stations.reserve(references->size() +
                (directStation ? 1 : 0));
            markers.reserve(references->size());
            for (const auto& reference : *references) {
                if (IsLiveStationReference(reference, keyword, cell.get())) {
                    const auto base = reference->GetBaseObject();
                    stations.emplace_back(reference->GetFormID(),
                        base ? base->GetFormID() : 0);
                    if (!stationReference) {
                        stationReference = reference;
                    }
                }
                if (IsLiveCourseMarker(reference, cell.get())) {
                    markers.push_back(reference->GetFormID());
                    if (!courseReference) {
                        courseReference = reference;
                    }
                }
            }
            if (directStation) {
                const auto base = directStation->GetBaseObject();
                stations.emplace_back(directStation->GetFormID(),
                    base ? base->GetFormID() : 0);
                if (!stationReference) {
                    stationReference = directStation;
                }
            }
            std::ranges::sort(stations);
            stations.erase(std::unique(stations.begin(), stations.end(),
                [](const auto& left, const auto& right) {
                    return left.first == right.first;
                }), stations.end());
            std::ranges::sort(markers);
            markers.erase(std::unique(markers.begin(), markers.end()),
                markers.end());

            tuple.stationCount = static_cast<std::uint32_t>(stations.size());
            tuple.markerCount = static_cast<std::uint32_t>(markers.size());
            if (stations.size() != 1 || markers.size() != 1 ||
                stations.front().first == markers.front()) {
                return tuple;
            }

            tuple.targetRefId = stations.front().first;
            tuple.baseId = stations.front().second;
            tuple.courseMarkerId = markers.front();
            if (!stationReference || !courseReference ||
                stationReference->GetFormID() != tuple.targetRefId ||
                courseReference->GetFormID() != tuple.courseMarkerId ||
                !g_resolvePlanetFromRef) {
                return tuple;
            }
            std::int32_t physicalOrbital = 0;
            std::int32_t physicalSecondary = 0;
            tuple.physicalOrbitalResolved = g_resolvePlanetFromRef(
                stationReference.get(), std::addressof(physicalOrbital),
                std::addressof(physicalSecondary)) != 0;
            tuple.physicalOrbitalPndtId =
                static_cast<FormID>(physicalOrbital);
            tuple.physicalSecondaryId =
                static_cast<FormID>(physicalSecondary);

            std::int32_t courseOrbital = 0;
            std::int32_t courseSecondary = 0;
            tuple.courseOrbitalResolved = g_resolvePlanetFromRef(
                courseReference.get(), std::addressof(courseOrbital),
                std::addressof(courseSecondary)) != 0;
            tuple.courseOrbitalPndtId = static_cast<FormID>(courseOrbital);
            tuple.courseSecondaryId = static_cast<FormID>(courseSecondary);

            const bool physicalCandidate = tuple.physicalOrbitalResolved &&
                tuple.physicalOrbitalPndtId != 0;
            const bool courseCandidate = tuple.courseOrbitalResolved &&
                tuple.courseOrbitalPndtId != 0;
            if (physicalCandidate && courseCandidate) {
                if (tuple.physicalOrbitalPndtId ==
                    tuple.courseOrbitalPndtId) {
                    tuple.nativeOrbitalResolved = true;
                    tuple.nativeOrbitalPndtId =
                        tuple.physicalOrbitalPndtId;
                    tuple.nativeSecondaryId = tuple.physicalSecondaryId;
                }
            } else if (physicalCandidate) {
                tuple.nativeOrbitalResolved = true;
                tuple.nativeOrbitalPndtId = tuple.physicalOrbitalPndtId;
                tuple.nativeSecondaryId = tuple.physicalSecondaryId;
            } else if (courseCandidate) {
                tuple.nativeOrbitalResolved = true;
                tuple.nativeOrbitalFromCourseMarker = true;
                tuple.nativeOrbitalPndtId = tuple.courseOrbitalPndtId;
                tuple.nativeSecondaryId = tuple.courseSecondaryId;
            }
            if (map.selectedIdentity != target.id &&
                map.selectedIdentity != tuple.cellId) {
                return tuple;
            }
            if (!IsLiveFormType(
                    map.displayedSystem, RE::FormType::kSTDT)) {
                return tuple;
            }
            tuple.displayedSystemFormId = map.displayedSystem;
            tuple.valid = tuple.targetRefId != 0 && tuple.baseId != 0 &&
                tuple.courseMarkerId != 0 && tuple.cellId != 0 &&
                tuple.cellEditorId[0] != '\0' &&
                !tuple.truncatedEditorId;
            return tuple;
        }

        void ObserveStationBaseline(const MapSessionIdentity& identity,
            const StationTuple& tuple)
        {
            if (!tuple.valid) {
                return;
            }
            for (std::size_t index = 0;
                 index < g_ancestry.baselineCount; ++index) {
                auto& baseline = g_ancestry.baselines[index];
                if (baseline.mapId != tuple.mapId) {
                    continue;
                }
                // Counts are transient diagnostics. Revalidation is only the
                // copied identity contract needed by remote station travel.
                const bool exact = baseline.tuple.valid == tuple.valid &&
                    baseline.tuple.mapId == tuple.mapId &&
                    baseline.tuple.cellId == tuple.cellId &&
                    baseline.tuple.targetRefId == tuple.targetRefId &&
                    baseline.tuple.baseId == tuple.baseId &&
                    baseline.tuple.courseMarkerId ==
                        tuple.courseMarkerId &&
                    baseline.tuple.displayedSystemFormId ==
                        tuple.displayedSystemFormId &&
                    baseline.tuple.cellEditorId == tuple.cellEditorId;
                const bool laterSession =
                    identity != baseline.firstIdentity;
                const bool postTravel = laterSession &&
                    g_state.completedTravelSequence >
                        baseline.completedTravelFloor;
                REX::INFO("[remote-native-probe] station revalidation map={:08X} first-session={}/{} current-session={}/{} later-session={} completed-travel-floor={} completed-travel-seq={} post-travel={} classification={} exact={} physical={:08X} base={:08X} CELL={:08X} XMRK={:08X}",
                    tuple.mapId, baseline.firstIdentity.session,
                    baseline.firstIdentity.generation, identity.session,
                    identity.generation, laterSession,
                    baseline.completedTravelFloor,
                    g_state.completedTravelSequence, postTravel,
                    postTravel ? "arrival-revalidation" :
                        "repeatability-only",
                    exact, tuple.targetRefId,
                    tuple.baseId, tuple.cellId, tuple.courseMarkerId);
                if (!exact) {
                    REX::ERROR("[remote-native-probe] FAIL: station tuple changed between observations for map {:08X}",
                        tuple.mapId);
                }
                return;
            }
            if (g_ancestry.baselineCount ==
                g_ancestry.baselines.size()) {
                REX::ERROR("[remote-native-probe] FAIL: station baseline capacity exhausted at {} distinct map IDs",
                    g_ancestry.baselineCount);
                return;
            }
            g_ancestry.baselines[g_ancestry.baselineCount++] = {
                .mapId = tuple.mapId,
                .firstIdentity = identity,
                .completedTravelFloor =
                    g_state.completedTravelSequence,
                .tuple = tuple,
            };
            REX::INFO("[remote-native-probe] station baseline map={:08X} physical={:08X} base={:08X} CELL={:08X} XMRK={:08X} STDT={:08X} CELL-editor='{}' refs={} station-count={} marker-count={}",
                tuple.mapId, tuple.targetRefId, tuple.baseId,
                tuple.cellId, tuple.courseMarkerId,
                tuple.displayedSystemFormId, tuple.cellEditorId.data(),
                tuple.referenceCount, tuple.stationCount,
                tuple.markerCount);
        }

        void ClearSelectionEnumeration() noexcept
        {
            g_ancestry.ids.clear();
            g_ancestry.bodies.clear();
            g_ancestry.selectionIdentity = {};
            g_ancestry.target = {};
            g_ancestry.station.reset();
            g_ancestry.expectedForms = 0;
            g_ancestry.cursor = 0;
            g_ancestry.failedRows = 0;
            g_ancestry.selectedStarFormId = 0;
            g_ancestry.selectedNumericSystemId = 0;
            g_ancestry.hasSelectedStarFormId = false;
            g_ancestry.hasSelectedNumericSystemId = false;
            g_ancestry.scan = {};
            g_ancestry.hasSelection = false;
            g_ancestry.enumerationActive = false;
            g_ancestry.enumerationComplete = false;
            g_ancestry.analysisLogged = false;
            g_ancestry.failed = false;
        }

        bool RelevantSelectionMatches(const MapSessionIdentity& identity,
            const TargetCopy& target) noexcept
        {
            return g_ancestry.hasSelection &&
                g_ancestry.selectionIdentity == identity &&
                g_ancestry.target.id == target.id &&
                g_ancestry.target.kind == target.kind;
        }

        enum class SnapshotFailure : std::uint8_t
        {
            None,
            MissingMap,
            InvalidLayout,
            InvalidEntry,
            TooManyPndt,
            ActiveCountMismatch,
            DuplicatePndt,
            CountOutOfRange,
        };

        struct AllFormsSnapshotStats
        {
            std::uint64_t tableAddress {0};
            std::uint64_t capacity {0};
            std::uint64_t freeCount {0};
            std::uint64_t lastFree {0};
            std::uint64_t activeExpected {0};
            std::uint64_t activeObserved {0};
            FormID invalidKey {0};
            FormID invalidFormId {0};
        };

        bool CapturePndtIdSnapshot(std::vector<FormID>& destination,
            AllFormsSnapshotStats& stats, const char* stage)
        {
            destination.clear();
            if (destination.capacity() < MaxPndtForms) {
                destination.reserve(MaxPndtForms);
            }

            GuardedAllFormsMap* guardedForms = nullptr;
            SnapshotFailure failure = SnapshotFailure::None;
            if (!g_allFormsMapAddress ||
                !IsReadableRange(g_allFormsMapAddress,
                    sizeof(guardedForms))) {
                failure = SnapshotFailure::MissingMap;
            } else {
                std::memcpy(&guardedForms,
                    reinterpret_cast<const void*>(g_allFormsMapAddress),
                    sizeof(guardedForms));
                if (!guardedForms || !IsReadableRange(
                        reinterpret_cast<std::uintptr_t>(guardedForms),
                        sizeof(GuardedAllFormsMap))) {
                    failure = SnapshotFailure::MissingMap;
                }
            }

            if (failure == SnapshotFailure::None) {
                auto guard = guardedForms->LockRead();
                const auto& forms = *guard;
                const auto mapAddress = reinterpret_cast<std::uintptr_t>(
                    std::addressof(forms));
                const bool headerRead =
                    ReadScalar(mapAddress + AllFormsTableOffset,
                        stats.tableAddress) &&
                    ReadScalar(mapAddress + AllFormsCapacityOffset,
                        stats.capacity) &&
                    ReadScalar(mapAddress + AllFormsFreeOffset,
                        stats.freeCount) &&
                    ReadScalar(mapAddress + AllFormsLastFreeOffset,
                        stats.lastFree);
                const bool layoutValid = headerRead &&
                    stats.tableAddress != 0 &&
                    stats.capacity >= MinAllFormsCapacity &&
                    stats.capacity <= MaxAllFormsCapacity &&
                    std::has_single_bit(stats.capacity) &&
                    stats.freeCount <= stats.capacity &&
                    stats.lastFree <= stats.capacity &&
                    stats.capacity <=
                        std::numeric_limits<std::size_t>::max() /
                        sizeof(AllFormsMap::entry_type) &&
                    IsReadableSpan(stats.tableAddress,
                        static_cast<std::size_t>(stats.capacity) *
                            sizeof(AllFormsMap::entry_type));
                if (!layoutValid) {
                    failure = SnapshotFailure::InvalidLayout;
                } else {
                    stats.activeExpected =
                        stats.capacity - stats.freeCount;
                    for (const auto& entry : forms) {
                        ++stats.activeObserved;
                        const auto* form = entry.value;
                        if (!form) {
                            stats.invalidKey = entry.key;
                            failure = SnapshotFailure::InvalidEntry;
                            break;
                        }
                        stats.invalidFormId = form->GetFormID();
                        if (entry.key == 0 ||
                            entry.key != stats.invalidFormId) {
                            stats.invalidKey = entry.key;
                            failure = SnapshotFailure::InvalidEntry;
                            break;
                        }
                        if (form->IsDeleted() ||
                            form->GetFormType() != RE::FormType::kPNDT) {
                            continue;
                        }
                        if (destination.size() == MaxPndtForms) {
                            failure = SnapshotFailure::TooManyPndt;
                            break;
                        }
                        destination.push_back(entry.key);
                    }
                    if (failure == SnapshotFailure::None &&
                        stats.activeObserved != stats.activeExpected) {
                        failure = SnapshotFailure::ActiveCountMismatch;
                    }
                }
            }

            if (failure == SnapshotFailure::None) {
                std::ranges::sort(destination);
                if (std::adjacent_find(destination.begin(),
                        destination.end()) != destination.end()) {
                    failure = SnapshotFailure::DuplicatePndt;
                } else if (destination.empty() ||
                    destination.size() > MaxPndtForms) {
                    failure = SnapshotFailure::CountOutOfRange;
                }
            }
            if (failure != SnapshotFailure::None) {
                REX::ERROR("[remote-native-probe] FAIL: guarded AllForms PNDT snapshot stage={} rejected reason={} table={:016X} capacity={} free={} last-free={} active-expected={} active-observed={} PNDT-copied={} invalid-key={:08X} invalid-form={:08X}",
                    stage, std::to_underlying(failure), stats.tableAddress,
                    stats.capacity, stats.freeCount, stats.lastFree,
                    stats.activeExpected, stats.activeObserved,
                    destination.size(), stats.invalidKey,
                    stats.invalidFormId);
                destination.clear();
                return false;
            }
            return true;
        }

        bool StartPndtEnumeration()
        {
            AllFormsSnapshotStats stats;
            if (!CapturePndtIdSnapshot(g_ancestry.ids, stats, "initial")) {
                return false;
            }
            const auto count = g_ancestry.ids.size();
            g_ancestry.bodies.clear();
            g_ancestry.bodies.reserve(count);
            g_ancestry.expectedForms = count;
            g_ancestry.cursor = 0;
            g_ancestry.failedRows = 0;
            g_ancestry.scan = {};
            g_ancestry.enumerationActive = true;
            g_ancestry.enumerationComplete = false;
            g_ancestry.analysisLogged = false;
            g_ancestry.failed = false;
            REX::INFO("[remote-native-probe] selected-only guarded AllForms PNDT snapshot target={:08X} rows={} all-forms-active={} capacity={} chunk={} lock-released=true",
                g_ancestry.target.id, count, stats.activeObserved,
                stats.capacity,
                PndtFormsPerDrain);
            return true;
        }

        bool ResolveExactSystemForm(FormID bodyId, FormID& starFormId)
        {
            starFormId = MissingFormId;
            return g_resolveBodySystemForm &&
                g_resolveBodySystemForm(&starFormId, bodyId) ==
                    std::addressof(starFormId);
        }

        bool LogDirectSelectedBody(const TargetCopy& target)
        {
            if (!IsLiveFormType(target.id, RE::FormType::kPNDT)) {
                REX::ERROR("[remote-native-probe] FAIL: selected planet/moon {:08X} is not a live PNDT",
                    target.id);
                return false;
            }
            FormID numeric = MissingFormId;
            ComponentCopy component;
            FormID star = MissingFormId;
            const bool numericRead = ResolveNumericWithCapture(target.id,
                numeric, &component, false);
            const bool starRead = ResolveExactSystemForm(target.id, star) &&
                IsLiveFormType(star, RE::FormType::kSTDT);
            FormID postStar = MissingFormId;
            const bool agreement = numericRead &&
                component.bodyId == target.id &&
                component.satellitePresent &&
                component.numericSystemId == numeric &&
                IsLiveFormType(target.id, RE::FormType::kPNDT) &&
                ResolveExactSystemForm(target.id, postStar) &&
                postStar == star;
            REX::INFO("[remote-native-probe] selected PNDT={:08X} kind={} STDT={:08X} numeric-wrapper={:08X} Satellite-system={:08X} agreement={} parent={:08X} planet={:08X}",
                target.id, target.kind, star, numeric,
                component.numericSystemId, agreement,
                component.parentOrdinal, component.planetOrdinal);
            if (!starRead || !agreement) {
                REX::ERROR("[remote-native-probe] FAIL: selected PNDT identity could not be revalidated exactly");
                return false;
            }
            g_ancestry.selectedStarFormId = star;
            g_ancestry.selectedNumericSystemId = numeric;
            g_ancestry.hasSelectedStarFormId = true;
            // Numeric zero is Sol and is deliberately represented by this
            // separate validity bit.
            g_ancestry.hasSelectedNumericSystemId = true;
            return true;
        }

        void BeginSelectionEvidence(const MapSessionIdentity& identity,
            const TargetCopy& target, const NativeMapState&)
        {
            if (!identity.IsValid() || target.id == 0) {
                ClearSelectionEnumeration();
                return;
            }
            if (RelevantSelectionMatches(identity, target)) {
                return;
            }

            const auto kind = static_cast<ObservedTargetKind>(target.kind);
            if (kind != ObservedTargetKind::Planet &&
                kind != ObservedTargetKind::Moon) {
                ClearSelectionEnumeration();
                return;
            }

            ClearSelectionEnumeration();
            g_ancestry.hasSelection = true;
            g_ancestry.selectionIdentity = identity;
            g_ancestry.target = target;
            if (kind == ObservedTargetKind::Planet ||
                kind == ObservedTargetKind::Moon) {
                if (!LogDirectSelectedBody(target)) {
                    g_ancestry.failed = true;
                    return;
                }
            }
            if (kind == ObservedTargetKind::Planet) {
                return;
            }

            if (!g_ancestry.hasSelectedStarFormId ||
                !IsLiveFormType(g_ancestry.selectedStarFormId,
                    RE::FormType::kSTDT)) {
                g_ancestry.failed = true;
                REX::ERROR("[remote-native-probe] FAIL: selected target lacks a live STDT before PNDT enumeration");
                return;
            }
            if (!StartPndtEnumeration()) {
                g_ancestry.failed = true;
            }
        }

        void AddPndtFailureSample(PndtFailureReason reason, FormID bodyId,
            FormID starFormId, FormID value1 = 0, FormID value2 = 0)
        {
            auto& scan = g_ancestry.scan;
            if (scan.sampleCount == scan.samples.size()) {
                return;
            }
            scan.samples[scan.sampleCount++] = {
                .reason = reason,
                .bodyId = bodyId,
                .starFormId = starFormId,
                .value1 = value1,
                .value2 = value2,
            };
        }

        void RejectPndtRow(PndtFailureReason reason, FormID bodyId,
            FormID starFormId, FormID value1 = 0, FormID value2 = 0)
        {
            ++g_ancestry.failedRows;
            AddPndtFailureSample(reason, bodyId, starFormId, value1, value2);
        }

        void LogPndtScanPartition(bool stableSnapshot, bool success)
        {
            const auto& scan = g_ancestry.scan;
            REX::INFO("[remote-native-probe] guarded AllForms PNDT processing partition success={} rows={} processed={} unrooted={} unrelated={} selected-STDT={} classification-failed={} selected-candidates={} selected-rejected={} TLS-captured={} Satellite-present={} Satellite-absent={} selected-CT-absent={} selected-CT-present={} selected-CT-invalid={} selected-CT-empty={} selected-CT-text-rows={} selected-station-CT-exact={} global-station-TLS={} global-CT-absent={} global-CT-present={} global-CT-invalid={} global-CT-empty={} global-CT-text-rows={} global-station-CT-exact={} full-identity-retained={} numeric-disagreement={} row-failures={} stable-resnapshot={} samples={}",
                success, g_ancestry.expectedForms, scan.processed,
                scan.unrooted, scan.unrelated, scan.selectedSystem,
                scan.classificationFailed, scan.selectedCandidates,
                scan.selectedRejected,
                scan.tlsCaptured,
                scan.satellitePresent, scan.satelliteAbsent,
                scan.cellDataAbsent, scan.cellDataPresent,
                scan.cellDataInvalid, scan.cellDataEmpty,
                scan.cellDataTextRows,
                scan.stationCtExact,
                scan.stationGlobalTlsCaptured,
                scan.stationGlobalCtAbsent,
                scan.stationGlobalCtPresent,
                scan.stationGlobalCtInvalid,
                scan.stationGlobalCtEmpty,
                scan.stationGlobalCtTextRows,
                scan.stationGlobalCtExact,
                scan.fullIdentityRetained, scan.numericDisagreement,
                g_ancestry.failedRows, stableSnapshot,
                scan.sampleCount);
            for (std::size_t index = 0; index < scan.sampleCount; ++index) {
                const auto& sample = scan.samples[index];
                REX::ERROR("[remote-native-probe] PNDT failure sample[{}] reason={} body={:08X} STDT={:08X} value1={:08X} value2={:08X}",
                    index, std::to_underlying(sample.reason),
                    sample.bodyId, sample.starFormId,
                    sample.value1, sample.value2);
            }
            if (g_ancestry.station) {
                for (std::size_t index = 0;
                     index < scan.cellDataTextSampleCount; ++index) {
                    const auto& sample = scan.cellDataTextSamples[index];
                    REX::INFO("[remote-native-probe] global station CT text sample[{}] body={:08X} numeric-read={} numeric-wrapper={:08X} Satellite-present={} Satellite-system={:08X} parent={:08X} planet={:08X} editor='{}'",
                        index, sample.bodyId, sample.numericRead,
                        sample.numericWrapperId,
                        sample.satellitePresent, sample.numericSystemId,
                        sample.parentOrdinal, sample.planetOrdinal,
                        sample.editorId.data());
                }
                if (scan.stationGlobalCtTextRows >
                    scan.cellDataTextSampleCount) {
                    REX::INFO("[remote-native-probe] global station CT text samples capped logged={} total={}",
                        scan.cellDataTextSampleCount,
                        scan.stationGlobalCtTextRows);
                }
                if (scan.hasStationGlobalCtExactSample) {
                    const auto& exact = scan.stationGlobalCtExactSample;
                    REX::INFO("[remote-native-probe] global station CT exact candidate body={:08X} numeric-read={} numeric-wrapper={:08X} Satellite-present={} Satellite-system={:08X} parent={:08X} planet={:08X} editor='{}'",
                        exact.bodyId, exact.numericRead,
                        exact.numericWrapperId,
                        exact.satellitePresent, exact.numericSystemId,
                        exact.parentOrdinal, exact.planetOrdinal,
                        exact.editorId.data());
                }
            }
        }

        void AdvancePndtEnumeration()
        {
            if (!g_ancestry.enumerationActive ||
                g_ancestry.enumerationComplete || g_ancestry.failed) {
                return;
            }
            if (g_ancestry.ids.size() !=
                g_ancestry.expectedForms) {
                g_ancestry.failed = true;
                REX::ERROR("[remote-native-probe] FAIL: bounded PNDT ID snapshot size changed internally");
                return;
            }
            const auto remaining = g_ancestry.expectedForms -
                g_ancestry.cursor;
            const auto copied = std::min<std::size_t>(
                remaining, PndtFormsPerDrain);
            for (std::size_t index = 0; index < copied; ++index) {
                const auto bodyId = g_ancestry.ids[
                    g_ancestry.cursor + index];
                auto& scan = g_ancestry.scan;
                ++scan.processed;
                if (!IsLiveFormType(bodyId, RE::FormType::kPNDT)) {
                    ++scan.classificationFailed;
                    RejectPndtRow(PndtFailureReason::PreCaptureStale,
                        bodyId, 0);
                    continue;
                }

                FormID numeric = MissingFormId;
                ComponentCopy component;
                bool numericRead = false;
                bool captured = false;

                FormID star = MissingFormId;
                if (!ResolveExactSystemForm(bodyId, star)) {
                    ++scan.classificationFailed;
                    RejectPndtRow(
                        PndtFailureReason::SystemResolverFailure,
                        bodyId, star);
                    continue;
                }

                if (star != 0 &&
                    !IsLiveFormType(star, RE::FormType::kSTDT)) {
                    ++scan.classificationFailed;
                    RejectPndtRow(PndtFailureReason::InvalidSystemForm,
                        bodyId, star);
                    continue;
                }

                if (star == 0 || star != g_ancestry.selectedStarFormId) {
                    FormID postStar = MissingFormId;
                    const bool stable =
                        IsLiveFormType(bodyId, RE::FormType::kPNDT) &&
                        ResolveExactSystemForm(bodyId, postStar) &&
                        postStar == star &&
                        (star == 0 || IsLiveFormType(
                            star, RE::FormType::kSTDT));
                    if (!stable) {
                        ++scan.classificationFailed;
                        RejectPndtRow(PndtFailureReason::SystemDrift,
                            bodyId, star, postStar);
                    } else if (star == 0) {
                        ++scan.unrooted;
                    } else {
                        ++scan.unrelated;
                    }
                    continue;
                }

                ++scan.selectedSystem;
                numericRead = ResolveNumericWithCapture(bodyId,
                    numeric, &component, false);
                captured = component.bodyId == bodyId;
                if (captured) {
                    ++scan.tlsCaptured;
                    if (component.satellitePresent) {
                        ++scan.satellitePresent;
                    } else {
                        ++scan.satelliteAbsent;
                    }
                    switch (component.cellDataState) {
                    case CellDataCaptureState::Absent:
                        ++scan.cellDataAbsent;
                        break;
                    case CellDataCaptureState::Present:
                        ++scan.cellDataPresent;
                        if (component.cellEditorId[0] == '\0') {
                            ++scan.cellDataEmpty;
                        } else {
                            ++scan.cellDataTextRows;
                        }
                        break;
                    case CellDataCaptureState::Invalid:
                        ++scan.cellDataInvalid;
                        break;
                    }
                }

                FormID postStar = MissingFormId;
                const bool stable =
                    IsLiveFormType(bodyId, RE::FormType::kPNDT) &&
                    ResolveExactSystemForm(bodyId, postStar) &&
                    postStar == star &&
                    IsLiveFormType(star, RE::FormType::kSTDT);
                std::optional<PndtFailureReason> rejection;
                if (!captured) {
                    rejection = PndtFailureReason::TlsCaptureFailure;
                } else if (!stable) {
                    rejection = PndtFailureReason::SystemDrift;
                } else if (!component.satellitePresent) {
                    // ID124608 has already placed this PNDT in the selected
                    // BodyChild/STDT graph.  Without SatelliteCSVData we
                    // cannot prove that it is not an allowed parent, even
                    // when it is not the station CT match.
                    rejection = PndtFailureReason::SatelliteMissing;
                } else if (!numericRead) {
                    rejection = PndtFailureReason::NumericWrapperFailure;
                } else if (component.numericSystemId != numeric) {
                    rejection =
                        PndtFailureReason::WrapperSatelliteDisagreement;
                } else if (g_ancestry.hasSelectedNumericSystemId &&
                    numeric != g_ancestry.selectedNumericSystemId) {
                    rejection =
                        PndtFailureReason::SelectedNumericDisagreement;
                } else if (g_ancestry.bodies.size() == MaxPndtForms) {
                    rejection = PndtFailureReason::RetainedCapacity;
                }
                if (rejection) {
                    ++scan.selectedRejected;
                    RejectPndtRow(*rejection, bodyId, star, numeric,
                        captured ? component.numericSystemId : postStar);
                    continue;
                }
                ++scan.selectedCandidates;
                g_ancestry.bodies.push_back({
                    .starFormId = star,
                    .component = component,
                });
            }
            g_ancestry.cursor += copied;
            if (g_ancestry.cursor != g_ancestry.expectedForms) {
                return;
            }

            g_ancestry.enumerationActive = false;
            bool fatal = g_ancestry.failedRows != 0;

            std::vector<FormID> finalIds;
            AllFormsSnapshotStats finalStats;
            bool stableSnapshot = CapturePndtIdSnapshot(
                finalIds, finalStats, "completion") &&
                finalIds == g_ancestry.ids;
            if (!stableSnapshot) {
                FormID initialValue = 0;
                FormID finalValue = 0;
                const auto common = std::min(finalIds.size(),
                    g_ancestry.ids.size());
                std::size_t mismatch = 0;
                while (mismatch < common &&
                    finalIds[mismatch] == g_ancestry.ids[mismatch]) {
                    ++mismatch;
                }
                if (mismatch < g_ancestry.ids.size()) {
                    initialValue = g_ancestry.ids[mismatch];
                }
                if (mismatch < finalIds.size()) {
                    finalValue = finalIds[mismatch];
                }
                AddPndtFailureSample(PndtFailureReason::SnapshotDrift, 0, 0,
                    initialValue, finalValue);
                REX::ERROR("[remote-native-probe] FAIL: guarded AllForms PNDT snapshot drift initial-rows={} final-rows={} first-mismatch={} initial={:08X} final={:08X}",
                    g_ancestry.ids.size(), finalIds.size(), mismatch,
                    initialValue, finalValue);
                fatal = true;
            }

            auto& scan = g_ancestry.scan;
            const auto classified = scan.unrooted + scan.unrelated +
                scan.selectedSystem + scan.classificationFailed;
            const auto selectedPartition = scan.selectedCandidates +
                scan.selectedRejected;
            const bool countersExact = scan.processed ==
                    g_ancestry.expectedForms &&
                classified == scan.processed &&
                selectedPartition == scan.selectedSystem &&
                g_ancestry.failedRows == scan.classificationFailed +
                    scan.selectedRejected &&
                g_ancestry.bodies.size() == scan.selectedCandidates;
            if (!countersExact) {
                AddPndtFailureSample(PndtFailureReason::CounterMismatch,
                    0, 0, static_cast<FormID>(classified),
                    static_cast<FormID>(selectedPartition));
                fatal = true;
            }

            if (g_ancestry.station) {
                const BodyFacts* orbital = nullptr;
                std::size_t orbitalCount = 0;
                for (const auto& facts : g_ancestry.bodies) {
                    if (facts.component.satellitePresent &&
                        facts.component.bodyId ==
                            g_ancestry.station->nativeOrbitalPndtId &&
                        facts.starFormId ==
                            g_ancestry.station->displayedSystemFormId) {
                        ++orbitalCount;
                        orbital = std::addressof(facts);
                    }
                }
                if (orbitalCount != 1 || !orbital) {
                    AddPndtFailureSample(
                        PndtFailureReason::StationNativeOrbitalMissing,
                        g_ancestry.station->nativeOrbitalPndtId,
                        g_ancestry.selectedStarFormId,
                        static_cast<FormID>(orbitalCount));
                    fatal = true;
                } else {
                    const auto ordinalMatches = std::count_if(
                        g_ancestry.bodies.begin(),
                        g_ancestry.bodies.end(),
                        [&](const BodyFacts& facts) {
                            return facts.component.satellitePresent &&
                                facts.component.numericSystemId ==
                                    g_ancestry.selectedNumericSystemId &&
                                facts.component.planetOrdinal ==
                                    orbital->component.planetOrdinal;
                        });
                    FormID nativeReverse = MissingFormId;
                    const auto* nativeReturn =
                        g_resolveBodyBySystemOrdinal ?
                        g_resolveBodyBySystemOrdinal(
                            std::addressof(nativeReverse),
                            g_ancestry.selectedNumericSystemId,
                            orbital->component.planetOrdinal) : nullptr;
                    const bool reverseExact = ordinalMatches == 1 &&
                        nativeReturn == std::addressof(nativeReverse) &&
                        nativeReverse == orbital->component.bodyId;
                    REX::INFO("[remote-native-probe] station native orbital exact={} source={} map={:08X} physical={:08X} XMRK={:08X} orbital-PNDT={:08X} secondary={:08X} STDT={:08X} numeric={:08X} parent={:08X} planet={:08X} unique-AllForms={} native-reverse={:08X} native-reverse-exact={}",
                        reverseExact,
                        g_ancestry.station->nativeOrbitalFromCourseMarker ?
                            "course-XMRK" : "physical-REFR",
                        g_ancestry.station->mapId,
                        g_ancestry.station->targetRefId,
                        g_ancestry.station->courseMarkerId,
                        orbital->component.bodyId,
                        g_ancestry.station->nativeSecondaryId,
                        orbital->starFormId,
                        orbital->component.numericSystemId,
                        orbital->component.parentOrdinal,
                        orbital->component.planetOrdinal,
                        ordinalMatches == 1, nativeReverse,
                        reverseExact);
                    if (!reverseExact) {
                        AddPndtFailureSample(
                            PndtFailureReason::
                                StationNativeReverseDisagreement,
                            orbital->component.bodyId,
                            orbital->starFormId,
                            static_cast<FormID>(ordinalMatches),
                            nativeReverse);
                        fatal = true;
                    }
                }
            }

            if (static_cast<ObservedTargetKind>(g_ancestry.target.kind) ==
                ObservedTargetKind::Moon) {
                const auto retainedTargetCount = std::count_if(
                    g_ancestry.bodies.begin(), g_ancestry.bodies.end(),
                    [](const BodyFacts& facts) {
                        return facts.component.satellitePresent &&
                            facts.component.bodyId == g_ancestry.target.id &&
                            facts.starFormId ==
                                g_ancestry.selectedStarFormId;
                    });
                if (retainedTargetCount != 1) {
                    AddPndtFailureSample(
                        PndtFailureReason::SelectedTargetMissing,
                        g_ancestry.target.id,
                        g_ancestry.selectedStarFormId,
                        static_cast<FormID>(retainedTargetCount));
                    fatal = true;
                }
            }

            if (g_ancestry.hasSelectedNumericSystemId) {
                for (const auto& facts : g_ancestry.bodies) {
                    if (facts.starFormId ==
                            g_ancestry.selectedStarFormId &&
                        facts.component.satellitePresent &&
                        facts.component.numericSystemId ==
                            g_ancestry.selectedNumericSystemId) {
                        ++scan.fullIdentityRetained;
                    } else {
                        ++scan.numericDisagreement;
                        AddPndtFailureSample(
                            PndtFailureReason::StationNumericDisagreement,
                            facts.component.bodyId, facts.starFormId,
                            g_ancestry.selectedNumericSystemId,
                            facts.component.numericSystemId);
                    }
                }
                if (scan.numericDisagreement != 0) {
                    fatal = true;
                }
            } else {
                fatal = true;
            }

            LogPndtScanPartition(stableSnapshot, !fatal);
            if (fatal) {
                g_ancestry.failed = true;
                REX::ERROR("[remote-native-probe] FAIL: guarded AllForms PNDT processing rejected; ancestry evidence invalid");
                return;
            }
            g_ancestry.enumerationComplete = true;
            REX::INFO("[remote-native-probe] guarded AllForms PNDT processing complete selected-full-identity-rows={} failed=0 stable-resnapshot=true; no disk/plugin reads used",
                scan.fullIdentityRetained);
        }

        const BodyFacts* FindBodyFacts(FormID bodyId) noexcept
        {
            const auto found = std::find_if(g_ancestry.bodies.begin(),
                g_ancestry.bodies.end(), [bodyId](const BodyFacts& facts) {
                    return facts.component.satellitePresent &&
                        facts.component.bodyId == bodyId;
                });
            return found == g_ancestry.bodies.end() ? nullptr :
                std::addressof(*found);
        }

        const BodyFacts* FindStationOrbitalBody(
            const StationTuple& station)
        {
            const BodyFacts* unique = nullptr;
            std::size_t count = 0;
            std::array<FormID, MaxLoggedCandidates> candidates {};
            for (const auto& facts : g_ancestry.bodies) {
                const auto& component = facts.component;
                if (!component.satellitePresent ||
                    facts.starFormId != station.displayedSystemFormId ||
                    component.bodyId != station.nativeOrbitalPndtId) {
                    continue;
                }
                if (count < candidates.size()) {
                    candidates[count] = component.bodyId;
                }
                ++count;
                unique = std::addressof(facts);
            }
            if (count == 1) {
                REX::INFO("[remote-native-probe] station orbital PNDT selected from native ref resolver source={} map={:08X} CELL-editor='{}' STDT={:08X} orbital-PNDT={:08X} numeric={:08X} parent={:08X} planet={:08X}",
                    station.nativeOrbitalFromCourseMarker ?
                        "course-XMRK" : "physical-REFR",
                    station.mapId, station.cellEditorId.data(),
                    station.displayedSystemFormId,
                    unique->component.bodyId,
                    unique->component.numericSystemId,
                    unique->component.parentOrdinal,
                    unique->component.planetOrdinal);
                return unique;
            }

            REX::ERROR("[remote-native-probe] FAIL: station native orbital PNDT match count={} map={:08X} expected-PNDT={:08X} STDT={:08X} (candidate log capped at {})",
                count, station.mapId, station.nativeOrbitalPndtId,
                station.displayedSystemFormId, candidates.size());
            for (std::size_t index = 0;
                 index < std::min(count, candidates.size()); ++index) {
                REX::ERROR("[remote-native-probe] station orbital ambiguous candidate[{}]={:08X}",
                    index, candidates[index]);
            }
            return nullptr;
        }

        void LogOrderedAncestry(const BodyFacts& initial)
        {
            if (!initial.component.satellitePresent) {
                REX::ERROR("[remote-native-probe] FAIL: PNDT ancestry initial row {:08X} lacks Satellite",
                    initial.component.bodyId);
                return;
            }
            const BodyFacts* child = std::addressof(initial);
            std::array<FormID, MaxAncestryDepth + 1> visited {};
            std::size_t visitedCount = 0;
            visited[visitedCount++] = child->component.bodyId;

            const auto wrongOrdinalCollision = std::count_if(
                g_ancestry.bodies.begin(), g_ancestry.bodies.end(),
                [&initial](const BodyFacts& candidate) {
                    return candidate.component.satellitePresent &&
                        candidate.starFormId == initial.starFormId &&
                        candidate.component.numericSystemId ==
                            initial.component.numericSystemId &&
                        candidate.component.planetOrdinal ==
                            WrongOrdinalControl;
                });
            FormID wrongOrdinalResult = MissingFormId;
            const auto* wrongOrdinalReturn = g_resolveBodyBySystemOrdinal ?
                g_resolveBodyBySystemOrdinal(&wrongOrdinalResult,
                    initial.component.numericSystemId,
                    WrongOrdinalControl) : nullptr;
            if (wrongOrdinalCollision != 0 ||
                wrongOrdinalReturn != std::addressof(wrongOrdinalResult) ||
                wrongOrdinalResult != 0) {
                REX::ERROR("[remote-native-probe] FAIL: native body reverse-resolver wrong-ordinal control numeric={:08X} ordinal={:08X} snapshot-candidates={} result={:08X}",
                    initial.component.numericSystemId,
                    WrongOrdinalControl, wrongOrdinalCollision,
                    wrongOrdinalResult);
                return;
            }
            REX::INFO("[remote-native-probe] native body reverse-resolver wrong-ordinal control numeric={:08X} ordinal={:08X} result=00000000 exact=true",
                initial.component.numericSystemId, WrongOrdinalControl);

            for (std::size_t depth = 0; depth < MaxAncestryDepth; ++depth) {
                FormID freshChildNumeric = MissingFormId;
                FormID freshChildStar = MissingFormId;
                ComponentCopy freshChildComponent;
                const bool freshChildNumericRead = ResolveNumericWithCapture(
                    child->component.bodyId, freshChildNumeric,
                    &freshChildComponent, false);
                const bool freshChildStarRead = ResolveExactSystemForm(
                        child->component.bodyId, freshChildStar) &&
                    IsLiveFormType(freshChildStar, RE::FormType::kSTDT);
                const bool freshChildExact =
                    IsLiveFormType(child->component.bodyId,
                        RE::FormType::kPNDT) &&
                    freshChildNumericRead && freshChildStarRead &&
                    freshChildComponent.satellitePresent &&
                    freshChildNumeric == child->component.numericSystemId &&
                    freshChildStar == child->starFormId &&
                    SameComponent(freshChildComponent, child->component);
                if (!freshChildExact) {
                    REX::ERROR("[remote-native-probe] FAIL: PNDT ancestry child changed after guarded AllForms snapshot child={:08X} snapshot-STDT={:08X} fresh-STDT={:08X} snapshot-numeric={:08X} fresh-numeric={:08X} snapshot-parent={:08X} fresh-parent={:08X} snapshot-planet={:08X} fresh-planet={:08X}",
                        child->component.bodyId, child->starFormId,
                        freshChildStar,
                        child->component.numericSystemId,
                        freshChildNumeric,
                        child->component.parentOrdinal,
                        freshChildComponent.parentOrdinal,
                        child->component.planetOrdinal,
                        freshChildComponent.planetOrdinal);
                    return;
                }
                const auto parentOrdinal =
                    freshChildComponent.parentOrdinal;
                if (parentOrdinal == 0) {
                    REX::INFO("[remote-native-probe] ancestry complete target-PNDT={:08X} root-PNDT={:08X} allowed-waypoints={} bounded=true",
                        initial.component.bodyId,
                        child->component.bodyId, depth);
                    return;
                }

                const BodyFacts* unique = nullptr;
                std::size_t count = 0;
                std::array<FormID, MaxLoggedCandidates> candidates {};
                for (const auto& candidate : g_ancestry.bodies) {
                    if (!candidate.component.satellitePresent ||
                        candidate.component.bodyId ==
                            child->component.bodyId ||
                        candidate.starFormId != child->starFormId ||
                        candidate.component.numericSystemId !=
                            child->component.numericSystemId ||
                        candidate.component.planetOrdinal != parentOrdinal) {
                        continue;
                    }
                    if (count < candidates.size()) {
                        candidates[count] = candidate.component.bodyId;
                    }
                    ++count;
                    unique = std::addressof(candidate);
                }
                if (count != 1 || !unique) {
                    REX::ERROR("[remote-native-probe] FAIL: PNDT parent ambiguity child={:08X} numeric={:08X} parent-ordinal={:08X} candidates={} (log capped at {})",
                        child->component.bodyId,
                        child->component.numericSystemId,
                        parentOrdinal, count, candidates.size());
                    for (std::size_t index = 0;
                         index < std::min(count, candidates.size()); ++index) {
                        REX::ERROR("[remote-native-probe] parent candidate[{}]={:08X}",
                            index, candidates[index]);
                    }
                    return;
                }

                FormID nativeParent = MissingFormId;
                const auto* nativeReturn = g_resolveBodyBySystemOrdinal ?
                    g_resolveBodyBySystemOrdinal(&nativeParent,
                        child->component.numericSystemId,
                        parentOrdinal) : nullptr;
                FormID freshNumeric = MissingFormId;
                FormID freshStar = MissingFormId;
                ComponentCopy freshComponent;
                const bool freshNumericRead = nativeParent != 0 &&
                    nativeParent != MissingFormId &&
                    ResolveNumericWithCapture(nativeParent, freshNumeric,
                        &freshComponent, false);
                const bool freshStarRead = nativeParent != 0 &&
                    nativeParent != MissingFormId &&
                    ResolveExactSystemForm(nativeParent, freshStar) &&
                    IsLiveFormType(freshStar, RE::FormType::kSTDT);
                const bool nativeExact =
                    nativeReturn == std::addressof(nativeParent) &&
                    nativeParent == unique->component.bodyId &&
                    IsLiveFormType(nativeParent, RE::FormType::kPNDT) &&
                    freshNumericRead && freshStarRead &&
                    freshComponent.satellitePresent &&
                    freshNumeric == unique->component.numericSystemId &&
                    freshStar == unique->starFormId &&
                    SameComponent(freshComponent, unique->component) &&
                    freshComponent.planetOrdinal == parentOrdinal;
                if (!nativeExact) {
                    REX::ERROR("[remote-native-probe] FAIL: native body reverse-resolver disagreement child={:08X} numeric={:08X} requested-ordinal={:08X} unique-AllForms={:08X} native={:08X} fresh-STDT={:08X} fresh-numeric={:08X} fresh-parent={:08X} fresh-planet={:08X}",
                        child->component.bodyId,
                        child->component.numericSystemId,
                        parentOrdinal, unique->component.bodyId,
                        nativeParent, freshStar, freshNumeric,
                        freshComponent.parentOrdinal,
                        freshComponent.planetOrdinal);
                    return;
                }
                if (std::find(visited.begin(),
                        visited.begin() + visitedCount,
                        unique->component.bodyId) !=
                    visited.begin() + visitedCount) {
                    REX::ERROR("[remote-native-probe] FAIL: PNDT ancestry cycle at {:08X}",
                        unique->component.bodyId);
                    return;
                }
                visited[visitedCount++] = unique->component.bodyId;
                REX::INFO("[remote-native-probe] allowed-waypoint nearest-parent-first-order={} PNDT={:08X} child={:08X} STDT={:08X} numeric={:08X} planet-ordinal={:08X} unique-AllForms=true native-reverse-exact=true",
                    depth, unique->component.bodyId,
                    child->component.bodyId,
                    unique->starFormId,
                    unique->component.numericSystemId,
                    unique->component.planetOrdinal);
                child = unique;
            }
            REX::ERROR("[remote-native-probe] FAIL: PNDT ancestry exceeded bounded depth {}",
                MaxAncestryDepth);
        }

        void AnalyzeSelectedAncestry()
        {
            if (!g_ancestry.enumerationComplete ||
                g_ancestry.analysisLogged || g_ancestry.failed) {
                return;
            }
            g_ancestry.analysisLogged = true;
            const BodyFacts* selected = nullptr;
            const auto kind = static_cast<ObservedTargetKind>(
                g_ancestry.target.kind);
            if (kind == ObservedTargetKind::Moon) {
                selected = FindBodyFacts(g_ancestry.target.id);
                if (!selected) {
                    REX::ERROR("[remote-native-probe] FAIL: selected moon {:08X} absent from bounded PNDT facts",
                        g_ancestry.target.id);
                    return;
                }
            } else if (kind == ObservedTargetKind::Station &&
                g_ancestry.station) {
                selected = FindStationOrbitalBody(*g_ancestry.station);
                if (!selected) {
                    return;
                }
            }
            if (!selected) {
                return;
            }
            LogOrderedAncestry(*selected);
        }

        void ClearPendingRouteCorrelation() noexcept
        {
            g_state.pendingSetRouteIdentity = {};
            g_state.pendingSetRouteExpected = {};
            g_state.pendingPreexistingEndpoint = 0;
            g_state.pendingSetRoute = false;
            g_state.pendingRouteSampleNeeded = false;
            g_state.provenRouteEndpoint = 0;
            g_state.provenRouteEndpointSystem = {};
            g_state.routeEndpointProven = false;
            g_state.executeReadySince = {};
            g_state.executeReadyDwellProven = false;
            g_state.focusSuspended = false;
            g_state.focusLostAt = {};
            g_state.executeSequence = 0;
            g_state.executeObservedAt = {};
            g_state.awaitingExecuteClose = false;
        }

        void ClearCommittedArrival() noexcept
        {
            g_state.committedDestination = {};
            g_state.routeCommitSequence = 0;
            g_state.pendingArrival = false;
            g_state.completedJumpAfterCommit = false;
            g_state.arrivalLogged = false;
        }

        void ResetEvidenceState() noexcept
        {
            g_state = ProbeState {};
            ClearSelectionEnumeration();
            g_ancestry.baselines.fill({});
            g_ancestry.baselineCount = 0;
        }

        void LogTargetObservation(const char* source,
            const Observation& observation, bool current)
        {
            const auto& target = observation.target;
            REX::INFO("[remote-native-probe] {} seq={} session={}/{} current={} count={} kind={} map/body={:08X} resolved-ref={:08X} course={:08X} displayed-STDT={} {:08X} label='{}' (label is diagnostic only, never identity)",
                source, observation.sequence,
                observation.identity.session,
                observation.identity.generation, current,
                observation.count, target.kind, target.id,
                target.resolvedTargetId, target.resolvedCourseId,
                target.hasDisplayedSystemFormId,
                target.displayedSystemFormId, target.name.data());
        }

        void LogHudObservation(const Observation& observation,
            std::uint32_t activeGeneration)
        {
            const bool generationFresh =
                observation.generation == activeGeneration;
            const bool travelFresh =
                observation.sequence > g_state.lastTravelSequence;
            REX::INFO("[remote-native-probe] HUD publication-seq={} previous-HUD-seq={} last-travel-seq={} generation={} active-generation={} rows={} overflow={} generation-fresh={} travel-fresh={}",
                observation.sequence, g_state.lastHudSequence,
                g_state.lastTravelSequence, observation.generation,
                activeGeneration, observation.count,
                observation.overflowed, generationFresh, travelFresh);
            const auto count = std::min<std::size_t>(
                observation.count, observation.hudRows.size());
            for (std::size_t index = 0; index < count; ++index) {
                REX::INFO("[remote-native-probe] HUD row index={} course={:08X} locked={}",
                    index, observation.hudRows[index].courseId,
                    observation.hudRows[index].locked);
            }
            g_state.lastHudSequence = observation.sequence;
            if (generationFresh) {
                g_state.lastHudRows = observation.hudRows;
                g_state.lastHudRowCount = count;
                g_state.lastHudOverflowed = observation.overflowed;
                g_state.hasHudSnapshot = true;
            }
        }

        void LogNativeMapTransition(const NativeMapState& map)
        {
            REX::INFO("[remote-native-probe] native-map open={} stable={} layout={} view={} displayed-STDT={:08X} display-root={:08X} selected={:08X} quick-select={} route-readable={} alternate={} points={} endpoint={:08X} endpoint-STDT={}",
                map.menuOpen, map.stable, map.layoutValid,
                NativeViewName(map.view), map.displayedSystem,
                map.displayRoot, map.selectedIdentity,
                map.quickSelectOpen, map.routeReadable,
                map.routeAlternate, map.routePointCount,
                map.routeEndpoint, map.routeEndpointIsStdt);
            if (g_state.pendingSetRoute && map.stable &&
                map.routeReadable && map.routeEndpoint != 0 &&
                map.routeEndpoint != g_state.pendingPreexistingEndpoint) {
                const auto* endpointForm =
                    RE::TESForm::LookupByID(map.routeEndpoint);
                const bool live = endpointForm &&
                    !endpointForm->IsDeleted() &&
                    endpointForm->GetFormID() == map.routeEndpoint;
                const auto formType = live ?
                    static_cast<std::uint32_t>(
                        endpointForm->GetFormType()) :
                    std::numeric_limits<std::uint32_t>::max();
                SystemIdentity endpointSystem;
                const bool identityRead = live &&
                    ResolveSystemIdentity(map.routeEndpoint,
                        endpointSystem);
                const bool systemMatch = identityRead &&
                    g_state.pendingSetRouteExpected.valid &&
                    endpointSystem == g_state.pendingSetRouteExpected;
                REX::INFO("[remote-native-probe] route endpoint live classification endpoint={:08X} live={} form-type={} resolved-STDT={:08X} resolved-numeric={:08X} identity-exact={} selected-STDT={:08X} selected-numeric={:08X} selected-system-match={}",
                    map.routeEndpoint, live, formType,
                    endpointSystem.starFormId, endpointSystem.numericId,
                    identityRead,
                    g_state.pendingSetRouteExpected.starFormId,
                    g_state.pendingSetRouteExpected.numericId,
                    systemMatch);
            }
            if (g_state.pendingSetRoute) {
                const bool changed = map.routeEndpoint !=
                    g_state.pendingPreexistingEndpoint;
                SystemIdentity endpointSystem;
                const bool endpointIdentityExact =
                    map.routeEndpoint != 0 &&
                    ResolveSystemIdentity(map.routeEndpoint,
                        endpointSystem);
                const bool exact = changed &&
                    endpointIdentityExact &&
                    g_state.pendingSetRouteExpected.valid &&
                    endpointSystem == g_state.pendingSetRouteExpected;
                REX::INFO("[remote-native-probe] SetRouteDestination correlation seq={} source-session={}/{} expected-STDT={:08X} expected-numeric={:08X} preexisting-endpoint={:08X} observed-endpoint={:08X} observed-STDT={:08X} observed-numeric={:08X} changed={} exact-new-system-endpoint={} stable-readable={}",
                    g_state.lastSetRouteSequence,
                    g_state.pendingSetRouteIdentity.session,
                    g_state.pendingSetRouteIdentity.generation,
                    g_state.pendingSetRouteExpected.starFormId,
                    g_state.pendingSetRouteExpected.numericId,
                    g_state.pendingPreexistingEndpoint,
                    map.routeEndpoint,
                    endpointSystem.starFormId,
                    endpointSystem.numericId,
                    changed, exact,
                    map.stable && map.routeReadable);
            }
        }
    }

    void Drain(const MapSessionIdentity& activeMapIdentity,
        std::uint32_t hudGeneration) noexcept
    {
        try {
            if (!g_initialized) {
                return;
            }
            TakeInbox(g_drainBatch);
            const auto& batch = g_drainBatch;
            if (g_producerFaulted.exchange(false,
                    std::memory_order_acq_rel)) {
                ResetEvidenceState();
                REX::ERROR("[remote-native-probe] FAIL: a callback/event producer faulted; discarded the bounded inbox and restarted evidence state");
                return;
            }
            if (batch.overflowed) {
                ResetEvidenceState();
                REX::ERROR("[remote-native-probe] FAIL: bounded observation inbox overflowed; discarded the batch and restarted evidence state");
                return;
            }

            for (std::size_t index = 0; index < batch.count; ++index) {
                const auto& observation = batch.entries[index];
                const bool currentIdentity =
                    observation.identity == activeMapIdentity;
                switch (observation.kind) {
                case ObservationKind::Movie:
                    REX::INFO("[remote-native-probe] movie-created seq={} menu='{}' born-ticks={}",
                        observation.sequence, observation.text.data(),
                        observation.ticks);
                    if (std::strcmp(observation.text.data(),
                            MapMenuName) == 0 &&
                        observation.identity.generation != 0) {
                        if (g_state.pendingSetRoute &&
                            g_state.pendingSetRouteIdentity.generation !=
                                observation.identity.generation) {
                            REX::WARN("[remote-native-probe] route-input correlation cleared on map movie replacement source-generation={} new-generation={}",
                                g_state.pendingSetRouteIdentity.generation,
                                observation.identity.generation);
                            ClearPendingRouteCorrelation();
                        }
                        g_state.observedMapMovieGeneration =
                            observation.identity.generation;
                        g_state.observedMapIdentity = {};
                        g_state.hasSelectedTarget = false;
                        g_state.selectedFromResolvedMarker = false;
                        ClearSelectionEnumeration();
                    }
                    break;
                case ObservationKind::Menu:
                    REX::INFO("[remote-native-probe] menu-lifecycle seq={} menu='{}' opening={} active-session={}/{}",
                        observation.sequence, observation.text.data(),
                        observation.flag1, activeMapIdentity.session,
                        activeMapIdentity.generation);
                    if (std::strcmp(observation.text.data(),
                            LoadingMenuName) == 0) {
                        g_state.lastTravelSequence = observation.sequence;
                        g_state.forceSystemPairPoll = true;
                        g_state.loadingOpen = observation.flag1;
                        g_state.lastUnsettledAt = Clock::now();
                    }
                    if (std::strcmp(observation.text.data(), MapMenuName) ==
                        0) {
                        if (observation.flag1) {
                            g_state.mapOpen = true;
                            g_state.observedMapIdentity =
                                observation.identity;
                            g_state.observedMapMovieGeneration =
                                observation.identity.generation;
                            g_state.hasSelectedTarget = false;
                            g_state.selectedIdentity = {};
                            g_state.selectedTarget = {};
                            g_state.selectedFromResolvedMarker = false;
                            ClearSelectionEnumeration();
                        }
                    }
                    if (!observation.flag1 &&
                        std::strcmp(observation.text.data(), MapMenuName) ==
                            0) {
                        g_state.mapOpen = false;
                        g_state.hasSelectedTarget = false;
                        g_state.selectedIdentity = {};
                        g_state.selectedTarget = {};
                        g_state.selectedFromResolvedMarker = false;
                        ClearSelectionEnumeration();
                        if (g_state.pendingSetRoute &&
                            g_state.awaitingExecuteClose) {
                            const bool sameSession =
                                observation.identity ==
                                    g_state.pendingSetRouteIdentity;
                            const bool afterExecute =
                                observation.sequence >
                                    g_state.executeSequence;
                            if (sameSession && afterExecute) {
                                g_state.committedDestination =
                                    g_state.pendingSetRouteExpected;
                                g_state.routeCommitSequence =
                                    observation.sequence;
                                g_state.pendingArrival = true;
                                g_state.completedJumpAfterCommit = false;
                                g_state.arrivalLogged = false;
                                REX::INFO("[remote-native-probe] PASS: stock Execute produced matching Starmap-close acknowledgement execute-seq={} close-seq={} session={}/{} destination-STDT={:08X} destination-numeric={:08X}; pending arrival armed",
                                    g_state.executeSequence,
                                    observation.sequence,
                                    observation.identity.session,
                                    observation.identity.generation,
                                    g_state.committedDestination.starFormId,
                                    g_state.committedDestination.numericId);
                            } else {
                                REX::ERROR("[remote-native-probe] FAIL: Starmap close after Execute did not match source session/order execute-seq={} close-seq={} source={}/{} close={}/{}",
                                    g_state.executeSequence,
                                    observation.sequence,
                                    g_state.pendingSetRouteIdentity.session,
                                    g_state.pendingSetRouteIdentity.generation,
                                    observation.identity.session,
                                    observation.identity.generation);
                            }
                            ClearPendingRouteCorrelation();
                        } else if (g_state.pendingSetRoute) {
                            REX::ERROR("[remote-native-probe] FAIL: Starmap closed before a correlated ExecuteRoute event seq={} source-session={}/{} close-session={}/{}",
                                g_state.lastSetRouteSequence,
                                g_state.pendingSetRouteIdentity.session,
                                g_state.pendingSetRouteIdentity.generation,
                                observation.identity.session,
                                observation.identity.generation);
                            ClearPendingRouteCorrelation();
                        }
                        if (g_state.observedMapIdentity ==
                            observation.identity) {
                            g_state.observedMapIdentity = {};
                        }
                    }
                    break;
                case ObservationKind::MapData:
                    REX::INFO("[remote-native-probe] map-data seq={} session={}/{} current={} view={} displayed-body={:08X} published-current-STDT={:08X}",
                        observation.sequence,
                        observation.identity.session,
                        observation.identity.generation, currentIdentity,
                        observation.small1, observation.id1,
                        observation.id2);
                    break;
                case ObservationKind::Markers:
                    LogTargetObservation("markers", observation,
                        currentIdentity);
                    if (currentIdentity && observation.count == 1) {
                        g_state.selectedIdentity = observation.identity;
                        g_state.selectedTarget = observation.target;
                        g_state.hasSelectedTarget = true;
                        g_state.selectedFromResolvedMarker = false;
                    } else if (currentIdentity) {
                        g_state.selectedIdentity = {};
                        g_state.selectedTarget = {};
                        g_state.hasSelectedTarget = false;
                        g_state.selectedFromResolvedMarker = false;
                        ClearSelectionEnumeration();
                    }
                    break;
                case ObservationKind::Dossier:
                    LogTargetObservation("dossier", observation,
                        currentIdentity);
                    break;
                case ObservationKind::ResolvedMarker:
                    LogTargetObservation("resolved-marker", observation,
                        currentIdentity);
                    if (currentIdentity && observation.target.id != 0) {
                        g_state.selectedIdentity = observation.identity;
                        g_state.selectedTarget = observation.target;
                        g_state.hasSelectedTarget = true;
                        g_state.selectedFromResolvedMarker = true;
                    } else if (currentIdentity) {
                        g_state.selectedIdentity = {};
                        g_state.selectedTarget = {};
                        g_state.hasSelectedTarget = false;
                        g_state.selectedFromResolvedMarker = false;
                        ClearSelectionEnumeration();
                    }
                    break;
                case ObservationKind::Hud:
                    LogHudObservation(observation, hudGeneration);
                    break;
                case ObservationKind::HudMovie:
                    REX::INFO("[remote-native-probe] HUD movie generation={} active-generation={}",
                        observation.generation, hudGeneration);
                    break;
                case ObservationKind::Input: {
                    const auto before = g_state.lastNativeMap.value_or(
                        NativeMapState {});
                    ClearPendingRouteCorrelation();
                    ClearCommittedArrival();
                    g_state.lastSetRouteSequence = observation.sequence;
                    const auto sourceIdentity =
                        activeMapIdentity.IsValid() ? activeMapIdentity :
                            g_state.observedMapIdentity;
                    g_state.pendingSetRouteIdentity = sourceIdentity;
                    g_state.pendingSetRouteExpected =
                        ExpectedRouteSystem(before);
                    g_state.pendingPreexistingEndpoint =
                        before.routeEndpoint;
                    g_state.pendingSetRoute =
                        sourceIdentity.IsValid() &&
                        g_state.pendingSetRouteExpected.valid;
                    g_state.pendingRouteSampleNeeded =
                        g_state.pendingSetRoute;
                    REX::INFO("[remote-native-probe] stock SetRouteDestination first-down seq={} source-session={}/{} device={} id-code={} disabled={} native-view-before={} expected-STDT={:08X} expected-numeric={:08X} preexisting-endpoint={:08X} correlated={}",
                        observation.sequence, sourceIdentity.session,
                        sourceIdentity.generation,
                        observation.value1, observation.value2,
                        observation.flag1,
                        NativeViewName(before.view),
                        g_state.pendingSetRouteExpected.starFormId,
                        g_state.pendingSetRouteExpected.numericId,
                        g_state.pendingPreexistingEndpoint,
                        g_state.pendingSetRoute);
                    break;
                }
                case ObservationKind::ExecuteRoute: {
                    const auto correlationIdentity =
                        activeMapIdentity.IsValid() ? activeMapIdentity :
                            g_state.observedMapIdentity;
                    const bool correlated = g_state.pendingSetRoute &&
                        g_state.routeEndpointProven &&
                        g_state.executeReadyDwellProven &&
                        correlationIdentity.IsValid() &&
                        correlationIdentity ==
                            g_state.pendingSetRouteIdentity;
                    if (!correlated) {
                        REX::WARN("[remote-native-probe] unrelated/unready StarMapMenu_ExecuteRoute ignored seq={} pending-route={} endpoint-proven={} ready-dwell={} active-session={}/{} source-session={}/{}",
                            observation.sequence,
                            g_state.pendingSetRoute,
                            g_state.routeEndpointProven,
                            g_state.executeReadyDwellProven,
                            correlationIdentity.session,
                            correlationIdentity.generation,
                            g_state.pendingSetRouteIdentity.session,
                            g_state.pendingSetRouteIdentity.generation);
                        break;
                    }
                    g_state.executeSequence = observation.sequence;
                    g_state.executeObservedAt = Clock::now();
                    g_state.awaitingExecuteClose = true;
                    REX::INFO("[remote-native-probe] PASS: correlated StarMapMenu_ExecuteRoute observed seq={} session={}/{} endpoint={:08X} destination-STDT={:08X} destination-numeric={:08X}; awaiting matching close within {} ms",
                        observation.sequence,
                        correlationIdentity.session,
                        correlationIdentity.generation,
                        g_state.provenRouteEndpoint,
                        g_state.pendingSetRouteExpected.starFormId,
                        g_state.pendingSetRouteExpected.numericId,
                        std::chrono::duration_cast<
                            std::chrono::milliseconds>(
                                ExecuteCloseTimeout).count());
                    break;
                }
                case ObservationKind::GravJump:
                    g_state.forceSystemPairPoll = true;
                    g_state.lastTravelSequence = observation.sequence;
                    g_state.lastUnsettledAt = Clock::now();
                    if (observation.value1 == 0) {
                        g_state.gravJumpProgress = 1;
                    } else if (observation.value1 == 1 &&
                        g_state.gravJumpProgress == 1) {
                        g_state.gravJumpProgress = 2;
                    } else if (observation.value1 == 2 &&
                        g_state.gravJumpProgress == 2) {
                        g_state.completedTravelSequence =
                            observation.sequence;
                        if (g_state.pendingArrival &&
                            observation.sequence >
                                g_state.routeCommitSequence) {
                            g_state.completedJumpAfterCommit = true;
                        }
                        g_state.gravJumpProgress = 0;
                    } else {
                        g_state.gravJumpProgress = 0;
                    }
                    REX::INFO("[remote-native-probe] player GravJump seq={} state={} destination={:08X} progress={} completed-travel-seq={}",
                        observation.sequence, observation.value1,
                        observation.id2, g_state.gravJumpProgress,
                        g_state.completedTravelSequence);
                    break;
                case ObservationKind::LoadGame:
                    ResetEvidenceState();
                    REX::WARN("[remote-native-probe] TESLoadGame seq={}; all probe evidence state reset",
                        observation.sequence);
                    break;
                case ObservationKind::Component:
                    REX::INFO("[remote-native-probe] native PNDT body={:08X} Satellite-present={} numeric-system={:08X} parent-ordinal={:08X} planet-ordinal={:08X} CT-state={} CT-present={} CT-flag={} CT-editor='{}' CT-truncated={}",
                        observation.component.bodyId,
                        observation.component.satellitePresent,
                        observation.component.numericSystemId,
                        observation.component.parentOrdinal,
                        observation.component.planetOrdinal,
                        std::to_underlying(
                            observation.component.cellDataState),
                        observation.component.cellDataState ==
                            CellDataCaptureState::Present,
                        observation.component.cellDataFlag,
                        observation.component.cellEditorId.data(),
                        observation.component.cellTextTruncated);
                    break;
                }
            }

            const auto correlationIdentity =
                activeMapIdentity.IsValid() ? activeMapIdentity :
                    g_state.observedMapIdentity;
            if (g_state.pendingSetRoute &&
                correlationIdentity.IsValid() &&
                g_state.pendingSetRouteIdentity != correlationIdentity) {
                REX::WARN("[remote-native-probe] route-input correlation cleared on map session/movie replacement source={}/{} active={}/{}",
                    g_state.pendingSetRouteIdentity.session,
                    g_state.pendingSetRouteIdentity.generation,
                    correlationIdentity.session,
                    correlationIdentity.generation);
                ClearPendingRouteCorrelation();
            }

            const auto nativeMap = ReadNativeMapState();
            bool nativeMapLogged = false;
            if (!g_state.lastNativeMap ||
                *g_state.lastNativeMap != nativeMap) {
                LogNativeMapTransition(nativeMap);
                g_state.lastNativeMap = nativeMap;
                nativeMapLogged = true;
            }
            if (g_state.pendingSetRoute &&
                g_state.pendingRouteSampleNeeded) {
                if (!nativeMapLogged) {
                    LogNativeMapTransition(nativeMap);
                }
                g_state.pendingRouteSampleNeeded = false;
            }
            if (g_state.pendingSetRoute &&
                !g_state.routeEndpointProven && nativeMap.stable &&
                nativeMap.routeReadable &&
                g_state.pendingSetRouteExpected.valid &&
                nativeMap.routeEndpoint != 0 &&
                nativeMap.routeEndpoint !=
                    g_state.pendingPreexistingEndpoint) {
                SystemIdentity endpointSystem;
                if (ResolveSystemIdentity(nativeMap.routeEndpoint,
                        endpointSystem) &&
                    endpointSystem ==
                        g_state.pendingSetRouteExpected) {
                    g_state.provenRouteEndpoint =
                        nativeMap.routeEndpoint;
                    g_state.provenRouteEndpointSystem = endpointSystem;
                    g_state.routeEndpointProven = true;
                    g_state.executeReadySince = {};
                    g_state.executeReadyDwellProven = false;
                    REX::INFO("[remote-native-probe] PASS: route endpoint exact post-input system sample complete seq={} session={}/{} endpoint={:08X} resolved-STDT={:08X} resolved-numeric={:08X}; awaiting public Execute readiness",
                        g_state.lastSetRouteSequence,
                        g_state.pendingSetRouteIdentity.session,
                        g_state.pendingSetRouteIdentity.generation,
                        nativeMap.routeEndpoint,
                        endpointSystem.starFormId,
                        endpointSystem.numericId);
                }
            }

            const auto now = Clock::now();
            const bool foreground = IsApplicationForeground();
            if (!foreground) {
                if (!g_state.focusSuspended &&
                    g_state.pendingSetRoute) {
                    g_state.focusSuspended = true;
                    g_state.focusLostAt = now;
                    g_state.executeReadySince = {};
                    g_state.executeReadyDwellProven = false;
                    REX::INFO("[remote-native-probe] route evidence suspended on foreground loss; partial Execute readiness dwell cleared");
                }
            } else if (g_state.focusSuspended) {
                if (g_state.awaitingExecuteClose &&
                    g_state.focusLostAt != Clock::time_point{}) {
                    g_state.executeObservedAt += now -
                        g_state.focusLostAt;
                }
                g_state.focusSuspended = false;
                g_state.focusLostAt = {};
                REX::INFO("[remote-native-probe] route evidence resumed after foreground restoration");
            }

            if (foreground && g_state.pendingSetRoute &&
                g_state.routeEndpointProven &&
                !g_state.awaitingExecuteClose) {
                const auto gate = ReadExecuteGate();
                if (!gate.resolved || !gate.ready) {
                    if (g_state.executeReadySince != Clock::time_point{} ||
                        g_state.executeReadyDwellProven) {
                        REX::INFO("[remote-native-probe] public Execute readiness lost resolved={} ready={}; dwell cleared",
                            gate.resolved, gate.ready);
                    }
                    g_state.executeReadySince = {};
                    g_state.executeReadyDwellProven = false;
                } else if (g_state.executeReadySince ==
                    Clock::time_point{}) {
                    g_state.executeReadySince = now;
                    REX::INFO("[remote-native-probe] public Execute gate ready for exact route endpoint={:08X}; starting {} ms continuous dwell",
                        g_state.provenRouteEndpoint,
                        ExecuteReadyDwell.count());
                } else if (!g_state.executeReadyDwellProven &&
                    now - g_state.executeReadySince >=
                        ExecuteReadyDwell) {
                    g_state.executeReadyDwellProven = true;
                    REX::INFO("[remote-native-probe] PASS: public Execute gate remained continuously ready for {} ms on exact destination SystemIdentity",
                        std::chrono::duration_cast<
                            std::chrono::milliseconds>(now -
                                g_state.executeReadySince).count());
                }
            }

            if (foreground && g_state.awaitingExecuteClose &&
                g_state.executeObservedAt != Clock::time_point{} &&
                now - g_state.executeObservedAt >
                    ExecuteCloseTimeout) {
                REX::ERROR("[remote-native-probe] FAIL: correlated ExecuteRoute produced no matching Starmap close within {} ms",
                    std::chrono::duration_cast<
                        std::chrono::milliseconds>(
                            ExecuteCloseTimeout).count());
                ClearPendingRouteCorrelation();
            }

            if (g_state.hasSelectedTarget &&
                g_state.selectedFromResolvedMarker &&
                g_state.selectedIdentity == activeMapIdentity) {
                BeginSelectionEvidence(g_state.selectedIdentity,
                    g_state.selectedTarget, nativeMap);
            } else if (g_ancestry.hasSelection) {
                ClearSelectionEnumeration();
            }
            AdvancePndtEnumeration();
            AnalyzeSelectedAncestry();

            if (g_state.forceSystemPairPoll ||
                now >= g_state.nextSystemPairPoll) {
                g_state.forceSystemPairPoll = false;
                g_state.nextSystemPairPoll =
                    now + CurrentPairPollInterval;
                const auto current = ReadCurrentSystemPair();
                g_state.lastCurrentPollTravelSequence =
                    g_state.lastTravelSequence;
                if (!g_state.currentSystemSampled ||
                    current != g_state.lastSystemPair) {
                    if (current) {
                        REX::INFO("[remote-native-probe] authoritative-current body={:08X} STDT={:08X} numeric-wrapper={:08X} Satellite-system={:08X} agreement=true (zero-valid={})",
                            current->bodyId, current->starFormId,
                            current->numericId,
                            current->satelliteNumericId,
                            current->numericId == 0);
                    } else {
                        REX::WARN("[remote-native-probe] authoritative-current unavailable under exact guarded native readers");
                    }
                    g_state.lastSystemPair = current;
                    g_state.currentSystemSampled = true;
                }
            }

            if (g_state.pendingArrival &&
                g_state.completedJumpAfterCommit &&
                g_state.lastSystemPair &&
                g_state.lastSystemPair->valid &&
                g_state.lastCurrentPollTravelSequence >=
                    g_state.lastTravelSequence) {
                const SystemIdentity currentIdentity {
                    .starFormId = g_state.lastSystemPair->starFormId,
                    .numericId = g_state.lastSystemPair->numericId,
                    .valid = true,
                };
                const bool finalSystem = currentIdentity ==
                    g_state.committedDestination;
                const bool settled =
                    g_state.lastUnsettledAt != Clock::time_point{} &&
                    now - g_state.lastUnsettledAt >= WorldSettleTime;
                const bool flying = IsPlayerFlying();
                const bool hudFresh = g_state.hasHudSnapshot &&
                    !g_state.lastHudOverflowed &&
                    g_state.lastHudSequence >
                        g_state.lastTravelSequence;
                if (finalSystem && !g_state.mapOpen &&
                    !g_state.loadingOpen && settled && flying &&
                    hudFresh) {
                    REX::INFO("[remote-native-probe] PASS: final-system arrival exact after committed route STDT={:08X} numeric={:08X} body={:08X} completed-jump-seq={} last-travel-seq={} current-poll-floor={} map-closed=true loading-closed=true settled-ms={} flying=true fresh-HUD-seq={} HUD-rows={} HUD-overflow=false",
                        currentIdentity.starFormId,
                        currentIdentity.numericId,
                        g_state.lastSystemPair->bodyId,
                        g_state.completedTravelSequence,
                        g_state.lastTravelSequence,
                        g_state.lastCurrentPollTravelSequence,
                        std::chrono::duration_cast<
                            std::chrono::milliseconds>(now -
                                g_state.lastUnsettledAt).count(),
                        g_state.lastHudSequence,
                        g_state.lastHudRowCount);
                    g_state.arrivalLogged = true;
                    g_state.pendingArrival = false;
                } else if (!g_state.loadingOpen && settled &&
                    !flying) {
                    REX::ERROR("[remote-native-probe] FAIL: committed remote arrival reached a settled non-flight state; pending evidence cleared");
                    ClearCommittedArrival();
                }
            }

            const auto nativeTarget = RE::ShipHudTarget::GetCurrent();
            if (nativeTarget != g_state.lastNativeTarget) {
                REX::INFO("[remote-native-probe] native ShipHudTarget readback changed {:08X}->{:08X}",
                    g_state.lastNativeTarget, nativeTarget);
                g_state.lastNativeTarget = nativeTarget;
            }
            g_state.hudGeneration = hudGeneration;
        } catch (const std::exception& error) {
            ResetEvidenceState();
            REX::ERROR("[remote-native-probe] FAIL: Drain exception '{}'; evidence state restarted",
                error.what());
        } catch (...) {
            ResetEvidenceState();
            REX::ERROR("[remote-native-probe] FAIL: unknown Drain exception; evidence state restarted");
        }
    }
}

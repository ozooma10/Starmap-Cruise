// Included by Bridge.cpp inside CFS::Bridge's anonymous namespace.
// Owns shared state and constants.

        using Clock = std::chrono::steady_clock;
        using V = RE::Scaleform::GFx::Value;

        constexpr const char* kMapMenu = "GalaxyStarMapMenu";
        constexpr const char* kHudMenu = "SpaceshipHudMenu";
        constexpr std::int32_t kGalaxyView = 0;
        constexpr std::int32_t kSystemView = 1;
        constexpr std::uint32_t kPlanetType = 2;
        constexpr std::uint32_t kMoonType = 3;
        constexpr double kLightSecondMeters = 299'792'458.0;
        constexpr double kArrivalDistanceMeters = 0.05 * kLightSecondMeters;
        constexpr const char* kCruiseMapActionLabel = "SET CRUISE TARGET";
        constexpr const char* kRemoteCruiseMapActionLabel = "JUMP THEN CRUISE";
        constexpr const char* kCruiseMapActionHoldLabel = "HOLD TO CRUISE";
        constexpr const char* kCruiseMapUserEvent = "Cruise";
        constexpr const char* kCruiseMapGamepadUserEvent = "SHMonocle";
        constexpr auto kHudMovieSettleTime = std::chrono::milliseconds(1500);
        constexpr auto kMapRoutePollTime = std::chrono::milliseconds(250);
        constexpr auto kRemoteRouteExecuteSettleTime = std::chrono::milliseconds(500);
        constexpr auto kRemoteRouteTimeout = std::chrono::seconds(5);
        constexpr auto kRemoteExecuteAckTimeout = std::chrono::seconds(2);
        constexpr auto kRemoteMoonFeedTimeout = std::chrono::seconds(10);
        constexpr auto kRemoteMoonCruiseTimeout = std::chrono::seconds(5);
        constexpr auto kRemoteMoonLockExitTimeout = std::chrono::seconds(2);
        constexpr auto kRemoteStationResolveTimeout = std::chrono::seconds(10);
        // Distinct fail-closed budgets that happen to share values; do not merge.
        constexpr auto kArrivalEvidenceWindow = std::chrono::seconds(2);
        constexpr auto kQueuedCourseExpiry = std::chrono::seconds(2);
        constexpr auto kCourseLockReadbackTimeout = std::chrono::milliseconds(1500);
        constexpr auto kHudCruisePressSafetyLimit = std::chrono::seconds(4);
        constexpr auto kWorldSettleTime = std::chrono::milliseconds(2500);
        constexpr auto kMovieSubscribeSettleTime = std::chrono::milliseconds(250);
        constexpr auto kFocusPollTime = std::chrono::milliseconds(50);
        constexpr std::size_t kMaxStationAncestryDepth = 8;
        // Post-advance passes the native selection call keeps before diagnostics.
        // The unit is completed AS3 advances, not wall clock: each pass means
        // native finished one advance with the selection already applied.
        constexpr std::uint32_t kGalaxyFocusReadbackPasses = 10;
        // Starfield 1.16.244: GalaxyState's non-entering selected-system setter
        // and the stock Quick Select close/consume path. The setter is vtable
        // slot +0x48; SetRouteDestination reads that selected ID when Quick
        // Select mode is active, then closes the mode itself.
        constexpr REL::ID kLoadGameGetEventSource{ 64149 };
        constexpr REL::ID kLoadGameSourceStatic{ 838425 };
        constexpr REL::ID kLoadGameSourceVtable{ 413741 };
        constexpr REL::ID kGravJumpGetEventSource{ 93876 };
        constexpr REL::ID kGravJumpSourceVtable{ 445846 };
        constexpr std::size_t kStarMapMenuDataModelOffset = 0x1B8;
        constexpr std::size_t kStarMapMenuGalaxyStateOffset = 0x1240;
        constexpr std::size_t kGalaxyStateSelectedSystemOffset = 0x880;
        constexpr std::size_t kGalaxyStateQuickSelectOpenOffset = 0x8F8;
        constexpr std::array<std::uint8_t, 16> kGlobalEventGetEventSource116244Prologue{
            0x48, 0x83, 0xEC, 0x28, 0x65, 0x48, 0x8B, 0x04,
            0x25, 0x58, 0x00, 0x00, 0x00, 0xBA, 0xB8, 0x00,
        };

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
            std::uint32_t treeBodyID{ 0 };
            std::size_t highlightedMarkerCount{ 0 };
            std::uint32_t markerBodyID{ 0 };
            std::uint32_t markerBodyType{ 0 };
            std::string markerName;
            std::uint32_t dossierBodyID{ 0 };
            std::uint32_t dossierBodyType{ 0 };
            std::string dossierName;
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
            kCruiseActive,
            kRemoteSafetyUnavailable,
            kRemoteCourseUnavailable,
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
            kAwaitExecuteAck,
        };

        struct RemoteRouteRequest
        {
            RemoteRoutePhase phase{ RemoteRoutePhase::kNone };
            std::uint32_t session{ 0 };
            std::uint32_t generation{ 0 };
            std::uint32_t targetFormID{ 0 };
            std::uint32_t systemBodyID{ 0 };
            // One exact vanilla focus operation establishes the galaxy system
            // context. It runs only on a post-advance pass that already failed
            // the proof test; native then gets focusReadbackPasses completed
            // advances to publish its readback before diagnostics fire.
            bool focusAttempted{ false };
            std::uint32_t focusReadbackPasses{ 0 };
            bool focusDiagnosticsLogged{ false };
            std::string expectedSystemName;
            Clock::time_point started{};
            Clock::time_point executeReadySince{};
        };
        std::mutex g_remoteRouteMutex;
        RemoteRouteRequest g_remoteRouteRequest;

        enum class RemoteMoonPhase : std::uint8_t
        {
            kNone,
            kAwaitingParentFeed,
            kAwaitingParentCruise,
            // One retained-target dispatch is in flight. The Ariel/Chawla
            // traces prove this is engine-owned travel with no handshake
            // boundary: the state is unbounded while Cruise stays active, and
            // dispatchConfirmed records whether the asked-course registration
            // was observed (bounded) before the unbounded wait begins.
            kTraveling,
            kParentLocked,
            kAwaitingParentArrival,
            kAwaitingFinalLock,
        };

        struct RemoteMoonContinuation
        {
            RemoteMoonPhase phase{ RemoteMoonPhase::kNone };
            BodyKind finalKind{ BodyKind::kOther };
            std::uint32_t finalFormID{ 0 };
            std::uint32_t finalCourseFormID{ 0 };
            std::uint32_t system{ 0 };
            std::uint32_t parentFormID{ 0 };
            std::string parentEditorID;
            std::string parentName;
            std::vector<BodyIndex::IndexedBody> stationWaypoints;
            std::size_t waypointIndex{ 0 };
            std::uint64_t feedRevisionFloor{ 0 };
            bool dispatchConfirmed{ false };
            Clock::time_point phaseStarted{};
            Clock::time_point inactiveSince{};
        };
        std::mutex g_remoteMoonMutex;
        RemoteMoonContinuation g_remoteMoonContinuation;

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
        std::atomic<std::int64_t> g_pendingStationResolveTicks{ 0 };
        std::atomic<std::uint32_t> g_pendingStationAssignedID{ 0 };
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
            bool suppressUntilRelease{ false };
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
            std::string name;
            bool courseLocked{ false };
        };
        std::mutex g_hudRowsMutex;
        std::vector<HudRow> g_hudRows;
        std::uint32_t g_hudRowsGeneration{ 0 };

        struct ProcessedHudSnapshot
        {
            std::vector<HudRow> rows;
            std::uint32_t course{ 0 };
            std::uint64_t revision{ 0 };
            std::uint32_t generation{ 0 };
        };
        std::mutex g_processedHudMutex;
        ProcessedHudSnapshot g_processedHudSnapshot;

        std::atomic<bool> g_hudLowDirty{ false };
        std::atomic<std::uint64_t> g_hudLowRevision{ 0 };

        struct DistanceSample
        {
            bool valid{ false };
            double distance{ -1.0 };
        };
        std::mutex g_hudDistancesMutex;
        std::vector<DistanceSample> g_hudDistances;
        std::uint32_t g_hudDistancesGeneration{ 0 };
        std::atomic<bool> g_hudUiDirty{ false };

        struct ArrivalAuditState
        {
            double markedDistance{ -1.0 };
            std::uint32_t distanceGeneration{ 0 };
            bool courseWasLocked{ false };
            std::uint32_t courseLockGeneration{ 0 };
            std::uint32_t checkID{ 0 };
            std::uint32_t checkGeneration{ 0 };
            std::int64_t checkTicks{ 0 };
        };
        std::mutex g_arrivalAuditMutex;
        ArrivalAuditState g_arrivalAudit;

        std::atomic<std::int64_t> g_lastUnsettledTicks{ 0 };

        using ProcessInput_t = void (*)(RE::BSInputEventReceiver*, const RE::InputEvent*);
        std::atomic<ProcessInput_t> g_originalInput{ nullptr };
        std::atomic<bool> g_inputInstalled{ false };
        std::atomic<std::int32_t> g_cruiseMapKey{ -1 };
        std::atomic<std::int32_t> g_cruiseMapModifier{ -1 };
        std::atomic<std::int32_t> g_cruiseMapMouseButton{ -1 };
        std::atomic<std::int32_t> g_cruiseMapGamepadButton{ -1 };

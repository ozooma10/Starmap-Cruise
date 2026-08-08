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
        constexpr auto kOrbitalFeedTimeout = std::chrono::seconds(10);
        constexpr auto kOrbitalCruiseTimeout = std::chrono::seconds(5);
        constexpr auto kOrbitalLockExitTimeout = std::chrono::seconds(2);
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
        constexpr REL::ID kLoadGameGetEventSource{ 64149 };
        constexpr REL::ID kLoadGameSourceStatic{ 838425 };
        constexpr REL::ID kLoadGameSourceVtable{ 413741 };
        constexpr REL::ID kGravJumpGetEventSource{ 93876 };
        constexpr REL::ID kGravJumpSourceVtable{ 445846 };

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

        // The shared remote moon/station continuation. "Orbital" covers both
        // final-target kinds: a moon staging through its unique GNAM parent and
        // a station staging through its CELL/DNAM/GNAM ancestry.
        enum class OrbitalPhase : std::uint8_t
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

        struct OrbitalContinuation
        {
            OrbitalPhase phase{ OrbitalPhase::kNone };
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
        std::mutex g_orbitalMutex;
        OrbitalContinuation g_orbitalContinuation;

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
        // NavState has no single owning fragment; its transitions are
        // published where their evidence lives. StoreDestination/
        // ClearDestination (Destination.inl) set kMapSelection/kIdle with the
        // mark itself; ReleaseNavStateToMark CASes kAwaitingCruise back to
        // kMarked/kIdle; MapAction.inl sets kMapSelection on a remote accept;
        // Driver.inl sets kPendingJump immediately before stock Execute;
        // Course.inl/Continuation.inl publish kAwaitingCruise around queued
        // Cruise presses and waypoint commits (Continuation also CASes
        // kPendingJump->kAwaitingCruise on arrival); OrbitalTick::CompleteFinalLock
        // and the HudCruise course confirmation publish kAutopilotLocked. Several
        // stores are CAS- or lock-context-dependent; do not funnel them
        // through a wrapper that cannot enforce those contexts.
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

        struct QueuedCourse
        {
            std::uint32_t id{ 0 };
            bool clearing{ false };
        };

        struct AskedCourse
        {
            std::uint32_t id{ 0 };
            bool clearing{ false };
            Clock::time_point at{};
        };

        // The two-stage course pipeline: a queued request waits for the Cruise
        // HUD to become ready, and a dispatched ("asked") request awaits exact
        // bIsCruiseTargetLock readback. Pure state transitions only; dispatch,
        // logging, and every timeout policy stay with the callers.
        class CoursePipeline
        {
        public:
            void Queue(std::uint32_t a_id, bool a_clearing)
            {
                std::lock_guard lock{ mutex };
                queued = { a_id, a_clearing };
                queuedAt = Clock::now();
            }

            // Takes the queued request for dispatch only when Cruise is active;
            // otherwise the request stays queued for a later ready pass.
            [[nodiscard]] std::optional<QueuedCourse> TakeQueuedForDispatch(
                bool a_cruiseActive)
            {
                std::lock_guard lock{ mutex };
                if (!queued.id || !a_cruiseActive)
                    return std::nullopt;
                const auto request = queued;
                queued = {};
                queuedAt = {};
                return request;
            }

            [[nodiscard]] std::optional<QueuedCourse> ExpireQueued(
                Clock::duration a_limit)
            {
                std::lock_guard lock{ mutex };
                if (!queued.id || Clock::now() - queuedAt <= a_limit)
                    return std::nullopt;
                const auto expired = queued;
                queued = {};
                queuedAt = {};
                return expired;
            }

            void MarkAsked(std::uint32_t a_id, bool a_clearing)
            {
                std::lock_guard lock{ mutex };
                asked = { a_id, a_clearing, Clock::now() };
            }

            [[nodiscard]] AskedCourse Asked()
            {
                std::lock_guard lock{ mutex };
                return asked;
            }

            void ClearAsked()
            {
                std::lock_guard lock{ mutex };
                asked = {};
            }

            void Reset()
            {
                std::lock_guard lock{ mutex };
                queued = {};
                queuedAt = {};
                asked = {};
            }

        private:
            std::mutex mutex;
            QueuedCourse queued;
            Clock::time_point queuedAt{};
            AskedCourse asked;
        };
        CoursePipeline g_coursePipeline;

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

        struct ArmedArrivalCheck
        {
            bool armed{ false };
            double lastDistance{ -1.0 };
            std::uint32_t distanceGeneration{ 0 };
        };

        // The independent close-distance arrival audit: an exact course-lock
        // loss arms a bounded check against the last same-generation distance
        // sample. Pure state transitions only; the evidence evaluation and the
        // destination-clearing policy stay with the callers.
        class ArrivalAudit
        {
        public:
            // Reacquiring an exact lock invalidates any still-armed audit from
            // its previous loss while preserving the latest valid distance.
            void RecordCourseLock(std::uint32_t a_hudGeneration)
            {
                std::lock_guard lock{ mutex };
                state.courseWasLocked = a_hudGeneration != 0;
                state.courseLockGeneration = a_hudGeneration;
                state.checkID = 0;
                state.checkGeneration = 0;
                state.checkTicks = 0;
            }

            [[nodiscard]] ArmedArrivalCheck Arm(std::uint32_t a_destinationID,
                std::uint32_t a_hudGeneration)
            {
                std::lock_guard lock{ mutex };
                ArmedArrivalCheck result{
                    .lastDistance = state.markedDistance,
                    .distanceGeneration = state.distanceGeneration,
                };
                if (!a_destinationID || !a_hudGeneration ||
                    !state.courseWasLocked ||
                    state.courseLockGeneration != a_hudGeneration) {
                    if (state.courseWasLocked &&
                        state.courseLockGeneration != a_hudGeneration) {
                        state.courseWasLocked = false;
                        state.courseLockGeneration = 0;
                    }
                    return result;
                }

                state.courseWasLocked = false;
                state.courseLockGeneration = 0;
                state.checkID = a_destinationID;
                state.checkGeneration = a_hudGeneration;
                state.checkTicks = Clock::now().time_since_epoch().count();
                result.armed = true;
                return result;
            }

            void RecordDistance(double a_distance, std::uint32_t a_generation)
            {
                std::lock_guard lock{ mutex };
                state.markedDistance = a_distance;
                state.distanceGeneration = a_generation;
            }

            [[nodiscard]] ArrivalAuditState Snapshot()
            {
                std::lock_guard lock{ mutex };
                return state;
            }

            // Clears the armed check only while it is still the one the caller
            // sampled; a newer arming survives.
            bool TryClearCheck(std::uint32_t a_checkID,
                std::uint32_t a_checkGeneration)
            {
                std::lock_guard lock{ mutex };
                if (state.checkID != a_checkID ||
                    state.checkGeneration != a_checkGeneration)
                    return false;
                state.checkID = 0;
                state.checkGeneration = 0;
                state.checkTicks = 0;
                return true;
            }

            void Reset()
            {
                std::lock_guard lock{ mutex };
                state = {};
            }

        private:
            std::mutex mutex;
            ArrivalAuditState state;
        };
        ArrivalAudit g_arrivalAudit;

        std::atomic<std::int64_t> g_lastUnsettledTicks{ 0 };

        using ProcessInput_t = void (*)(RE::BSInputEventReceiver*, const RE::InputEvent*);
        std::atomic<ProcessInput_t> g_originalInput{ nullptr };
        std::atomic<bool> g_inputInstalled{ false };
        std::atomic<std::int32_t> g_cruiseMapKey{ -1 };
        std::atomic<std::int32_t> g_cruiseMapModifier{ -1 };
        std::atomic<std::int32_t> g_cruiseMapMouseButton{ -1 };
        std::atomic<std::int32_t> g_cruiseMapGamepadButton{ -1 };

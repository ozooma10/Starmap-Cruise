// Included by Bridge.cpp inside CFS::Bridge's anonymous namespace.
// Owns arrival/timeout audits and the queued main-thread frame pump.

        void CheckArrival()
        {
            ArrivalAuditState audit;
            {
                std::lock_guard lock{ g_arrivalAuditMutex };
                audit = g_arrivalAudit;
            }
            if (!audit.checkID)
                return;

            const auto destination = Destination();
            if (!destination || destination->formID != audit.checkID) {
                std::lock_guard lock{ g_arrivalAuditMutex };
                if (g_arrivalAudit.checkID == audit.checkID &&
                    g_arrivalAudit.checkGeneration == audit.checkGeneration) {
                    g_arrivalAudit.checkID = 0;
                    g_arrivalAudit.checkGeneration = 0;
                    g_arrivalAudit.checkTicks = 0;
                }
                return;
            }

            const auto since = Clock::time_point{ Clock::duration{ audit.checkTicks } };
            const auto age = Clock::now() - since;
            const auto currentGeneration =
                g_hudMovie.generation.load(std::memory_order_acquire);
            const bool sameGeneration = audit.checkGeneration != 0 &&
                audit.checkGeneration == audit.distanceGeneration &&
                audit.checkGeneration == currentGeneration;
            const bool evidence = sameGeneration && audit.markedDistance >= 0.0 &&
                audit.markedDistance <= kArrivalDistanceMeters;
            if (evidence) {
                {
                    std::lock_guard lock{ g_arrivalAuditMutex };
                    if (g_arrivalAudit.checkID != audit.checkID ||
                        g_arrivalAudit.checkGeneration != audit.checkGeneration)
                        return;
                    g_arrivalAudit.checkID = 0;
                    g_arrivalAudit.checkGeneration = 0;
                    g_arrivalAudit.checkTicks = 0;
                }
                REX::INFO("[arrival] exact prior lock plus same-generation close distance {:.3f} m <= {:.3f} m confirmed arrival for {:08X} on HUD generation {}",
                    audit.markedDistance, kArrivalDistanceMeters, audit.checkID,
                    audit.checkGeneration);
                ClearDestination("confirmed arrival (course transition plus close distance)");
            } else if (age > kArrivalEvidenceWindow) {
                {
                    std::lock_guard lock{ g_arrivalAuditMutex };
                    if (g_arrivalAudit.checkID != audit.checkID ||
                        g_arrivalAudit.checkGeneration != audit.checkGeneration)
                        return;
                    g_arrivalAudit.checkID = 0;
                    g_arrivalAudit.checkGeneration = 0;
                    g_arrivalAudit.checkTicks = 0;
                }
                if (Settings::Verbose())
                    REX::INFO("[arrival] no same-generation arrival evidence after lock transition: distance={:.3f} m threshold={:.3f} m sampleGeneration={} auditGeneration={} currentGeneration={}; preserving mark {:08X}",
                        audit.markedDistance, kArrivalDistanceMeters,
                        audit.distanceGeneration, audit.checkGeneration,
                        currentGeneration, audit.checkID);
            }
        }

        void CheckCourseTimeout()
        {
            bool queuedExpired = false;
            {
                std::lock_guard lock{ g_courseMutex };
                if (g_courseRequest.id &&
                    Clock::now() - g_courseRequest.queued > kQueuedCourseExpiry) {
                    REX::WARN("[course] queued {} for {:08X} expired before Cruise HUD became ready; mark preserved",
                        g_courseRequest.clearing ? "clear" : "lock", g_courseRequest.id);
                    g_courseRequest = {};
                    queuedExpired = true;
                }
            }
            if (queuedExpired &&
                FailActiveContinuationsOrRelease(
                    "course request expired before the Cruise HUD became ready"))
                return;

            const auto asked = g_courseAskedID.load(std::memory_order_acquire);
            if (asked) {
                const auto at = Clock::time_point{ Clock::duration{
                    g_courseAskedTicks.load(std::memory_order_acquire) } };
                const auto continuation = RemoteMoonState();
                const bool awaitingStockCourseResolution = continuation &&
                    continuation->phase == RemoteMoonPhase::kTraveling &&
                    continuation->finalCourseFormID == asked &&
                    !g_courseAskedClearing.load(std::memory_order_acquire);
                if (!awaitingStockCourseResolution &&
                    Clock::now() - at > kCourseLockReadbackTimeout) {
                    g_courseAskedID.store(0, std::memory_order_release);
                    g_courseAskedClearing.store(false, std::memory_order_release);
                    REX::WARN("[course] no bIsCruiseTargetLock confirmation for {:08X} after {} ms; mark preserved",
                        asked, kCourseLockReadbackTimeout.count());
                    if (FailActiveContinuationsOrRelease(
                            "course dispatch received no exact bIsCruiseTargetLock readback"))
                        return;
                }
            }

            bool hudHoldExpired = false;
            {
                std::lock_guard lock{ g_hudCruiseInputMutex };
                hudHoldExpired = g_hudCruiseInputLatched &&
                    g_hudCruiseInputStarted != Clock::time_point{} &&
                    Clock::now() - g_hudCruiseInputStarted > kHudCruisePressSafetyLimit;
            }
            if (hudHoldExpired) {
                const auto safetyReason = std::format(
                    "{}-second activation safety limit",
                    kHudCruisePressSafetyLimit.count());
                CancelOrReleaseHudCruiseInput(safetyReason.c_str());
                REX::WARN("[input] latched HUD Cruise hold did not make CruiseModeHUDActive within {} seconds; released automatically and preserved destination",
                    kHudCruisePressSafetyLimit.count());
                FailActiveContinuationsOrRelease(
                    "stock Cruise activation timed out during remote continuation");
            }
        }

        bool FailClosedPostAdvanceState(const char* a_reason) noexcept
        {
            g_pendingMapAction.store(MapAction::kNone, std::memory_order_release);
            g_selectionAcceptedThisOpen.store(false, std::memory_order_release);
            g_closeRequested.store(false, std::memory_order_release);
            g_mapActionInteractive.store(false, std::memory_order_release);
            g_mapActionTapOnly.store(false, std::memory_order_release);

            try {
                std::lock_guard lock{ g_holdMutex };
                g_hold = {};
                g_claimMapKey = false;
            } catch (...) {
            }

            bool unresolvedPressedEdge = false;
            try {
                std::lock_guard lock{ g_hudCruiseInputMutex };
                unresolvedPressedEdge =
                    g_hudCruiseInputPhase == HudCruiseInputPhase::kPressed ||
                    g_hudCruiseInputPhase == HudCruiseInputPhase::kReleasePending;
                g_hudCruiseInputPhase = HudCruiseInputPhase::kIdle;
                g_hudCruiseUserEvent = "Cruise";
                g_hudCruiseInputLatched = false;
                g_hudCruiseInputStarted = {};
            } catch (...) {
                unresolvedPressedEdge = true;
            }

            try {
                ClearDestination(a_reason);
            } catch (...) {
                // The fatal path must not throw out of the exception handler.
                // Repeat the lock-owned pieces independently, then publish Idle
                // even if one best-effort lock reset itself fails.
                try {
                    std::lock_guard lock{ g_destinationMutex };
                    g_destination.reset();
                } catch (...) {
                }
                try {
                    std::lock_guard lock{ g_courseMutex };
                    g_courseRequest = {};
                } catch (...) {
                }
                try {
                    std::lock_guard lock{ g_remoteRouteMutex };
                    g_remoteRouteRequest = {};
                } catch (...) {
                }
                try {
                    ResetDestinationDependentState(NavState::kIdle);
                } catch (...) {
                }
                g_courseAskedID.store(0, std::memory_order_release);
                g_courseAskedClearing.store(false, std::memory_order_release);
                g_pendingStationResolveTicks.store(0, std::memory_order_release);
                g_pendingStationAssignedID.store(0, std::memory_order_release);
                g_state.store(NavState::kIdle, std::memory_order_release);
            }
            return unresolvedPressedEdge;
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

            auto destination = Destination();
            if (destination && RemoteMoonContinuationActive()) {
                DriveRemoteMoonContinuation(*destination);
                destination = Destination();
            }
            if (destination) {
                const auto state = g_state.load(std::memory_order_acquire);
                const bool remoteMapSelection =
                    (state == NavState::kMapSelection || RemoteRouteRequestActive()) &&
                    UsesRemoteSystemRoute(*destination) &&
                    g_haveCurrentSystem.load(std::memory_order_acquire) &&
                    g_currentSystem.load(std::memory_order_acquire) !=
                        destination->galaxy.system;
                if (RemoteMoonContinuationActive()) {
                    // The private orbital-waypoint driver owns all mutation and
                    // safety boundaries until exact final-target lock readback.
                } else if (state == NavState::kPendingJump) {
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

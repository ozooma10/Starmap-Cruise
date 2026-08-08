// Included by Bridge.cpp inside CFS::Bridge's anonymous namespace.
// Owns arrival/timeout audits and the queued main-thread frame pump.

        void CheckArrival()
        {
            const auto audit = g_arrivalAudit.Snapshot();
            if (!audit.checkID)
                return;

            const auto destination = Destination();
            if (!destination || destination->formID != audit.checkID) {
                g_arrivalAudit.TryClearCheck(audit.checkID, audit.checkGeneration);
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
                if (!g_arrivalAudit.TryClearCheck(audit.checkID,
                        audit.checkGeneration))
                    return;
                REX::INFO("[arrival] exact prior lock plus same-generation close distance {:.3f} m <= {:.3f} m confirmed arrival for {:08X} on HUD generation {}",
                    audit.markedDistance, kArrivalDistanceMeters, audit.checkID,
                    audit.checkGeneration);
                ClearDestination("confirmed arrival (course transition plus close distance)");
            } else if (age > kArrivalEvidenceWindow) {
                if (!g_arrivalAudit.TryClearCheck(audit.checkID,
                        audit.checkGeneration))
                    return;
                if (Settings::Verbose())
                    REX::INFO("[arrival] no same-generation arrival evidence after lock transition: distance={:.3f} m threshold={:.3f} m sampleGeneration={} auditGeneration={} currentGeneration={}; preserving mark {:08X}",
                        audit.markedDistance, kArrivalDistanceMeters,
                        audit.distanceGeneration, audit.checkGeneration,
                        currentGeneration, audit.checkID);
            }
        }

        void CheckCourseTimeout()
        {
            if (const auto expired = g_coursePipeline.ExpireQueued(kQueuedCourseExpiry)) {
                REX::WARN("[course] queued {} for {:08X} expired before Cruise HUD became ready; mark preserved",
                    expired->clearing ? "clear" : "lock", expired->id);
                if (FailActiveContinuationsOrRelease(
                        "course request expired before the Cruise HUD became ready"))
                    return;
            }

            const auto asked = g_coursePipeline.Asked();
            if (asked.id) {
                const auto continuation = OrbitalState();
                const bool awaitingStockCourseResolution = continuation &&
                    continuation->phase == OrbitalPhase::kTraveling &&
                    continuation->finalCourseFormID == asked.id &&
                    !asked.clearing;
                if (!awaitingStockCourseResolution &&
                    Clock::now() - asked.at > kCourseLockReadbackTimeout) {
                    g_coursePipeline.ClearAsked();
                    REX::WARN("[course] no bIsCruiseTargetLock confirmation for {:08X} after {} ms; mark preserved",
                        asked.id, kCourseLockReadbackTimeout.count());
                    if (FailActiveContinuationsOrRelease(
                            "course dispatch received no exact bIsCruiseTargetLock readback"))
                        return;
                }
            }

            if (g_hudCruiseInput.PressExpired(kHudCruisePressSafetyLimit)) {
                const auto safetyReason = std::format(
                    "{}-second activation safety limit",
                    kHudCruisePressSafetyLimit.count());
                g_hudCruiseInput.CancelOrRelease(safetyReason.c_str());
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

            const bool unresolvedPressedEdge = g_hudCruiseInput.FailClosed();

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
                    g_coursePipeline.Reset();
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
            if (destination && OrbitalContinuationActive()) {
                DriveOrbitalContinuation(*destination);
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
                if (OrbitalContinuationActive()) {
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

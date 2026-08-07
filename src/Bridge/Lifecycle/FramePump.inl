// Included by Bridge.cpp inside CFS::Bridge's anonymous namespace.
// Owns arrival/timeout audits and the queued main-thread frame pump.

        void CheckArrival()
        {
            const auto id = g_arrivalCheckID.load(std::memory_order_acquire);
            if (!id)
                return;
            const auto since = Clock::time_point{ Clock::duration{
                g_arrivalCheckTicks.load(std::memory_order_acquire) } };
            const auto age = Clock::now() - since;
            const double distance = g_markedDistance.load(std::memory_order_acquire);
            const bool evidence = distance >= 0.0 && distance <= kArrivalDistanceMeters;
            if (evidence) {
                g_arrivalCheckID.store(0, std::memory_order_release);
                REX::INFO("[arrival] exact prior lock plus close distance {:.3f} m <= {:.3f} m confirmed arrival for {:08X}",
                    distance, kArrivalDistanceMeters, id);
                ClearDestination("confirmed arrival (course transition plus close distance)");
            } else if (age > std::chrono::seconds(2)) {
                g_arrivalCheckID.store(0, std::memory_order_release);
                if (Settings::Verbose())
                    REX::INFO("[arrival] no arrival evidence after lock transition: distance={:.3f} m threshold={:.3f} m; preserving mark {:08X}",
                        distance, kArrivalDistanceMeters, id);
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
            if (queuedExpired) {
                if (RemoteMoonContinuationActive()) {
                    FailRemoteMoonContinuation("internal course request expired before the Cruise HUD became ready");
                    return;
                }
                if (RemoteStationContinuationActive()) {
                    FailRemoteStationContinuation(
                        "remote station course request expired before the Cruise HUD became ready");
                    return;
                }
                g_state.store(Destination() ? NavState::kMarked : NavState::kIdle,
                    std::memory_order_release);
            }

            const auto asked = g_courseAskedID.load(std::memory_order_acquire);
            if (asked) {
                const auto at = Clock::time_point{ Clock::duration{
                    g_courseAskedTicks.load(std::memory_order_acquire) } };
                const auto continuation = RemoteMoonState();
                const bool awaitingStockCourseResolution = continuation &&
                    (continuation->phase == RemoteMoonPhase::kAwaitingParentLock ||
                        continuation->phase == RemoteMoonPhase::kAwaitingLatentFinalLock) &&
                    continuation->finalCourseFormID == asked &&
                    !g_courseAskedClearing.load(std::memory_order_acquire);
                if (!awaitingStockCourseResolution &&
                    Clock::now() - at > std::chrono::milliseconds(1500)) {
                    g_courseAskedID.store(0, std::memory_order_release);
                    g_courseAskedClearing.store(false, std::memory_order_release);
                    REX::WARN("[course] no bIsCruiseTargetLock confirmation for {:08X} after 1.5 seconds; mark preserved",
                        asked);
                    if (RemoteMoonContinuationActive()) {
                        FailRemoteMoonContinuation("internal course dispatch received no exact bIsCruiseTargetLock readback");
                        return;
                    }
                    if (RemoteStationContinuationActive()) {
                        FailRemoteStationContinuation(
                            "remote station course dispatch received no exact bIsCruiseTargetLock readback");
                        return;
                    }
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
                if (RemoteMoonContinuationActive()) {
                    FailRemoteMoonContinuation("stock Cruise activation timed out during remote orbital continuation");
                    return;
                }
                if (RemoteStationContinuationActive()) {
                    FailRemoteStationContinuation(
                        "stock Cruise activation timed out during remote-station continuation");
                    return;
                }
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

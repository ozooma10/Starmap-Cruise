// Included by Bridge.cpp inside CFS::Bridge's anonymous namespace.
// Drives remote moon/station (orbital) continuations and post-jump
// reconciliation.

        // Shared per-tick context for the orbital phase handlers: the verified
        // retained identities, one processed HUD snapshot, and the helpers the
        // phases share. Built once per DriveOrbitalContinuation pass after the
        // identity, world, and ambiguity prologue has passed.
        struct OrbitalTick
        {
            const BodyDestination& destination;
            const OrbitalContinuation& continuation;
            std::uint32_t finalCourseTarget{ 0 };
            Clock::time_point now{};
            Clock::duration phaseAge{};
            ProcessedHudSnapshot hud;
            bool cruiseActive{ false };
            std::vector<HudRow> parentRows;
            std::vector<HudRow> finalRows;

            [[nodiscard]] std::vector<HudRow> RowsFor(std::uint32_t a_formID) const
            {
                std::vector<HudRow> matches;
                for (const auto& row : hud.rows) {
                    if (row.id == a_formID)
                        matches.push_back(row);
                }
                return matches;
            }

            [[nodiscard]] bool ContinuousCruiseExitExpired(OrbitalPhase a_phase) const
            {
                std::lock_guard lock{ g_orbitalMutex };
                auto& live = g_orbitalContinuation;
                if (live.phase != a_phase)
                    return false;
                if (cruiseActive) {
                    live.inactiveSince = {};
                    return false;
                }
                if (live.inactiveSince == Clock::time_point{})
                    live.inactiveSince = now;
                return now - live.inactiveSince > kOrbitalLockExitTimeout;
            }

            void CompleteFinalLock(bool a_stagedThroughParent) const
            {
                g_arrivalAudit.RecordCourseLock(hud.generation);
                g_coursePipeline.ClearAsked();
                g_state.store(NavState::kAutopilotLocked, std::memory_order_release);
                if (a_stagedThroughParent) {
                    REX::INFO("[orbital] engine confirmed exact final {} lock on {:08X} '{}' after exact waypoint {:08X} '{}' staging in one continuous Cruise session",
                        DestinationKindName(destination.kind),
                        finalCourseTarget, destination.localizedName,
                        continuation.parentFormID, continuation.parentName);
                } else {
                    REX::INFO("[orbital] engine confirmed exact final {} lock on {:08X} '{}' after one retained-target dispatch and latent stock course resolution",
                        DestinationKindName(destination.kind),
                        finalCourseTarget, destination.localizedName);
                }
                REX::INFO("[course] engine confirmed lock on {:08X} '{}'",
                    finalCourseTarget, destination.localizedName);
                if (destination.kind == BodyKind::kStation) {
                    g_pendingStationResolveTicks.store(0,
                        std::memory_order_release);
                    g_pendingStationAssignedID.store(0,
                        std::memory_order_release);
                }
                ResetOrbitalContinuation();
            }
        };

        void TickOrbitalAwaitingParentFeed(const OrbitalTick& a_tick)
        {
            if (a_tick.phaseAge > kOrbitalFeedTimeout) {
                FailOrbitalContinuation(std::format(
                    "private orbital waypoint did not remain a unique cockpit HUD row within {} seconds",
                    kOrbitalFeedTimeout.count()));
                return;
            }
            if (!g_haveCurrentSystem.load(std::memory_order_acquire))
                return;
            if (a_tick.parentRows.empty())
                return;
            bool firstResolved = false;
            if (!TryCommitOrbitalPhase(OrbitalPhase::kAwaitingParentFeed,
                    a_tick.continuation, [&](OrbitalContinuation& a_live) {
                        firstResolved = a_live.parentName.empty();
                        a_live.parentName = a_tick.parentRows.front().name.empty() ?
                            a_live.parentEditorID : a_tick.parentRows.front().name;
                    }))
                return;
            if (firstResolved) {
                REX::INFO("[orbital] exact private waypoint {:08X} '{}' confirmed as one HUD row; retained public {} destination remains {:08X} '{}'",
                    a_tick.continuation.parentFormID,
                    a_tick.parentRows.front().name.empty() ?
                        a_tick.continuation.parentEditorID :
                        a_tick.parentRows.front().name,
                    DestinationKindName(a_tick.destination.kind),
                    a_tick.destination.formID, a_tick.destination.localizedName);
            }
            BeginOrbitalCourse();
        }

        void TickOrbitalAwaitingParentCruise(const OrbitalTick& a_tick)
        {
            if (a_tick.phaseAge > kOrbitalCruiseTimeout)
                FailOrbitalContinuation(std::format(
                    "stock Cruise did not activate for the continuous final-target course within {} seconds",
                    kOrbitalCruiseTimeout.count()));
        }

        void TickOrbitalTraveling(const OrbitalTick& a_tick)
        {
            if (!a_tick.cruiseActive) {
                if (a_tick.ContinuousCruiseExitExpired(OrbitalPhase::kTraveling))
                    FailOrbitalContinuation("Cruise exited before exact final-target readback for the retained dispatch");
                return;
            }
            const auto asked = g_coursePipeline.Asked().id;
            if (!a_tick.continuation.dispatchConfirmed) {
                if (asked != 0 && asked != a_tick.finalCourseTarget) {
                    FailOrbitalContinuation("another course request replaced the retained final-target dispatch");
                    return;
                }
                if (asked == a_tick.finalCourseTarget) {
                    TryCommitOrbitalPhase(OrbitalPhase::kTraveling,
                        a_tick.continuation, [](OrbitalContinuation& a_live) {
                            a_live.dispatchConfirmed = true;
                        });
                } else {
                    // The queued dispatch has not registered yet. This is
                    // the only bounded part of the phase; travel itself is
                    // engine-owned and unbounded (Ariel/Chawla traces).
                    if (a_tick.phaseAge > kOrbitalCruiseTimeout)
                        FailOrbitalContinuation(std::format(
                            "retained final-target dispatch did not register within {} seconds",
                            kOrbitalCruiseTimeout.count()));
                    return;
                }
            } else if (asked != a_tick.finalCourseTarget) {
                FailOrbitalContinuation("retained final-target course request changed during latent resolution");
                return;
            }
            if (a_tick.hud.revision <= a_tick.continuation.feedRevisionFloor)
                return;
            if (a_tick.hud.course == a_tick.finalCourseTarget &&
                a_tick.finalRows.size() == 1 &&
                a_tick.finalRows.front().courseLocked) {
                a_tick.CompleteFinalLock(false);
                return;
            }
            if (a_tick.hud.course == a_tick.continuation.parentFormID &&
                a_tick.parentRows.size() == 1 &&
                a_tick.parentRows.front().courseLocked) {
                if (!TryCommitOrbitalPhase(OrbitalPhase::kTraveling,
                        a_tick.continuation, [&](OrbitalContinuation& a_live) {
                            AdvanceOrbitalPhase(a_live,
                                OrbitalPhase::kParentLocked, a_tick.now,
                                a_tick.hud.revision);
                        }))
                    return;
                g_coursePipeline.ClearAsked();
                g_state.store(NavState::kAwaitingCruise, std::memory_order_release);
                REX::INFO("[orbital] engine published exact private-waypoint lock {:08X} '{}' for retained final {} {:08X} '{}' in one continuous Cruise session",
                    a_tick.continuation.parentFormID, a_tick.continuation.parentName,
                    DestinationKindName(a_tick.destination.kind),
                    a_tick.finalCourseTarget, a_tick.destination.localizedName);
                return;
            }
            if (a_tick.hud.course != 0)
                FailOrbitalContinuation("engine exact-locked an unrelated cockpit target while the retained dispatch was active");
        }

        void TickOrbitalParentLocked(const OrbitalTick& a_tick)
        {
            if (a_tick.hud.course == a_tick.continuation.parentFormID) {
                if (a_tick.parentRows.size() != 1 ||
                    !a_tick.parentRows.front().courseLocked) {
                    FailOrbitalContinuation("private waypoint readback no longer has one exact locked HUD row");
                    return;
                }
                TryCommitOrbitalPhase(OrbitalPhase::kParentLocked,
                    a_tick.continuation, [&](OrbitalContinuation& a_live) {
                        a_live.feedRevisionFloor = std::max(
                            a_live.feedRevisionFloor, a_tick.hud.revision);
                        if (a_tick.cruiseActive)
                            a_live.inactiveSince = {};
                    });
                if (!a_tick.cruiseActive) {
                    if (a_tick.ContinuousCruiseExitExpired(OrbitalPhase::kParentLocked))
                        FailOrbitalContinuation("Cruise exited while the exact private waypoint course was still active");
                }
                return;
            }
            if (a_tick.hud.revision <= a_tick.continuation.feedRevisionFloor)
                return;
            if (a_tick.hud.course != 0 &&
                a_tick.hud.course != a_tick.finalCourseTarget) {
                FailOrbitalContinuation("engine course left the exact private waypoint for an unrelated target");
                return;
            }
            if (!a_tick.cruiseActive) {
                if (a_tick.ContinuousCruiseExitExpired(OrbitalPhase::kParentLocked))
                    FailOrbitalContinuation("Cruise exited before the final target became exact-lockable");
                return;
            }

            const bool finalExposed = a_tick.finalRows.size() == 1;
            if (!TryCommitOrbitalPhase(OrbitalPhase::kParentLocked,
                    a_tick.continuation, [&](OrbitalContinuation& a_live) {
                        AdvanceOrbitalPhase(a_live,
                            finalExposed ? OrbitalPhase::kAwaitingFinalLock :
                                           OrbitalPhase::kAwaitingParentArrival,
                            a_tick.now, a_tick.hud.revision);
                    }))
                return;
            g_state.store(NavState::kAwaitingCruise, std::memory_order_release);
            if (finalExposed) {
                REX::INFO("[orbital] waypoint {:08X} '{}' arrival/feed refresh independently confirmed: its exact lock ended and newer feed {} uniquely exposes retained final {} {:08X} '{}' while Cruise remains active",
                    a_tick.continuation.parentFormID, a_tick.continuation.parentName,
                    a_tick.hud.revision, DestinationKindName(a_tick.destination.kind),
                    a_tick.finalCourseTarget,
                    a_tick.destination.localizedName);
                if (a_tick.hud.course == a_tick.finalCourseTarget &&
                    a_tick.finalRows.front().courseLocked)
                    a_tick.CompleteFinalLock(true);
            } else {
                REX::INFO("[orbital] exact waypoint lock left {:08X} '{}'; continuous Cruise remains active while awaiting a newer feed that uniquely exposes retained final {} {:08X} '{}'",
                    a_tick.continuation.parentFormID, a_tick.continuation.parentName,
                    DestinationKindName(a_tick.destination.kind),
                    a_tick.finalCourseTarget, a_tick.destination.localizedName);
            }
        }

        void TickOrbitalAwaitingParentArrival(const OrbitalTick& a_tick)
        {
            if (a_tick.phaseAge > kOrbitalFeedTimeout) {
                FailOrbitalContinuation(std::format(
                    "waypoint lock ended without a newer unique final-target HUD row within {} seconds",
                    kOrbitalFeedTimeout.count()));
                return;
            }
            if (!a_tick.cruiseActive) {
                if (a_tick.ContinuousCruiseExitExpired(OrbitalPhase::kAwaitingParentArrival))
                    FailOrbitalContinuation("continuous Cruise exited before the final-target feed refresh");
                return;
            }
            if (!g_haveCurrentSystem.load(std::memory_order_acquire))
                return;
            if (a_tick.hud.revision <= a_tick.continuation.feedRevisionFloor)
                return;
            if (a_tick.hud.course == a_tick.continuation.parentFormID &&
                a_tick.parentRows.size() == 1 &&
                a_tick.parentRows.front().courseLocked) {
                if (TryCommitOrbitalPhase(OrbitalPhase::kAwaitingParentArrival,
                        a_tick.continuation, [&](OrbitalContinuation& a_live) {
                            AdvanceOrbitalPhase(a_live,
                                OrbitalPhase::kParentLocked, a_tick.now,
                                a_tick.hud.revision);
                        }))
                    REX::INFO("[orbital] exact waypoint lock {:08X} '{}' republished before the final-target feed transition",
                        a_tick.continuation.parentFormID,
                        a_tick.continuation.parentName);
                return;
            }
            if (a_tick.hud.course != 0 &&
                a_tick.hud.course != a_tick.finalCourseTarget) {
                FailOrbitalContinuation("engine selected an unrelated course while awaiting the final-target feed");
                return;
            }
            if (a_tick.finalRows.empty())
                return;
            if (!TryCommitOrbitalPhase(OrbitalPhase::kAwaitingParentArrival,
                    a_tick.continuation, [&](OrbitalContinuation& a_live) {
                        AdvanceOrbitalPhase(a_live,
                            OrbitalPhase::kAwaitingFinalLock, a_tick.now,
                            a_tick.hud.revision);
                    }))
                return;
            g_state.store(NavState::kAwaitingCruise, std::memory_order_release);
            REX::INFO("[orbital] waypoint {:08X} '{}' arrival/feed refresh independently confirmed: exact prior lock ended and newer feed {} uniquely exposes retained final {} {:08X} '{}' while Cruise remains active",
                a_tick.continuation.parentFormID, a_tick.continuation.parentName,
                a_tick.hud.revision,
                DestinationKindName(a_tick.destination.kind), a_tick.finalCourseTarget,
                a_tick.destination.localizedName);
            if (a_tick.hud.course == a_tick.finalCourseTarget &&
                a_tick.finalRows.front().courseLocked)
                a_tick.CompleteFinalLock(true);
        }

        void TickOrbitalAwaitingFinalLock(const OrbitalTick& a_tick)
        {
            if (a_tick.phaseAge > kOrbitalFeedTimeout) {
                FailOrbitalContinuation(std::format(
                    "continuous Cruise did not produce exact final-target lock within {} seconds of the waypoint feed transition",
                    kOrbitalFeedTimeout.count()));
                return;
            }
            if (!a_tick.cruiseActive) {
                if (a_tick.ContinuousCruiseExitExpired(OrbitalPhase::kAwaitingFinalLock))
                    FailOrbitalContinuation("continuous Cruise exited before exact final-target lock readback");
                return;
            }
            if (a_tick.hud.course == a_tick.finalCourseTarget &&
                a_tick.finalRows.size() == 1 &&
                a_tick.finalRows.front().courseLocked) {
                a_tick.CompleteFinalLock(true);
                return;
            }
            if (a_tick.hud.course != 0 &&
                a_tick.hud.course != a_tick.continuation.parentFormID) {
                FailOrbitalContinuation("continuous Cruise selected an unrelated target before exact final-target lock");
            }
        }

        // Ordered exact ancestry-waypoint advance shared by every traveling
        // station phase; the caller has already matched the fast-path guard.
        void AdvanceStationWaypointLock(const OrbitalTick& a_tick,
            std::size_t a_exactStationWaypoint)
        {
            const auto nextWaypoint =
                a_tick.continuation.stationWaypoints[a_exactStationWaypoint];
            const auto nextRows = a_tick.RowsFor(nextWaypoint.formID);
            {
                std::lock_guard lock{ g_orbitalMutex };
                auto& live = g_orbitalContinuation;
                if (live.finalKind != BodyKind::kStation ||
                    live.finalFormID != a_tick.destination.formID ||
                    live.finalCourseFormID != a_tick.finalCourseTarget ||
                    a_exactStationWaypoint < live.waypointIndex ||
                    a_exactStationWaypoint >= live.stationWaypoints.size() ||
                    live.stationWaypoints[a_exactStationWaypoint].formID !=
                        nextWaypoint.formID)
                    return;
                live.waypointIndex = a_exactStationWaypoint;
                live.parentFormID = nextWaypoint.formID;
                live.parentEditorID = nextWaypoint.editorID;
                live.parentName = nextRows.front().name.empty() ?
                    nextWaypoint.editorID : nextRows.front().name;
                live.phase = OrbitalPhase::kParentLocked;
                live.phaseStarted = a_tick.now;
                live.feedRevisionFloor = a_tick.hud.revision;
                live.inactiveSince = {};
            }
            g_coursePipeline.ClearAsked();
            g_state.store(NavState::kAwaitingCruise,
                std::memory_order_release);
            REX::INFO("[station] engine confirmed ordered exact ancestry-waypoint lock {}/{} on {:08X} '{}'; retained public destination remains {:08X} '{}'",
                a_exactStationWaypoint + 1,
                a_tick.continuation.stationWaypoints.size(), nextWaypoint.formID,
                nextRows.front().name.empty() ? nextWaypoint.editorID :
                                               nextRows.front().name,
                a_tick.destination.formID, a_tick.destination.localizedName);
        }

        void DriveOrbitalContinuation(const BodyDestination& a_destination)
        {
            const auto continuation = OrbitalState();
            if (!continuation)
                return;
            const auto finalCourseTarget = CourseTargetID(a_destination);
            if ((a_destination.kind != BodyKind::kMoon &&
                    a_destination.kind != BodyKind::kStation) ||
                continuation->finalKind != a_destination.kind ||
                continuation->finalFormID != a_destination.formID ||
                continuation->finalCourseFormID != finalCourseTarget ||
                continuation->system != a_destination.galaxy.system) {
                FailOrbitalContinuation("retained final orbital-target identity changed");
                return;
            }
            if (g_mapOpen.load(std::memory_order_acquire)) {
                // Browsing does not cancel an engine-owned latent course. Any
                // accepted selection calls StoreDestination(), which replaces the
                // public mark and resets this private continuation atomically.
                return;
            }

            const auto player = RE::PlayerCharacter::GetSingleton();
            const auto ship = player ? player->GetSpaceship() : nullptr;
            if (!IsShipInSpace(ship)) {
                if (WorldSettled())
                    FailOrbitalContinuation("landing, docking, or leaving the pilot seat during remote orbital continuation");
                return;
            }
            if (!WorldSettled())
                return;
            if (g_haveCurrentSystem.load(std::memory_order_acquire) &&
                g_currentSystem.load(std::memory_order_acquire) != continuation->system) {
                FailOrbitalContinuation("cockpit system no longer matches the retained final orbital target");
                return;
            }
            if (a_destination.kind == BodyKind::kStation) {
                const auto stations = ResolveStationTargets(a_destination.mapFormID);
                if (stations.size() != 1 ||
                    stations.front().referenceFormID != a_destination.formID ||
                    stations.front().baseFormID != a_destination.targetBaseFormID) {
                    FailOrbitalContinuation("retained station CELL no longer resolves to its exact live REFR/base identity");
                    return;
                }
                const auto indexed = BodyIndex::StationTargets(
                    a_destination.mapFormID);
                const auto exactIndexed = std::ranges::count_if(indexed,
                    [&](const BodyIndex::StationTarget& a_target) {
                        return a_target.referenceFormID == a_destination.formID &&
                               a_target.baseFormID == a_destination.targetBaseFormID &&
                               a_target.courseFormID == finalCourseTarget;
                    });
                const auto courseForm = RE::TESForm::LookupByID(finalCourseTarget);
                if (exactIndexed != 1 || !courseForm ||
                    !courseForm->As<RE::TESObjectREFR>()) {
                    FailOrbitalContinuation("retained station CELL/XMRK course identity no longer has one exact indexed live REFR");
                    return;
                }
            }

            const auto now = Clock::now();
            OrbitalTick tick{
                .destination = a_destination,
                .continuation = *continuation,
                .finalCourseTarget = finalCourseTarget,
                .now = now,
                .phaseAge = now - continuation->phaseStarted,
                .hud = CurrentProcessedHudSnapshot(),
                .cruiseActive = g_cruiseActive.load(std::memory_order_acquire),
            };
            tick.parentRows = tick.RowsFor(continuation->parentFormID);
            tick.finalRows = tick.RowsFor(finalCourseTarget);
            if (tick.parentRows.size() > 1) {
                FailOrbitalContinuation("private waypoint became ambiguous in the cockpit target feed");
                return;
            }
            if (tick.finalRows.size() > 1) {
                FailOrbitalContinuation("retained final target became ambiguous in the cockpit target feed");
                return;
            }
            std::optional<std::size_t> exactStationWaypoint;
            if (a_destination.kind == BodyKind::kStation) {
                if (continuation->stationWaypoints.empty() ||
                    continuation->waypointIndex >=
                        continuation->stationWaypoints.size() ||
                    continuation->stationWaypoints[continuation->waypointIndex].formID !=
                        continuation->parentFormID) {
                    FailOrbitalContinuation("private station waypoint chain state is invalid");
                    return;
                }
                for (std::size_t index = continuation->waypointIndex;
                     index < continuation->stationWaypoints.size(); ++index) {
                    const auto rows = tick.RowsFor(
                        continuation->stationWaypoints[index].formID);
                    if (rows.size() > 1) {
                        FailOrbitalContinuation("station ancestry waypoint became ambiguous in the cockpit target feed");
                        return;
                    }
                    if (tick.hud.course == continuation->stationWaypoints[index].formID &&
                        rows.size() == 1 && rows.front().courseLocked) {
                        exactStationWaypoint = index;
                    }
                }
            }

            const bool stationWaypointPhase =
                continuation->phase == OrbitalPhase::kTraveling ||
                continuation->phase == OrbitalPhase::kParentLocked ||
                continuation->phase == OrbitalPhase::kAwaitingParentArrival ||
                continuation->phase == OrbitalPhase::kAwaitingFinalLock;
            if (exactStationWaypoint && stationWaypointPhase &&
                tick.hud.revision > continuation->feedRevisionFloor &&
                (continuation->phase != OrbitalPhase::kParentLocked ||
                    *exactStationWaypoint != continuation->waypointIndex)) {
                AdvanceStationWaypointLock(tick, *exactStationWaypoint);
                return;
            }

            switch (continuation->phase) {
            case OrbitalPhase::kAwaitingParentFeed:
                TickOrbitalAwaitingParentFeed(tick);
                return;
            case OrbitalPhase::kAwaitingParentCruise:
                TickOrbitalAwaitingParentCruise(tick);
                return;
            case OrbitalPhase::kTraveling:
                TickOrbitalTraveling(tick);
                return;
            case OrbitalPhase::kParentLocked:
                TickOrbitalParentLocked(tick);
                return;
            case OrbitalPhase::kAwaitingParentArrival:
                TickOrbitalAwaitingParentArrival(tick);
                return;
            case OrbitalPhase::kAwaitingFinalLock:
                TickOrbitalAwaitingFinalLock(tick);
                return;
            default:
                return;
            }
        }

        void ReconcilePendingJump(const BodyDestination& a_destination)
        {
            if (!IsPlanetary(a_destination) &&
                a_destination.kind != BodyKind::kStation) {
                ClearDestination("invalid non-routable pending-jump state");
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
            const bool haveCurrentSystem =
                g_haveCurrentSystem.load(std::memory_order_acquire);
            const auto currentSystem =
                g_currentSystem.load(std::memory_order_acquire);
            if (a_destination.kind == BodyKind::kStation &&
                g_pendingStationResolveTicks.load(std::memory_order_acquire) != 0 &&
                haveCurrentSystem && currentSystem != a_destination.galaxy.system &&
                WorldSettled()) {
                ClearDestination(
                    "system mismatch after remote station arrival processing began");
                return;
            }
            if (!haveCurrentSystem ||
                currentSystem != a_destination.galaxy.system ||
                g_mapOpen.load(std::memory_order_acquire) || !WorldSettled())
                return;

            if (a_destination.kind == BodyKind::kStation) {
                const auto now = Clock::now();
                auto startedTicks =
                    g_pendingStationResolveTicks.load(std::memory_order_acquire);
                if (startedTicks == 0) {
                    const auto desired = now.time_since_epoch().count();
                    g_pendingStationResolveTicks.compare_exchange_strong(startedTicks,
                        desired, std::memory_order_acq_rel);
                    startedTicks = g_pendingStationResolveTicks.load(
                        std::memory_order_acquire);
                    REX::INFO("[station] settled target system {}; resolving remote CELL {:08X}/{} to retained physical REFR/base {:08X}/{:08X} and course XMRK {:08X}",
                        a_destination.galaxy.system, a_destination.mapFormID,
                        a_destination.mapType, a_destination.formID,
                        a_destination.targetBaseFormID,
                        a_destination.courseFormID);
                }
                auto phaseStarted = Clock::time_point{ Clock::duration{
                    startedTicks } };
                const auto indexedTargets =
                    BodyIndex::StationTargets(a_destination.mapFormID);
                const auto exactIndexed = std::ranges::count_if(indexedTargets,
                    [&](const BodyIndex::StationTarget& a_target) {
                        return a_target.referenceFormID == a_destination.formID &&
                               a_target.baseFormID == a_destination.targetBaseFormID &&
                               a_target.courseFormID ==
                                   a_destination.courseFormID;
                    });
                const auto courseMarker = a_destination.courseFormID ?
                    RE::TESForm::LookupByID(a_destination.courseFormID) : nullptr;
                if (exactIndexed != 1 || !courseMarker ||
                    !courseMarker->As<RE::TESObjectREFR>()) {
                    ClearDestination("remote station CELL/XMRK course identity failed exact live revalidation after arrival");
                    return;
                }
                const auto stationTargets =
                    ResolveStationTargets(a_destination.mapFormID);
                if (stationTargets.size() > 1) {
                    ClearDestination("remote station CELL resolved to ambiguous live references after arrival");
                    return;
                }
                if (stationTargets.empty()) {
                    if (now - phaseStarted > kRemoteStationResolveTimeout)
                        ClearDestination(std::format(
                            "remote station REFR did not become live within {} seconds of settled system arrival",
                            kRemoteStationResolveTimeout.count()).c_str());
                    return;
                }
                if (!a_destination.targetBaseFormID ||
                    stationTargets.front().referenceFormID != a_destination.formID ||
                    stationTargets.front().baseFormID != a_destination.targetBaseFormID) {
                    REX::WARN("[station] remote CELL {:08X} live identity REFR/base {:08X}/{:08X} does not match retained index {:08X}/{:08X}",
                        a_destination.mapFormID,
                        stationTargets.front().referenceFormID,
                        stationTargets.front().baseFormID,
                        a_destination.formID,
                        a_destination.targetBaseFormID);
                    ClearDestination("remote station live identity differs from retained index identity");
                    return;
                }
                if (g_pendingStationAssignedID.load(std::memory_order_acquire) !=
                    a_destination.formID) {
                    if (!AssignNativeShipTarget(a_destination)) {
                        ClearDestination("remote station native target assignment failed after arrival");
                        return;
                    }
                    g_pendingStationAssignedID.store(a_destination.formID,
                        std::memory_order_release);
                    g_pendingStationResolveTicks.store(
                        now.time_since_epoch().count(), std::memory_order_release);
                    REX::INFO("[station] exact live remote station {:08X} assigned after system arrival; checking direct HUD exposure before exact orbital staging",
                        a_destination.formID);
                }

                const auto stationRows = CurrentHudTargets(
                    CourseTargetID(a_destination));
                if (stationRows.size() > 1) {
                    ClearDestination("remote station has ambiguous cockpit HUD rows after native assignment");
                    return;
                }
                if (stationRows.empty()) {
                    StartRemoteStationContinuation(a_destination);
                    return;
                }
            }

            // Resolver agreement alone is not enough. A directly exposed final
            // body retains the original exact-one-row path. A missing remote
            // moon may stage only through its unique live PNDT/GNAM parent. A
            // station reaches this shared path only when its exact CELL-owned
            // XMRK course identity is already exposed; otherwise its separately
            // validated CELL/DNAM/GNAM ancestry owns the private continuation.
            // Remote planets and ambiguous final rows never take either detour.
            const auto finalRows = CurrentHudTargets(CourseTargetID(a_destination));
            if (finalRows.size() > 1) {
                ClearDestination("pending-jump destination has ambiguous cockpit HUD rows");
                return;
            }
            if (finalRows.empty()) {
                if (a_destination.kind == BodyKind::kMoon)
                    StartRemoteMoonContinuation(a_destination);
                return;
            }

            if (g_cruiseActive.load(std::memory_order_acquire)) {
                auto expected = NavState::kPendingJump;
                if (g_state.compare_exchange_strong(expected, NavState::kAwaitingCruise,
                        std::memory_order_acq_rel)) {
                    QueueCourse(CourseTargetID(a_destination), false);
                    REX::INFO("[destination] pending jump arrived in system {}; Cruise already active, queued course {:08X}",
                        a_destination.galaxy.system,
                        CourseTargetID(a_destination));
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
            if (!g_hudCruiseInput.QueuePress(device)) {
                g_state.store(NavState::kPendingJump, std::memory_order_release);
                return;
            }
            REX::INFO("[destination] pending jump arrived in system {}; queued latched stock HUD Cruise press for {:08X} '{}'",
                a_destination.galaxy.system, CourseTargetID(a_destination),
                a_destination.localizedName);
        }

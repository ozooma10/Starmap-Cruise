// Included by Bridge.cpp inside CFS::Bridge's anonymous namespace.
// Owns subscriptions, lifecycle, continuation drivers, and frame pump.

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
                    const bool executeAcknowledged = accepted &&
                        ConsumeRemoteExecuteCloseAcknowledgement();
                    if (executeAcknowledged) {
                        REX::INFO("[jump] stock Execute Route acknowledged by Starmap close");
                    } else if (accepted && RemoteRouteRequestActive()) {
                        CancelOrReleaseHudCruiseInput(
                            "Starmap closed during remote route handoff");
                        ClearDestination("Starmap closed before vanilla route became executable");
                        accepted = false;
                        REX::WARN("[jump] Starmap closed during remote Set Course handoff; mark cleared");
                    }
                    auto acceptedDestination = accepted ? Destination() : std::nullopt;
                    const bool pendingJump = acceptedDestination &&
                        UsesRemoteSystemRoute(*acceptedDestination) &&
                        (g_state.load(std::memory_order_acquire) == NavState::kPendingJump ||
                            (g_haveCurrentSystem.load(std::memory_order_acquire) &&
                                acceptedDestination->galaxy.system !=
                                    g_currentSystem.load(std::memory_order_acquire)));
                    if (acceptedDestination && !pendingJump &&
                        acceptedDestination->kind == BodyKind::kStation &&
                        !AssignNativeShipTarget(*acceptedDestination)) {
                        ClearDestination("native station target assignment failed");
                        accepted = false;
                        acceptedDestination.reset();
                    }
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
                                QueueCourse(CourseTargetID(*destination), false);
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

        void DriveRemoteMoonContinuation(const BodyDestination& a_destination)
        {
            const auto continuation = RemoteMoonState();
            if (!continuation)
                return;
            const auto finalCourseTarget = CourseTargetID(a_destination);
            if ((a_destination.kind != BodyKind::kMoon &&
                    a_destination.kind != BodyKind::kStation) ||
                continuation->finalKind != a_destination.kind ||
                continuation->finalFormID != a_destination.formID ||
                continuation->finalCourseFormID != finalCourseTarget ||
                continuation->system != a_destination.galaxy.system) {
                FailRemoteMoonContinuation("retained final orbital-target identity changed");
                return;
            }
            if (g_mapOpen.load(std::memory_order_acquire)) {
                // Browsing does not cancel an engine-owned latent course. Any
                // accepted selection calls SetDestination(), which replaces the
                // public mark and resets this private continuation atomically.
                return;
            }

            const auto player = RE::PlayerCharacter::GetSingleton();
            const auto ship = player ? player->GetSpaceship() : nullptr;
            if (!IsShipInSpace(ship)) {
                if (WorldSettled())
                    FailRemoteMoonContinuation("landing, docking, or leaving the pilot seat during remote orbital continuation");
                return;
            }
            if (!WorldSettled())
                return;
            if (g_haveCurrentSystem.load(std::memory_order_acquire) &&
                g_currentSystem.load(std::memory_order_acquire) != continuation->system) {
                FailRemoteMoonContinuation("cockpit system no longer matches the retained final orbital target");
                return;
            }
            if (a_destination.kind == BodyKind::kStation) {
                const auto stations = ResolveStationTargets(a_destination.mapFormID);
                if (stations.size() != 1 ||
                    stations.front().referenceFormID != a_destination.formID ||
                    stations.front().baseFormID != a_destination.targetBaseFormID) {
                    FailRemoteMoonContinuation("retained station CELL no longer resolves to its exact live REFR/base identity");
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
                    FailRemoteMoonContinuation("retained station CELL/XMRK course identity no longer has one exact indexed live REFR");
                    return;
                }
            }

            const auto now = Clock::now();
            const auto phaseAge = now - continuation->phaseStarted;
            const auto hud = CurrentProcessedHudSnapshot();
            const auto feedRevision = hud.revision;
            const auto course = hud.course;
            const auto cruiseActive =
                g_cruiseActive.load(std::memory_order_acquire);
            const auto snapshotTargets = [&](std::uint32_t a_formID) {
                std::vector<HudRow> matches;
                for (const auto& row : hud.rows) {
                    if (row.id == a_formID)
                        matches.push_back(row);
                }
                return matches;
            };
            const auto parentRows = snapshotTargets(continuation->parentFormID);
            const auto finalRows = snapshotTargets(finalCourseTarget);
            if (parentRows.size() > 1) {
                FailRemoteMoonContinuation("private waypoint became ambiguous in the cockpit target feed");
                return;
            }
            if (finalRows.size() > 1) {
                FailRemoteMoonContinuation("retained final target became ambiguous in the cockpit target feed");
                return;
            }
            std::optional<std::size_t> exactStationWaypoint;
            if (a_destination.kind == BodyKind::kStation) {
                if (continuation->stationWaypoints.empty() ||
                    continuation->waypointIndex >=
                        continuation->stationWaypoints.size() ||
                    continuation->stationWaypoints[continuation->waypointIndex].formID !=
                        continuation->parentFormID) {
                    FailRemoteMoonContinuation("private station waypoint chain state is invalid");
                    return;
                }
                for (std::size_t index = continuation->waypointIndex;
                     index < continuation->stationWaypoints.size(); ++index) {
                    const auto rows = snapshotTargets(
                        continuation->stationWaypoints[index].formID);
                    if (rows.size() > 1) {
                        FailRemoteMoonContinuation("station ancestry waypoint became ambiguous in the cockpit target feed");
                        return;
                    }
                    if (course == continuation->stationWaypoints[index].formID &&
                        rows.size() == 1 && rows.front().courseLocked) {
                        exactStationWaypoint = index;
                    }
                }
            }

            const auto continuousCruiseExitExpired =
                [&](RemoteMoonPhase a_phase) {
                    std::lock_guard lock{ g_remoteMoonMutex };
                    auto& live = g_remoteMoonContinuation;
                    if (live.phase != a_phase)
                        return false;
                    if (cruiseActive) {
                        live.inactiveSince = {};
                        return false;
                    }
                    if (live.inactiveSince == Clock::time_point{})
                        live.inactiveSince = now;
                    return now - live.inactiveSince > kRemoteMoonLockExitTimeout;
                };

            const auto completeFinalLock = [&](bool a_stagedThroughParent) {
                g_courseWasLocked.store(true, std::memory_order_release);
                g_courseAskedID.store(0, std::memory_order_release);
                g_courseAskedClearing.store(false, std::memory_order_release);
                g_state.store(NavState::kAutopilotLocked, std::memory_order_release);
                if (a_stagedThroughParent) {
                    REX::INFO("[orbital] engine confirmed exact final {} lock on {:08X} '{}' after exact waypoint {:08X} '{}' staging in one continuous Cruise session",
                        DestinationKindName(a_destination.kind),
                        finalCourseTarget, a_destination.localizedName,
                        continuation->parentFormID, continuation->parentName);
                } else {
                    REX::INFO("[orbital] engine confirmed exact final {} lock on {:08X} '{}' after one retained-target dispatch and latent stock course resolution",
                        DestinationKindName(a_destination.kind),
                        finalCourseTarget, a_destination.localizedName);
                }
                REX::INFO("[course] engine confirmed lock on {:08X} '{}'",
                    finalCourseTarget, a_destination.localizedName);
                if (a_destination.kind == BodyKind::kStation) {
                    g_pendingStationResolveTicks.store(0,
                        std::memory_order_release);
                    g_pendingStationAssignedID.store(0,
                        std::memory_order_release);
                }
                ResetRemoteMoonContinuation();
            };

            const bool stationWaypointPhase =
                continuation->phase == RemoteMoonPhase::kAwaitingParentLock ||
                continuation->phase == RemoteMoonPhase::kAwaitingLatentFinalLock ||
                continuation->phase == RemoteMoonPhase::kParentLocked ||
                continuation->phase == RemoteMoonPhase::kAwaitingParentArrival ||
                continuation->phase == RemoteMoonPhase::kAwaitingFinalLock;
            if (exactStationWaypoint && stationWaypointPhase &&
                feedRevision > continuation->feedRevisionFloor &&
                (continuation->phase != RemoteMoonPhase::kParentLocked ||
                    *exactStationWaypoint != continuation->waypointIndex)) {
                const auto nextWaypoint =
                    continuation->stationWaypoints[*exactStationWaypoint];
                const auto nextRows = snapshotTargets(nextWaypoint.formID);
                {
                    std::lock_guard lock{ g_remoteMoonMutex };
                    auto& live = g_remoteMoonContinuation;
                    if (live.finalKind != BodyKind::kStation ||
                        live.finalFormID != a_destination.formID ||
                        live.finalCourseFormID != finalCourseTarget ||
                        *exactStationWaypoint < live.waypointIndex ||
                        *exactStationWaypoint >= live.stationWaypoints.size() ||
                        live.stationWaypoints[*exactStationWaypoint].formID !=
                            nextWaypoint.formID)
                        return;
                    live.waypointIndex = *exactStationWaypoint;
                    live.parentFormID = nextWaypoint.formID;
                    live.parentEditorID = nextWaypoint.editorID;
                    live.parentName = nextRows.front().name.empty() ?
                        nextWaypoint.editorID : nextRows.front().name;
                    live.phase = RemoteMoonPhase::kParentLocked;
                    live.phaseStarted = now;
                    live.feedRevisionFloor = feedRevision;
                    live.inactiveSince = {};
                }
                g_courseAskedID.store(0, std::memory_order_release);
                g_courseAskedClearing.store(false, std::memory_order_release);
                g_state.store(NavState::kAwaitingCruise,
                    std::memory_order_release);
                REX::INFO("[station] engine confirmed ordered exact ancestry-waypoint lock {}/{} on {:08X} '{}'; retained public destination remains {:08X} '{}'",
                    *exactStationWaypoint + 1,
                    continuation->stationWaypoints.size(), nextWaypoint.formID,
                    nextRows.front().name.empty() ? nextWaypoint.editorID :
                                                   nextRows.front().name,
                    a_destination.formID, a_destination.localizedName);
                return;
            }

            switch (continuation->phase) {
            case RemoteMoonPhase::kAwaitingParentFeed: {
                if (phaseAge > kRemoteMoonFeedTimeout) {
                    FailRemoteMoonContinuation("private orbital waypoint did not remain a unique cockpit HUD row within 10 seconds");
                    return;
                }
                if (!g_haveCurrentSystem.load(std::memory_order_acquire))
                    return;
                if (parentRows.empty())
                    return;
                bool firstResolved = false;
                {
                    std::lock_guard lock{ g_remoteMoonMutex };
                    if (g_remoteMoonContinuation.phase != RemoteMoonPhase::kAwaitingParentFeed)
                        return;
                    firstResolved = g_remoteMoonContinuation.parentName.empty();
                    g_remoteMoonContinuation.parentName = parentRows.front().name.empty() ?
                        g_remoteMoonContinuation.parentEditorID : parentRows.front().name;
                }
                if (firstResolved) {
                    REX::INFO("[orbital] exact private waypoint {:08X} '{}' confirmed as one HUD row; retained public {} destination remains {:08X} '{}'",
                        continuation->parentFormID,
                        parentRows.front().name.empty() ? continuation->parentEditorID : parentRows.front().name,
                        DestinationKindName(a_destination.kind),
                        a_destination.formID, a_destination.localizedName);
                }
                BeginRemoteMoonCourse();
                return;
            }
            case RemoteMoonPhase::kAwaitingParentCruise:
                if (phaseAge > kRemoteMoonCruiseTimeout)
                    FailRemoteMoonContinuation("stock Cruise did not activate for the continuous final-target course within 5 seconds");
                return;
            case RemoteMoonPhase::kAwaitingParentLock: {
                if (!cruiseActive) {
                    if (continuousCruiseExitExpired(RemoteMoonPhase::kAwaitingParentLock))
                        FailRemoteMoonContinuation("Cruise exited before stock resolved the retained final-target dispatch");
                    return;
                }
                const auto asked = g_courseAskedID.load(std::memory_order_acquire);
                if (asked != 0 && asked != finalCourseTarget) {
                    FailRemoteMoonContinuation("another course request replaced the retained final-target dispatch");
                    return;
                }
                if (asked != finalCourseTarget)
                    return;
                if (feedRevision > continuation->feedRevisionFloor) {
                    if (course == finalCourseTarget && finalRows.size() == 1 &&
                        finalRows.front().courseLocked) {
                        completeFinalLock(false);
                        return;
                    }
                    if (course == continuation->parentFormID && parentRows.size() == 1 &&
                        parentRows.front().courseLocked) {
                        std::lock_guard lock{ g_remoteMoonMutex };
                        auto& live = g_remoteMoonContinuation;
                        if (live.phase != RemoteMoonPhase::kAwaitingParentLock ||
                            live.finalFormID != a_destination.formID ||
                            live.finalCourseFormID != finalCourseTarget ||
                            live.parentFormID != course)
                            return;
                        live.phase = RemoteMoonPhase::kParentLocked;
                        live.phaseStarted = now;
                        live.feedRevisionFloor = feedRevision;
                        live.inactiveSince = {};
                        g_courseAskedID.store(0, std::memory_order_release);
                        g_courseAskedClearing.store(false, std::memory_order_release);
                        g_state.store(NavState::kAwaitingCruise, std::memory_order_release);
                        REX::INFO("[orbital] engine resolved retained final {} {:08X} '{}' through exact private-waypoint lock {:08X} '{}' in one continuous Cruise session",
                            DestinationKindName(a_destination.kind),
                            finalCourseTarget, a_destination.localizedName,
                            continuation->parentFormID, continuation->parentName);
                        return;
                    }
                    if (course != 0) {
                        FailRemoteMoonContinuation("final-target dispatch exact-locked an unrelated cockpit target");
                        return;
                    }
                }
                if (phaseAge > kRemoteMoonCruiseTimeout) {
                    {
                        std::lock_guard lock{ g_remoteMoonMutex };
                        auto& live = g_remoteMoonContinuation;
                        if (live.phase != RemoteMoonPhase::kAwaitingParentLock)
                            return;
                        live.phase = RemoteMoonPhase::kAwaitingLatentFinalLock;
                        live.phaseStarted = now;
                        live.feedRevisionFloor = feedRevision;
                        live.inactiveSince = {};
                    }
                    REX::INFO("[orbital] no immediate exact waypoint/final lock after 5 seconds; stock Cruise remains active and the single retained-target dispatch is entering its unbounded engine-owned travel phase");
                }
                return;
            }
            case RemoteMoonPhase::kAwaitingLatentFinalLock: {
                if (!cruiseActive) {
                    if (continuousCruiseExitExpired(RemoteMoonPhase::kAwaitingLatentFinalLock))
                        FailRemoteMoonContinuation("Cruise exited before exact final-target readback during latent resolution");
                    return;
                }
                const auto asked = g_courseAskedID.load(std::memory_order_acquire);
                if (asked != finalCourseTarget) {
                    FailRemoteMoonContinuation("retained final-target course request changed during latent resolution");
                    return;
                }
                if (feedRevision <= continuation->feedRevisionFloor)
                    return;
                if (course == finalCourseTarget && finalRows.size() == 1 &&
                    finalRows.front().courseLocked) {
                    completeFinalLock(false);
                    return;
                }
                if (course == continuation->parentFormID && parentRows.size() == 1 &&
                    parentRows.front().courseLocked) {
                    std::lock_guard lock{ g_remoteMoonMutex };
                    auto& live = g_remoteMoonContinuation;
                    if (live.phase != RemoteMoonPhase::kAwaitingLatentFinalLock ||
                        live.finalFormID != a_destination.formID ||
                        live.finalCourseFormID != finalCourseTarget ||
                        live.parentFormID != course)
                        return;
                    live.phase = RemoteMoonPhase::kParentLocked;
                    live.phaseStarted = now;
                    live.feedRevisionFloor = feedRevision;
                    live.inactiveSince = {};
                    g_courseAskedID.store(0, std::memory_order_release);
                    g_courseAskedClearing.store(false, std::memory_order_release);
                    REX::INFO("[orbital] latent stock course published exact private-waypoint lock {:08X} '{}' for retained final {} {:08X} '{}'",
                        continuation->parentFormID, continuation->parentName,
                        DestinationKindName(a_destination.kind),
                        finalCourseTarget, a_destination.localizedName);
                    return;
                }
                if (course != 0)
                    FailRemoteMoonContinuation("latent stock course exact-locked an unrelated cockpit target");
                return;
            }
            case RemoteMoonPhase::kParentLocked: {
                if (course == continuation->parentFormID) {
                    if (parentRows.size() != 1 || !parentRows.front().courseLocked) {
                        FailRemoteMoonContinuation("private waypoint readback no longer has one exact locked HUD row");
                        return;
                    }
                    {
                        std::lock_guard lock{ g_remoteMoonMutex };
                        auto& live = g_remoteMoonContinuation;
                        if (live.phase == RemoteMoonPhase::kParentLocked) {
                            live.feedRevisionFloor = std::max(
                                live.feedRevisionFloor, feedRevision);
                            if (cruiseActive)
                                live.inactiveSince = {};
                        }
                    }
                    if (!cruiseActive) {
                        if (continuousCruiseExitExpired(RemoteMoonPhase::kParentLocked))
                            FailRemoteMoonContinuation("Cruise exited while the exact private waypoint course was still active");
                    }
                    return;
                }
                if (feedRevision <= continuation->feedRevisionFloor)
                    return;
                if (course != 0 && course != finalCourseTarget) {
                    FailRemoteMoonContinuation("engine course left the exact private waypoint for an unrelated target");
                    return;
                }
                if (!cruiseActive) {
                    if (continuousCruiseExitExpired(RemoteMoonPhase::kParentLocked))
                        FailRemoteMoonContinuation("Cruise exited before the final target became exact-lockable");
                    return;
                }

                const bool finalExposed = finalRows.size() == 1;
                {
                    std::lock_guard lock{ g_remoteMoonMutex };
                    auto& live = g_remoteMoonContinuation;
                    if (live.phase != RemoteMoonPhase::kParentLocked)
                        return;
                    live.phase = finalExposed ? RemoteMoonPhase::kAwaitingFinalLock :
                                               RemoteMoonPhase::kAwaitingParentArrival;
                    live.phaseStarted = now;
                    live.feedRevisionFloor = feedRevision;
                    live.inactiveSince = {};
                }
                g_state.store(NavState::kAwaitingCruise, std::memory_order_release);
                if (finalExposed) {
                    REX::INFO("[orbital] waypoint {:08X} '{}' arrival/feed refresh independently confirmed: its exact lock ended and newer feed {} uniquely exposes retained final {} {:08X} '{}' while Cruise remains active",
                        continuation->parentFormID, continuation->parentName,
                        feedRevision, DestinationKindName(a_destination.kind),
                        finalCourseTarget,
                        a_destination.localizedName);
                    if (course == finalCourseTarget && finalRows.front().courseLocked)
                        completeFinalLock(true);
                } else {
                    REX::INFO("[orbital] exact waypoint lock left {:08X} '{}'; continuous Cruise remains active while awaiting a newer feed that uniquely exposes retained final {} {:08X} '{}'",
                        continuation->parentFormID, continuation->parentName,
                        DestinationKindName(a_destination.kind),
                        finalCourseTarget, a_destination.localizedName);
                }
                return;
            }
            case RemoteMoonPhase::kAwaitingParentArrival: {
                if (phaseAge > kRemoteMoonFeedTimeout) {
                    FailRemoteMoonContinuation("waypoint lock ended without a newer unique final-target HUD row within 10 seconds");
                    return;
                }
                if (!cruiseActive) {
                    if (continuousCruiseExitExpired(RemoteMoonPhase::kAwaitingParentArrival))
                        FailRemoteMoonContinuation("continuous Cruise exited before the final-target feed refresh");
                    return;
                }
                if (!g_haveCurrentSystem.load(std::memory_order_acquire))
                    return;
                if (feedRevision <= continuation->feedRevisionFloor)
                    return;
                if (course == continuation->parentFormID && parentRows.size() == 1 &&
                    parentRows.front().courseLocked) {
                    std::lock_guard lock{ g_remoteMoonMutex };
                    auto& live = g_remoteMoonContinuation;
                    if (live.phase != RemoteMoonPhase::kAwaitingParentArrival)
                        return;
                    live.phase = RemoteMoonPhase::kParentLocked;
                    live.phaseStarted = now;
                    live.feedRevisionFloor = feedRevision;
                    live.inactiveSince = {};
                    REX::INFO("[orbital] exact waypoint lock {:08X} '{}' republished before the final-target feed transition",
                        continuation->parentFormID, continuation->parentName);
                    return;
                }
                if (course != 0 && course != finalCourseTarget) {
                    FailRemoteMoonContinuation("engine selected an unrelated course while awaiting the final-target feed");
                    return;
                }
                if (finalRows.empty())
                    return;
                {
                    std::lock_guard lock{ g_remoteMoonMutex };
                    auto& live = g_remoteMoonContinuation;
                    if (live.phase != RemoteMoonPhase::kAwaitingParentArrival)
                        return;
                    live.phase = RemoteMoonPhase::kAwaitingFinalLock;
                    live.phaseStarted = now;
                    live.feedRevisionFloor = feedRevision;
                    live.inactiveSince = {};
                }
                g_state.store(NavState::kAwaitingCruise, std::memory_order_release);
                REX::INFO("[orbital] waypoint {:08X} '{}' arrival/feed refresh independently confirmed: exact prior lock ended and newer feed {} uniquely exposes retained final {} {:08X} '{}' while Cruise remains active",
                    continuation->parentFormID, continuation->parentName, feedRevision,
                    DestinationKindName(a_destination.kind), finalCourseTarget,
                    a_destination.localizedName);
                if (course == finalCourseTarget && finalRows.front().courseLocked)
                    completeFinalLock(true);
                return;
            }
            case RemoteMoonPhase::kAwaitingFinalLock: {
                if (phaseAge > kRemoteMoonFeedTimeout) {
                    FailRemoteMoonContinuation("continuous Cruise did not produce exact final-target lock within 10 seconds of the waypoint feed transition");
                    return;
                }
                if (!cruiseActive) {
                    if (continuousCruiseExitExpired(RemoteMoonPhase::kAwaitingFinalLock))
                        FailRemoteMoonContinuation("continuous Cruise exited before exact final-target lock readback");
                    return;
                }
                if (course == finalCourseTarget && finalRows.size() == 1 &&
                    finalRows.front().courseLocked) {
                    completeFinalLock(true);
                    return;
                }
                if (course != 0 && course != continuation->parentFormID) {
                    FailRemoteMoonContinuation("continuous Cruise selected an unrelated target before exact final-target lock");
                }
                return;
            }
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
                        ClearDestination("remote station REFR did not become live within 10 seconds of settled system arrival");
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
            if (!QueueHudCruisePress(device)) {
                g_state.store(NavState::kPendingJump, std::memory_order_release);
                return;
            }
            REX::INFO("[destination] pending jump arrived in system {}; queued latched stock HUD Cruise press for {:08X} '{}'",
                a_destination.galaxy.system, CourseTargetID(a_destination),
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

// Included by Bridge.cpp inside CFS::Bridge's anonymous namespace.
// Owns exact-course requests, continuation setup, and stock UI dispatch.

        void QueueCourse(std::uint32_t a_id, bool a_clearing)
        {
            g_coursePipeline.Queue(a_id, a_clearing);
            g_hudUiDirty.store(true, std::memory_order_release);
            if (Settings::Verbose())
                REX::INFO("[course] queued {} for {:08X}", a_clearing ? "clear" : "lock", a_id);
        }

        // The continuation drivers tick from a lock-free copy; every mutation
        // re-locks and re-verifies the continuation identity (phase, the
        // retained final-target tuple, system, and the current private
        // waypoint) before committing, mirroring TryCommitRemoteRoutePhase.
        // A false return means the live continuation moved on and this tick's
        // decision must be abandoned.
        template <class Apply>
        bool TryCommitOrbitalPhase(OrbitalPhase a_expectedPhase,
            const OrbitalContinuation& a_continuation, Apply&& a_apply)
        {
            std::lock_guard lock{ g_orbitalMutex };
            auto& live = g_orbitalContinuation;
            if (live.phase != a_expectedPhase ||
                live.finalKind != a_continuation.finalKind ||
                live.finalFormID != a_continuation.finalFormID ||
                live.finalCourseFormID != a_continuation.finalCourseFormID ||
                live.system != a_continuation.system ||
                live.parentFormID != a_continuation.parentFormID)
                return false;
            a_apply(live);
            return true;
        }

        // The common phase-transition tail every commit shares.
        void AdvanceOrbitalPhase(OrbitalContinuation& a_live,
            OrbitalPhase a_phase, Clock::time_point a_now,
            std::uint64_t a_feedRevisionFloor)
        {
            a_live.phase = a_phase;
            a_live.phaseStarted = a_now;
            a_live.feedRevisionFloor = a_feedRevisionFloor;
            a_live.inactiveSince = {};
        }

        bool StartRemoteMoonContinuation(const BodyDestination& a_destination)
        {
            if (a_destination.kind != BodyKind::kMoon)
                return false;

            const auto parents = BodyIndex::ParentPlanets(a_destination.formID);
            if (parents.size() != 1) {
                REX::WARN("[moon] refusing automatic continuation for {:08X} '{}': parent identity is {} ({} exact GNAM candidates)",
                    a_destination.formID, a_destination.localizedName,
                    parents.empty() ? "missing" : "ambiguous", parents.size());
                ClearDestination("missing or ambiguous live PNDT/GNAM parent identity");
                return false;
            }

            const auto& parent = parents.front();
            const auto liveParent = RE::TESForm::LookupByID(parent.formID);
            if (!liveParent || liveParent->GetFormType() != RE::FormType::kPNDT ||
                parent.galaxy.system != a_destination.galaxy.system ||
                parent.galaxy.parent != 0 ||
                parent.galaxy.planet != a_destination.galaxy.parent) {
                REX::WARN("[moon] refusing automatic continuation for {:08X} '{}': exact parent candidate {:08X} failed live PNDT/GNAM validation",
                    a_destination.formID, a_destination.localizedName, parent.formID);
                ClearDestination("live PNDT/GNAM parent validation failed");
                return false;
            }

            {
                std::lock_guard lock{ g_orbitalMutex };
                if (g_orbitalContinuation.phase != OrbitalPhase::kNone)
                    return g_orbitalContinuation.finalKind == a_destination.kind &&
                           g_orbitalContinuation.finalFormID == a_destination.formID;
                g_orbitalContinuation = {
                    .phase = OrbitalPhase::kAwaitingParentFeed,
                    .finalKind = BodyKind::kMoon,
                    .finalFormID = a_destination.formID,
                    .finalCourseFormID = CourseTargetID(a_destination),
                    .system = a_destination.galaxy.system,
                    .parentFormID = parent.formID,
                    .parentEditorID = parent.editorID,
                    .feedRevisionFloor = g_hudLowRevision.load(std::memory_order_acquire),
                    .phaseStarted = Clock::now(),
                };
            }
            g_state.store(NavState::kAwaitingCruise, std::memory_order_release);
            REX::INFO("[moon] final {:08X} '{}' is absent from the settled cockpit feed; exact GNAM parent {:08X} '{}' retained as a private waypoint",
                a_destination.formID, a_destination.localizedName, parent.formID,
                parent.editorID);
            return true;
        }

        bool StartRemoteStationContinuation(const BodyDestination& a_destination)
        {
            if (a_destination.kind != BodyKind::kStation)
                return false;

            const auto courseMarkerForm = a_destination.courseFormID ?
                RE::TESForm::LookupByID(a_destination.courseFormID) : nullptr;
            const auto courseMarker = courseMarkerForm ?
                courseMarkerForm->As<RE::TESObjectREFR>() : nullptr;
            if (!courseMarker ||
                a_destination.courseFormID == a_destination.formID) {
                REX::WARN("[station] refusing automatic continuation for {:08X} '{}': retained exact CELL/XMRK course identity {:08X} is not a distinct live REFR",
                    a_destination.formID, a_destination.localizedName,
                    a_destination.courseFormID);
                ClearDestination("live station CELL/XMRK course-marker validation failed");
                return false;
            }

            const auto orbitals = BodyIndex::StationOrbitals(a_destination.mapFormID);
            if (orbitals.size() != 1) {
                REX::WARN("[station] refusing automatic continuation for {:08X} '{}': CELL {:08X} orbital PNDT identity is {} ({} exact DNAM candidates)",
                    a_destination.formID, a_destination.localizedName,
                    a_destination.mapFormID,
                    orbitals.empty() ? "missing" : "ambiguous", orbitals.size());
                ClearDestination("missing or ambiguous station CELL/PNDT orbital identity");
                return false;
            }

            const auto& orbital = orbitals.front();
            const auto liveOrbital = RE::TESForm::LookupByID(orbital.formID);
            if (!liveOrbital || liveOrbital->GetFormType() != RE::FormType::kPNDT ||
                orbital.galaxy.system != a_destination.galaxy.system) {
                REX::WARN("[station] refusing automatic continuation for {:08X} '{}': exact CELL/DNAM orbital {:08X} failed live PNDT/system validation",
                    a_destination.formID, a_destination.localizedName,
                    orbital.formID);
                ClearDestination("live station orbital PNDT validation failed");
                return false;
            }

            auto child = orbital;
            std::optional<BodyIndex::IndexedBody> waypoint;
            std::vector<BodyIndex::IndexedBody> stationWaypoints;
            std::vector<std::uint32_t> ancestry{ orbital.formID };
            for (std::size_t depth = 0;
                 depth < kMaxStationAncestryDepth && child.galaxy.parent;
                 ++depth) {
                const auto parents = BodyIndex::ParentBodies(child.formID);
                if (parents.size() != 1) {
                    REX::WARN("[station] refusing automatic continuation for {:08X} '{}': GNAM parent of {:08X} '{}' is {} ({} exact candidates)",
                        a_destination.formID, a_destination.localizedName,
                        child.formID, child.editorID,
                        parents.empty() ? "missing" : "ambiguous", parents.size());
                    ClearDestination("missing or ambiguous station GNAM ancestry");
                    return false;
                }

                const auto& parent = parents.front();
                if (std::ranges::find(ancestry, parent.formID) != ancestry.end()) {
                    ClearDestination("station GNAM ancestry contains a cycle");
                    return false;
                }
                const auto liveParent = RE::TESForm::LookupByID(parent.formID);
                if (!liveParent || liveParent->GetFormType() != RE::FormType::kPNDT ||
                    parent.galaxy.system != a_destination.galaxy.system ||
                    parent.galaxy.planet != child.galaxy.parent) {
                    REX::WARN("[station] refusing automatic continuation for {:08X} '{}': exact GNAM ancestor {:08X} failed live PNDT/system/planet validation",
                        a_destination.formID, a_destination.localizedName,
                        parent.formID);
                    ClearDestination("live station GNAM ancestry validation failed");
                    return false;
                }

                const auto rows = CurrentHudTargets(parent.formID);
                if (rows.size() > 1) {
                    ClearDestination("station GNAM ancestor has ambiguous cockpit HUD rows");
                    return false;
                }
                REX::INFO("[station] exact orbital ancestry depth {}: child {:08X} '{}' parent {:08X} '{}' HUD rows={}",
                    depth + 1, child.formID, child.editorID, parent.formID,
                    parent.editorID, rows.size());
                stationWaypoints.push_back(parent);
                if (!waypoint && rows.size() == 1)
                    waypoint = parent;
                ancestry.push_back(parent.formID);
                child = parent;
            }

            if (child.galaxy.parent) {
                ClearDestination("station GNAM ancestry exceeds the guarded depth limit");
                return false;
            }
            if (stationWaypoints.empty()) {
                ClearDestination("station orbital PNDT has no GNAM parent ancestry");
                return false;
            }
            if (!waypoint)
                waypoint = stationWaypoints.back();
            std::ranges::reverse(stationWaypoints);
            const auto waypointAt = std::ranges::find(stationWaypoints,
                waypoint->formID, &BodyIndex::IndexedBody::formID);
            if (waypointAt == stationWaypoints.end()) {
                ClearDestination("selected station ancestry waypoint was not retained");
                return false;
            }
            const auto waypointIndex = static_cast<std::size_t>(
                std::distance(stationWaypoints.begin(), waypointAt));

            {
                std::lock_guard lock{ g_orbitalMutex };
                if (g_orbitalContinuation.phase != OrbitalPhase::kNone)
                    return g_orbitalContinuation.finalKind == a_destination.kind &&
                           g_orbitalContinuation.finalFormID == a_destination.formID;
                g_orbitalContinuation = {
                    .phase = OrbitalPhase::kAwaitingParentFeed,
                    .finalKind = BodyKind::kStation,
                    .finalFormID = a_destination.formID,
                    .finalCourseFormID = a_destination.courseFormID,
                    .system = a_destination.galaxy.system,
                    .parentFormID = waypoint->formID,
                    .parentEditorID = waypoint->editorID,
                    .stationWaypoints = std::move(stationWaypoints),
                    .waypointIndex = waypointIndex,
                    .feedRevisionFloor = g_hudLowRevision.load(
                        std::memory_order_acquire),
                    .phaseStarted = Clock::now(),
                };
            }
            g_state.store(NavState::kAwaitingCruise, std::memory_order_release);
            REX::INFO("[station] retained public destination {:08X} '{}' uses exact CELL-owned XMRK course {:08X} and maps from CELL {:08X} to orbital PNDT {:08X}; exact GNAM ancestor {:08X} '{}' retained as private waypoint {}/{} and must expose one HUD row before activation",
                a_destination.formID, a_destination.localizedName,
                a_destination.courseFormID, a_destination.mapFormID,
                orbital.formID, waypoint->formID, waypoint->editorID,
                waypointIndex + 1,
                ancestry.size() - 1);
            return true;
        }

        bool BeginOrbitalCourse()
        {
            const auto destination = Destination();
            const auto continuation = OrbitalState();
            if (!continuation || !destination ||
                destination->kind != continuation->finalKind ||
                destination->formID != continuation->finalFormID ||
                CourseTargetID(*destination) != continuation->finalCourseFormID ||
                continuation->phase != OrbitalPhase::kAwaitingParentFeed)
                return false;

            const auto now = Clock::now();
            if (g_cruiseActive.load(std::memory_order_acquire)) {
                if (!TryCommitOrbitalPhase(OrbitalPhase::kAwaitingParentFeed,
                        *continuation, [now](OrbitalContinuation& a_live) {
                            AdvanceOrbitalPhase(a_live,
                                OrbitalPhase::kTraveling, now,
                                CurrentProcessedHudSnapshot().revision);
                            a_live.dispatchConfirmed = false;
                        }))
                    return false;
                g_state.store(NavState::kAwaitingCruise, std::memory_order_release);
                QueueCourse(CourseTargetID(*destination), false);
                REX::INFO("[orbital] Cruise already active; dispatched retained final {} {:08X} '{}' once and awaiting exact final or optional waypoint readback",
                    DestinationKindName(destination->kind),
                    CourseTargetID(*destination), destination->localizedName);
                return true;
            }

            if (!g_cruiseEngageAvailable.load(std::memory_order_acquire))
                return false;
            auto device = g_pendingJumpDevice.load(std::memory_order_acquire);
            if (device == RE::InputEvent::DeviceType::kNone)
                device = RE::InputEvent::DeviceType::kKeyboard;
            if (!g_hudCruiseInput.QueuePress(device))
                return false;
            if (!TryCommitOrbitalPhase(OrbitalPhase::kAwaitingParentFeed,
                    *continuation, [now](OrbitalContinuation& a_live) {
                        AdvanceOrbitalPhase(a_live,
                            OrbitalPhase::kAwaitingParentCruise, now,
                            CurrentProcessedHudSnapshot().revision);
                    }))
                return false;
            g_state.store(NavState::kAwaitingCruise, std::memory_order_release);
            REX::INFO("[orbital] queued one latched stock HUD Cruise press for retained final {} {:08X} '{}'; stock may resolve it latently through waypoint {:08X} '{}'",
                DestinationKindName(destination->kind),
                CourseTargetID(*destination),
                destination->localizedName,
                continuation->parentFormID, continuation->parentName);
            return true;
        }

        std::optional<OrbitalContinuation> TakeOrbitalCruiseActivation()
        {
            std::lock_guard lock{ g_orbitalMutex };
            auto& continuation = g_orbitalContinuation;
            if (continuation.phase != OrbitalPhase::kAwaitingParentCruise)
                return std::nullopt;
            continuation.phase = OrbitalPhase::kTraveling;
            continuation.dispatchConfirmed = false;
            continuation.feedRevisionFloor =
                CurrentProcessedHudSnapshot().revision;
            continuation.phaseStarted = Clock::now();
            continuation.inactiveSince = {};
            return continuation;
        }

        bool DispatchVanillaSetCourse(RE::Scaleform::GFx::ASMovieRootBase* a_root)
        {
            V params;
            a_root->CreateObject(&params);
            if (!params.IsObject() ||
                !params.SetMember("buttonAction", V{ "SetRouteDestination" }))
                return false;
            return DispatchUiEvent(a_root,
                "StarMapMenu_OnHintButtonClicked", &params);
        }

        bool DispatchVanillaMapCancel(RE::Scaleform::GFx::ASMovieRootBase* a_root)
        {
            // This is the exact Event emitted by GalaxyStarMapMenu's visible
            // Back button. From system view, native returns to galaxy view with
            // that system focused; it does not close the Starmap.
            return DispatchUiEvent(a_root, "StarMapMenu_OnCancel", nullptr);
        }

        bool DispatchVanillaCloseAllMenus(RE::Scaleform::GFx::ASMovieRootBase* a_root)
        {
            // StarMapButtonHintBar.onCloseSubMenuToGame emits these events in
            // this order. The first keeps DataMenu quick-entry state coherent;
            // the second closes the entire menu stack and returns to gameplay.
            const bool quickEntrySet = DispatchUiEvent(
                a_root, "DataMenu_SetMenuForQuickEntry", nullptr);
            const bool closeAll = DispatchUiEvent(
                a_root, "GlobalFunc_CloseAllMenus", nullptr);
            if (!quickEntrySet)
                REX::WARN("[map] stock DataMenu quick-entry dispatch failed before close-all");
            return closeAll;
        }

        void RunCourseRequest(RE::Scaleform::GFx::ASMovieRootBase* a_root)
        {
            const auto request = g_coursePipeline.TakeQueuedForDispatch(
                g_cruiseActive.load(std::memory_order_acquire));
            if (!request)
                return;

            V params;
            a_root->CreateObject(&params);
            if (!params.IsObject()) {
                REX::WARN("[course] could not create event payload for {:08X}", request->id);
                FailActiveContinuationsOrRelease("could not create the course event payload");
                return;
            }
            params.SetMember("uBodyID", V{ static_cast<double>(request->id) });
            if (DispatchUiEvent(a_root, "Reticle_OnCruiseLockCourse", &params)) {
                g_coursePipeline.MarkAsked(request->id, request->clearing);
                REX::INFO("[course] dispatched {} uBodyID={:08X}",
                    request->clearing ? "clear/toggle" : "lock", request->id);
            } else {
                REX::WARN("[course] HUD rejected {} dispatch for {:08X}; mark preserved",
                    request->clearing ? "clear" : "lock", request->id);
                FailActiveContinuationsOrRelease("HUD rejected the course dispatch");
            }
        }

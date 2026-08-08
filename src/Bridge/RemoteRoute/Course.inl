// Included by Bridge.cpp inside CFS::Bridge's anonymous namespace.
// Owns exact-course requests, continuation setup, and stock UI dispatch.

        void QueueCourse(std::uint32_t a_id, bool a_clearing)
        {
            std::lock_guard lock{ g_courseMutex };
            g_courseRequest = { a_id, a_clearing, Clock::now() };
            g_hudUiDirty.store(true, std::memory_order_release);
            if (Settings::Verbose())
                REX::INFO("[course] queued {} for {:08X}", a_clearing ? "clear" : "lock", a_id);
        }

        void FailRemoteMoonContinuation(const std::string& a_reason)
        {
            if (!RemoteMoonContinuationActive())
                return;
            const auto continuation = RemoteMoonState();
            REX::WARN("[orbital] automatic {} continuation failed closed: {}",
                continuation ? DestinationKindName(continuation->finalKind) : "target",
                a_reason);
            CancelOrReleaseHudCruiseInput(a_reason.c_str());
            ClearDestination(a_reason.c_str());
        }

        // Fails whichever remote continuation currently owns the automation, or
        // releases a kAwaitingCruise nav state back to the mark when neither
        // does. Returns true when a continuation consumed the failure.
        bool FailActiveContinuationsOrRelease(const char* a_reason)
        {
            if (RemoteMoonContinuationActive()) {
                FailRemoteMoonContinuation(a_reason);
                return true;
            }
            if (RemoteStationTargetAssigned()) {
                FailRemoteStationContinuation(a_reason);
                return true;
            }
            ReleaseNavStateToMark();
            return false;
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
                std::lock_guard lock{ g_remoteMoonMutex };
                if (g_remoteMoonContinuation.phase != RemoteMoonPhase::kNone)
                    return g_remoteMoonContinuation.finalKind == a_destination.kind &&
                           g_remoteMoonContinuation.finalFormID == a_destination.formID;
                g_remoteMoonContinuation = {
                    .phase = RemoteMoonPhase::kAwaitingParentFeed,
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
            constexpr std::size_t kMaxStationAncestryDepth = 8;
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
                std::lock_guard lock{ g_remoteMoonMutex };
                if (g_remoteMoonContinuation.phase != RemoteMoonPhase::kNone)
                    return g_remoteMoonContinuation.finalKind == a_destination.kind &&
                           g_remoteMoonContinuation.finalFormID == a_destination.formID;
                g_remoteMoonContinuation = {
                    .phase = RemoteMoonPhase::kAwaitingParentFeed,
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

        bool BeginRemoteMoonCourse()
        {
            const auto destination = Destination();
            const auto continuation = RemoteMoonState();
            if (!continuation || !destination ||
                destination->kind != continuation->finalKind ||
                destination->formID != continuation->finalFormID ||
                CourseTargetID(*destination) != continuation->finalCourseFormID ||
                continuation->phase != RemoteMoonPhase::kAwaitingParentFeed)
                return false;

            const auto now = Clock::now();
            if (g_cruiseActive.load(std::memory_order_acquire)) {
                {
                    std::lock_guard lock{ g_remoteMoonMutex };
                    if (g_remoteMoonContinuation.phase != RemoteMoonPhase::kAwaitingParentFeed)
                        return false;
                    g_remoteMoonContinuation.phase = RemoteMoonPhase::kTraveling;
                    g_remoteMoonContinuation.dispatchConfirmed = false;
                    g_remoteMoonContinuation.feedRevisionFloor =
                        CurrentProcessedHudSnapshot().revision;
                    g_remoteMoonContinuation.phaseStarted = now;
                    g_remoteMoonContinuation.inactiveSince = {};
                }
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
            if (!QueueHudCruisePress(device))
                return false;
            {
                std::lock_guard lock{ g_remoteMoonMutex };
                if (g_remoteMoonContinuation.phase != RemoteMoonPhase::kAwaitingParentFeed)
                    return false;
                g_remoteMoonContinuation.phase = RemoteMoonPhase::kAwaitingParentCruise;
                g_remoteMoonContinuation.feedRevisionFloor =
                    CurrentProcessedHudSnapshot().revision;
                g_remoteMoonContinuation.phaseStarted = now;
                g_remoteMoonContinuation.inactiveSince = {};
            }
            g_state.store(NavState::kAwaitingCruise, std::memory_order_release);
            REX::INFO("[orbital] queued one latched stock HUD Cruise press for retained final {} {:08X} '{}'; stock may resolve it latently through waypoint {:08X} '{}'",
                DestinationKindName(destination->kind),
                CourseTargetID(*destination),
                destination->localizedName,
                continuation->parentFormID, continuation->parentName);
            return true;
        }

        std::optional<RemoteMoonContinuation> TakeRemoteMoonCruiseActivation()
        {
            std::lock_guard lock{ g_remoteMoonMutex };
            auto& continuation = g_remoteMoonContinuation;
            if (continuation.phase != RemoteMoonPhase::kAwaitingParentCruise)
                return std::nullopt;
            continuation.phase = RemoteMoonPhase::kTraveling;
            continuation.dispatchConfirmed = false;
            continuation.feedRevisionFloor =
                CurrentProcessedHudSnapshot().revision;
            continuation.phaseStarted = Clock::now();
            continuation.inactiveSince = {};
            return continuation;
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

        struct LiveGalaxyState
        {
            std::uintptr_t menuAddress{ 0 };
            void* galaxyState{ nullptr };
        };

        bool ResolveLiveGalaxyState(const MapSnapshot& a_snapshot,
            LiveGalaxyState& a_live, std::string& a_detail)
        {
            const auto ui = RE::UI::GetSingleton();
            const RE::BSFixedString mapName{ kMapMenu };
            const auto menu = ui ? ui->GetMenu(mapName) : nullptr;
            if (!menu || !menu->uiMovie || !menu->uiMovie->asMovieRoot ||
                !g_mapOpen.load(std::memory_order_acquire) ||
                a_snapshot.generation !=
                    g_mapMovie.generation.load(std::memory_order_acquire)) {
                a_detail = "live StarMapMenu instance changed before galaxy selection";
                return false;
            }

            a_live.menuAddress = reinterpret_cast<std::uintptr_t>(menu.get());
            const auto expectedMenuVtable = RuntimeBindings::StarMapMenuVtable();
            std::uintptr_t actualMenuVtable = 0;
            std::memcpy(&actualMenuVtable,
                reinterpret_cast<const void*>(a_live.menuAddress),
                sizeof(actualMenuVtable));
            if (!expectedMenuVtable || actualMenuVtable != expectedMenuVtable) {
                a_detail = std::format(
                    "StarMapMenu primary vtable mismatch (actual={:016X} expected={:016X})",
                    actualMenuVtable, expectedMenuVtable);
                return false;
            }

            std::memcpy(&a_live.galaxyState,
                reinterpret_cast<const void*>(a_live.menuAddress +
                    kStarMapMenuGalaxyStateOffset),
                sizeof(a_live.galaxyState));
            if (!a_live.galaxyState) {
                a_detail = "StarMapMenu has no active GalaxyState";
                return false;
            }

            const auto expectedGalaxyVtable = RuntimeBindings::GalaxyStateVtable();
            std::uintptr_t actualGalaxyVtable = 0;
            std::memcpy(&actualGalaxyVtable, a_live.galaxyState,
                sizeof(actualGalaxyVtable));
            if (!expectedGalaxyVtable || actualGalaxyVtable != expectedGalaxyVtable) {
                a_detail = std::format(
                    "GalaxyState primary vtable mismatch (actual={:016X} expected={:016X})",
                    actualGalaxyVtable, expectedGalaxyVtable);
                return false;
            }
            return true;
        }

        bool ReadNativeGalaxySelection(const MapSnapshot& a_snapshot,
            std::uint32_t& a_selectedSystem, bool& a_quickSelectOpen,
            std::string& a_detail)
        {
            LiveGalaxyState live;
            if (!ResolveLiveGalaxyState(a_snapshot, live, a_detail))
                return false;

            const auto galaxyAddress =
                reinterpret_cast<std::uintptr_t>(live.galaxyState);
            std::memcpy(&a_selectedSystem,
                reinterpret_cast<const void*>(galaxyAddress +
                    kGalaxyStateSelectedSystemOffset),
                sizeof(a_selectedSystem));
            std::uint8_t quickSelectOpen = 0;
            std::memcpy(&quickSelectOpen,
                reinterpret_cast<const void*>(galaxyAddress +
                    kGalaxyStateQuickSelectOpenOffset),
                sizeof(quickSelectOpen));
            a_quickSelectOpen = quickSelectOpen != 0;
            a_detail = std::format("selected={:08X} quickSelectOpen={}",
                a_selectedSystem, a_quickSelectOpen);
            return true;
        }

        bool InvokeNativeGalaxySystemSelection(const MapSnapshot& a_snapshot,
            std::uint32_t a_systemBodyID, std::string& a_detail)
        {
            if (a_systemBodyID == 0) {
                a_detail = "captured system body ID is zero";
                return false;
            }

            LiveGalaxyState live;
            if (!ResolveLiveGalaxyState(a_snapshot, live, a_detail))
                return false;

            // This is GalaxyState's stock non-entering selected-system setter,
            // used by normal galaxy selection before Quick Select decides
            // whether the action means focus or plot.
            if (!RuntimeBindings::SelectGalaxySystem(live.galaxyState,
                    a_systemBodyID)) {
                a_detail = "native selected-system binding is unavailable";
                return false;
            }

            std::uint32_t selectedSystem = 0;
            bool quickSelectOpen = false;
            if (!ReadNativeGalaxySelection(a_snapshot, selectedSystem,
                    quickSelectOpen, a_detail) ||
                selectedSystem != a_systemBodyID) {
                if (selectedSystem != a_systemBodyID)
                    a_detail = std::format(
                        "native selected-system readback mismatch (expected={:08X} actual={:08X})",
                        a_systemBodyID, selectedSystem);
                return false;
            }
            a_detail = std::format(
                "native GalaxyState selected system bodyID={:08X}",
                selectedSystem);
            return true;
        }

        bool ArmNativeQuickSelectRouteSelection(const MapSnapshot& a_snapshot,
            std::uint32_t a_systemBodyID, std::string& a_detail)
        {
            LiveGalaxyState live;
            if (!ResolveLiveGalaxyState(a_snapshot, live, a_detail))
                return false;

            const auto galaxyAddress =
                reinterpret_cast<std::uintptr_t>(live.galaxyState);
            std::uint32_t selectedSystem = 0;
            std::memcpy(&selectedSystem,
                reinterpret_cast<const void*>(galaxyAddress +
                    kGalaxyStateSelectedSystemOffset),
                sizeof(selectedSystem));
            if (selectedSystem != a_systemBodyID) {
                a_detail = std::format(
                    "native selected-system changed before Set Course (expected={:08X} actual={:08X})",
                    a_systemBodyID, selectedSystem);
                return false;
            }

            const std::uint8_t open = 1;
            std::memcpy(reinterpret_cast<void*>(galaxyAddress +
                    kGalaxyStateQuickSelectOpenOffset),
                &open, sizeof(open));
            a_detail = std::format(
                "armed native Quick Select route ownership for selected={:08X}",
                selectedSystem);
            return true;
        }

        bool ConfirmNativeQuickSelectConsumed(const MapSnapshot& a_snapshot,
            std::string& a_detail)
        {
            LiveGalaxyState live;
            if (!ResolveLiveGalaxyState(a_snapshot, live, a_detail))
                return false;

            const auto galaxyAddress =
                reinterpret_cast<std::uintptr_t>(live.galaxyState);
            std::uint8_t open = 0;
            std::memcpy(&open,
                reinterpret_cast<const void*>(galaxyAddress +
                    kGalaxyStateQuickSelectOpenOffset),
                sizeof(open));
            if (open == 0) {
                a_detail = "native Set Course consumed Quick Select route ownership";
                return true;
            }

            const bool closed = RuntimeBindings::CloseGalaxyQuickSelect(
                live.galaxyState,
                reinterpret_cast<void*>(live.menuAddress +
                    kStarMapMenuDataModelOffset));
            a_detail = closed ?
                "Set Course did not consume Quick Select route ownership; stock close restored it" :
                "Set Course did not consume Quick Select route ownership and close binding is unavailable";
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
                FailActiveContinuationsOrRelease("stock HUD Cruise press invocation failed");
            } else if (release)
                InvokeHudCruiseUserEvent(a_root, a_rootPath, userEvent, false);
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
                FailActiveContinuationsOrRelease("could not create the course event payload");
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
                FailActiveContinuationsOrRelease("HUD rejected the course dispatch");
            }
        }

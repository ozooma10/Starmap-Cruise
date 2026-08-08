// Included by Bridge.cpp inside CFS::Bridge's anonymous namespace.
// Converts an accepted Starmap action into local or remote navigation state.

        enum class MapAction : std::uint8_t
        {
            kNone,
            kTap,
            kHold,
        };
        std::atomic<MapAction> g_pendingMapAction{ MapAction::kNone };

        void AcceptSelection(MapAction a_action)
        {
            if (a_action == MapAction::kNone)
                return;
            MapSnapshot snapshot;
            {
                std::lock_guard lock{ g_mapMutex };
                snapshot = g_map;
            }
            const auto eligibility = EvaluateMapSelection(snapshot);
            if (!eligibility.enabled || !eligibility.destination) {
                if (Settings::Verbose())
                    REX::INFO("[map] Cruise action rejected: {}", eligibility.detail);
                return;
            }
            const auto& selected = *eligibility.destination;
            const bool remoteRoutable = UsesRemoteSystemRoute(selected) &&
                g_haveCurrentSystem.load(std::memory_order_acquire) &&
                selected.galaxy.system != g_currentSystem.load(std::memory_order_acquire);
            if (remoteRoutable &&
                g_cruiseActive.load(std::memory_order_acquire)) {
                g_mapUiDirty.store(true, std::memory_order_release);
                REX::INFO("[jump] remote action rejected before mutation: Cruise is active; exit Cruise in the cockpit before selecting JUMP THEN CRUISE");
                return;
            }

            RE::Scaleform::GFx::ASMovieRootBase* mapRoot = nullptr;
            std::string expectedSystemName;
            std::uint32_t expectedSystemBodyID = 0;
            if (remoteRoutable) {
                V menuRoot;
                if (!GetLiveMapMenuRoot(snapshot, mapRoot, menuRoot)) {
                    REX::WARN("[jump] remote action rejected: live Starmap root is unavailable");
                    return;
                }
                V hintBar;
                V setCourseData;
                std::string setCourseDetail;
                if (!GetVanillaSetCourseData(menuRoot, hintBar,
                        setCourseData, setCourseDetail)) {
                    g_mapUiDirty.store(true, std::memory_order_release);
                    REX::INFO("[jump] remote action rejected: {}", setCourseDetail);
                    return;
                }
                expectedSystemName = BrowsedSystemName(menuRoot);
                if (expectedSystemName.empty()) {
                    g_mapUiDirty.store(true, std::memory_order_release);
                    REX::WARN("[jump] remote action rejected: browsed system identity is unavailable");
                    return;
                }
                const auto treeSystemID = MapTreeSystemID(snapshot.treeBodyID);
                if (!treeSystemID || *treeSystemID != selected.galaxy.system) {
                    g_mapUiDirty.store(true, std::memory_order_release);
                    REX::WARN("[jump] remote action rejected: focused STDT root {:08X} resolves to {}, expected system {}",
                        snapshot.treeBodyID,
                        treeSystemID ? std::format("system {}", *treeSystemID) : "no live star",
                        selected.galaxy.system);
                    return;
                }
                expectedSystemBodyID = snapshot.treeBodyID;
            }

            const auto existing = Destination();
            const bool same = existing && existing->mapFormID == selected.mapFormID &&
                existing->mapType == selected.mapType &&
                existing->formID == selected.formID &&
                existing->targetBaseFormID == selected.targetBaseFormID &&
                CourseTargetID(*existing) == CourseTargetID(selected);
            if (same && a_action == MapAction::kTap && !remoteRoutable) {
                const auto courseTarget = CourseTargetID(selected);
                const bool courseMatches =
                    g_confirmedCourseID.load(std::memory_order_acquire) ==
                    courseTarget;
                ClearDestination("explicit same-target toggle");
                if (courseMatches && g_cruiseActive.load(std::memory_order_acquire))
                    QueueCourse(courseTarget, true);
            } else if (!same) {
                StoreDestination(selected);
            }

            auto selectionDevice = RE::InputEvent::DeviceType::kNone;
            {
                std::lock_guard lock{ g_holdMutex };
                selectionDevice = g_hold.device;
                const bool liveHold = a_action == MapAction::kHold && g_hold.active &&
                    g_hold.session == g_mapSession.load(std::memory_order_acquire);
                if (a_action == MapAction::kHold && !liveHold) {
                    REX::WARN("[map] completed UI hold had no matching physical control; preserving target without starting Cruise");
                }
                g_hold.completed = liveHold;
                g_claimMapKey = liveHold;
            }
            if (remoteRoutable && Destination()) {
                if (selectionDevice == RE::InputEvent::DeviceType::kNone)
                    selectionDevice = RE::InputEvent::DeviceType::kKeyboard;
                g_pendingJumpDevice.store(selectionDevice, std::memory_order_release);
            }
            g_selectionAcceptedThisOpen.store(true, std::memory_order_release);
            if (remoteRoutable) {
                // Preserve the final target only as our Cruise destination. First emit
                // the exact stock Back event so native returns from the body
                // system view to the focused system node in galaxy view. A
                // later post-advance pass proves that focus before asking
                // vanilla to Set Course at system scope.
                g_state.store(NavState::kMapSelection, std::memory_order_release);
                {
                    std::lock_guard lock{ g_remoteRouteMutex };
                    g_remoteRouteRequest = {
                        .phase = RemoteRoutePhase::kAwaitGalaxy,
                        .session = snapshot.session,
                        .generation = snapshot.generation,
                        .targetFormID = selected.formID,
                        .systemBodyID = expectedSystemBodyID,
                        .expectedSystemName = expectedSystemName,
                        .started = Clock::now(),
                    };
                }
                if (!DispatchVanillaMapCancel(mapRoot)) {
                    g_selectionAcceptedThisOpen.store(false, std::memory_order_release);
                    ClearDestination("vanilla system-view Back handoff failed");
                    g_mapUiDirty.store(true, std::memory_order_release);
                    REX::WARN("[jump] stock StarMapMenu_OnCancel dispatch failed; remote mark cleared");
                    return;
                }
                REX::INFO("[jump] accepted tap map={:08X}/{} target={:08X} systemRoot={:08X} system='{}'; dispatched stock Back and awaiting matching galaxy focus",
                    selected.mapFormID, selected.mapType, selected.formID,
                    expectedSystemBodyID, expectedSystemName);
                return;
            }

            V menuRoot;
            if (!GetLiveMapMenuRoot(snapshot, mapRoot, menuRoot) ||
                !DispatchVanillaCloseAllMenus(mapRoot)) {
                // Preserve the previous safe behavior if the stock AS3 event
                // path is unexpectedly unavailable. This may reveal a parent
                // DataMenu, but never leaves the accepted Starmap stuck open.
                g_closeRequested.store(true, std::memory_order_release);
                REX::WARN("[map] stock close-all dispatch failed; queued Starmap hide fallback");
            }
            REX::INFO("[map] accepted {} map={:08X}/{} target={:08X}; requested stock return-to-game close",
                a_action == MapAction::kHold ? "hold" : "tap", selected.mapFormID,
                selected.mapType, selected.formID);
        }

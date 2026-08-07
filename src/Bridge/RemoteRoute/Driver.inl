// Included by Bridge.cpp inside CFS::Bridge's anonymous namespace.
// Drives the guarded vanilla Back -> Set Course -> Execute handoff.

        bool RemoteRouteRequestActive()
        {
            std::lock_guard lock{ g_remoteRouteMutex };
            return g_remoteRouteRequest.phase != RemoteRoutePhase::kNone;
        }

        bool ConsumeRemoteExecuteCloseAcknowledgement()
        {
            std::lock_guard lock{ g_remoteRouteMutex };
            if (g_remoteRouteRequest.phase != RemoteRoutePhase::kAwaitExecuteAck)
                return false;
            g_remoteRouteRequest = {};
            return true;
        }

        void DriveRemoteRouteRequest()
        {
            if (!g_applicationForeground.load(std::memory_order_acquire))
                return;

            RemoteRouteRequest request;
            {
                std::lock_guard lock{ g_remoteRouteMutex };
                request = g_remoteRouteRequest;
            }
            if (request.phase == RemoteRoutePhase::kNone)
                return;

            MapSnapshot snapshot;
            {
                std::lock_guard lock{ g_mapMutex };
                snapshot = g_map;
            }
            const auto age = Clock::now() - request.started;
            const auto destination = Destination();
            if (!g_mapOpen.load(std::memory_order_acquire) ||
                !destination || destination->formID != request.targetFormID ||
                request.session != snapshot.session ||
                request.generation != snapshot.generation) {
                if (!g_mapOpen.load(std::memory_order_acquire))
                    return;  // The menu-close sink owns cancellation or success.
                g_selectionAcceptedThisOpen.store(false, std::memory_order_release);
                ClearDestination("remote Set Course session or destination changed");
                REX::WARN("[jump] remote route request lost its guarded map identity; mark cleared");
                return;
            }

            if (request.phase == RemoteRoutePhase::kAwaitExecuteAck) {
                if (age <= kRemoteExecuteAckTimeout)
                    return;
                g_selectionAcceptedThisOpen.store(false, std::memory_order_release);
                ClearDestination(
                    "stock Execute Route produced no map-close acknowledgement");
                g_mapUiDirty.store(true, std::memory_order_release);
                REX::WARN("[jump] stock Execute Route produced no map-close acknowledgement after {} ms; remote mark cleared",
                    std::chrono::duration_cast<std::chrono::milliseconds>(age).count());
                return;
            }

            RE::Scaleform::GFx::ASMovieRootBase* root = nullptr;
            V menuRoot;
            if (!GetLiveMapMenuRoot(snapshot, root, menuRoot))
                return;

            if (request.phase == RemoteRoutePhase::kAwaitGalaxy) {
                std::string gateDetail;
                if (snapshot.view != kGalaxyView) {
                    gateDetail = std::format("Starmap view is {} rather than galaxy view",
                        snapshot.view);
                } else {
                    const auto focusedSystemID = MapTreeSystemID(snapshot.treeBodyID);
                    if (!focusedSystemID) {
                        gateDetail = "focused galaxy STDT/DNAM identity is unavailable";
                    } else if (snapshot.treeBodyID != request.systemBodyID ||
                               *focusedSystemID != destination->galaxy.system) {
                        gateDetail = std::format("galaxy root {:08X}/system {} differs from marked root {:08X}/system {}",
                            snapshot.treeBodyID, *focusedSystemID,
                            request.systemBodyID, destination->galaxy.system);
                    } else {
                        // Galaxy view now carries the exact captured root. The
                        // remaining work is native galaxy-marker selection, which
                        // gets its own phase and its own full timeout so a slow
                        // Back transition cannot consume the focus budget.
                        {
                            std::lock_guard lock{ g_remoteRouteMutex };
                            if (g_remoteRouteRequest.phase != RemoteRoutePhase::kAwaitGalaxy ||
                                g_remoteRouteRequest.targetFormID != request.targetFormID ||
                                g_remoteRouteRequest.generation != request.generation)
                                return;
                            g_remoteRouteRequest.phase = RemoteRoutePhase::kEstablishSelection;
                            g_remoteRouteRequest.started = Clock::now();
                            g_remoteRouteRequest.executeReadySince = {};
                        }
                        REX::INFO("[jump] matching galaxy STDT/DNAM root {:08X}/system {} reached after {} ms; establishing cursor-independent marker context for '{}'",
                            snapshot.treeBodyID, *focusedSystemID,
                            std::chrono::duration_cast<std::chrono::milliseconds>(age).count(),
                            request.expectedSystemName);
                        return;
                    }
                }

                if (age < kRemoteRouteTimeout)
                    return;
                g_selectionAcceptedThisOpen.store(false, std::memory_order_release);
                const auto reason = std::format("vanilla Back did not produce a matching galaxy focus: {}",
                    gateDetail);
                ClearDestination(reason.c_str());
                g_mapUiDirty.store(true, std::memory_order_release);
                REX::WARN("[jump] {}; remote mark cleared", reason);
                return;
            }

            if (request.phase == RemoteRoutePhase::kEstablishSelection) {
                if (snapshot.view != kGalaxyView) {
                    if (age < kRemoteRouteTimeout)
                        return;
                    g_selectionAcceptedThisOpen.store(false, std::memory_order_release);
                    ClearDestination("galaxy marker context left galaxy view before Set Course");
                    g_mapUiDirty.store(true, std::memory_order_release);
                    REX::WARN("[jump] marker-context gate timed out outside galaxy view; remote mark cleared");
                    return;
                }

                const auto proof = EvaluateGalaxySelection(menuRoot, snapshot,
                    request.systemBodyID);
                if (proof.proven) {
                    {
                        std::lock_guard lock{ g_remoteRouteMutex };
                        if (g_remoteRouteRequest.phase != RemoteRoutePhase::kEstablishSelection ||
                            g_remoteRouteRequest.targetFormID != request.targetFormID ||
                            g_remoteRouteRequest.generation != request.generation)
                            return;
                        g_remoteRouteRequest.phase = RemoteRoutePhase::kAwaitRoute;
                        g_remoteRouteRequest.started = Clock::now();
                        g_remoteRouteRequest.executeReadySince = {};
                    }
                    REX::INFO("[jump] marker context established by {} after {} ms; {}",
                        proof.authority,
                        std::chrono::duration_cast<std::chrono::milliseconds>(age).count(),
                        proof.Describe(snapshot, request.systemBodyID));
                    if (proof.button.Ready())
                        REX::INFO("[jump] Set Course enabled for '{}' root={:08X}",
                            request.expectedSystemName, request.systemBodyID);
                    else
                        REX::INFO("[jump] Set Course still reports enabled={} visible={} while native selection is proven by {}; the vanilla button is never written to",
                            proof.button.enabled, proof.button.visible,
                            proof.authority);
                    std::string routeSelectionDetail;
                    if (!ArmNativeQuickSelectRouteSelection(snapshot,
                            request.systemBodyID, routeSelectionDetail)) {
                        g_selectionAcceptedThisOpen.store(false, std::memory_order_release);
                        ClearDestination("native Quick Select route selection could not be armed");
                        g_mapUiDirty.store(true, std::memory_order_release);
                        REX::WARN("[jump] native Quick Select route selection unavailable ({}); remote mark cleared",
                            routeSelectionDetail);
                        return;
                    }
                    REX::INFO("[jump] Quick Select route selection armed: {}",
                        routeSelectionDetail);
                    if (!DispatchVanillaSetCourse(root)) {
                        std::string cleanupDetail;
                        ConfirmNativeQuickSelectConsumed(snapshot, cleanupDetail);
                        g_selectionAcceptedThisOpen.store(false, std::memory_order_release);
                        ClearDestination("vanilla system-level Set Course handoff failed");
                        g_mapUiDirty.store(true, std::memory_order_release);
                        REX::WARN("[jump] stock system-level SetRouteDestination dispatch failed; remote mark cleared ({})",
                            cleanupDetail);
                        return;
                    }
                    std::string consumedDetail;
                    if (!ConfirmNativeQuickSelectConsumed(snapshot,
                            consumedDetail)) {
                        g_selectionAcceptedThisOpen.store(false, std::memory_order_release);
                        ClearDestination("vanilla Set Course did not consume Quick Select route selection");
                        g_mapUiDirty.store(true, std::memory_order_release);
                        REX::WARN("[jump] {}; remote mark cleared and vanilla route state preserved",
                            consumedDetail);
                        return;
                    }
                    REX::INFO("[jump] Set Course dispatched at system scope for '{}' root={:08X} (authority={})",
                        request.expectedSystemName, request.systemBodyID,
                        proof.authority);
                    return;  // Never consume a route that predates this dispatch.
                }

                // No proof yet. A rung that has just run keeps the ladder for a
                // fixed number of completed advances so native can publish its
                // result before the next rung is allowed to change the same
                // state.
                if (request.focusRungCooldown != 0) {
                    std::lock_guard lock{ g_remoteRouteMutex };
                    if (g_remoteRouteRequest.phase == RemoteRoutePhase::kEstablishSelection &&
                        g_remoteRouteRequest.targetFormID == request.targetFormID &&
                        g_remoteRouteRequest.generation == request.generation &&
                        g_remoteRouteRequest.focusRungCooldown != 0)
                        --g_remoteRouteRequest.focusRungCooldown;
                    return;
                }

                // Invoke the exact stock non-entering system-selection path once, then
                // leave completed advances for native to publish readback.
                if (request.nextFocusRung != GalaxyFocusRung::kExhausted) {
                    {
                        std::lock_guard lock{ g_remoteRouteMutex };
                        if (g_remoteRouteRequest.phase != RemoteRoutePhase::kEstablishSelection ||
                            g_remoteRouteRequest.targetFormID != request.targetFormID ||
                            g_remoteRouteRequest.generation != request.generation)
                            return;
                        g_remoteRouteRequest.nextFocusRung = GalaxyFocusRung::kExhausted;
                        g_remoteRouteRequest.focusRungCooldown = kGalaxyFocusRungPasses;
                    }
                    std::string detail;
                    if (!InvokeNativeGalaxySystemSelection(snapshot,
                            request.systemBodyID, detail)) {
                        REX::WARN("[jump] focus rung 1: stock native galaxy system selection unavailable ({})",
                            detail);
                        LogGalaxyFocusDiagnostics(menuRoot, snapshot, proof,
                            request.systemBodyID);
                        g_selectionAcceptedThisOpen.store(false, std::memory_order_release);
                        ClearDestination("guarded native galaxy system selection was unavailable");
                        g_mapUiDirty.store(true, std::memory_order_release);
                        REX::WARN("[jump] guarded native galaxy system selection failed closed; remote mark cleared and vanilla route state preserved");
                        return;
                    }
                    REX::INFO("[jump] focus rung 1: invoked stock native galaxy selected-system setter for '{}' ({}) without changing map view",
                        request.expectedSystemName, detail);
                    return;  // Re-test native state on the next advance.
                }

                if (!request.focusDiagnosticsLogged) {
                    {
                        std::lock_guard lock{ g_remoteRouteMutex };
                        if (g_remoteRouteRequest.phase != RemoteRoutePhase::kEstablishSelection ||
                            g_remoteRouteRequest.targetFormID != request.targetFormID ||
                            g_remoteRouteRequest.generation != request.generation)
                            return;
                        g_remoteRouteRequest.focusDiagnosticsLogged = true;
                    }
                    LogGalaxyFocusDiagnostics(menuRoot, snapshot, proof,
                        request.systemBodyID);
                    return;
                }

                if (age < kRemoteRouteTimeout)
                    return;
                g_selectionAcceptedThisOpen.store(false, std::memory_order_release);
                const auto reason = std::format("no cursor-independent galaxy marker context was established: {}",
                    proof.Describe(snapshot, request.systemBodyID));
                ClearDestination(reason.c_str());
                g_mapUiDirty.store(true, std::memory_order_release);
                REX::WARN("[jump] {}; remote mark cleared and vanilla route state preserved",
                    reason);
                return;
            }

            if (snapshot.view != kGalaxyView) {
                if (age < kRemoteRouteTimeout)
                    return;
                g_selectionAcceptedThisOpen.store(false, std::memory_order_release);
                ClearDestination("system-level Set Course left galaxy view before producing a route");
                g_mapUiDirty.store(true, std::memory_order_release);
                REX::WARN("[jump] system-level route gate timed out outside galaxy view; remote mark cleared");
                return;
            }

            V jumpData;
            auto gate = InspectRemoteRoute(menuRoot,
                request.expectedSystemName, &jumpData);

            if (!gate.ready) {
                if (request.executeReadySince != Clock::time_point{}) {
                    std::lock_guard lock{ g_remoteRouteMutex };
                    if (g_remoteRouteRequest.phase == RemoteRoutePhase::kAwaitRoute &&
                        g_remoteRouteRequest.targetFormID == request.targetFormID &&
                        g_remoteRouteRequest.generation == request.generation) {
                        g_remoteRouteRequest.executeReadySince = {};
                    }
                }
                if (age < kRemoteRouteTimeout)
                    return;

                g_selectionAcceptedThisOpen.store(false, std::memory_order_release);
                const auto reason = std::format("vanilla Set Course did not produce an executable matching route: {}",
                    gate.detail);
                ClearDestination(reason.c_str());
                g_mapUiDirty.store(true, std::memory_order_release);
                REX::WARN("[jump] {}; remote mark cleared and vanilla route state preserved",
                    reason);
                return;
            }

            const auto now = Clock::now();
            if (request.executeReadySince == Clock::time_point{}) {
                {
                    std::lock_guard lock{ g_remoteRouteMutex };
                    if (g_remoteRouteRequest.phase != RemoteRoutePhase::kAwaitRoute ||
                        g_remoteRouteRequest.targetFormID != request.targetFormID ||
                        g_remoteRouteRequest.generation != request.generation)
                        return;
                    g_remoteRouteRequest.executeReadySince = now;
                }
                REX::INFO("[jump] route identity confirmed: vanilla route ends in '{}' body='{}'; requiring {} ms continuous readiness before stock Execute Route",
                    request.expectedSystemName, gate.destinationBodyName,
                    kRemoteRouteExecuteSettleTime.count());
                return;
            }
            const auto readyAge = now - request.executeReadySince;
            if (readyAge < kRemoteRouteExecuteSettleTime)
                return;

            if (g_cruiseActive.load(std::memory_order_acquire)) {
                g_selectionAcceptedThisOpen.store(false,
                    std::memory_order_release);
                ClearDestination(
                    "Cruise became active before remote Execute");
                g_mapUiDirty.store(true, std::memory_order_release);
                REX::WARN("[jump] Cruise became active after remote selection; route cleared before Execute");
                return;
            }

            {
                std::lock_guard lock{ g_remoteRouteMutex };
                if (g_remoteRouteRequest.phase != RemoteRoutePhase::kAwaitRoute ||
                    g_remoteRouteRequest.targetFormID != request.targetFormID ||
                    g_remoteRouteRequest.generation != request.generation)
                    return;
                g_remoteRouteRequest.phase = RemoteRoutePhase::kAwaitExecuteAck;
                g_remoteRouteRequest.started = now;
                g_remoteRouteRequest.executeReadySince = {};
            }

            // SendExecuteEvent is the callback behind the visible vanilla
            // Execute hold. It rechecks ExecuteButtonHint.Visible before
            // dispatching StarMapMenu_ExecuteRoute.
            g_state.store(NavState::kPendingJump, std::memory_order_release);
            if (!jumpData.Invoke("SendExecuteEvent")) {
                g_selectionAcceptedThisOpen.store(false, std::memory_order_release);
                ClearDestination("vanilla Execute Route handoff failed");
                g_mapUiDirty.store(true, std::memory_order_release);
                REX::WARN("[jump] JumpDataPanel.SendExecuteEvent invocation failed; remote mark cleared");
                return;
            }
            REX::INFO("[jump] Execute dispatched: vanilla matching-system route remained executable for {} ms for '{}' body='{}' (route age {} ms); stock StarMapMenu_ExecuteRoute sent while retaining Cruise target {:08X}",
                std::chrono::duration_cast<std::chrono::milliseconds>(readyAge).count(),
                request.expectedSystemName,
                gate.destinationBodyName,
                std::chrono::duration_cast<std::chrono::milliseconds>(age).count(),
                request.targetFormID);
        }

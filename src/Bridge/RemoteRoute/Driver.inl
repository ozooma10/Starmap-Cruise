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

        // The driver ticks from a lock-free copy of the request; every mutation
        // re-locks and re-verifies the request identity (phase, target,
        // generation) before committing, because the focus watcher thread and
        // the BSService main frame are real concurrent writers. A false return
        // means the live request moved on and this tick's decision must be
        // abandoned.
        template <class Apply>
        bool TryCommitRemoteRoutePhase(RemoteRoutePhase a_expectedPhase,
            const RemoteRouteRequest& a_request, Apply&& a_apply)
        {
            std::lock_guard lock{ g_remoteRouteMutex };
            if (g_remoteRouteRequest.phase != a_expectedPhase ||
                g_remoteRouteRequest.targetFormID != a_request.targetFormID ||
                g_remoteRouteRequest.generation != a_request.generation)
                return false;
            a_apply(g_remoteRouteRequest);
            return true;
        }

        // Shared fail-closed exit for the route driver: every abandoned
        // request drops the session acceptance latch, clears the mark, and
        // re-dirties the map hint. Each caller logs its own [jump] warning.
        void FailRemoteRoute(const char* a_reason)
        {
            g_selectionAcceptedThisOpen.store(false, std::memory_order_release);
            ClearDestination(a_reason);
            g_mapUiDirty.store(true, std::memory_order_release);
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
                FailRemoteRoute("remote Set Course session or destination changed");
                REX::WARN("[jump] remote route request lost its guarded map identity; mark cleared");
                return;
            }

            if (request.phase == RemoteRoutePhase::kAwaitExecuteAck) {
                if (age <= kRemoteExecuteAckTimeout)
                    return;
                FailRemoteRoute(
                    "stock Execute Route produced no map-close acknowledgement");
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
                        if (!TryCommitRemoteRoutePhase(RemoteRoutePhase::kAwaitGalaxy,
                                request, [](RemoteRouteRequest& a_live) {
                                    a_live.phase = RemoteRoutePhase::kEstablishSelection;
                                    a_live.started = Clock::now();
                                    a_live.executeReadySince = {};
                                }))
                            return;
                        REX::INFO("[jump] matching galaxy STDT/DNAM root {:08X}/system {} reached after {} ms; establishing cursor-independent marker context for '{}'",
                            snapshot.treeBodyID, *focusedSystemID,
                            std::chrono::duration_cast<std::chrono::milliseconds>(age).count(),
                            request.expectedSystemName);
                        return;
                    }
                }

                if (age < kRemoteRouteTimeout)
                    return;
                const auto reason = std::format("vanilla Back did not produce a matching galaxy focus: {}",
                    gateDetail);
                FailRemoteRoute(reason.c_str());
                REX::WARN("[jump] {}; remote mark cleared", reason);
                return;
            }

            if (request.phase == RemoteRoutePhase::kEstablishSelection) {
                if (snapshot.view != kGalaxyView) {
                    if (age < kRemoteRouteTimeout)
                        return;
                    FailRemoteRoute("galaxy marker context left galaxy view before Set Course");
                    REX::WARN("[jump] marker-context gate timed out outside galaxy view; remote mark cleared");
                    return;
                }

                const auto proof = EvaluateGalaxySelection(menuRoot, snapshot,
                    request.systemBodyID);
                if (proof.proven) {
                    if (!TryCommitRemoteRoutePhase(
                            RemoteRoutePhase::kEstablishSelection, request,
                            [](RemoteRouteRequest& a_live) {
                                a_live.phase = RemoteRoutePhase::kAwaitRoute;
                                a_live.started = Clock::now();
                                a_live.executeReadySince = {};
                            }))
                        return;
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
                        FailRemoteRoute("native Quick Select route selection could not be armed");
                        REX::WARN("[jump] native Quick Select route selection unavailable ({}); remote mark cleared",
                            routeSelectionDetail);
                        return;
                    }
                    REX::INFO("[jump] Quick Select route selection armed: {}",
                        routeSelectionDetail);
                    if (!DispatchVanillaSetCourse(root)) {
                        std::string cleanupDetail;
                        ConfirmNativeQuickSelectConsumed(snapshot, cleanupDetail);
                        FailRemoteRoute("vanilla system-level Set Course handoff failed");
                        REX::WARN("[jump] stock system-level SetRouteDestination dispatch failed; remote mark cleared ({})",
                            cleanupDetail);
                        return;
                    }
                    std::string consumedDetail;
                    if (!ConfirmNativeQuickSelectConsumed(snapshot,
                            consumedDetail)) {
                        FailRemoteRoute("vanilla Set Course did not consume Quick Select route selection");
                        REX::WARN("[jump] {}; remote mark cleared and vanilla route state preserved",
                            consumedDetail);
                        return;
                    }
                    REX::INFO("[jump] Set Course dispatched at system scope for '{}' root={:08X} (authority={})",
                        request.expectedSystemName, request.systemBodyID,
                        proof.authority);
                    return;  // Never consume a route that predates this dispatch.
                }

                // No proof yet. After the focus attempt runs, native keeps a
                // fixed number of completed advances to publish its result
                // before diagnostics are allowed to fire.
                if (request.focusReadbackPasses != 0) {
                    TryCommitRemoteRoutePhase(RemoteRoutePhase::kEstablishSelection,
                        request, [](RemoteRouteRequest& a_live) {
                            if (a_live.focusReadbackPasses != 0)
                                --a_live.focusReadbackPasses;
                        });
                    return;
                }

                // Invoke the exact stock non-entering system-selection path once, then
                // leave completed advances for native to publish readback.
                if (!request.focusAttempted) {
                    if (!TryCommitRemoteRoutePhase(
                            RemoteRoutePhase::kEstablishSelection, request,
                            [](RemoteRouteRequest& a_live) {
                                a_live.focusAttempted = true;
                                a_live.focusReadbackPasses = kGalaxyFocusReadbackPasses;
                            }))
                        return;
                    std::string detail;
                    if (!InvokeNativeGalaxySystemSelection(snapshot,
                            request.systemBodyID, detail)) {
                        REX::WARN("[jump] native focus attempt: stock native galaxy system selection unavailable ({})",
                            detail);
                        LogGalaxyFocusDiagnostics(menuRoot, snapshot, proof,
                            request.systemBodyID);
                        FailRemoteRoute("guarded native galaxy system selection was unavailable");
                        REX::WARN("[jump] guarded native galaxy system selection failed closed; remote mark cleared and vanilla route state preserved");
                        return;
                    }
                    REX::INFO("[jump] native focus attempt: invoked stock native galaxy selected-system setter for '{}' ({}) without changing map view",
                        request.expectedSystemName, detail);
                    return;  // Re-test native state on the next advance.
                }

                if (!request.focusDiagnosticsLogged) {
                    if (!TryCommitRemoteRoutePhase(
                            RemoteRoutePhase::kEstablishSelection, request,
                            [](RemoteRouteRequest& a_live) {
                                a_live.focusDiagnosticsLogged = true;
                            }))
                        return;
                    LogGalaxyFocusDiagnostics(menuRoot, snapshot, proof,
                        request.systemBodyID);
                    return;
                }

                if (age < kRemoteRouteTimeout)
                    return;
                const auto reason = std::format("no cursor-independent galaxy marker context was established: {}",
                    proof.Describe(snapshot, request.systemBodyID));
                FailRemoteRoute(reason.c_str());
                REX::WARN("[jump] {}; remote mark cleared and vanilla route state preserved",
                    reason);
                return;
            }

            if (snapshot.view != kGalaxyView) {
                if (age < kRemoteRouteTimeout)
                    return;
                FailRemoteRoute("system-level Set Course left galaxy view before producing a route");
                REX::WARN("[jump] system-level route gate timed out outside galaxy view; remote mark cleared");
                return;
            }

            V jumpData;
            auto gate = InspectRemoteRoute(menuRoot,
                request.expectedSystemName, &jumpData);

            if (!gate.ready) {
                if (request.executeReadySince != Clock::time_point{})
                    TryCommitRemoteRoutePhase(RemoteRoutePhase::kAwaitRoute,
                        request, [](RemoteRouteRequest& a_live) {
                            a_live.executeReadySince = {};
                        });
                if (age < kRemoteRouteTimeout)
                    return;

                const auto reason = std::format("vanilla Set Course did not produce an executable matching route: {}",
                    gate.detail);
                FailRemoteRoute(reason.c_str());
                REX::WARN("[jump] {}; remote mark cleared and vanilla route state preserved",
                    reason);
                return;
            }

            const auto now = Clock::now();
            if (request.executeReadySince == Clock::time_point{}) {
                if (!TryCommitRemoteRoutePhase(RemoteRoutePhase::kAwaitRoute,
                        request, [now](RemoteRouteRequest& a_live) {
                            a_live.executeReadySince = now;
                        }))
                    return;
                REX::INFO("[jump] route identity confirmed: vanilla route ends in '{}' body='{}'; requiring {} ms continuous readiness before stock Execute Route",
                    request.expectedSystemName, gate.destinationBodyName,
                    kRemoteRouteExecuteSettleTime.count());
                return;
            }
            const auto readyAge = now - request.executeReadySince;
            if (readyAge < kRemoteRouteExecuteSettleTime)
                return;

            if (g_cruiseActive.load(std::memory_order_acquire)) {
                FailRemoteRoute("Cruise became active before remote Execute");
                REX::WARN("[jump] Cruise became active after remote selection; route cleared before Execute");
                return;
            }

            if (!TryCommitRemoteRoutePhase(RemoteRoutePhase::kAwaitRoute,
                    request, [now](RemoteRouteRequest& a_live) {
                        a_live.phase = RemoteRoutePhase::kAwaitExecuteAck;
                        a_live.started = now;
                        a_live.executeReadySince = {};
                    }))
                return;

            // SendExecuteEvent is the callback behind the visible vanilla
            // Execute hold. It rechecks ExecuteButtonHint.Visible before
            // dispatching StarMapMenu_ExecuteRoute.
            g_state.store(NavState::kPendingJump, std::memory_order_release);
            if (!jumpData.Invoke("SendExecuteEvent")) {
                FailRemoteRoute("vanilla Execute Route handoff failed");
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

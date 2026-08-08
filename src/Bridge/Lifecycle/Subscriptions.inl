// Included by Bridge.cpp inside CFS::Bridge's anonymous namespace.
// Owns Scaleform subscriptions plus menu and focus lifecycle events.

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
                if (Clock::now() - born < kMovieSubscribeSettleTime)
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
                    std::this_thread::sleep_for(kFocusPollTime);
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

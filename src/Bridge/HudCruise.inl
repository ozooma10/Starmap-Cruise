// Included by Bridge.cpp inside CFS::Bridge's anonymous namespace.
// Owns HUD feeds, Cruise state, and exact course sampling.

        class LowCollector : public V::ArrayVisitor
        {
        public:
            std::vector<HudRow> rows;

            void Visit(std::uint32_t a_index, const V& a_value) override
            {
                V entry = a_value;
                HudRow row;
                row.id = UIntMember(entry, "uniqueID");
                row.name = StringMember(entry, "name");
                V lock;
                row.courseLocked = entry.GetMember("bIsCruiseTargetLock", &lock) &&
                    lock.IsBoolean() && lock.GetBoolean();
                if (rows.size() <= a_index)
                    rows.resize(a_index + 1);
                rows[a_index] = std::move(row);
            }
        };

        class LowHandler : public RE::Scaleform::GFx::FunctionHandler
        {
        public:
            void Call(const Params& a_params) override
            {
                const auto generation =
                    g_hudMovie.generation.load(std::memory_order_acquire);
                V data;
                if (!Payload(a_params, data))
                    return;
                V array;
                if (!data.GetMember("targetArray", &array))
                    return;
                V inner;
                if (array.GetMember("dataA", &inner) && inner.IsArray())
                    array = inner;
                if (!array.IsArray())
                    return;

                LowCollector collector;
                array.VisitElements(&collector);
                if (!generation ||
                    g_hudMovie.generation.load(std::memory_order_acquire) != generation)
                    return;
                {
                    std::lock_guard lock{ g_hudRowsMutex };
                    g_hudRows = std::move(collector.rows);
                    g_hudRowsGeneration = generation;
                }
                g_hudLowDirty.store(true, std::memory_order_release);
                g_hudUiDirty.store(true, std::memory_order_release);
            }
        } g_lowHandler;

        void ProcessLowSnapshot()
        {
            if (!g_hudLowDirty.exchange(false, std::memory_order_acq_rel))
                return;

            const auto feedRevision =
                g_hudLowRevision.fetch_add(1, std::memory_order_acq_rel) + 1;

            std::vector<HudRow> rows;
            std::uint32_t hudGeneration = 0;
            {
                std::lock_guard lock{ g_hudRowsMutex };
                rows = g_hudRows;
                hudGeneration = g_hudRowsGeneration;
            }
            if (!hudGeneration ||
                g_hudMovie.generation.load(std::memory_order_acquire) != hudGeneration)
                return;
            ResolveCurrentSystem(rows);

            std::uint32_t course = 0;
            for (const auto& row : rows)
                if (row.courseLocked) {
                    course = row.id;
                    break;
                }
            {
                std::lock_guard lock{ g_processedHudMutex };
                g_processedHudSnapshot = { rows, course, feedRevision, hudGeneration };
            }
            const auto previousCourse = g_confirmedCourseID.exchange(course, std::memory_order_acq_rel);
            if (previousCourse != course && Settings::Verbose()) {
                REX::INFO("[course] engine lock transition {:08X} -> {:08X} on low-frequency feed {}",
                    previousCourse, course, feedRevision);
            }
            const auto asked = g_courseAskedID.load(std::memory_order_acquire);
            if (asked && g_courseAskedClearing.load(std::memory_order_acquire) && course != asked) {
                g_courseAskedID.store(0, std::memory_order_release);
                g_courseAskedClearing.store(false, std::memory_order_release);
                REX::INFO("[course] engine confirmed clear of {:08X}", asked);
            }

            const auto destination = Destination();
            if (!destination)
                return;
            const auto courseTarget = CourseTargetID(*destination);

            if (RemoteMoonContinuationActive())
                return;

            if (RemoteStationTargetAssigned() && course != 0 &&
                course != courseTarget) {
                FailRemoteStationContinuation(
                    "engine selected an unrelated course before exact station lock");
                return;
            }

            if (course == courseTarget) {
                if (RemoteStationTargetAssigned()) {
                    const auto matchingRows = std::ranges::count_if(rows,
                        [&](const HudRow& a_row) {
                            return a_row.id == courseTarget;
                        });
                    if (matchingRows != 1) {
                        FailRemoteStationContinuation(
                            "exact station course appeared on ambiguous cockpit HUD rows");
                        return;
                    }
                    g_pendingStationResolveTicks.store(0,
                        std::memory_order_release);
                    g_pendingStationAssignedID.store(0,
                        std::memory_order_release);
                    REX::INFO("[station] exact remote station course-marker lock confirmed: physicalRef={:08X} courseMarker={:08X} '{}'",
                        destination->formID, courseTarget,
                        destination->localizedName);
                }
                RecordCourseLock(hudGeneration);
                g_courseAskedID.store(0, std::memory_order_release);
                g_courseAskedClearing.store(false, std::memory_order_release);
                g_state.store(NavState::kAutopilotLocked, std::memory_order_release);
                if (previousCourse != course) {
                    REX::INFO("[course] engine confirmed lock on {:08X} '{}'",
                        courseTarget, destination->localizedName);
                }
            } else if (previousCourse == courseTarget) {
                const auto arrival = ArmArrivalCheck(destination->formID, hudGeneration);
                if (!arrival.armed)
                    return;
                g_state.store(NavState::kMarked, std::memory_order_release);
                if (Settings::Verbose())
                    REX::INFO("[arrival] Cruise lock left {:08X}; last distance={:.3f} m from HUD generation {}, threshold={:.3f} m; waiting for same-generation arrival evidence",
                        courseTarget,
                        arrival.lastDistance, arrival.distanceGeneration,
                        kArrivalDistanceMeters);
            }
        }

        class HighCollector : public V::ArrayVisitor
        {
        public:
            std::vector<DistanceSample> rows;

            void Visit(std::uint32_t a_index, const V& a_value) override
            {
                V entry = a_value;
                DistanceSample row;
                V distance;
                if (entry.GetMember("distance", &distance) &&
                    (distance.IsNumber() || distance.IsInt() || distance.IsUInt())) {
                    row.valid = true;
                    row.distance = AsNumber(distance);
                }
                if (rows.size() <= a_index)
                    rows.resize(a_index + 1);
                rows[a_index] = row;
            }
        };

        void UpdateMarkedDistance(const std::vector<DistanceSample>& a_distances,
            std::uint32_t a_hudGeneration)
        {
            if (!a_hudGeneration ||
                g_hudMovie.generation.load(std::memory_order_acquire) != a_hudGeneration)
                return;
            const auto destination = Destination();
            if (!destination)
                return;

            std::size_t index = static_cast<std::size_t>(-1);
            {
                std::lock_guard lock{ g_hudRowsMutex };
                const auto count = std::min(g_hudRows.size(), a_distances.size());
                for (std::size_t i = 0; i < count; ++i)
                    if (g_hudRows[i].id == CourseTargetID(*destination)) {
                        index = i;
                        break;
                    }
            }
            if (index != static_cast<std::size_t>(-1) && a_distances[index].valid) {
                std::lock_guard lock{ g_arrivalAuditMutex };
                g_arrivalAudit.markedDistance = a_distances[index].distance;
                g_arrivalAudit.distanceGeneration = a_hudGeneration;
            }
        }

        class HighHandler : public RE::Scaleform::GFx::FunctionHandler
        {
        public:
            void Call(const Params& a_params) override
            {
                const auto generation =
                    g_hudMovie.generation.load(std::memory_order_acquire);
                V data;
                if (!Payload(a_params, data))
                    return;
                V array;
                if (!data.GetMember("targetArray", &array))
                    return;
                V inner;
                if (array.GetMember("dataA", &inner) && inner.IsArray())
                    array = inner;
                if (!array.IsArray())
                    return;

                HighCollector collector;
                array.VisitElements(&collector);
                if (!generation ||
                    g_hudMovie.generation.load(std::memory_order_acquire) != generation)
                    return;
                {
                    std::lock_guard lock{ g_hudDistancesMutex };
                    g_hudDistances = std::move(collector.rows);
                    g_hudDistancesGeneration = generation;
                }
                // Passed GFx values die with this callback. The post-advance
                // pump consumes only copied C++ rows before entering AS3.
                g_hudUiDirty.store(true, std::memory_order_release);
            }
        } g_highHandler;

        // Called from OnMovieCreated on the menu-creation thread: plain-state
        // resets only, no Scaleform access.
        void OnHudMovieReplaced()
        {
            ResetHold("Spaceship HUD movie replacement");
            {
                std::lock_guard lock{ g_hudCruiseInputMutex };
                g_hudCruiseInputPhase = HudCruiseInputPhase::kIdle;
                g_hudCruiseUserEvent = "Cruise";
                g_hudCruiseInputLatched = false;
                g_hudCruiseInputStarted = {};
            }
            {
                std::lock_guard lock{ g_courseMutex };
                g_courseRequest = {};
            }
            g_courseAskedID.store(0, std::memory_order_release);
            g_courseAskedClearing.store(false, std::memory_order_release);
            g_confirmedCourseID.store(0, std::memory_order_release);
            g_haveCurrentSystem.store(false, std::memory_order_release);
            {
                std::lock_guard lock{ g_hudRowsMutex };
                g_hudRows.clear();
                g_hudRowsGeneration = 0;
            }
            {
                std::lock_guard lock{ g_processedHudMutex };
                g_processedHudSnapshot = {};
            }
            g_hudLowDirty.store(false, std::memory_order_release);
            g_cruiseActive.store(false, std::memory_order_release);
            g_cruiseEngageAvailable.store(false, std::memory_order_release);
            g_hudUiDirty.store(true, std::memory_order_release);
            g_uiResetMask.fetch_or(kResetHudUi, std::memory_order_acq_rel);
            FailActiveContinuationsOrRelease(
                "Spaceship HUD movie was replaced during automatic continuation");
        }

        void ReleaseStaleHudUiState()
        {
            const auto reset = g_uiResetMask.fetch_and(
                ~kResetHudUi, std::memory_order_acq_rel);
            if ((reset & kResetHudUi) != 0) {
                std::lock_guard lock{ g_hudDistancesMutex };
                g_hudDistances.clear();
                g_hudDistancesGeneration = 0;
            }
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

        void ReconcileHudUi()
        {
            const bool dirty = g_hudUiDirty.load(std::memory_order_acquire);
            const bool mapOpen = g_mapOpen.load(std::memory_order_acquire);
            // A settled HUD may be sampled while the map is open so a short
            // stock Cruise cooldown can expire without a close/reopen.
            if ((!dirty && !mapOpen) || !WorldSettled() || !IsFlying())
                return;

            const auto ui = RE::UI::GetSingleton();
            const RE::BSFixedString hudName{ kHudMenu };
            if (!ui || !ui->IsMenuOpen(hudName))
                return;
            const auto menu = ui->GetMenu(hudName);
            if (!menu || !menu->uiMovie || !menu->uiMovie->asMovieRoot)
                return;

            auto* root = menu->uiMovie->asMovieRoot.get();
            const auto generation = g_hudMovie.generation.load(std::memory_order_acquire);
            if (!HudMovieSettled(generation))
                return;

            const char* rootPath = menu->GetRootPath();
            const std::string base{ rootPath ? rootPath : "root" };
            V reticle;
            if (!root->GetVariable(&reticle, (base + ".Reticle_mc").c_str()) ||
                !reticle.IsObject() ||
                g_hudMovie.generation.load(std::memory_order_acquire) != generation ||
                !menu->uiMovie || !menu->uiMovie->asMovieRoot ||
                menu->uiMovie->asMovieRoot.get() != root)
                return;

            g_hudUiDirty.store(false, std::memory_order_release);

            DriveHudCruiseInput(root, rootPath);

            std::vector<DistanceSample> distances;
            std::uint32_t distancesGeneration = 0;
            {
                std::lock_guard lock{ g_hudDistancesMutex };
                distances = g_hudDistances;
                distancesGeneration = g_hudDistancesGeneration;
            }

            V cruise;
            const bool activeResolved = reticle.GetMember("CruiseModeHUDActive", &cruise) &&
                                        cruise.IsBoolean();
            const bool active = activeResolved ? cruise.GetBoolean() :
                g_cruiseActive.load(std::memory_order_acquire);
            const bool wasActive = g_cruiseActive.exchange(active, std::memory_order_acq_rel);

            // ShipReticle.UpdateCruiseButton enables the stock hold event only
            // when CanActivateCruiseMode is true and neither Monocle nor Cruise
            // mode is active. Mirror those public getters rather than guessing
            // at cooldown timing in native code.
            V canActivateValue;
            V monocleValue;
            const bool canActivateResolved = reticle.GetMember(
                "CanActivateCruiseMode", &canActivateValue) &&
                canActivateValue.IsBoolean();
            const bool monocleResolved = reticle.GetMember(
                "MonocleModeActive", &monocleValue) &&
                monocleValue.IsBoolean();
            const bool engageAvailable = activeResolved && canActivateResolved &&
                                         monocleResolved && canActivateValue.GetBoolean() &&
                                         !monocleValue.GetBoolean() && !active;
            const bool wasEngageAvailable = g_cruiseEngageAvailable.exchange(
                engageAvailable, std::memory_order_acq_rel);
            if (engageAvailable != wasEngageAvailable) {
                if (g_mapOpen.load(std::memory_order_acquire))
                    g_mapUiDirty.store(true, std::memory_order_release);
                if (Settings::Verbose())
                    REX::INFO("[hud] stock Cruise engage availability -> {} (activeResolved={} resolved={} monocleResolved={} active={})",
                        engageAvailable, activeResolved, canActivateResolved,
                        monocleResolved, active);
            }

            if (active && !wasActive) {
                CancelOrReleaseHudCruiseInput("CruiseModeHUDActive confirmed");
                const auto state = g_state.load(std::memory_order_acquire);
                const auto destination = Destination();
                if (RemoteMoonContinuationActive()) {
                    const auto continuation = TakeRemoteMoonCruiseActivation();
                    if (!continuation) {
                        FailRemoteMoonContinuation("Cruise activated outside the guarded continuous orbital transition");
                    } else {
                        g_state.store(NavState::kAwaitingCruise, std::memory_order_release);
                        REX::INFO("[orbital] stock Cruise activation confirmed; dispatching retained final {} {:08X} '{}' once; exact final readback remains the success gate",
                            DestinationKindName(continuation->finalKind),
                            continuation->finalCourseFormID,
                            destination ? destination->localizedName : "");
                        QueueCourse(continuation->finalCourseFormID, false);
                    }
                } else if (destination &&
                    (state == NavState::kAwaitingCruise || state == NavState::kMarked)) {
                    g_state.store(NavState::kAwaitingCruise, std::memory_order_release);
                    if (state == NavState::kMarked)
                        REX::INFO("[course] vanilla Cruise activation detected; locking marked destination {:08X}",
                            CourseTargetID(*destination));
                    QueueCourse(CourseTargetID(*destination), false);
                }
            }
            if (!active && wasActive) {
                if (g_state.load(std::memory_order_acquire) ==
                    NavState::kAutopilotLocked) {
                    g_state.store(NavState::kMarked, std::memory_order_release);
                }
            }

            UpdateMarkedDistance(distances, distancesGeneration);
            RunCourseRequest(root);
        }

// Included by Bridge.cpp inside CFS::Bridge's anonymous namespace.
// Owns the destination lifecycle: current-system resolution, native station
// target assignment, and the store/clear/fail-closed plumbing every driver
// funnels through. The arrival audit itself is the ArrivalAudit machine in
// State.inl; its evidence policy lives with the HUD feed and frame audits.

        void ResolveCurrentSystem(const std::vector<HudRow>& a_rows)
        {
            if (!BodyIndex::Ready())
                return;
            std::unordered_map<std::uint32_t, std::size_t> systems;
            for (const auto& row : a_rows)
                if (const auto body = BodyIndex::Lookup(row.id))
                    ++systems[body->galaxy.system];
            if (systems.empty())
                return;

            auto best = systems.begin();
            bool unique = true;
            for (auto it = std::next(systems.begin()); it != systems.end(); ++it) {
                if (it->second > best->second) {
                    best = it;
                    unique = true;
                } else if (it->second == best->second) {
                    unique = false;
                }
            }
            if (!unique) {
                g_haveCurrentSystem.store(false, std::memory_order_release);
                return;
            }
            const auto old = g_currentSystem.exchange(best->first, std::memory_order_acq_rel);
            const bool had = g_haveCurrentSystem.exchange(true, std::memory_order_acq_rel);
            if ((!had || old != best->first) && Settings::Verbose())
                REX::INFO("[system] cockpit feed resolves current system {}", best->first);

            // A fast Starmap open can race the load-order index: the HUD rows
            // already exist, but ResolveCurrentSystem cannot join them until
            // BodyIndex becomes ready. Recover only an unresolved snapshot from
            // the same still-open movie/session. Never rewrite a captured system.
            bool recoveredMapSession = false;
            if (g_mapOpen.load(std::memory_order_acquire)) {
                std::lock_guard lock{ g_mapMutex };
                if (g_mapOpen.load(std::memory_order_acquire) &&
                    !g_map.haveCapturedSystem &&
                    g_map.session != 0 &&
                    g_map.session == g_mapSession.load(std::memory_order_acquire) &&
                    g_map.generation ==
                        g_mapMovie.generation.load(std::memory_order_acquire)) {
                    g_map.haveCapturedSystem = true;
                    g_map.capturedSystem = best->first;
                    recoveredMapSession = true;
                }
            }
            if (recoveredMapSession) {
                g_mapActionHintSignature.store(0, std::memory_order_release);
                g_mapUiDirty.store(true, std::memory_order_release);
                REX::INFO("[map] recovered current system {} for the open Starmap session",
                    best->first);
            }
        }

        bool AssignNativeShipTarget(const BodyDestination& a_destination)
        {
            if (a_destination.kind != BodyKind::kStation)
                return true;

            const auto form = RE::TESForm::LookupByID(a_destination.formID);
            const auto reference = form ? form->As<RE::TESObjectREFR>() : nullptr;
            const auto base = reference ? reference->GetBaseObject() : nullptr;
            const bool exactBase = base &&
                (!a_destination.targetBaseFormID ||
                    base->GetFormID() == a_destination.targetBaseFormID);
            const bool validStation = exactBase &&
                a_destination.kind == BodyKind::kStation &&
                BodyIndex::IsStationBase(base->GetFormID());
            if (!validStation) {
                REX::ERROR("[target] refusing native assignment for {:08X}: live {} REFR validation failed",
                    a_destination.formID, DestinationKindName(a_destination.kind));
                return false;
            }

            if (!RuntimeBindings::SetShipHudTarget(a_destination.formID)) {
                REX::ERROR("[target] native ship-target binding became unavailable for {:08X}",
                    a_destination.formID);
                return false;
            }
            const auto observed = RuntimeBindings::CurrentShipHudTarget();
            if (!observed || *observed != a_destination.formID) {
                REX::ERROR("[target] native assignment of {:08X} did not commit (observed {:08X})",
                    a_destination.formID, observed.value_or(0));
                return false;
            }

            REX::INFO("[target] native cockpit target assigned: map={:08X}/{} ref={:08X} base={:08X}",
                a_destination.mapFormID, a_destination.mapType, a_destination.formID,
                base->GetFormID());
            return true;
        }

        bool ReleaseNavStateToMark()
        {
            auto expected = NavState::kAwaitingCruise;
            const auto released = Destination() ? NavState::kMarked : NavState::kIdle;
            return g_state.compare_exchange_strong(expected, released,
                std::memory_order_acq_rel, std::memory_order_acquire);
        }

        void ResetDestinationDependentState(NavState a_state)
        {
            ResetOrbitalContinuation();
            g_coursePipeline.Reset();
            g_state.store(a_state, std::memory_order_release);
            g_arrivalAudit.Reset();
            g_pendingJumpDevice.store(RE::InputEvent::DeviceType::kNone,
                std::memory_order_release);
            g_pendingStationResolveTicks.store(0, std::memory_order_release);
            g_pendingStationAssignedID.store(0, std::memory_order_release);
            g_hudUiDirty.store(true, std::memory_order_release);
        }

        void ClearDestination(const char* a_reason)
        {
            std::optional<BodyDestination> old;
            {
                std::lock_guard lock{ g_destinationMutex };
                old = std::move(g_destination);
                g_destination.reset();
            }
            {
                std::lock_guard lock{ g_remoteRouteMutex };
                g_remoteRouteRequest = {};
            }
            ResetDestinationDependentState(NavState::kIdle);
            if (old)
                REX::INFO("[destination] cleared {:08X} '{}': {}", old->formID,
                    old->localizedName, a_reason);
        }

        void StoreDestination(BodyDestination a_destination)
        {
            std::optional<BodyDestination> old;
            {
                std::lock_guard lock{ g_destinationMutex };
                old = g_destination;
                g_destination = a_destination;
            }
            // Do not clear g_remoteRouteRequest here. An active guarded route is
            // authoritative: if another mark appears, its retained target must
            // differ and let the driver fail the whole handoff closed.
            ResetDestinationDependentState(NavState::kMapSelection);
            if (old && old->formID != a_destination.formID)
                REX::INFO("[destination] replaced {:08X} '{}' with {:08X} '{}'",
                    old->formID, old->localizedName, a_destination.formID,
                    a_destination.localizedName);
            else
                REX::INFO("[destination] marked {:08X} '{}' (course={:08X} system={} parent={} planet={} kind={})",
                    a_destination.formID, a_destination.localizedName,
                    CourseTargetID(a_destination),
                    a_destination.galaxy.system, a_destination.galaxy.parent,
                    a_destination.galaxy.planet,
                    DestinationKindName(a_destination.kind));
        }

        void FailRemoteStationContinuation(const char* a_reason)
        {
            if (!RemoteStationTargetAssigned())
                return;
            REX::WARN("[station] automatic remote continuation failed closed: {}",
                a_reason);
            g_hudCruiseInput.CancelOrRelease(a_reason);
            ClearDestination(a_reason);
        }

        void FailOrbitalContinuation(const std::string& a_reason)
        {
            if (!OrbitalContinuationActive())
                return;
            const auto continuation = OrbitalState();
            REX::WARN("[orbital] automatic {} continuation failed closed: {}",
                continuation ? DestinationKindName(continuation->finalKind) : "target",
                a_reason);
            g_hudCruiseInput.CancelOrRelease(a_reason.c_str());
            ClearDestination(a_reason.c_str());
        }

        // Fails whichever remote continuation currently owns the automation, or
        // releases a kAwaitingCruise nav state back to the mark when neither
        // does. Returns true when a continuation consumed the failure.
        bool FailActiveContinuationsOrRelease(const char* a_reason)
        {
            if (OrbitalContinuationActive()) {
                FailOrbitalContinuation(a_reason);
                return true;
            }
            if (RemoteStationTargetAssigned()) {
                FailRemoteStationContinuation(a_reason);
                return true;
            }
            ReleaseNavStateToMark();
            return false;
        }

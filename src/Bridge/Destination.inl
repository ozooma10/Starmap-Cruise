// Included by Bridge.cpp inside CFS::Bridge's anonymous namespace.
// Owns current-system resolution and destination state.

        const char* DestinationKindName(BodyKind a_kind)
        {
            switch (a_kind) {
            case BodyKind::kPlanet:
                return "planet";
            case BodyKind::kMoon:
                return "moon";
            case BodyKind::kStation:
                return "station";
            default:
                return "non-planet target";
            }
        }

        bool IsPlanetary(const BodyDestination& a_destination)
        {
            return a_destination.kind == BodyKind::kPlanet ||
                   a_destination.kind == BodyKind::kMoon;
        }

        std::uint32_t CourseTargetID(const BodyDestination& a_destination)
        {
            return a_destination.courseFormID ? a_destination.courseFormID :
                                                a_destination.formID;
        }

        bool UsesRemoteSystemRoute(const BodyDestination& a_destination)
        {
            return IsPlanetary(a_destination) ||
                   a_destination.kind == BodyKind::kStation;
        }

        std::optional<std::uint32_t> MapTreeSystemID(std::uint32_t a_formID)
        {
            return BodyIndex::LookupSystemRoot(a_formID);
        }

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

        void ResolveCruiseMapBinding()
        {
            const auto resolved = Input::ResolveCruiseBindings();
            if (!resolved) {
                g_cruiseMapKey.store(-1, std::memory_order_release);
                g_cruiseMapModifier.store(-1, std::memory_order_release);
                g_cruiseMapMouseButton.store(-1, std::memory_order_release);
                g_cruiseMapGamepadButton.store(-1, std::memory_order_release);
                REX::WARN("[input] live Cruise bindings unavailable: ControlMap validation failed");
                return;
            }

            auto key = resolved->keyboard.code;
            auto modifier = resolved->keyboard.modifier;
            auto mouseButton = resolved->mouse.code;
            const auto mouseModifier = resolved->mouse.modifier;
            auto gamepadButton = resolved->gamepad.code;
            const auto gamepadModifier = resolved->gamepad.modifier;

            // The UI hook can identify one physical ButtonEvent at a time. Do
            // not claim a mouse/gamepad chord unless its second edge can also
            // be proven; the shipped SHMonocle binding is a single button.
            if (mouseButton >= 0 && mouseModifier >= 0) {
                REX::WARN("[input] mouse Cruise chord is unsupported; mouse routing disabled");
                mouseButton = -1;
            }
            if (gamepadButton >= 0 && gamepadModifier >= 0) {
                REX::WARN("[input] controller '{}' chord is unsupported; controller routing disabled",
                    kCruiseMapGamepadUserEvent);
                gamepadButton = -1;
            }

            const auto oldKey = g_cruiseMapKey.exchange(key, std::memory_order_acq_rel);
            const auto oldModifier = g_cruiseMapModifier.exchange(modifier, std::memory_order_acq_rel);
            const auto oldMouse = g_cruiseMapMouseButton.exchange(mouseButton,
                std::memory_order_acq_rel);
            const auto oldGamepad = g_cruiseMapGamepadButton.exchange(gamepadButton,
                std::memory_order_acq_rel);
            if (key >= 0 && (oldKey != key || oldModifier != modifier)) {
                REX::INFO("[input] Starmap Cruise action follows live Cruise binding: VK=0x{:02X} modifier={}",
                    key, modifier < 0 ? "none" : std::format("0x{:02X}", modifier));
            }
            if (mouseButton >= 0 && oldMouse != mouseButton)
                REX::INFO("[input] Starmap Cruise action follows live mouse Cruise binding: id={}",
                    mouseButton);
            if (gamepadButton >= 0 && oldGamepad != gamepadButton)
                REX::INFO("[input] Starmap Cruise action follows live controller '{}' binding: id={} modifier={}",
                    kCruiseMapGamepadUserEvent, gamepadButton,
                    gamepadModifier < 0 ? "none" : std::format("{}", gamepadModifier));
            if (key < 0 && mouseButton < 0 && gamepadButton < 0)
                REX::WARN("[input] Cruise has no keyboard, mouse, or controller binding; Starmap Cruise action disabled");
        }

        bool IsShipInSpace(RE::TESObjectREFR* a_ship)
        {
            return RuntimeBindings::IsShipInSpace(a_ship);
        }

        bool IsFlying()
        {
            const auto player = RE::PlayerCharacter::GetSingleton();
            const auto ship = player ? player->GetSpaceship() : nullptr;
            return IsShipInSpace(ship);
        }

        struct LiveReferenceTarget
        {
            std::uint32_t referenceFormID{ 0 };
            std::uint32_t baseFormID{ 0 };
        };

        std::vector<LiveReferenceTarget> ResolveStationTargets(std::uint32_t a_mapFormID)
        {
            std::vector<LiveReferenceTarget> resolved;
            const auto appendLive = [&resolved](LiveReferenceTarget a_candidate) {
                const auto form = RE::TESForm::LookupByID(a_candidate.referenceFormID);
                const auto reference = form ? form->As<RE::TESObjectREFR>() : nullptr;
                const auto base = reference ? reference->GetBaseObject() : nullptr;
                if (!base || !BodyIndex::IsStationBase(base->GetFormID()))
                    return;
                a_candidate.baseFormID = base->GetFormID();
                resolved.push_back(std::move(a_candidate));
            };

            // Dynamic map markers may already be the live station reference.
            if (const auto form = RE::TESForm::LookupByID(a_mapFormID)) {
                if (const auto reference = form->As<RE::TESObjectREFR>()) {
                    const auto base = reference->GetBaseObject();
                    if (base && BodyIndex::IsStationBase(base->GetFormID())) {
                        appendLive({
                            .referenceFormID = a_mapFormID,
                            .baseFormID = base->GetFormID(),
                        });
                    }
                }
            }
            for (const auto& candidate : BodyIndex::StationTargets(a_mapFormID))
                appendLive({ candidate.referenceFormID, candidate.baseFormID });

            std::ranges::sort(resolved, {}, &LiveReferenceTarget::referenceFormID);
            resolved.erase(std::unique(resolved.begin(), resolved.end(),
                [](const LiveReferenceTarget& a_left,
                    const LiveReferenceTarget& a_right) {
                    return a_left.referenceFormID == a_right.referenceFormID;
                }), resolved.end());
            return resolved;
        }

        std::vector<HudRow> CurrentHudTargets(std::uint32_t a_formID)
        {
            std::vector<HudRow> matches;
            std::lock_guard lock{ g_hudRowsMutex };
            for (const auto& row : g_hudRows) {
                if (row.id == a_formID)
                    matches.push_back(row);
            }
            return matches;
        }

        ProcessedHudSnapshot CurrentProcessedHudSnapshot()
        {
            std::lock_guard lock{ g_processedHudMutex };
            return g_processedHudSnapshot;
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

        std::optional<BodyDestination> Destination()
        {
            std::lock_guard lock{ g_destinationMutex };
            return g_destination;
        }

        std::optional<RemoteMoonContinuation> RemoteMoonState()
        {
            std::lock_guard lock{ g_remoteMoonMutex };
            if (g_remoteMoonContinuation.phase == RemoteMoonPhase::kNone)
                return std::nullopt;
            return g_remoteMoonContinuation;
        }

        bool RemoteMoonContinuationActive()
        {
            std::lock_guard lock{ g_remoteMoonMutex };
            return g_remoteMoonContinuation.phase != RemoteMoonPhase::kNone;
        }

        void ResetRemoteMoonContinuation()
        {
            std::lock_guard lock{ g_remoteMoonMutex };
            g_remoteMoonContinuation = {};
        }

        void RecordCourseLock(std::uint32_t a_hudGeneration)
        {
            std::lock_guard lock{ g_arrivalAuditMutex };
            g_arrivalAudit.courseWasLocked = a_hudGeneration != 0;
            g_arrivalAudit.courseLockGeneration = a_hudGeneration;
            // Reacquiring an exact lock invalidates any still-armed audit from
            // its previous loss while preserving the latest valid distance.
            g_arrivalAudit.checkID = 0;
            g_arrivalAudit.checkGeneration = 0;
            g_arrivalAudit.checkTicks = 0;
        }

        struct ArmedArrivalCheck
        {
            bool armed{ false };
            double lastDistance{ -1.0 };
            std::uint32_t distanceGeneration{ 0 };
        };

        ArmedArrivalCheck ArmArrivalCheck(std::uint32_t a_destinationID,
            std::uint32_t a_hudGeneration)
        {
            std::lock_guard lock{ g_arrivalAuditMutex };
            ArmedArrivalCheck result{
                .lastDistance = g_arrivalAudit.markedDistance,
                .distanceGeneration = g_arrivalAudit.distanceGeneration,
            };
            if (!a_destinationID || !a_hudGeneration ||
                !g_arrivalAudit.courseWasLocked ||
                g_arrivalAudit.courseLockGeneration != a_hudGeneration) {
                if (g_arrivalAudit.courseWasLocked &&
                    g_arrivalAudit.courseLockGeneration != a_hudGeneration) {
                    g_arrivalAudit.courseWasLocked = false;
                    g_arrivalAudit.courseLockGeneration = 0;
                }
                return result;
            }

            g_arrivalAudit.courseWasLocked = false;
            g_arrivalAudit.courseLockGeneration = 0;
            g_arrivalAudit.checkID = a_destinationID;
            g_arrivalAudit.checkGeneration = a_hudGeneration;
            g_arrivalAudit.checkTicks =
                Clock::now().time_since_epoch().count();
            result.armed = true;
            return result;
        }

        bool ReleaseNavStateToMark()
        {
            auto expected = NavState::kAwaitingCruise;
            const auto released = Destination() ? NavState::kMarked : NavState::kIdle;
            return g_state.compare_exchange_strong(expected, released,
                std::memory_order_acq_rel, std::memory_order_acquire);
        }

        void CancelOrReleaseHudCruiseInput(const char* a_reason)
        {
            const char* action = nullptr;
            {
                std::lock_guard lock{ g_hudCruiseInputMutex };
                g_hudCruiseInputLatched = false;
                g_hudCruiseInputStarted = {};
                if (g_hudCruiseInputPhase == HudCruiseInputPhase::kPressPending) {
                    g_hudCruiseInputPhase = HudCruiseInputPhase::kIdle;
                    g_hudCruiseUserEvent = "Cruise";
                    action = "cancelled pending press";
                } else if (g_hudCruiseInputPhase == HudCruiseInputPhase::kPressed) {
                    g_hudCruiseInputPhase = HudCruiseInputPhase::kReleasePending;
                    action = "queued release";
                }
            }
            g_hudUiDirty.store(true, std::memory_order_release);
            if (action && Settings::Verbose())
                REX::INFO("[input] HUD Cruise {}: {}", action, a_reason);
        }

        bool QueueHudCruisePress(RE::InputEvent::DeviceType a_device)
        {
            std::lock_guard lock{ g_hudCruiseInputMutex };
            if (g_hudCruiseInputPhase != HudCruiseInputPhase::kIdle)
                return false;
            // ShipReticle installs a different quick/hold combo for controller
            // mode. Both combos reach the same stock Cruise hold callback.
            g_hudCruiseUserEvent = a_device == RE::InputEvent::DeviceType::kGamepad ?
                                       kCruiseMapGamepadUserEvent :
                                       "Cruise";
            g_hudCruiseInputPhase = HudCruiseInputPhase::kPressPending;
            // The Starmap's completed fill is the user's confirmation. Keep
            // the separate cockpit hold pressed even if the physical key is
            // released, then release on Cruise activation or the safety limit.
            g_hudCruiseInputLatched = true;
            g_hudCruiseInputStarted = Clock::now();
            g_hudUiDirty.store(true, std::memory_order_release);
            return true;
        }

        bool HudCruiseInputLatched()
        {
            std::lock_guard lock{ g_hudCruiseInputMutex };
            return g_hudCruiseInputLatched;
        }

        void ResetHold(const char* a_reason)
        {
            bool changed = false;
            {
                std::lock_guard lock{ g_holdMutex };
                changed = g_hold.active || g_claimMapKey;
                g_hold = {};
                g_claimMapKey = false;
            }
            CancelOrReleaseHudCruiseInput(a_reason);
            if (!ReleaseNavStateToMark() &&
                g_state.load(std::memory_order_acquire) == NavState::kMapSelection &&
                Settings::Verbose())
                REX::INFO("[input] active Starmap selection preserved across hold reset: {}",
                    a_reason);
            if (changed && Settings::Verbose())
                REX::INFO("[input] pending physical hold reset: {}", a_reason);
        }

        void ResetDestinationDependentState(NavState a_state)
        {
            ResetRemoteMoonContinuation();
            g_courseAskedID.store(0, std::memory_order_release);
            g_courseAskedClearing.store(false, std::memory_order_release);
            g_state.store(a_state, std::memory_order_release);
            {
                std::lock_guard lock{ g_arrivalAuditMutex };
                g_arrivalAudit = {};
            }
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
                std::lock_guard lock{ g_courseMutex };
                g_courseRequest = {};
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
            {
                std::lock_guard lock{ g_courseMutex };
                g_courseRequest = {};
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

        bool RemoteStationContinuationActive()
        {
            return g_pendingStationAssignedID.load(std::memory_order_acquire) != 0;
        }

        void FailRemoteStationContinuation(const char* a_reason)
        {
            if (!RemoteStationContinuationActive())
                return;
            REX::WARN("[station] automatic remote continuation failed closed: {}",
                a_reason);
            CancelOrReleaseHudCruiseInput(a_reason);
            ClearDestination(a_reason);
        }

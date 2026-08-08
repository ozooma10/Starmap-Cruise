// Included by Bridge.cpp inside CFS::Bridge's anonymous namespace.
// The shared utility floor: pure predicates over destinations, live-engine
// lookups, settle gates, and mutex-guarded state accessors. Everything here
// depends only on State.inl and normal modules; every later fragment may
// call it.

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

        bool WorldSettled()
        {
            const auto last = Clock::time_point{ Clock::duration{
                g_lastUnsettledTicks.load(std::memory_order_acquire) } };
            return Clock::now() - last > kWorldSettleTime;
        }

        bool HudMovieSettled(std::uint32_t a_generation)
        {
            if (a_generation == 0 ||
                g_hudMovie.generation.load(std::memory_order_acquire) != a_generation)
                return false;
            const auto born = Clock::time_point{ Clock::duration{
                g_hudMovie.bornTicks.load(std::memory_order_acquire) } };
            return Clock::now() - born >= kHudMovieSettleTime;
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

        std::optional<BodyDestination> Destination()
        {
            std::lock_guard lock{ g_destinationMutex };
            return g_destination;
        }

        std::optional<OrbitalContinuation> OrbitalState()
        {
            std::lock_guard lock{ g_orbitalMutex };
            if (g_orbitalContinuation.phase == OrbitalPhase::kNone)
                return std::nullopt;
            return g_orbitalContinuation;
        }

        bool OrbitalContinuationActive()
        {
            std::lock_guard lock{ g_orbitalMutex };
            return g_orbitalContinuation.phase != OrbitalPhase::kNone;
        }

        void ResetOrbitalContinuation()
        {
            std::lock_guard lock{ g_orbitalMutex };
            g_orbitalContinuation = {};
        }

        // A native station target assignment is pending exact-lock readback.
        // Orthogonal to OrbitalContinuationActive(): the shared continuation
        // record covers both moons and stations, while this flag is also set on
        // the direct station path where no continuation record exists.
        bool RemoteStationTargetAssigned()
        {
            return g_pendingStationAssignedID.load(std::memory_order_acquire) != 0;
        }

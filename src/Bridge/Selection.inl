// Included by Bridge.cpp inside CFS::Bridge's anonymous namespace.
// Owns map-target eligibility and exact identity construction.

        MapEligibility EvaluateMapSelection(MapSnapshot a_snapshot)
        {
            const auto unavailable = [](EligibilityCode a_code, std::string a_label,
                                         std::string a_detail) {
                return MapEligibility{
                    .code = a_code,
                    .show = true,
                    .enabled = false,
                    .label = std::move(a_label),
                    .detail = std::move(a_detail),
                };
            };

            if (!a_snapshot.openedWhileFlying || a_snapshot.view != kSystemView ||
                a_snapshot.session == 0 ||
                a_snapshot.session != g_mapSession.load(std::memory_order_acquire) ||
                a_snapshot.generation != g_mapMovie.generation.load(std::memory_order_acquire)) {
                return {
                    .code = EligibilityCode::kHidden,
                    .detail = "not an active-flight system-view map session",
                };
            }
            const bool usingGamepad = g_lastInputWasGamepad.load(std::memory_order_acquire);
            const bool cruiseControlBound = usingGamepad ?
                g_cruiseMapGamepadButton.load(std::memory_order_acquire) >= 0 :
                g_cruiseMapKey.load(std::memory_order_acquire) >= 0 ||
                    g_cruiseMapMouseButton.load(std::memory_order_acquire) >= 0;
            if (!cruiseControlBound)
                return unavailable(EligibilityCode::kCruiseControlUnbound,
                    "CRUISE CONTROL IS NOT BOUND",
                    usingGamepad ? "SHMonocle has no controller binding" :
                                   "Cruise has no keyboard or mouse binding");
            if (!a_snapshot.haveCapturedSystem)
                return unavailable(EligibilityCode::kCurrentSystemUnavailable,
                    "CURRENT SYSTEM UNAVAILABLE",
                    "cockpit current system is not resolved for this map session");
            if (a_snapshot.highlightedMarkerCount == 0)
                return unavailable(EligibilityCode::kSelectBody,
                    "HIGHLIGHT A DESTINATION",
                    "system view has no highlight-radius target marker");
            if (a_snapshot.highlightedMarkerCount != 1)
                return unavailable(EligibilityCode::kAmbiguousTarget,
                    "TARGET IS AMBIGUOUS",
                    std::format("system view has {} highlight-radius marker candidates",
                        a_snapshot.highlightedMarkerCount));
            if (a_snapshot.markerBodyID == 0) {
                return unavailable(EligibilityCode::kTargetTypeUnsupported,
                    "TARGET HAS NO CRUISE ID",
                    std::format("highlight-radius marker has type {} but no id",
                        a_snapshot.markerBodyType));
            }

            const bool planetary = a_snapshot.markerBodyType == kPlanetType ||
                a_snapshot.markerBodyType == kMoonType;
            if (!BodyIndex::Ready())
                return unavailable(EligibilityCode::kTargetDataLoading,
                    "CRUISE TARGET DATA LOADING",
                    "PNDT/GNAM and starstation reference index is not ready");
            if (!planetary) {
                const auto browsedSystemID = MapTreeSystemID(a_snapshot.treeBodyID);
                if (browsedSystemID && *browsedSystemID != a_snapshot.capturedSystem) {
                    if (g_cruiseActive.load(std::memory_order_acquire)) {
                        return unavailable(EligibilityCode::kCruiseActive,
                            "EXIT CRUISE FIRST",
                            "vanilla cannot execute a grav-jump route while Cruise is active, and the stock HUD Cruise control is not handled while the Starmap is open");
                    }
                    auto indexedStations =
                        BodyIndex::StationTargets(a_snapshot.markerBodyID);
                    indexedStations.erase(std::remove_if(indexedStations.begin(),
                        indexedStations.end(), [](const BodyIndex::StationTarget& a_target) {
                            return !a_target.referenceFormID ||
                                   !a_target.courseFormID ||
                                   !BodyIndex::IsStationBase(a_target.baseFormID);
                        }), indexedStations.end());
                    std::ranges::sort(indexedStations, {},
                        &BodyIndex::StationTarget::referenceFormID);
                    indexedStations.erase(std::unique(indexedStations.begin(),
                        indexedStations.end(),
                        [](const BodyIndex::StationTarget& a_left,
                            const BodyIndex::StationTarget& a_right) {
                            return a_left.referenceFormID == a_right.referenceFormID;
                        }), indexedStations.end());
                    if (indexedStations.size() > 1) {
                        return unavailable(EligibilityCode::kAmbiguousTarget,
                            "STATION TARGET IS AMBIGUOUS",
                            std::format("remote station CELL {:08X}/{} has {} exact indexed references",
                                a_snapshot.markerBodyID, a_snapshot.markerBodyType,
                                indexedStations.size()));
                    }
                    if (indexedStations.size() == 1) {
                        if (!g_loadGameSinkReady.load(std::memory_order_acquire)) {
                            return unavailable(EligibilityCode::kRemoteSafetyUnavailable,
                                "REMOTE CRUISE SAFETY UNAVAILABLE",
                                "guarded TESLoadGameEvent sink is unavailable; refusing a remote station mark that could survive a save load");
                        }
                        const auto& station = indexedStations.front();
                        auto destination = BodyDestination{
                            .kind = BodyKind::kStation,
                            .formID = station.referenceFormID,
                            .targetBaseFormID = station.baseFormID,
                            .courseFormID = station.courseFormID,
                            .mapFormID = a_snapshot.markerBodyID,
                            .mapType = a_snapshot.markerBodyType,
                            .galaxy = { .system = *browsedSystemID },
                            .localizedName = a_snapshot.markerName.empty() ?
                                (station.editorID.empty() ?
                                        std::format("STATION {:08X}", station.referenceFormID) :
                                        station.editorID) :
                                a_snapshot.markerName,
                            .menuGeneration = a_snapshot.generation,
                        };
                        return {
                            .code = EligibilityCode::kEligible,
                            .show = true,
                            .enabled = true,
                            .label = kRemoteCruiseMapActionLabel,
                            .detail = std::format("eligible remote station CELL={:08X}/{} indexedRef={:08X} base={:08X} courseMarker={:08X} '{}' system={}",
                                destination.mapFormID, destination.mapType,
                                destination.formID, station.baseFormID,
                                station.courseFormID, destination.localizedName,
                                destination.galaxy.system),
                            .destination = std::move(destination),
                        };
                    }
                    return {
                        .code = EligibilityCode::kHidden,
                        .detail = std::format("remote non-station marker {:08X}/{} has no stable unloaded target identity",
                            a_snapshot.markerBodyID, a_snapshot.markerBodyType),
                    };
                }

                const auto stationTargets = ResolveStationTargets(a_snapshot.markerBodyID);
                if (stationTargets.size() > 1)
                    return unavailable(EligibilityCode::kAmbiguousTarget,
                        "STATION TARGET IS AMBIGUOUS",
                        std::format("non-planet marker {:08X}/{} resolves to {} live starstation references",
                            a_snapshot.markerBodyID, a_snapshot.markerBodyType,
                            stationTargets.size()));

                if (!stationTargets.empty()) {
                    const auto& station = stationTargets.front();
                    auto destination = BodyDestination{
                        .kind = BodyKind::kStation,
                        .formID = station.referenceFormID,
                        .targetBaseFormID = station.baseFormID,
                        .mapFormID = a_snapshot.markerBodyID,
                        .mapType = a_snapshot.markerBodyType,
                        .galaxy = { .system = a_snapshot.capturedSystem },
                        .localizedName = a_snapshot.markerName.empty() ?
                            std::format("STATION {:08X}", station.referenceFormID) :
                            a_snapshot.markerName,
                        .menuGeneration = a_snapshot.generation,
                    };
                    return {
                        .code = EligibilityCode::kEligible,
                        .show = true,
                        .enabled = true,
                        .label = kCruiseMapActionLabel,
                        .detail = std::format("eligible station map={:08X}/{} ref={:08X} base={:08X} '{}'",
                            destination.mapFormID, destination.mapType,
                            destination.formID, station.baseFormID,
                            destination.localizedName),
                        .destination = std::move(destination),
                    };
                }

                return {
                    .code = EligibilityCode::kHidden,
                    .detail = std::format("unsupported non-station marker {:08X}/{} is vanilla-owned",
                        a_snapshot.markerBodyID, a_snapshot.markerBodyType),
                };
            }

            if (a_snapshot.dossierBodyID == 0 ||
                (a_snapshot.dossierBodyType != kPlanetType &&
                    a_snapshot.dossierBodyType != kMoonType) ||
                a_snapshot.markerBodyID != a_snapshot.dossierBodyID ||
                a_snapshot.markerBodyType != a_snapshot.dossierBodyType) {
                return unavailable(EligibilityCode::kTargetDataUpdating,
                    "TARGET DATA IS UPDATING",
                    std::format("marker {:08X}/{} differs from dossier {:08X}/{}",
                        a_snapshot.markerBodyID, a_snapshot.markerBodyType,
                        a_snapshot.dossierBodyID, a_snapshot.dossierBodyType));
            }

            // Live 1.16.244 proof identifies the selected system-view body as
            // the one StarMapMenuMarkersData row with bIsInHighlightRadius.
            // Tree focus remains the system/star and does not join identity.
            const auto form = RE::TESForm::LookupByID(a_snapshot.dossierBodyID);
            if (!form || form->GetFormType() != RE::FormType::kPNDT) {
                return unavailable(EligibilityCode::kTargetTypeUnsupported,
                    "TARGET TYPE IS NOT SUPPORTED",
                    std::format("dossier {:08X} is not a live PNDT form",
                        a_snapshot.dossierBodyID));
            }
            const auto body = BodyIndex::Lookup(a_snapshot.dossierBodyID);
            if (!body) {
                return unavailable(EligibilityCode::kTargetNotIndexed,
                    "TARGET DATA IS NOT AVAILABLE",
                    std::format("dossier PNDT {:08X} has no parsed GNAM identity",
                        a_snapshot.dossierBodyID));
            }
            const bool remote = body->galaxy.system != a_snapshot.capturedSystem;
            if (remote && g_cruiseActive.load(std::memory_order_acquire)) {
                return unavailable(EligibilityCode::kCruiseActive,
                    "EXIT CRUISE FIRST",
                    "vanilla cannot execute a grav-jump route while Cruise is active, and the stock HUD Cruise control is not handled while the Starmap is open");
            }
            if (remote && !g_loadGameSinkReady.load(std::memory_order_acquire))
                return unavailable(EligibilityCode::kRemoteSafetyUnavailable,
                    "REMOTE CRUISE SAFETY UNAVAILABLE",
                    "guarded TESLoadGameEvent sink is unavailable; refusing a mark that could survive a save load");

            auto destination = BodyDestination{
                .kind = a_snapshot.dossierBodyType == kMoonType ? BodyKind::kMoon : BodyKind::kPlanet,
                .formID = a_snapshot.dossierBodyID,
                .mapFormID = a_snapshot.markerBodyID,
                .mapType = a_snapshot.dossierBodyType,
                .galaxy = body->galaxy,
                .localizedName = a_snapshot.dossierName.empty() ?
                    a_snapshot.markerName : a_snapshot.dossierName,
                .menuGeneration = a_snapshot.generation,
            };
            return {
                .code = EligibilityCode::kEligible,
                .show = true,
                .enabled = true,
                .label = remote ? kRemoteCruiseMapActionLabel : kCruiseMapActionLabel,
                .detail = std::format("eligible {}{} {:08X} '{}' system={}",
                    remote ? "remote " : "",
                    DestinationKindName(destination.kind),
                    destination.formID, destination.localizedName,
                    destination.galaxy.system),
                .destination = std::move(destination),
            };
        }

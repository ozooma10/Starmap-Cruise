// Included by Bridge.cpp inside CFS::Bridge's anonymous namespace.
// Owns vanilla remote routing and continuation setup.

        struct RemoteRouteGate
        {
            bool ready{ false };
            EligibilityCode code{ EligibilityCode::kRemoteCourseUnavailable };
            std::string detail{ "vanilla travel data is unavailable" };
            std::string destinationBodyName;
        };

        bool GetLiveMapMenuRoot(const MapSnapshot& a_snapshot,
            RE::Scaleform::GFx::ASMovieRootBase*& a_root, V& a_menuRoot)
        {
            const auto ui = RE::UI::GetSingleton();
            const RE::BSFixedString mapName{ kMapMenu };
            const auto menu = ui ? ui->GetMenu(mapName) : nullptr;
            if (!menu || !menu->uiMovie || !menu->uiMovie->asMovieRoot ||
                a_snapshot.generation !=
                    g_mapMovie.generation.load(std::memory_order_acquire))
                return false;

            a_root = menu->uiMovie->asMovieRoot.get();
            const char* path = menu->GetRootPath();
            const std::string rootPath = path && *path ? path : "root";
            return a_root->GetVariable(&a_menuRoot, rootPath.c_str()) &&
                   (a_menuRoot.IsObject() || a_menuRoot.IsDisplayObject()) &&
                   menu->uiMovie && menu->uiMovie->asMovieRoot &&
                   menu->uiMovie->asMovieRoot.get() == a_root &&
                   a_snapshot.generation ==
                       g_mapMovie.generation.load(std::memory_order_acquire);
        }

        struct SetCourseButtonState
        {
            bool resolved{ false };
            bool enabled{ false };
            bool visible{ false };
            std::string detail{ "vanilla Set Course button data is unavailable" };

            [[nodiscard]] bool Ready() const noexcept
            {
                return resolved && enabled && visible;
            }
        };

        SetCourseButtonState ReadVanillaSetCourseButton(V& a_menuRoot,
            V& a_hintBar, V& a_buttonData)
        {
            SetCourseButtonState state;
            if (!ObjectMember(a_menuRoot, "ButtonHintBar_mc", a_hintBar) ||
                !ObjectMember(a_hintBar, "SetRouteDestinationButtonData",
                    a_buttonData))
                return state;

            if (!BooleanMember(a_buttonData, "bEnabled", state.enabled) ||
                !BooleanMember(a_buttonData, "bVisible", state.visible)) {
                state.detail = "vanilla Set Course button state is unresolved";
                return state;
            }
            state.resolved = true;
            state.detail = state.Ready() ?
                "vanilla Set Course is enabled and visible" :
                std::format("vanilla Set Course is disabled or hidden (enabled={} visible={})",
                    state.enabled, state.visible);
            return state;
        }

        bool GetVanillaSetCourseData(V& a_menuRoot, V& a_hintBar,
            V& a_buttonData, std::string& a_detail)
        {
            const auto state = ReadVanillaSetCourseButton(a_menuRoot, a_hintBar,
                a_buttonData);
            a_detail = state.detail;
            return state.Ready();
        }

        std::string BrowsedSystemName(V& a_menuRoot)
        {
            V systemHeader;
            V header;
            V textField;
            if (!ObjectMember(a_menuRoot, "SystemNameHeader_mc", systemHeader) ||
                !ObjectMember(systemHeader, "Header_mc", header) ||
                !ObjectMember(header, "text_tf", textField))
                return {};
            return StringMember(textField, "text");
        }

        // Bounded, read-only member enumeration. It copies names and a type tag
        // only: no GFx handle is retained past the visit, so nothing can outlive
        // the movie generation that produced it.
        class MemberNameCollector : public V::ObjectVisitor
        {
        public:
            explicit MemberNameCollector(std::size_t a_limit) : limit(a_limit) {}

            bool IncludeAS3PublicMembers() const override { return true; }

            void Visit(const char* a_name, const V& a_value) override
            {
                ++seen;
                if (!a_name || names.size() >= limit)
                    return;
                const char* kind = "value";
                if (a_value.IsArray())
                    kind = "array";
                else if (a_value.IsDisplayObject())
                    kind = "displayobject";
                else if (a_value.IsObject())
                    kind = "object";
                else if (a_value.IsBoolean())
                    kind = "bool";
                else if (a_value.IsString() || a_value.IsStringW())
                    kind = "string";
                else if (a_value.IsNumber() || a_value.IsInt() || a_value.IsUInt())
                    kind = "number";
                names.emplace_back(std::format("{}:{}", a_name, kind));
            }

            std::vector<std::string> names;
            std::size_t seen{ 0 };
            std::size_t limit{ 0 };
        };

        std::string JoinMemberNames(V& a_object, std::size_t a_limit)
        {
            if (!a_object.IsObject())
                return "<not an object>";
            MemberNameCollector collector{ a_limit };
            a_object.VisitMembers(&collector);
            std::string joined;
            for (const auto& name : collector.names) {
                if (!joined.empty())
                    joined += ", ";
                joined += name;
            }
            if (collector.seen > collector.names.size())
                joined += std::format(", ...(+{} more)",
                    collector.seen - collector.names.size());
            return joined.empty() ? "<none>" : joined;
        }

        bool ReadNativeGalaxySelection(const MapSnapshot& a_snapshot,
            std::uint32_t& a_selectedSystem, bool& a_quickSelectOpen,
            std::string& a_detail);

        struct GalaxySelectionProof
        {
            bool proven{ false };
            const char* authority{ "none" };
            SetCourseButtonState button;
            bool nativeSelectionResolved{ false };
            bool nativeSelectedMatch{ false };
            std::uint32_t nativeSelectedSystem{ 0 };
            bool nativeQuickSelectOpen{ false };
            bool quickSelectMatch{ false };
            bool markerMatch{ false };

            [[nodiscard]] std::string Describe(const MapSnapshot& a_snapshot,
                std::uint32_t a_root) const
            {
                return std::format(
                    "root={:08X} setCourse(resolved={} enabled={} visible={}) nativeSelection(resolved={} selected={:08X} quickSelectOpen={}) quickSelect(published={} count={} cursor={} bodyID={:08X}) marker(count={} bodyID={:08X})",
                    a_root, button.resolved, button.enabled, button.visible,
                    nativeSelectionResolved, nativeSelectedSystem,
                    nativeQuickSelectOpen,
                    a_snapshot.quickSelectPublished, a_snapshot.quickSelectCount,
                    a_snapshot.quickSelectCursorIndex,
                    a_snapshot.quickSelectCursorBodyID,
                    a_snapshot.highlightedMarkerCount, a_snapshot.markerBodyID);
            }
        };

        // A galaxy selection counts as established only when native itself says
        // so. The vanilla Set Course button is the strongest statement; the
        // exact GalaxyState selected-system field, Quick Select cursor, and
        // unique galaxy highlight marker are the other readbacks that name a
        // system directly. Nothing here forces, writes, or infers button state.
        GalaxySelectionProof EvaluateGalaxySelection(V& a_menuRoot,
            const MapSnapshot& a_snapshot, std::uint32_t a_systemBodyID)
        {
            GalaxySelectionProof proof;
            V hintBar;
            V buttonData;
            proof.button = ReadVanillaSetCourseButton(a_menuRoot, hintBar,
                buttonData);
            std::string nativeDetail;
            proof.nativeSelectionResolved = ReadNativeGalaxySelection(a_snapshot,
                proof.nativeSelectedSystem, proof.nativeQuickSelectOpen,
                nativeDetail);
            proof.nativeSelectedMatch = proof.nativeSelectionResolved &&
                a_systemBodyID != 0 &&
                proof.nativeSelectedSystem == a_systemBodyID;
            proof.quickSelectMatch = a_snapshot.quickSelectPublished &&
                a_snapshot.quickSelectCursorIndex >= 0 && a_systemBodyID != 0 &&
                a_snapshot.quickSelectCursorBodyID == a_systemBodyID;
            proof.markerMatch = a_snapshot.highlightedMarkerCount == 1 &&
                a_systemBodyID != 0 &&
                a_snapshot.markerBodyID == a_systemBodyID;

            if (proof.button.Ready()) {
                proof.proven = true;
                proof.authority = "vanilla Set Course button";
            } else if (proof.button.resolved && proof.button.visible &&
                       proof.nativeSelectedMatch) {
                proof.proven = true;
                proof.authority = "native GalaxyState selected system";
            } else if (proof.button.resolved && proof.button.visible &&
                       proof.quickSelectMatch) {
                // QuickSystemSelect.OnItemPress plots from the list cursor
                // without consulting the hint bar. Mirror that seam only when
                // native published the cursor on the captured root.
                proof.proven = true;
                proof.authority = "native Quick Select cursor";
            } else if (proof.button.resolved && proof.button.visible &&
                       proof.markerMatch) {
                proof.proven = true;
                proof.authority = "unique galaxy highlight marker";
            }
            return proof;
        }

        void LogGalaxyFocusDiagnostics(V& a_menuRoot,
            const MapSnapshot& a_snapshot, const GalaxySelectionProof& a_proof,
            std::uint32_t a_systemBodyID)
        {
            REX::WARN("[jump] galaxy marker context not established: {}",
                a_proof.Describe(a_snapshot, a_systemBodyID));
            REX::INFO("[jump] galaxy diagnostics root members: {}",
                JoinMemberNames(a_menuRoot, 96));

            V hintBar;
            if (!ObjectMember(a_menuRoot, "ButtonHintBar_mc", hintBar)) {
                REX::INFO("[jump] galaxy diagnostics: ButtonHintBar_mc is unavailable");
                return;
            }
            MemberNameCollector collector{ 96 };
            hintBar.VisitMembers(&collector);
            std::string joined;
            for (const auto& entry : collector.names) {
                if (!joined.empty())
                    joined += ", ";
                joined += entry;
            }
            REX::INFO("[jump] galaxy diagnostics hint bar members: {}",
                joined.empty() ? "<none>" : joined);
            for (const auto& entry : collector.names) {
                const auto colon = entry.rfind(':');
                const auto name = entry.substr(0, colon);
                if (name.size() < 10 ||
                    name.compare(name.size() - 10, 10, "ButtonData") != 0)
                    continue;
                V data;
                if (!ObjectMember(hintBar, name.c_str(), data))
                    continue;
                bool enabled = false;
                bool visible = false;
                BooleanMember(data, "bEnabled", enabled);
                BooleanMember(data, "bVisible", visible);
                auto text = StringMember(data, "sText");
                if (text.empty())
                    text = StringMember(data, "text");
                auto action = StringMember(data, "sButtonAction");
                if (action.empty())
                    action = StringMember(data, "buttonAction");
                REX::INFO("[jump] galaxy diagnostics hint '{}' enabled={} visible={} text='{}' action='{}'",
                    name, enabled, visible, text, action);
            }
        }

        RemoteRouteGate InspectRemoteRoute(V& a_menuRoot,
            const std::string& a_expectedSystemName, V* a_jumpDataOut = nullptr)
        {
            RemoteRouteGate gate;
            V jumpData;
            bool panelVisible = false;
            if (!ObjectMember(a_menuRoot, "JumpData_mc", jumpData) ||
                !BooleanMember(jumpData, "visible", panelVisible) ||
                !panelVisible) {
                gate.detail = "vanilla travel panel is not showing a plotted route";
                return gate;
            }
            V routeEnd;
            V routeSystemField;
            if (!ObjectMember(jumpData, "PlotPointDisplayEnd_mc", routeEnd) ||
                !ObjectMember(routeEnd, "systemName_tf", routeSystemField)) {
                gate.detail = "vanilla route destination identity is unavailable";
                return gate;
            }
            const auto routeSystem = StringMember(routeSystemField, "text");
            V routeBodyField;
            if (ObjectMember(routeEnd, "bodyName_tf", routeBodyField))
                gate.destinationBodyName = StringMember(routeBodyField, "text");
            if (routeSystem.empty()) {
                gate.detail = "vanilla route destination system is empty";
                return gate;
            }
            if (a_expectedSystemName.empty() ||
                routeSystem != a_expectedSystemName) {
                gate.code = EligibilityCode::kRemoteCourseMismatch;
                gate.detail = std::format("vanilla route ends in '{}' but marked system is '{}'",
                    routeSystem, a_expectedSystemName);
                return gate;
            }

            V executeContainer;
            V executeButton;
            bool executeVisible = false;
            if (!ObjectMember(jumpData, "ExecuteButton_mc", executeContainer) ||
                !ObjectMember(executeContainer, "ExecuteButtonHint_mc", executeButton) ||
                !BooleanMember(executeButton, "Visible", executeVisible) ||
                !executeVisible) {
                gate.detail = "vanilla bCanExecuteRoute gate is false";
                return gate;
            }

            gate.ready = true;
            gate.code = EligibilityCode::kEligible;
            gate.detail = std::format("vanilla executable route ends in '{}'",
                routeSystem);
            if (a_jumpDataOut)
                *a_jumpDataOut = std::move(jumpData);
            return gate;
        }

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
                    .stationOrbitalFormID = orbital.formID,
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
                    g_remoteMoonContinuation.phase = RemoteMoonPhase::kAwaitingParentLock;
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
            continuation.phase = RemoteMoonPhase::kAwaitingParentLock;
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
            REL::Relocation<std::uintptr_t> expectedMenuVtable{
                kStarMapMenuPrimaryVtable };
            std::uintptr_t actualMenuVtable = 0;
            std::memcpy(&actualMenuVtable,
                reinterpret_cast<const void*>(a_live.menuAddress),
                sizeof(actualMenuVtable));
            if (actualMenuVtable != expectedMenuVtable.address()) {
                a_detail = std::format(
                    "StarMapMenu primary vtable mismatch (actual={:016X} expected={:016X})",
                    actualMenuVtable, expectedMenuVtable.address());
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

            REL::Relocation<std::uintptr_t> expectedGalaxyVtable{
                kGalaxyStatePrimaryVtable };
            std::uintptr_t actualGalaxyVtable = 0;
            std::memcpy(&actualGalaxyVtable, a_live.galaxyState,
                sizeof(actualGalaxyVtable));
            if (actualGalaxyVtable != expectedGalaxyVtable.address()) {
                a_detail = std::format(
                    "GalaxyState primary vtable mismatch (actual={:016X} expected={:016X})",
                    actualGalaxyVtable, expectedGalaxyVtable.address());
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
            const auto select = g_selectGalaxySystem.load(std::memory_order_acquire);
            if (!select || a_systemBodyID == 0) {
                a_detail = select ? "captured system body ID is zero" :
                    "native selected-system binding is unavailable";
                return false;
            }

            LiveGalaxyState live;
            if (!ResolveLiveGalaxyState(a_snapshot, live, a_detail))
                return false;

            // This is GalaxyState's stock non-entering selected-system setter,
            // used by normal galaxy selection before Quick Select decides
            // whether the action means focus or plot.
            select(live.galaxyState, a_systemBodyID, false);

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

            const auto close =
                g_closeGalaxyQuickSelect.load(std::memory_order_acquire);
            if (close) {
                close(live.galaxyState,
                    reinterpret_cast<void*>(live.menuAddress +
                        kStarMapMenuDataModelOffset));
            }
            a_detail = close ?
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
                if (RemoteMoonContinuationActive())
                    FailRemoteMoonContinuation("stock HUD Cruise press invocation failed");
                else if (RemoteStationContinuationActive())
                    FailRemoteStationContinuation("stock HUD Cruise press invocation failed");
                else
                    g_state.store(Destination() ? NavState::kMarked : NavState::kIdle,
                        std::memory_order_release);
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
                if (RemoteMoonContinuationActive())
                    FailRemoteMoonContinuation("could not create the internal course event payload");
                else if (RemoteStationContinuationActive())
                    FailRemoteStationContinuation("could not create the remote station course event payload");
                else
                    g_state.store(Destination() ? NavState::kMarked : NavState::kIdle,
                        std::memory_order_release);
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
                if (RemoteMoonContinuationActive())
                    FailRemoteMoonContinuation("HUD rejected the internal course dispatch");
                else if (RemoteStationContinuationActive())
                    FailRemoteStationContinuation("HUD rejected the remote station course dispatch");
                else
                    g_state.store(Destination() ? NavState::kMarked : NavState::kIdle,
                        std::memory_order_release);
            }
        }

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
                        .targetName = selected.localizedName,
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

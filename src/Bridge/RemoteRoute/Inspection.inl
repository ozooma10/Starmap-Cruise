// Included by Bridge.cpp inside CFS::Bridge's anonymous namespace.
// Reads vanilla route/focus state without mutating it.

        struct RemoteRouteGate
        {
            bool ready{ false };
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

        // Thin guards over Engine::GalaxyState: the Bridge owns the live-menu,
        // map-open, and movie-generation gate; the Engine module owns the
        // vtable proofs and the raw memory access. Every native touch resolves
        // a fresh proven pair; nothing is retained between passes.
        bool ResolveLiveGalaxyState(const MapSnapshot& a_snapshot,
            Engine::GalaxyState::Live& a_live, std::string& a_detail)
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
            return Engine::GalaxyState::Resolve(menu.get(), a_live, a_detail);
        }

        bool ReadNativeGalaxySelection(const MapSnapshot& a_snapshot,
            std::uint32_t& a_selectedSystem, bool& a_quickSelectOpen,
            std::string& a_detail)
        {
            Engine::GalaxyState::Live live;
            if (!ResolveLiveGalaxyState(a_snapshot, live, a_detail))
                return false;
            Engine::GalaxyState::ReadSelection(live, a_selectedSystem,
                a_quickSelectOpen);
            a_detail = std::format("selected={:08X} quickSelectOpen={}",
                a_selectedSystem, a_quickSelectOpen);
            return true;
        }

        bool InvokeNativeGalaxySystemSelection(const MapSnapshot& a_snapshot,
            std::uint32_t a_systemBodyID, std::string& a_detail)
        {
            if (a_systemBodyID == 0) {
                a_detail = "captured system body ID is zero";
                return false;
            }

            Engine::GalaxyState::Live live;
            if (!ResolveLiveGalaxyState(a_snapshot, live, a_detail))
                return false;

            // This is GalaxyState's stock non-entering selected-system setter,
            // used by normal galaxy selection before Quick Select decides
            // whether the action means focus or plot.
            if (!RuntimeBindings::SelectGalaxySystem(live.state,
                    a_systemBodyID)) {
                a_detail = "native selected-system binding is unavailable";
                return false;
            }

            // Full re-proof for the post-call readback.
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
            Engine::GalaxyState::Live live;
            if (!ResolveLiveGalaxyState(a_snapshot, live, a_detail))
                return false;
            return Engine::GalaxyState::ArmQuickSelectRouteOwnership(live,
                a_systemBodyID, a_detail);
        }

        bool ConfirmNativeQuickSelectConsumed(const MapSnapshot& a_snapshot,
            std::string& a_detail)
        {
            Engine::GalaxyState::Live live;
            if (!ResolveLiveGalaxyState(a_snapshot, live, a_detail))
                return false;

            if (!Engine::GalaxyState::ReadQuickSelectOpen(live)) {
                a_detail = "native Set Course consumed Quick Select route ownership";
                return true;
            }

            const bool closed = Engine::GalaxyState::CloseQuickSelect(live);
            a_detail = closed ?
                "Set Course did not consume Quick Select route ownership; stock close restored it" :
                "Set Course did not consume Quick Select route ownership and close binding is unavailable";
            return false;
        }

        struct GalaxySelectionProof
        {
            bool proven{ false };
            const char* authority{ "none" };
            SetCourseButtonState button;
            bool nativeSelectionResolved{ false };
            bool nativeSelectedMatch{ false };
            std::uint32_t nativeSelectedSystem{ 0 };
            bool nativeQuickSelectOpen{ false };
            bool markerMatch{ false };

            [[nodiscard]] std::string Describe(const MapSnapshot& a_snapshot,
                std::uint32_t a_root) const
            {
                return std::format(
                    "root={:08X} setCourse(resolved={} enabled={} visible={}) nativeSelection(resolved={} selected={:08X} quickSelectOpen={}) marker(count={} bodyID={:08X})",
                    a_root, button.resolved, button.enabled, button.visible,
                    nativeSelectionResolved, nativeSelectedSystem,
                    nativeQuickSelectOpen,
                    a_snapshot.highlightedMarkerCount, a_snapshot.markerBodyID);
            }
        };

        // A galaxy selection counts as established only when native itself says
        // so. The vanilla Set Course button is the strongest statement; the
        // exact GalaxyState selected-system field and a unique galaxy highlight
        // marker are the other readbacks that name a system directly. The
        // StarMapMenuQuickSelectData feed was removed as an authority: live
        // 1.16.244 enumeration proved it publishes no entry or cursor state at
        // all, so a cursor match could never fire. Nothing here forces, writes,
        // or infers button state.
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
            gate.detail = std::format("vanilla executable route ends in '{}'",
                routeSystem);
            if (a_jumpDataOut)
                *a_jumpDataOut = std::move(jumpData);
            return gate;
        }

// Included by Bridge.cpp inside CFS::Bridge's anonymous namespace.
// Copies Starmap provider values into plain native snapshots.

        class MapDataHandler : public RE::Scaleform::GFx::FunctionHandler
        {
        public:
            void Call(const Params& a_params) override
            {
                V data;
                if (!Payload(a_params, data))
                    return;
                const bool preserveRemoteRoot = RemoteRouteRequestActive();
                V value;
                {
                    std::lock_guard lock{ g_mapMutex };
                    if (data.GetMember("iCurrentMenuView", &value)) {
                        const auto view = static_cast<std::int32_t>(AsNumber(value));
                        if (view != g_map.view) {
                            // Ordinarily galaxy view invalidates the prior root.
                            // A guarded remote Back carries its already-proven
                            // STDT root through this transition so Set Course is
                            // not delayed by an optional republish. The later
                            // route-display identity gate still fails closed.
                            if (view == kGalaxyView && !preserveRemoteRoot)
                                g_map.treeBodyID = 0;
                            g_map.highlightedMarkerCount = 0;
                            g_map.markerBodyID = 0;
                            g_map.markerBodyType = 0;
                            g_map.markerName.clear();
                            g_map.dossierBodyID = 0;
                            g_map.dossierBodyType = 0;
                            g_map.dossierName.clear();
                        }
                        g_map.view = view;
                    }
                }
                g_mapUiDirty.store(true, std::memory_order_release);
            }
        } g_mapDataHandler;

        class BodyInfoHandler : public RE::Scaleform::GFx::FunctionHandler
        {
        public:
            void Call(const Params& a_params) override
            {
                V data;
                if (!Payload(a_params, data))
                    return;
                const auto bodyID = UIntMember(data, "focusedBodyID");
                const auto bodyType = UIntMember(data, "focusedBodyType");
                const auto systemID = MapTreeSystemID(bodyID);
                std::optional<std::uint32_t> expectedRemoteRoot;
                {
                    std::lock_guard lock{ g_remoteRouteMutex };
                    if (g_remoteRouteRequest.phase == RemoteRoutePhase::kAwaitGalaxy ||
                        g_remoteRouteRequest.phase == RemoteRoutePhase::kEstablishSelection)
                        expectedRemoteRoot = g_remoteRouteRequest.systemBodyID;
                }
                const bool acceptedRoot = systemID &&
                    (!expectedRemoteRoot || bodyID == *expectedRemoteRoot);
                bool changed = false;
                {
                    std::lock_guard lock{ g_mapMutex };
                    // Live evidence shows this provider identifies the system/star,
                    // not the highlighted body. Keep it diagnostic-only: clearing
                    // the marker/dossier join here made eligibility depend on
                    // asynchronous callback order. Zero/transient rows must not
                    // erase the last live star. A stale star remains fail-closed
                    // because the remote gate compares its indexed DNAM system ID with the
                    // selected PNDT system.
                    if (acceptedRoot && g_map.treeBodyID != bodyID) {
                        g_map.treeBodyID = bodyID;
                        changed = true;
                    }
                }
                if (changed && Settings::Verbose())
                    REX::INFO("[map] tree root -> STDT {:08X}/{} system={}",
                        bodyID, bodyType, *systemID);
                else if (systemID && expectedRemoteRoot &&
                    bodyID != *expectedRemoteRoot && Settings::Verbose())
                    REX::INFO("[jump] ignored transient galaxy STDT root {:08X}/system {} while awaiting captured root {:08X}",
                        bodyID, *systemID, *expectedRemoteRoot);
                g_mapUiDirty.store(true, std::memory_order_release);
            }
        } g_bodyInfoHandler;

        class MarkerCollector : public V::ArrayVisitor
        {
        public:
            std::uint32_t bodyID{ 0 };
            std::uint32_t bodyType{ 0 };
            std::string name;
            std::size_t highlightedCount{ 0 };

            void Visit(std::uint32_t, const V& a_value) override
            {
                V entry = a_value;
                V highlighted;
                if (!entry.GetMember("bIsInHighlightRadius", &highlighted) ||
                    !highlighted.IsBoolean() || !highlighted.GetBoolean())
                    return;
                ++highlightedCount;
                bodyID = UIntMember(entry, "uBodyID");
                bodyType = UIntMember(entry, "uBodyType");
                name = StringMember(entry, "sMarkerText");
            }
        };

        class MarkersHandler : public RE::Scaleform::GFx::FunctionHandler
        {
        public:
            void Call(const Params& a_params) override
            {
                V data;
                if (!Payload(a_params, data))
                    return;
                MarkerCollector visitor;
                V markers;
                if (data.GetMember("aMarkersData", &markers)) {
                    V inner;
                    if (markers.GetMember("dataA", &inner) && inner.IsArray())
                        markers = inner;
                    if (markers.IsArray())
                        markers.VisitElements(&visitor);
                }
                {
                    std::lock_guard lock{ g_mapMutex };
                    g_map.highlightedMarkerCount = visitor.highlightedCount;
                    if (visitor.highlightedCount == 1) {
                        g_map.markerBodyID = visitor.bodyID;
                        g_map.markerBodyType = visitor.bodyType;
                        g_map.markerName = std::move(visitor.name);
                    } else {
                        g_map.markerBodyID = 0;
                        g_map.markerBodyType = 0;
                        g_map.markerName.clear();
                    }
                }
                g_mapUiDirty.store(true, std::memory_order_release);
            }
        } g_markersHandler;

        // Read-only evidence pin. Live 1.16.244 payload enumeration proved this
        // feed carries only {bShowMenu, bOpenForPlot}: Quick Select entries and
        // the cursor arrive through native's direct SetMarkers(Array) movie
        // call, never through this feed, so no selection authority can be
        // claimed here. The subscription stays so a future payload change is
        // caught by the one-shot shape log instead of being silently assumed.
        class QuickSelectHandler : public RE::Scaleform::GFx::FunctionHandler
        {
        public:
            void Call(const Params& a_params) override
            {
                V data;
                if (!Payload(a_params, data))
                    return;
                if (!shapeLogged.exchange(true, std::memory_order_acq_rel))
                    REX::INFO("[ui] StarMapMenuQuickSelectData members: {}",
                        JoinMemberNames(data, 48));
            }

        private:
            std::atomic<bool> shapeLogged{ false };
        } g_quickSelectHandler;

        class DossierHandler : public RE::Scaleform::GFx::FunctionHandler
        {
        public:
            void Call(const Params& a_params) override
            {
                V data;
                if (!Payload(a_params, data))
                    return;
                {
                    std::lock_guard lock{ g_mapMutex };
                    g_map.dossierBodyID = UIntMember(data, "uBodyID");
                    g_map.dossierBodyType = UIntMember(data, "iType");
                    g_map.dossierName = StringMember(data, "sBodyName");
                }
                g_mapUiDirty.store(true, std::memory_order_release);
            }
        } g_dossierHandler;

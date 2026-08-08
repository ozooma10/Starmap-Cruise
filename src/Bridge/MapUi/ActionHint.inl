// Included by Bridge.cpp inside CFS::Bridge's anonymous namespace.
// Builds and synchronizes the runtime Starmap action controls.

        bool GetMapButtonBar(V& a_hintBar, V& a_buttonBar)
        {
            return a_hintBar.GetMember("HintBar_mc", &a_buttonBar) &&
                   (a_buttonBar.IsObject() || a_buttonBar.IsDisplayObject());
        }

        // Called from OnMovieCreated on the menu-creation thread: plain-state
        // resets only, no Scaleform access.
        void OnMapMovieReplaced()
        {
            ResetHold("Starmap movie replacement");
            g_selectionAcceptedThisOpen.store(false, std::memory_order_release);
            g_mapActionHintSignature.store(0, std::memory_order_release);
            g_mapActionInteractive.store(false, std::memory_order_release);
            g_mapActionTapOnly.store(false, std::memory_order_release);
            g_mapUiDirty.store(true, std::memory_order_release);
            g_uiResetMask.fetch_or(kResetMapUi, std::memory_order_acq_rel);
        }

        void ReleaseStaleMapUiState()
        {
            const auto reset = g_uiResetMask.fetch_and(
                ~kResetMapUi, std::memory_order_acq_rel);
            if ((reset & kResetMapUi) != 0) {
                g_mapActionHint = {};
                g_mapActionInteractive.store(false, std::memory_order_release);
                g_mapActionTapOnly.store(false, std::memory_order_release);
                g_pendingMapAction.store(MapAction::kNone, std::memory_order_release);
            }
        }

        // The change-detector that keeps the hint pass from touching the
        // vanilla button state when nothing it depends on has changed.
        std::uint64_t EligibilitySignature(const MapSnapshot& a_snapshot,
            const MapEligibility& a_eligibility, bool a_engageAvailable)
        {
            std::uint64_t signature = 1469598103934665603ull;
            const auto mix = [&signature](std::uint64_t a_value) {
                signature ^= a_value;
                signature *= 1099511628211ull;
            };
            mix(a_snapshot.generation);
            mix(a_snapshot.session);
            mix(a_snapshot.wasCruising);
            mix(a_engageAvailable);
            mix(g_lastInputWasGamepad.load(std::memory_order_acquire));
            mix(static_cast<std::uint64_t>(a_eligibility.code));
            mix(a_snapshot.markerBodyID);
            mix(a_snapshot.markerBodyType);
            mix(a_snapshot.dossierBodyID);
            mix(a_snapshot.dossierBodyType);
            return signature;
        }

        // Builds either the stacked tap+hold combo button or the tap-only
        // button; the two differ only in the hold leg, the ButtonData class,
        // and the factory type.
        bool BuildCruiseActionButton(RE::Scaleform::GFx::ASMovieRootBase* a_root,
            V& a_buttonBar, V& a_vanillaData, V& a_button,
            V& a_mkbButtonData, V& a_gamepadButtonData, bool a_combo)
        {
            const char* const variant = a_combo ? "stacked" : "tap-only";
            V tapCallback;
            a_root->CreateFunction(&tapCallback, &g_mapTapActionHandler);
            V holdCallback;
            if (a_combo)
                a_root->CreateFunction(&holdCallback, &g_mapHoldActionHandler);
            // ShipReticle swaps distinct data objects on input-device changes:
            // Cruise for MKB and SHMonocle for gamepad. The Starmap mirrors
            // that stock pattern so ButtonKeyHelper can resolve a real glyph.
            const auto buildData = [&](const char* a_userEvent, V& a_buttonData) {
                V pressEventName;
                a_root->CreateString(&pressEventName, a_userEvent);
                V pressArgs[2]{ pressEventName, tapCallback };
                V pressEvent;
                a_root->CreateObject(&pressEvent,
                    "Shared.Components.ButtonControls.ButtonData.UserEventData", pressArgs, 2);
                if (!(pressEvent.IsObject() || pressEvent.IsDisplayObject())) {
                    REX::WARN("[map] {} '{}' action hint unavailable: UserEventData construction failed",
                        variant, a_userEvent);
                    return false;
                }

                V events;
                a_root->CreateArray(&events);
                if (!events.IsArray() || !events.PushBack(pressEvent)) {
                    REX::WARN("[map] {} '{}' action hint unavailable: event array construction failed",
                        variant, a_userEvent);
                    return false;
                }
                if (a_combo) {
                    V emptyName;
                    a_root->CreateString(&emptyName, "");
                    V holdArgs[2]{ emptyName, holdCallback };
                    V holdEvent;
                    a_root->CreateObject(&holdEvent,
                        "Shared.Components.ButtonControls.ButtonData.UserEventData", holdArgs, 2);
                    if (!(holdEvent.IsObject() || holdEvent.IsDisplayObject()) ||
                        !events.PushBack(holdEvent)) {
                        REX::WARN("[map] {} '{}' action hint unavailable: combo event array construction failed",
                            variant, a_userEvent);
                        return false;
                    }
                }

                V pressLabel;
                a_root->CreateString(&pressLabel, kCruiseMapActionLabel);
                if (a_combo) {
                    V holdLabel;
                    a_root->CreateString(&holdLabel, kCruiseMapActionHoldLabel);
                    V dataArgs[3]{ pressLabel, holdLabel, events };
                    a_root->CreateObject(&a_buttonData,
                        "Shared.Components.ButtonControls.ButtonData.ReleaseHoldComboButtonData",
                        dataArgs, 3);
                } else {
                    V dataArgs[2]{ pressLabel, events };
                    a_root->CreateObject(&a_buttonData,
                        "Shared.Components.ButtonControls.ButtonData.ButtonBaseData",
                        dataArgs, 2);
                }
                if (!(a_buttonData.IsObject() || a_buttonData.IsDisplayObject())) {
                    REX::WARN("[map] {} '{}' action hint unavailable: {} construction failed",
                        variant, a_userEvent, a_combo ?
                            "ReleaseHoldComboButtonData" : "ButtonBaseData");
                    return false;
                }

                for (const char* member : { "bEnabled", "bVisible" }) {
                    V value;
                    if (a_vanillaData.GetMember(member, &value))
                        a_buttonData.SetMember(member, value);
                }
                return true;
            };
            if (!buildData(kCruiseMapUserEvent, a_mkbButtonData) ||
                !buildData(kCruiseMapGamepadUserEvent, a_gamepadButtonData))
                return false;

            // ReleaseHoldComboButton/BasicButton are imported library symbols.
            // Let the movie instantiate them through the same factory used by
            // StarMapButtonHintBar.PopulateButtons; creating the AS3 class
            // directly does not attach the exported display asset.
            V factory;
            if (!a_root->GetVariable(&factory,
                    "Shared.Components.ButtonControls.ButtonFactory.ButtonFactory") ||
                !(factory.IsObject() || factory.IsDisplayObject())) {
                REX::WARN("[map] {} action hint unavailable: stock ButtonFactory is inaccessible",
                    variant);
                return false;
            }
            const char* const buttonClass =
                a_combo ? "ReleaseHoldComboButton" : "BasicButton";
            V buttonType;
            a_root->CreateString(&buttonType, buttonClass);
            V& initialData = g_lastInputWasGamepad.load(std::memory_order_acquire) ?
                                 a_gamepadButtonData : a_mkbButtonData;
            V factoryArgs[3]{ buttonType, initialData, a_buttonBar };
            if (!factory.Invoke("AddToButtonBar", &a_button, factoryArgs, 3) ||
                !(a_button.IsObject() || a_button.IsDisplayObject())) {
                REX::WARN("[map] {} action hint unavailable: stock ButtonFactory rejected {}",
                    variant, buttonClass);
                return false;
            }

            return true;
        }

        void SyncCruiseMapButtons(V& a_vanillaData, V& a_buttonBar,
            const MapEligibility& a_eligibility, bool a_tapOnly)
        {
            bool enabled = a_eligibility.enabled;
            V vanillaEnabled;
            if (enabled && a_vanillaData.GetMember("bEnabled", &vanillaEnabled) &&
                vanillaEnabled.IsBoolean()) {
                enabled = vanillaEnabled.GetBoolean();
            }

            const bool comboVisible = a_eligibility.show && !a_tapOnly &&
                                      g_mapActionHint.comboReady;
            const bool tapVisible = a_eligibility.show && a_tapOnly &&
                                    g_mapActionHint.tapReady;
            const bool useGamepad = g_lastInputWasGamepad.load(std::memory_order_acquire);
            bool labelSet = true;
            bool holdLabelSet = true;
            bool comboDataSet = true;
            bool tapDataSet = true;

            if (g_mapActionHint.comboReady) {
                for (V* data : { &g_mapActionHint.comboMkbButtonData,
                         &g_mapActionHint.comboGamepadButtonData }) {
                    data->SetMember("bEnabled", V{ enabled && comboVisible });
                    data->SetMember("bVisible", V{ comboVisible });
                    labelSet = data->SetMember("sButtonText",
                        V{ a_eligibility.label.c_str() }) && labelSet;
                    holdLabelSet = data->SetMember("sHoldText",
                        V{ enabled && comboVisible ? kCruiseMapActionHoldLabel : "" }) &&
                        holdLabelSet;
                }
                V& activeData = useGamepad ? g_mapActionHint.comboGamepadButtonData :
                                             g_mapActionHint.comboMkbButtonData;
                comboDataSet = g_mapActionHint.comboButton.Invoke(
                    "SetButtonData", nullptr, &activeData, 1);
                g_mapActionHint.comboButton.Invoke("RefreshButtonData");
            }
            if (g_mapActionHint.tapReady) {
                for (V* data : { &g_mapActionHint.tapMkbButtonData,
                         &g_mapActionHint.tapGamepadButtonData }) {
                    data->SetMember("bEnabled", V{ enabled && tapVisible });
                    data->SetMember("bVisible", V{ tapVisible });
                    labelSet = data->SetMember("sButtonText",
                        V{ a_eligibility.label.c_str() }) && labelSet;
                }
                V& activeData = useGamepad ? g_mapActionHint.tapGamepadButtonData :
                                             g_mapActionHint.tapMkbButtonData;
                tapDataSet = g_mapActionHint.tapButton.Invoke(
                    "SetButtonData", nullptr, &activeData, 1);
                g_mapActionHint.tapButton.Invoke("RefreshButtonData");
            }
            if ((!labelSet || !holdLabelSet) && Settings::Verbose())
                REX::WARN("[map] action data rejected dynamic labels (tap={} hold={})",
                    labelSet, holdLabelSet);
            const bool desiredReady = a_tapOnly ? g_mapActionHint.tapReady :
                                                  g_mapActionHint.comboReady;
            const bool desiredDataSet = a_tapOnly ? tapDataSet : comboDataSet;
            if (desiredReady && desiredDataSet)
                g_mapHintUsesGamepad.store(useGamepad, std::memory_order_release);
            if (desiredReady && !desiredDataSet)
                REX::WARN("[map] action hint rejected {} input data",
                    useGamepad ? "controller" : "keyboard/mouse");
            g_mapActionHint.installed = a_eligibility.show && desiredReady && desiredDataSet;
            g_mapActionInteractive.store(enabled && desiredReady && desiredDataSet,
                std::memory_order_release);
            g_mapActionTapOnly.store(enabled && desiredReady && desiredDataSet && a_tapOnly,
                std::memory_order_release);
            a_buttonBar.Invoke("RefreshButtons");
        }

        bool InstallCruiseMapButton(RE::Scaleform::GFx::ASMovieRootBase* a_root,
            V& a_buttonBar, V& a_vanillaData,
            std::uint32_t a_generation, const MapEligibility& a_eligibility,
            bool a_tapOnly)
        {
            g_mapActionHint.generation = a_generation;
            if (a_tapOnly && !g_mapActionHint.tapReady) {
                V tapButton;
                V tapMkbButtonData;
                V tapGamepadButtonData;
                if (!BuildCruiseActionButton(a_root, a_buttonBar, a_vanillaData,
                        tapButton, tapMkbButtonData, tapGamepadButtonData, false)) {
                    SyncCruiseMapButtons(a_vanillaData, a_buttonBar, {}, true);
                    REX::WARN("[map] tap-only action hint installation failed; preserving vanilla route button");
                    return false;
                }
                g_mapActionHint.tapReady = true;
                g_mapActionHint.tapButton = std::move(tapButton);
                g_mapActionHint.tapMkbButtonData = std::move(tapMkbButtonData);
                g_mapActionHint.tapGamepadButtonData = std::move(tapGamepadButtonData);
            } else if (!a_tapOnly && !g_mapActionHint.comboReady) {
                V comboButton;
                V comboMkbButtonData;
                V comboGamepadButtonData;
                if (!BuildCruiseActionButton(a_root, a_buttonBar, a_vanillaData,
                        comboButton, comboMkbButtonData, comboGamepadButtonData, true)) {
                    SyncCruiseMapButtons(a_vanillaData, a_buttonBar, {}, false);
                    REX::WARN("[map] stacked action hint installation failed; preserving vanilla route button");
                    return false;
                }
                g_mapActionHint.comboReady = true;
                g_mapActionHint.comboButton = std::move(comboButton);
                g_mapActionHint.comboMkbButtonData = std::move(comboMkbButtonData);
                g_mapActionHint.comboGamepadButtonData = std::move(comboGamepadButtonData);
            }

            SyncCruiseMapButtons(a_vanillaData, a_buttonBar, a_eligibility, a_tapOnly);
            return true;
        }

        void HideCruiseMapButton(V& a_vanillaData, V& a_buttonBar)
        {
            SyncCruiseMapButtons(a_vanillaData, a_buttonBar, {}, false);
        }

        void UpdateMapActionHint()
        {
            MapSnapshot snapshot;
            {
                std::lock_guard lock{ g_mapMutex };
                snapshot = g_map;
            }
            auto eligibility = EvaluateMapSelection(snapshot);
            const bool remoteRoutable = eligibility.destination &&
                UsesRemoteSystemRoute(*eligibility.destination) &&
                eligibility.destination->galaxy.system != snapshot.capturedSystem;
            // Read once so the tap-only decision, the hint signature, and the
            // verbose log stay coherent within this pass.
            const bool engageAvailable =
                g_cruiseEngageAvailable.load(std::memory_order_acquire);
            const bool tapOnly = remoteRoutable || snapshot.wasCruising ||
                                 !engageAvailable;

            RE::Scaleform::GFx::ASMovieRootBase* root = nullptr;
            V menuRoot;
            if (!GetLiveMapMenuRoot(snapshot, root, menuRoot))
                return;

            V hintBar;
            V buttonData;
            std::string setCourseDetail;
            const bool setCourseReady = GetVanillaSetCourseData(menuRoot,
                hintBar, buttonData, setCourseDetail);
            if (!(hintBar.IsObject() || hintBar.IsDisplayObject()) ||
                !(buttonData.IsObject() || buttonData.IsDisplayObject()))
                return;
            if (remoteRoutable && eligibility.enabled) {
                const auto treeSystemID = MapTreeSystemID(snapshot.treeBodyID);
                const bool systemRootReady = treeSystemID && eligibility.destination &&
                    *treeSystemID == eligibility.destination->galaxy.system;
                if (!systemRootReady) {
                    eligibility.code = EligibilityCode::kRemoteCourseUnavailable;
                    eligibility.enabled = false;
                    eligibility.label = "SYSTEM ROUTE IDENTITY UNAVAILABLE";
                    eligibility.detail = std::format("focused STDT root {:08X} resolves to {}, expected system {}",
                        snapshot.treeBodyID,
                        treeSystemID ? std::format("system {}", *treeSystemID) : "no live star",
                        eligibility.destination->galaxy.system);
                } else if (!setCourseReady) {
                    eligibility.code = EligibilityCode::kRemoteCourseUnavailable;
                    eligibility.enabled = false;
                    eligibility.label = "VANILLA SET COURSE UNAVAILABLE";
                    eligibility.detail = setCourseDetail;
                }
            }

            const auto signature = EligibilitySignature(snapshot, eligibility,
                engageAvailable);
            const bool signatureChanged =
                g_mapActionHintSignature.load(std::memory_order_acquire) != signature;
            if (!signatureChanged)
                return;

            V buttonBar;
            if (!GetMapButtonBar(hintBar, buttonBar))
                return;

            if ((g_mapActionHint.comboReady || g_mapActionHint.tapReady) &&
                g_mapActionHint.generation != snapshot.generation)
            {
                g_mapActionHint = {};
                g_mapActionInteractive.store(false, std::memory_order_release);
                g_mapActionTapOnly.store(false, std::memory_order_release);
            }

            bool updated = false;
            if (eligibility.show) {
                const bool desiredReady = tapOnly ?
                                              g_mapActionHint.tapReady :
                                              g_mapActionHint.comboReady;
                if (!g_mapActionHint.installed || !desiredReady) {
                    updated = InstallCruiseMapButton(root, buttonBar, buttonData,
                        snapshot.generation, eligibility, tapOnly);
                } else {
                    SyncCruiseMapButtons(buttonData, buttonBar, eligibility,
                        tapOnly);
                    updated = true;
                }
            } else if (g_mapActionHint.installed) {
                HideCruiseMapButton(buttonData, buttonBar);
                updated = true;
            } else {
                updated = true;
            }
            if (!updated)
                return;

            g_mapActionHintSignature.store(signature, std::memory_order_release);
            if (Settings::Verbose() && signatureChanged)
                REX::INFO("[map] action hint -> {} {} '{}' ({}, session={} generation={})",
                    eligibility.show ? (eligibility.enabled ? "ENABLED" : "DISABLED") : "HIDDEN",
                    remoteRoutable ? "TAP/REMOTE" :
                        snapshot.wasCruising ? "TAP/ACTIVE" :
                        (engageAvailable ? "TAP/HOLD" : "TAP/UNAVAILABLE"),
                    eligibility.show ? eligibility.label : "",
                    eligibility.detail, snapshot.session, snapshot.generation);
        }

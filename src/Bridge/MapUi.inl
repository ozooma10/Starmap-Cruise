// Included by Bridge.cpp inside CFS::Bridge's anonymous namespace.
// Owns map input, controls, and providers.

        class MapActionHandler : public RE::Scaleform::GFx::FunctionHandler
        {
        public:
            explicit MapActionHandler(MapAction a_action) : action(a_action) {}

            void Call(const Params&) override
            {
                // This callback is executing inside the AS3 VM. Record only a
                // value signal; selection evaluation and engine/UI side effects
                // run from the post-advance pump after the VM unwinds.
                const auto pending = action == MapAction::kHold &&
                                             g_mapActionTapOnly.load(std::memory_order_acquire) ?
                                         MapAction::kTap :
                                         action;
                g_pendingMapAction.store(pending, std::memory_order_release);
            }

        private:
            MapAction action;
        };

        MapActionHandler g_mapTapActionHandler{ MapAction::kTap };
        MapActionHandler g_mapHoldActionHandler{ MapAction::kHold };

        void TrackActiveInputDevice(const RE::ButtonEvent* a_button)
        {
            if (!a_button || a_button->value == 0.0f || a_button->heldDownSecs != 0.0f)
                return;
            if (a_button->deviceType != RE::InputEvent::DeviceType::kKeyboard &&
                a_button->deviceType != RE::InputEvent::DeviceType::kMouse &&
                a_button->deviceType != RE::InputEvent::DeviceType::kGamepad)
                return;

            const bool gamepad = a_button->deviceType == RE::InputEvent::DeviceType::kGamepad;
            if (g_lastInputWasGamepad.exchange(gamepad, std::memory_order_acq_rel) != gamepad &&
                g_mapOpen.load(std::memory_order_acquire)) {
                g_mapUiDirty.store(true, std::memory_order_release);
                if (Settings::Verbose())
                    REX::INFO("[input] Starmap action hint input mode -> {}",
                        gamepad ? "controller" : "keyboard/mouse");
            }
        }

        bool ObserveButton(const RE::ButtonEvent* a_button)
        {
            const bool down = a_button->value != 0.0f;
            const bool first = down && a_button->heldDownSecs == 0.0f;
            const char* raw = a_button->strUserEvent.c_str();
            const std::string_view name = raw ? raw : "";

            if (g_mapOpen.load(std::memory_order_acquire) && first &&
                (name == kCruiseMapUserEvent || name == kCruiseMapGamepadUserEvent)) {
                if (a_button->disabled)
                    return false;
                // The runtime-installed ReleaseHoldComboButton must receive the
                // Cruise down/up stream so its stock hold timer and fill
                // animation can distinguish tap from completed hold. Capture
                // only the physical identity here; its callbacks accept the
                // action after the UI gesture finishes.
                if (g_mapActionInteractive.load(std::memory_order_acquire)) {
                    std::lock_guard lock{ g_holdMutex };
                    g_claimMapKey = false;
                    g_hold = {
                        .active = true,
                        .device = a_button->deviceType,
                        .idCode = a_button->idCode,
                        .session = g_mapSession.load(std::memory_order_acquire),
                        .sawCockpitContext = false,
                        .timeoutLogged = false,
                        .suppressUntilRelease = false,
                        .started = Clock::now(),
                    };
                }
                return false;
            }

            std::lock_guard lock{ g_holdMutex };
            if (!g_hold.active || g_hold.device != a_button->deviceType || g_hold.idCode != a_button->idCode)
                return false;

            if (g_mapOpen.load(std::memory_order_acquire) && g_claimMapKey) {
                if (!down)
                    g_hold.active = false;
                return true;
            }

            if (g_hold.suppressUntilRelease) {
                if (!down) {
                    g_hold.active = false;
                    if (HudCruiseInputLatched()) {
                        if (Settings::Verbose())
                            REX::INFO("[input] physical control released after completed Starmap hold; HUD Cruise remains pressed until activation");
                    } else {
                        CancelOrReleaseHudCruiseInput("physical control released");
                    }
                    if (g_state.load(std::memory_order_acquire) == NavState::kAwaitingCruise &&
                        !g_cruiseActive.load(std::memory_order_acquire))
                        g_state.store(Destination() ? NavState::kMarked : NavState::kIdle,
                            std::memory_order_release);
                }
                return true;
            }

            if (name == kCruiseMapUserEvent || name == kCruiseMapGamepadUserEvent ||
                name == "LockCourse") {
                if (!g_hold.sawCockpitContext) {
                    g_hold.sawCockpitContext = true;
                    REX::INFO("[input] natural context handoff: device={} id={} now reports '{}' "
                              "value={:.2f} held={:.3f}",
                        static_cast<std::uint32_t>(a_button->deviceType), a_button->idCode,
                        name, a_button->value, a_button->heldDownSecs);
                }
            }
            if (!down) {
                g_hold.active = false;
                if (g_state.load(std::memory_order_acquire) == NavState::kAwaitingCruise &&
                    !g_cruiseActive.load(std::memory_order_acquire))
                    g_state.store(Destination() ? NavState::kMarked : NavState::kIdle,
                        std::memory_order_release);
                if (Settings::Verbose())
                    REX::INFO("[input] physical hold released (device={} id={} event='{}')",
                        static_cast<std::uint32_t>(a_button->deviceType), a_button->idCode, name);
            }
            return false;
        }

        bool RouteCruiseMapControl(RE::ButtonEvent* a_button)
        {
            if (!a_button || !g_mapOpen.load(std::memory_order_acquire) ||
                !g_mapActionInteractive.load(std::memory_order_acquire) || a_button->disabled)
                return false;

            std::int32_t binding = -1;
            std::int32_t modifier = -1;
            switch (a_button->deviceType) {
            case RE::InputEvent::DeviceType::kKeyboard:
                binding = g_cruiseMapKey.load(std::memory_order_acquire);
                modifier = g_cruiseMapModifier.load(std::memory_order_acquire);
                break;
            case RE::InputEvent::DeviceType::kMouse:
                binding = g_cruiseMapMouseButton.load(std::memory_order_acquire);
                break;
            case RE::InputEvent::DeviceType::kGamepad:
                binding = g_cruiseMapGamepadButton.load(std::memory_order_acquire);
                break;
            default:
                return false;
            }
            if (binding < 0 || a_button->idCode != binding)
                return false;

            // The engine's StarMap context normally names this physical key as
            // a map action rather than Cruise. Route the down edge only when a
            // configured chord modifier is held; always route release so the
            // active stock button cannot be left in its down state. Both
            // variants expose the real Cruise event so Starfield can resolve
            // the player's current binding and glyph; the inactive control is
            // disabled and hidden.
            if (a_button->deviceType == RE::InputEvent::DeviceType::kKeyboard &&
                a_button->value != 0.0f && modifier >= 0 &&
                (::GetAsyncKeyState(modifier) & 0x8000) == 0)
                return false;

            // Route to the data object currently installed on the Scaleform
            // button. If this edge also changed device mode, the next safe UI
            // pass swaps the data object; this first edge still reaches the old
            // object instead of being lost.
            a_button->strUserEvent = RE::BSFixedString{
                g_mapHintUsesGamepad.load(std::memory_order_acquire) ?
                    kCruiseMapGamepadUserEvent : kCruiseMapUserEvent
            };
            return true;
        }

        bool SuppressRemoteRouteControl(const RE::ButtonEvent* a_button)
        {
            if (!a_button || !g_mapOpen.load(std::memory_order_acquire) ||
                !RemoteRouteRequestActive())
                return false;

            std::int32_t binding = -1;
            switch (a_button->deviceType) {
            case RE::InputEvent::DeviceType::kKeyboard:
                binding = g_cruiseMapKey.load(std::memory_order_acquire);
                break;
            case RE::InputEvent::DeviceType::kMouse:
                binding = g_cruiseMapMouseButton.load(std::memory_order_acquire);
                break;
            case RE::InputEvent::DeviceType::kGamepad:
                binding = g_cruiseMapGamepadButton.load(std::memory_order_acquire);
                break;
            default:
                return false;
            }
            if (binding < 0 || a_button->idCode != binding)
                return false;
            if (a_button->value != 0.0f && a_button->heldDownSecs == 0.0f)
                REX::INFO("[input] suppressed repeated Starmap Cruise control while remote route handoff is active");
            return true;
        }

        void ProcessInputHook(RE::BSInputEventReceiver* a_receiver, const RE::InputEvent* a_head)
        {
            struct Fix
            {
                RE::InputEvent* node{ nullptr };
                RE::InputEvent* next{ nullptr };
            };
            struct RoutedEvent
            {
                RE::ButtonEvent* event{ nullptr };
                RE::BSFixedString originalName;
            };
            std::array<Fix, 16> fixes{};
            std::array<RoutedEvent, 16> routedEvents{};
            std::size_t fixCount = 0;
            std::size_t routedCount = 0;
            const RE::InputEvent* head = a_head;
            RE::InputEvent* previous = nullptr;

            for (auto* event = a_head; event;) {
                auto* next = event->next;
                bool drop = false;
                if (event->eventType == RE::InputEvent::EventType::kButton) {
                    auto* button = const_cast<RE::ButtonEvent*>(
                        static_cast<const RE::ButtonEvent*>(event));
                    TrackActiveInputDevice(button);
                    drop = SuppressRemoteRouteControl(button);
                    if (!drop && routedCount < routedEvents.size()) {
                        const auto originalName = button->strUserEvent;
                        if (RouteCruiseMapControl(button))
                            routedEvents[routedCount++] = { button, originalName };
                    }
                    if (!drop)
                        drop = ObserveButton(button);
                }
                if (drop && fixCount < fixes.size()) {
                    if (previous) {
                        fixes[fixCount++] = { previous, previous->next };
                        previous->next = next;
                    } else {
                        head = next;
                    }
                } else {
                    previous = const_cast<RE::InputEvent*>(event);
                }
                event = next;
            }

            if (const auto original = g_originalInput.load(std::memory_order_acquire))
                original(a_receiver, head);
            for (std::size_t i = fixCount; i-- > 0;)
                fixes[i].node->next = fixes[i].next;
            for (std::size_t i = routedCount; i-- > 0;)
                routedEvents[i].event->strUserEvent = routedEvents[i].originalName;
        }

        void TryInstallInputHook()
        {
            if (g_inputInstalled.load(std::memory_order_acquire))
                return;
            const auto ui = RE::UI::GetSingleton();
            if (!ui)
                return;
            bool expected = false;
            if (!g_inputInstalled.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
                return;
            auto* receiver = static_cast<RE::BSInputEventReceiver*>(ui);
            const auto vtable = *reinterpret_cast<std::uintptr_t*>(receiver);
            const auto original = *reinterpret_cast<std::uintptr_t*>(vtable + sizeof(void*));
            g_originalInput.store(reinterpret_cast<ProcessInput_t>(original), std::memory_order_release);
            REL::Relocation<std::uintptr_t> relocation{ vtable };
            relocation.write_vfunc(1, &ProcessInputHook);
            REX::INFO("[input] UI::PerformInputProcessing hook installed (physical device/id tracking)");
        }

        std::uint64_t EligibilitySignature(const MapSnapshot& a_snapshot,
            const MapEligibility& a_eligibility)
        {
            std::uint64_t signature = 1469598103934665603ull;
            const auto mix = [&signature](std::uint64_t a_value) {
                signature ^= a_value;
                signature *= 1099511628211ull;
            };
            mix(a_snapshot.generation);
            mix(a_snapshot.session);
            mix(a_snapshot.wasCruising);
            mix(a_snapshot.cruiseEngageAvailable);
            mix(g_lastInputWasGamepad.load(std::memory_order_acquire));
            mix(static_cast<std::uint64_t>(a_eligibility.code));
            mix(a_snapshot.markerBodyID);
            mix(a_snapshot.markerBodyType);
            mix(a_snapshot.dossierBodyID);
            mix(a_snapshot.dossierBodyType);
            return signature;
        }

        bool GetMapButtonBar(V& a_hintBar, V& a_buttonBar)
        {
            return a_hintBar.GetMember("HintBar_mc", &a_buttonBar) &&
                   (a_buttonBar.IsObject() || a_buttonBar.IsDisplayObject());
        }

        bool BuildCruiseComboButton(RE::Scaleform::GFx::ASMovieRootBase* a_root,
            V& a_buttonBar, V& a_vanillaData, V& a_button,
            V& a_mkbButtonData, V& a_gamepadButtonData)
        {
            V tapCallback;
            a_root->CreateFunction(&tapCallback, &g_mapTapActionHandler);
            V holdCallback;
            a_root->CreateFunction(&holdCallback, &g_mapHoldActionHandler);
            const auto buildData = [&](const char* a_userEvent, V& a_buttonData) {
                V pressEventName;
                a_root->CreateString(&pressEventName, a_userEvent);
                V pressArgs[2]{ pressEventName, tapCallback };
                V pressEvent;
                a_root->CreateObject(&pressEvent,
                    "Shared.Components.ButtonControls.ButtonData.UserEventData", pressArgs, 2);

                V emptyName;
                a_root->CreateString(&emptyName, "");
                V holdArgs[2]{ emptyName, holdCallback };
                V holdEvent;
                a_root->CreateObject(&holdEvent,
                    "Shared.Components.ButtonControls.ButtonData.UserEventData", holdArgs, 2);
                if (!(pressEvent.IsObject() || pressEvent.IsDisplayObject()) ||
                    !(holdEvent.IsObject() || holdEvent.IsDisplayObject())) {
                    REX::WARN("[map] stacked '{}' action hint unavailable: UserEventData construction failed",
                        a_userEvent);
                    return false;
                }

                V events;
                a_root->CreateArray(&events);
                if (!events.IsArray() || !events.PushBack(pressEvent) ||
                    !events.PushBack(holdEvent)) {
                    REX::WARN("[map] stacked '{}' action hint unavailable: combo event array construction failed",
                        a_userEvent);
                    return false;
                }

                V pressLabel;
                V holdLabel;
                a_root->CreateString(&pressLabel, kCruiseMapActionLabel);
                a_root->CreateString(&holdLabel, kCruiseMapActionHoldLabel);
                V dataArgs[3]{ pressLabel, holdLabel, events };
                a_root->CreateObject(&a_buttonData,
                    "Shared.Components.ButtonControls.ButtonData.ReleaseHoldComboButtonData",
                    dataArgs, 3);
                if (!(a_buttonData.IsObject() || a_buttonData.IsDisplayObject())) {
                    REX::WARN("[map] stacked '{}' action hint unavailable: ReleaseHoldComboButtonData construction failed",
                        a_userEvent);
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

            // ReleaseHoldComboButton is an imported library symbol. Let the
            // movie instantiate it through the same factory used by
            // StarMapButtonHintBar.PopulateButtons; creating the AS3 class
            // directly does not attach the exported display asset.
            V factory;
            if (!a_root->GetVariable(&factory,
                    "Shared.Components.ButtonControls.ButtonFactory.ButtonFactory") ||
                !(factory.IsObject() || factory.IsDisplayObject())) {
                REX::WARN("[map] stacked action hint unavailable: stock ButtonFactory is inaccessible");
                return false;
            }
            V buttonType;
            a_root->CreateString(&buttonType, "ReleaseHoldComboButton");
            V& initialData = g_lastInputWasGamepad.load(std::memory_order_acquire) ?
                                 a_gamepadButtonData : a_mkbButtonData;
            V factoryArgs[3]{ buttonType, initialData, a_buttonBar };
            if (!factory.Invoke("AddToButtonBar", &a_button, factoryArgs, 3) ||
                !(a_button.IsObject() || a_button.IsDisplayObject())) {
                REX::WARN("[map] stacked action hint unavailable: stock ButtonFactory rejected ReleaseHoldComboButton");
                return false;
            }

            return true;
        }

        bool BuildCruiseTapButton(RE::Scaleform::GFx::ASMovieRootBase* a_root,
            V& a_buttonBar, V& a_vanillaData, V& a_button,
            V& a_mkbButtonData, V& a_gamepadButtonData)
        {
            V tapCallback;
            a_root->CreateFunction(&tapCallback, &g_mapTapActionHandler);
            // ShipReticle swaps distinct data objects on input-device changes:
            // Cruise for MKB and SHMonocle for gamepad. The Starmap mirrors
            // that stock pattern so ButtonKeyHelper can resolve a real glyph.
            const auto buildData = [&](const char* a_userEvent, V& a_buttonData) {
                V eventName;
                a_root->CreateString(&eventName, a_userEvent);
                V eventArgs[2]{ eventName, tapCallback };
                V tapEvent;
                a_root->CreateObject(&tapEvent,
                    "Shared.Components.ButtonControls.ButtonData.UserEventData", eventArgs, 2);
                if (!(tapEvent.IsObject() || tapEvent.IsDisplayObject())) {
                    REX::WARN("[map] tap-only '{}' action hint unavailable: UserEventData construction failed",
                        a_userEvent);
                    return false;
                }

                V events;
                a_root->CreateArray(&events);
                if (!events.IsArray() || !events.PushBack(tapEvent)) {
                    REX::WARN("[map] tap-only '{}' action hint unavailable: event array construction failed",
                        a_userEvent);
                    return false;
                }

                V label;
                a_root->CreateString(&label, kCruiseMapActionLabel);
                V dataArgs[2]{ label, events };
                a_root->CreateObject(&a_buttonData,
                    "Shared.Components.ButtonControls.ButtonData.ButtonBaseData",
                    dataArgs, 2);
                if (!(a_buttonData.IsObject() || a_buttonData.IsDisplayObject())) {
                    REX::WARN("[map] tap-only '{}' action hint unavailable: ButtonBaseData construction failed",
                        a_userEvent);
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

            V factory;
            if (!a_root->GetVariable(&factory,
                    "Shared.Components.ButtonControls.ButtonFactory.ButtonFactory") ||
                !(factory.IsObject() || factory.IsDisplayObject())) {
                REX::WARN("[map] tap-only action hint unavailable: stock ButtonFactory is inaccessible");
                return false;
            }
            V buttonType;
            a_root->CreateString(&buttonType, "BasicButton");
            V& initialData = g_lastInputWasGamepad.load(std::memory_order_acquire) ?
                                 a_gamepadButtonData : a_mkbButtonData;
            V factoryArgs[3]{ buttonType, initialData, a_buttonBar };
            if (!factory.Invoke("AddToButtonBar", &a_button, factoryArgs, 3) ||
                !(a_button.IsObject() || a_button.IsDisplayObject())) {
                REX::WARN("[map] tap-only action hint unavailable: stock ButtonFactory rejected BasicButton");
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
                if (!BuildCruiseTapButton(a_root, a_buttonBar, a_vanillaData,
                        tapButton, tapMkbButtonData, tapGamepadButtonData)) {
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
                if (!BuildCruiseComboButton(a_root, a_buttonBar, a_vanillaData,
                        comboButton, comboMkbButtonData, comboGamepadButtonData)) {
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
            const bool tapOnly = remoteRoutable || snapshot.wasCruising ||
                                 !snapshot.cruiseEngageAvailable;

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

            const auto signature = EligibilitySignature(snapshot, eligibility);
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
                        (snapshot.cruiseEngageAvailable ? "TAP/HOLD" : "TAP/UNAVAILABLE"),
                    eligibility.show ? eligibility.label : "",
                    eligibility.detail, snapshot.session, snapshot.generation);
        }

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
                            if (view == kGalaxyView && !preserveRemoteRoot) {
                                g_map.treeBodyID = 0;
                                g_map.treeBodyType = 0;
                            }
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
                    g_map.systemLocationID = UIntMember(data, "uSystemLocationID");
                    g_map.bodyLocationID = UIntMember(data, "uBodyLocationID");
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
                    if (acceptedRoot && (g_map.treeBodyID != bodyID ||
                            g_map.treeBodyType != bodyType)) {
                        g_map.treeBodyID = bodyID;
                        g_map.treeBodyType = bodyType;
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

        class QuickSelectCollector : public V::ArrayVisitor
        {
        public:
            explicit QuickSelectCollector(std::int32_t a_cursor) : cursor(a_cursor) {}

            void Visit(std::uint32_t a_index, const V& a_value) override
            {
                ++count;
                if (cursor < 0 || static_cast<std::uint32_t>(cursor) != a_index)
                    return;
                V entry = a_value;
                cursorBodyID = UIntMember(entry, "uBodyID");
            }

            std::int32_t cursor{ -1 };
            std::uint32_t count{ 0 };
            std::uint32_t cursorBodyID{ 0 };
        };

        // Read-only mirror of vanilla's Quick Select state. It is the one native
        // statement of galaxy system selection that does not depend on the
        // physical cursor, so the remote driver can prove a selection without
        // touching it.
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

                V entries;
                if (!data.GetMember("entryList", &entries))
                    data.GetMember("aEntryList", &entries);
                if (entries.IsObject() && !entries.IsArray()) {
                    V inner;
                    if (entries.GetMember("dataA", &inner) && inner.IsArray())
                        entries = inner;
                }

                std::int32_t cursor = -1;
                V cursorValue;
                if (data.GetMember("uCursorSelectionIndex", &cursorValue) &&
                    !cursorValue.IsUndefined())
                    cursor = static_cast<std::int32_t>(AsNumber(cursorValue));

                QuickSelectCollector visitor{ cursor };
                if (entries.IsArray())
                    entries.VisitElements(&visitor);

                {
                    std::lock_guard lock{ g_mapMutex };
                    g_map.quickSelectPublished = true;
                    g_map.quickSelectCount = visitor.count;
                    g_map.quickSelectCursorIndex = cursor;
                    g_map.quickSelectCursorBodyID = visitor.cursorBodyID;
                }
                if (Settings::Verbose())
                    REX::INFO("[ui] quick select cursor={} of {} entries bodyID={:08X}",
                        cursor, visitor.count, visitor.cursorBodyID);
                g_mapUiDirty.store(true, std::memory_order_release);
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

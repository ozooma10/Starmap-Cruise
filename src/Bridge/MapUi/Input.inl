// Included by Bridge.cpp inside CFS::Bridge's anonymous namespace.
// Owns physical input tracking, routing, and the native input hook.

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
                        .suppressUntilRelease = false,
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
                        ReleaseNavStateToMark();
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
                    ReleaseNavStateToMark();
                if (Settings::Verbose())
                    REX::INFO("[input] physical hold released (device={} id={} event='{}')",
                        static_cast<std::uint32_t>(a_button->deviceType), a_button->idCode, name);
            }
            return false;
        }

        // Resolves the map-context Cruise binding (and keyboard chord
        // modifier) for a device; -1 means unbound.
        void CruiseMapBindingFor(RE::InputEvent::DeviceType a_device,
            std::int32_t& a_binding, std::int32_t& a_modifier)
        {
            a_binding = -1;
            a_modifier = -1;
            switch (a_device) {
            case RE::InputEvent::DeviceType::kKeyboard:
                a_binding = g_cruiseMapKey.load(std::memory_order_acquire);
                a_modifier = g_cruiseMapModifier.load(std::memory_order_acquire);
                break;
            case RE::InputEvent::DeviceType::kMouse:
                a_binding = g_cruiseMapMouseButton.load(std::memory_order_acquire);
                break;
            case RE::InputEvent::DeviceType::kGamepad:
                a_binding = g_cruiseMapGamepadButton.load(std::memory_order_acquire);
                break;
            default:
                break;
            }
        }

        bool RouteCruiseMapControl(RE::ButtonEvent* a_button)
        {
            if (!a_button || !g_mapOpen.load(std::memory_order_acquire) ||
                !g_mapActionInteractive.load(std::memory_order_acquire) || a_button->disabled)
                return false;

            std::int32_t binding = -1;
            std::int32_t modifier = -1;
            CruiseMapBindingFor(a_button->deviceType, binding, modifier);
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
            std::int32_t modifier = -1;
            CruiseMapBindingFor(a_button->deviceType, binding, modifier);
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

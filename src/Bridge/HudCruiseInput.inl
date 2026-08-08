// Included by Bridge.cpp inside CFS::Bridge's anonymous namespace.
// The complete synthetic HUD Cruise keypress latch: press queueing,
// cancellation/release, pending-edge handoff, the activation safety audit,
// and the fail-closed reset. The one consumer that enters AS3
// (DriveHudCruiseInput in HudCruise.inl) only takes edges from here; movie
// replacement resets route through Reset().

        enum class HudCruiseInputPhase : std::uint8_t
        {
            kIdle,
            kPressPending,
            kPressed,
            kReleasePending,
        };

        class HudCruiseInputLatch
        {
        public:
            void CancelOrRelease(const char* a_reason)
            {
                const char* action = nullptr;
                {
                    std::lock_guard lock{ mutex };
                    latched = false;
                    started = {};
                    if (phase == HudCruiseInputPhase::kPressPending) {
                        phase = HudCruiseInputPhase::kIdle;
                        userEvent = "Cruise";
                        action = "cancelled pending press";
                    } else if (phase == HudCruiseInputPhase::kPressed) {
                        phase = HudCruiseInputPhase::kReleasePending;
                        action = "queued release";
                    }
                }
                g_hudUiDirty.store(true, std::memory_order_release);
                if (action && Settings::Verbose())
                    REX::INFO("[input] HUD Cruise {}: {}", action, a_reason);
            }

            [[nodiscard]] bool QueuePress(RE::InputEvent::DeviceType a_device)
            {
                std::lock_guard lock{ mutex };
                if (phase != HudCruiseInputPhase::kIdle)
                    return false;
                // ShipReticle installs a different quick/hold combo for
                // controller mode. Both combos reach the same stock Cruise
                // hold callback.
                userEvent = a_device == RE::InputEvent::DeviceType::kGamepad ?
                                kCruiseMapGamepadUserEvent :
                                "Cruise";
                phase = HudCruiseInputPhase::kPressPending;
                // The Starmap's completed fill is the user's confirmation. Keep
                // the separate cockpit hold pressed even if the physical key is
                // released, then release on Cruise activation or the safety
                // limit.
                latched = true;
                started = Clock::now();
                g_hudUiDirty.store(true, std::memory_order_release);
                return true;
            }

            [[nodiscard]] bool Latched()
            {
                std::lock_guard lock{ mutex };
                return latched;
            }

            struct PendingEdges
            {
                bool press{ false };
                bool release{ false };
                const char* userEvent{ "Cruise" };
            };

            // Publishes the new phase before the caller enters ActionScript so
            // a synchronous callback cannot repeat the edge.
            [[nodiscard]] PendingEdges TakePending()
            {
                std::lock_guard lock{ mutex };
                PendingEdges edges;
                edges.userEvent = userEvent;
                if (phase == HudCruiseInputPhase::kPressPending) {
                    phase = HudCruiseInputPhase::kPressed;
                    edges.press = true;
                } else if (phase == HudCruiseInputPhase::kReleasePending) {
                    phase = HudCruiseInputPhase::kIdle;
                    userEvent = "Cruise";
                    edges.release = true;
                }
                return edges;
            }

            // The press invocation itself failed before reaching AS3.
            void FailPress()
            {
                std::lock_guard lock{ mutex };
                Clear();
            }

            // Movie replacement: plain-state reset only, no Scaleform access.
            void Reset()
            {
                std::lock_guard lock{ mutex };
                Clear();
            }

            // True while a latched press has outlived the activation safety
            // limit; CancelOrRelease still owns the actual release.
            [[nodiscard]] bool PressExpired(Clock::duration a_limit)
            {
                std::lock_guard lock{ mutex };
                return latched && started != Clock::time_point{} &&
                       Clock::now() - started > a_limit;
            }

            // Fault-latch reset. Returns true when a press may already have
            // reached AS3 and can no longer be safely released.
            [[nodiscard]] bool FailClosed() noexcept
            {
                try {
                    std::lock_guard lock{ mutex };
                    const bool unresolvedPressedEdge =
                        phase == HudCruiseInputPhase::kPressed ||
                        phase == HudCruiseInputPhase::kReleasePending;
                    Clear();
                    return unresolvedPressedEdge;
                } catch (...) {
                    return true;
                }
            }

        private:
            void Clear()
            {
                phase = HudCruiseInputPhase::kIdle;
                userEvent = "Cruise";
                latched = false;
                started = {};
            }

            std::mutex mutex;
            HudCruiseInputPhase phase{ HudCruiseInputPhase::kIdle };
            const char* userEvent{ "Cruise" };
            bool latched{ false };
            Clock::time_point started{};
        };
        HudCruiseInputLatch g_hudCruiseInput;

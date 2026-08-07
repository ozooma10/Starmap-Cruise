// Included by Bridge.cpp inside CFS::Bridge's anonymous namespace.
// Owns guarded load and grav-jump event subscriptions.

        class LoadGameSink final : public RE::BSTEventSink<RE::TESLoadGameEvent>
        {
        public:
            RE::BSEventNotifyControl ProcessEvent(const RE::TESLoadGameEvent&,
                RE::BSTEventSource<RE::TESLoadGameEvent>*) override
            {
                // Event delivery is not assumed to be the main game thread.
                // Publish only a value signal; the verified BSService frame
                // owns the actual navigation/input reset.
                g_loadClearPending.store(true, std::memory_order_release);
                REX::INFO("[safety] TESLoadGameEvent received; queued destination clear");
                return RE::BSEventNotifyControl::kContinue;
            }
        } g_loadGameSink;

        void TryInstallLoadGameSink()
        {
            if (g_loadGameSinkAttempted.exchange(true, std::memory_order_acq_rel))
                return;

            const auto function = kLoadGameGetEventSource.address();
            std::array<std::uint8_t, kGlobalEventGetEventSource116244Prologue.size()> prologue{};
            const bool readable = ReadMemory(function, prologue);
            const bool prologueMatches = readable &&
                prologue == kGlobalEventGetEventSource116244Prologue;
            if (!prologueMatches) {
                REX::ERROR("[safety] TESLoadGameEvent ID 64149 fingerprint failed at {:016X}: [{}]; remote targets disabled",
                    function, readable ? HexBytes(prologue) : "unreadable");
                return;
            }

            const auto source = RE::TESLoadGameEvent::GetEventSource();
            std::uintptr_t vtable = 0;
            const bool sourceReadable = source &&
                ReadMemory(reinterpret_cast<std::uintptr_t>(source), vtable);
            const auto expectedSource = kLoadGameSourceStatic.address();
            const auto expectedVtable = kLoadGameSourceVtable.address();
            const bool sourceMatches = reinterpret_cast<std::uintptr_t>(source) == expectedSource;
            const bool vtableMatches = sourceReadable && vtable == expectedVtable;
            REX::INFO("[safety] TESLoadGameEvent guard prologue=[{}] source={:016X}/{:016X} match={} vtable={:016X}/{:016X} match={}",
                HexBytes(prologue), reinterpret_cast<std::uintptr_t>(source), expectedSource,
                sourceMatches, vtable, expectedVtable, vtableMatches);
            if (!source || !sourceMatches || !vtableMatches) {
                REX::ERROR("[safety] TESLoadGameEvent identity guard failed; remote targets disabled");
                return;
            }

            source->RegisterSink(&g_loadGameSink);
            g_loadGameSinkReady.store(true, std::memory_order_release);
            REX::INFO("[safety] TESLoadGameEvent sink registered; jump-persistent remote targets enabled");
        }

        class GravJumpSink final : public RE::BSTEventSink<RE::Spaceship::GravJumpEvent>
        {
        public:
            RE::BSEventNotifyControl ProcessEvent(const RE::Spaceship::GravJumpEvent& a_event,
                RE::BSTEventSource<RE::Spaceship::GravJumpEvent>*) override
            {
                const auto player = RE::PlayerCharacter::GetSingleton();
                if (!player || !a_event.ship || a_event.ship.get() != player->GetSpaceship())
                    return RE::BSEventNotifyControl::kContinue;

                const auto retained = Destination();
                REX::INFO("[jump] player grav-jump state={} destination={:08X} navState={} retainedTarget={:08X}",
                    a_event.state,
                    a_event.destination ? a_event.destination->GetFormID() : 0,
                    static_cast<std::uint32_t>(g_state.load(std::memory_order_acquire)),
                    retained ? retained->formID : 0);
                return RE::BSEventNotifyControl::kContinue;
            }
        } g_gravJumpSink;

        void TryInstallGravJumpSink()
        {
            if (g_gravJumpSinkAttempted.exchange(true, std::memory_order_acq_rel))
                return;

            const auto function = kGravJumpGetEventSource.address();
            std::array<std::uint8_t, kGlobalEventGetEventSource116244Prologue.size()> prologue{};
            const bool readable = ReadMemory(function, prologue);
            const bool prologueMatches = readable &&
                prologue == kGlobalEventGetEventSource116244Prologue;
            const auto source = prologueMatches ?
                RE::Spaceship::GravJumpEvent::GetEventSource() : nullptr;
            std::uintptr_t vtable = 0;
            const bool sourceReadable = source &&
                ReadMemory(reinterpret_cast<std::uintptr_t>(source), vtable);
            const auto expectedVtable = kGravJumpSourceVtable.address();
            const bool vtableMatches = sourceReadable && vtable == expectedVtable;
            REX::INFO("[jump] GravJumpEvent guard prologue=[{}] source={:016X} vtable={:016X}/{:016X} match={}",
                readable ? HexBytes(prologue) : "unreadable",
                reinterpret_cast<std::uintptr_t>(source), vtable, expectedVtable,
                prologueMatches && vtableMatches);
            if (!source || !prologueMatches || !vtableMatches) {
                REX::WARN("[jump] GravJumpEvent identity guard failed; jump acknowledgement diagnostics unavailable");
                return;
            }

            source->RegisterSink(&g_gravJumpSink);
            REX::INFO("[jump] player-filtered GravJumpEvent acknowledgement sink registered");
        }

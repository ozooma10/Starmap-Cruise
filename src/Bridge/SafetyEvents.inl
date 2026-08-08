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

        // Shared mechanical legs of the global-event identity guard: exact
        // 1.16.244 getter prologue, then the live source's primary vtable.
        // Per-sink policy (the extra source-static leg, logging, registration)
        // stays with each installer.
        template <class Event>
        struct GlobalEventSourceProof
        {
            RE::BSTEventSource<Event>* source{ nullptr };
            std::uintptr_t vtable{ 0 };
            std::array<std::uint8_t, kGlobalEventGetEventSource116244Prologue.size()> prologue{};
            bool prologueReadable{ false };
            bool prologueMatches{ false };
            bool vtableMatches{ false };

            [[nodiscard]] std::uintptr_t SourceAddress() const noexcept
            {
                return reinterpret_cast<std::uintptr_t>(source);
            }
        };

        template <class Event>
        GlobalEventSourceProof<Event> ProveGlobalEventSource(
            const REL::ID& a_getter, const REL::ID& a_expectedVtable)
        {
            GlobalEventSourceProof<Event> proof;
            proof.prologueReadable = ReadMemory(a_getter.address(), proof.prologue);
            proof.prologueMatches = proof.prologueReadable &&
                proof.prologue == kGlobalEventGetEventSource116244Prologue;
            if (!proof.prologueMatches)
                return proof;
            proof.source = Event::GetEventSource();
            const bool sourceReadable = proof.source &&
                ReadMemory(proof.SourceAddress(), proof.vtable);
            proof.vtableMatches = sourceReadable &&
                proof.vtable == a_expectedVtable.address();
            return proof;
        }

        void TryInstallLoadGameSink()
        {
            if (g_loadGameSinkAttempted.exchange(true, std::memory_order_acq_rel))
                return;

            const auto proof = ProveGlobalEventSource<RE::TESLoadGameEvent>(
                kLoadGameGetEventSource, kLoadGameSourceVtable);
            if (!proof.prologueMatches) {
                REX::ERROR("[safety] TESLoadGameEvent ID 64149 fingerprint failed at {:016X}: [{}]; remote targets disabled",
                    kLoadGameGetEventSource.address(),
                    proof.prologueReadable ? HexBytes(proof.prologue) : "unreadable");
                return;
            }

            const auto expectedSource = kLoadGameSourceStatic.address();
            const auto expectedVtable = kLoadGameSourceVtable.address();
            const bool sourceMatches = proof.SourceAddress() == expectedSource;
            REX::INFO("[safety] TESLoadGameEvent guard prologue=[{}] source={:016X}/{:016X} match={} vtable={:016X}/{:016X} match={}",
                HexBytes(proof.prologue), proof.SourceAddress(), expectedSource,
                sourceMatches, proof.vtable, expectedVtable, proof.vtableMatches);
            if (!proof.source || !sourceMatches || !proof.vtableMatches) {
                REX::ERROR("[safety] TESLoadGameEvent identity guard failed; remote targets disabled");
                return;
            }

            proof.source->RegisterSink(&g_loadGameSink);
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

            const auto proof = ProveGlobalEventSource<RE::Spaceship::GravJumpEvent>(
                kGravJumpGetEventSource, kGravJumpSourceVtable);
            REX::INFO("[jump] GravJumpEvent guard prologue=[{}] source={:016X} vtable={:016X}/{:016X} match={}",
                proof.prologueReadable ? HexBytes(proof.prologue) : "unreadable",
                proof.SourceAddress(), proof.vtable, kGravJumpSourceVtable.address(),
                proof.prologueMatches && proof.vtableMatches);
            if (!proof.source || !proof.prologueMatches || !proof.vtableMatches) {
                REX::WARN("[jump] GravJumpEvent identity guard failed; jump acknowledgement diagnostics unavailable");
                return;
            }

            proof.source->RegisterSink(&g_gravJumpSink);
            REX::INFO("[jump] player-filtered GravJumpEvent acknowledgement sink registered");
        }

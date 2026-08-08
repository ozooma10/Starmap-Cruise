#pragma once

#include "Engine/RuntimeMemory.h"

#include "RE/Starfield.h"

#include <array>
#include <cstdint>

namespace CFS::Engine
{
    // Exact 1.16.244 prologue shared by the guarded global-event
    // GetEventSource getters (TESLoadGameEvent, GravJumpEvent).
    inline constexpr std::array<std::uint8_t, 16> kGlobalEventGetEventSource116244Prologue{
        0x48, 0x83, 0xEC, 0x28, 0x65, 0x48, 0x8B, 0x04,
        0x25, 0x58, 0x00, 0x00, 0x00, 0xBA, 0xB8, 0x00,
    };

    // Shared mechanical legs of the global-event identity guard: exact
    // 1.16.244 getter prologue, then the live source's primary vtable.
    // Per-sink policy (any extra source-static leg, logging, registration)
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
    [[nodiscard]] GlobalEventSourceProof<Event> ProveGlobalEventSource(
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
}

# Bridge internals

`Bridge.cpp` includes these fragments inside `CFS::Bridge`'s anonymous namespace.
They are ordered sections of one translation unit, not standalone headers.

The include order is intentional and is a pure topological sort of the internal
call graph — no forward declarations remain (the former
`ReadNativeGalaxySelection` back-edge dissolved when raw GalaxyState memory
access moved to `src/Engine/GalaxyState.*`):

1. `State.inl` — all shared constants, types, and mutable state
2. `NavShared.inl` — the utility floor: destination predicates, live-engine
   lookups, settle gates, mutex-guarded accessors
3. `HudCruiseInput.inl` — synthetic HUD Cruise keypress latch producers (the
   consumer lives beside its only caller in `HudCruise.inl`; the timeout audit
   in `Lifecycle/FramePump.inl`)
4. `Destination.inl` — destination lifecycle: current-system resolution,
   native station assignment, arrival-audit arming, store/clear/fail-closed
   plumbing
5. `SafetyEvents.inl`
6. `Selection.inl`
7. `RemoteRoute/Inspection.inl` — read-only vanilla Starmap reads plus the
   Bridge-side thin guards over `Engine::GalaxyState`
8. `RemoteRoute/Course.inl` — course requests, continuation setup,
   `TryCommitRemoteMoonPhase`, stock AS3 dispatch
9. `RemoteRoute/MapAction.inl`
10. `RemoteRoute/Driver.inl` — the jump-route phase machine and
    `FailRemoteRoute`
11. `MapUi/Input.inl` — physical input hook, binding resolution, hold latch
12. `MapUi/ActionHint.inl` — the Starmap action button, its eligibility
    signature, and map-UI stale-state release
13. `MapUi/Providers.inl`
14. `HudCruise.inl` — cockpit feed processing, HUD reconciliation, and the
    HUD Cruise input consumer
15. `Lifecycle/Subscriptions.inl`
16. `Lifecycle/Continuation.inl`
17. `Lifecycle/FramePump.inl`

Keep a change in the narrowest owning fragment. Shared constants, snapshots, and
process state belong in `State.inl`; avoid declaring state in a behavior file.
Move independently testable/reusable infrastructure to a normal module under
`Engine`, `Input`, or `Scaleform` instead of adding another cross-fragment helper.

Provider callbacks may copy GFx values into plain C++ snapshots only. Engine and
Scaleform mutation remains owned by the verified main-thread/post-advance paths.
Changing fragment order, shared-state ownership, or callback threading requires
explicit runtime revalidation.

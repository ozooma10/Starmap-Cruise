# Bridge internals

`Bridge.cpp` includes these fragments inside `CFS::Bridge`'s anonymous namespace.
They are ordered sections of one translation unit, not standalone headers.

The include order is intentional:

1. `State.inl`
2. `Destination.inl`, `SafetyEvents.inl`, `Selection.inl`
3. `RemoteRoute/Inspection.inl`, `Course.inl`, `MapAction.inl`, `Driver.inl`
4. `MapUi/Input.inl`, `ActionHint.inl`, `Providers.inl`
5. `HudCruise.inl`
6. `Lifecycle/Subscriptions.inl`, `Continuation.inl`, `FramePump.inl`

Keep a change in the narrowest owning fragment. Shared constants, snapshots, and
process state belong in `State.inl`; avoid declaring state in a behavior file.
Move independently testable/reusable infrastructure to a normal module under
`Engine`, `Input`, or `Scaleform` instead of adding another cross-fragment helper.

Provider callbacks may copy GFx values into plain C++ snapshots only. Engine and
Scaleform mutation remains owned by the verified main-thread/post-advance paths.
Changing fragment order, shared-state ownership, or callback threading requires
explicit runtime revalidation.

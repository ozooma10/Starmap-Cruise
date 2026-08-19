# Changelog

## 1.0.0 - Unreleased

- Added guarded remote-system navigation for planets and moons using Starfield's stock route and jump flow.
- Added same-system station resolution and target assignment.
- Added tap-to-mark and hold-to-start-Cruise actions directly to the system map.
- Added exact course-lock correlation, map/HUD generation checks, operation IDs, and fail-closed timeouts.
- Added copied map, HUD, and travel observation inboxes for safe post-advance processing.
- Added keyboard, mouse, and gamepad Cruise binding support.
- Expanded the native automated release gate across domain invariants, selection, presentation, map state, navigation, remote routing, dispatch recovery, input, and concurrent observation queues.
- Added direct boundary coverage for timeout deadlines, invalid identities, stale callbacks, overflow recovery, and nonzero counter rollover.
- Fixed a UI-frame race where an unrelated HUD row overflow could cancel a remote route immediately before its commit callback.
- Fixed release-completed map holds being downgraded to taps before the post-advance action drain could claim their physical control.
- Added FTL compatibility for remote jumps whose scripted travel replaces the vanilla grav-jump completion event.
- Promoted the current implementation from `v2/` to the primary `src/` and `tests/` layout.
- Removed the unused legacy implementation and dead diagnostic APIs.
- Simplified the release payload to the plugin DLL, with symbols distributed separately.

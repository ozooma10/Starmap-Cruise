# Changelog

## 0.5.0 - Unreleased

- Added guarded remote-system navigation for planets and moons using Starfield's stock route and jump flow.
- Added same-system station resolution and target assignment.
- Added tap-to-mark and hold-to-start-Cruise actions directly to the system map.
- Added exact course-lock correlation, map/HUD generation checks, operation IDs, and fail-closed timeouts.
- Added copied map, HUD, and travel observation inboxes for safe post-advance processing.
- Added keyboard, mouse, and gamepad Cruise binding support.
- Added automated coverage for selection, presentation, map state, navigation, remote routing, input, and observation queues.
- Promoted the current implementation from `v2/` to the primary `src/` and `tests/` layout.
- Removed the unused legacy implementation and dead diagnostic APIs.
- Simplified the release payload to the plugin DLL, with symbols distributed separately.

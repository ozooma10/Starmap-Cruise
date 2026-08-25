# Changelog

## 1.2 - 2026-08-25

### Added

- Better Astrogate support
- Kept the Cruise-bound map action available after opening a selected planet or moon's planet view.

### Fixed

- Cancelled a pending post-jump Cruise before activation when remote travel enters ship combat.
- Fixed Astrogate handoff stalling until the starmap was reopened by accepting its correlated travel end after exact destination-system, settled-flight, and Cruise-readiness proof.
- Added an Astrogate-specific cleanup grace before Cruise activation so its final reload cannot race the Autopilot course request.

## 1.1 - 2026-08-20

### Added

- Added same-system Cruise targeting and activation from navigation consoles or the personal starmap while roaming aboard the player's ship.
- Added vanilla `JUMP THEN CRUISE` from navigation consoles and the personal shipboard starmap while roaming in space.

### Fixed

- Fixed post-jump Cruise sometimes leaving Autopilot off when Starfield retained the destination in its native course slot.
- Fixed starmap Cruise controls not responding when their bound input was disabled by the active map context.
- Skipped the brief Cruise activation after a grav jump places the ship directly at the selected planet or moon.

## 1.0 - 2026-08-19

- Initial Release

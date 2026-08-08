# Changelog

## 0.1.0 — Unreleased

### Added

- Separate Starmap action using the player's live stock Cruise binding and
  native glyph.
- Current-system planet, moon, and indexed-station marking, hold-to-Cruise, and
  active-Cruise retargeting.
- Cursor-independent vanilla `JUMP THEN CRUISE` routing for eligible remote
  planets, moons, and indexed stations.
- Exact parent-assisted remote-moon continuation and orbital-ancestry remote
  station continuation.
- Exact course-lock readback and independent close-distance arrival audit.
- Fail-closed runtime, identity, route, HUD-row, loading, flight-state, and
  lifecycle guards for Starfield `1.16.244.0`.

### Changed

- Cruise targeting leaves all route construction and execution to vanilla.
- Configuration now exposes only `bVerboseLog`.
- Reorganized bridge orchestration into focused route, map UI, lifecycle,
  destination, and safety boundaries; extracted runtime-memory, native-binding,
  ControlMap, and Scaleform helpers into normal modules without changing the
  navigation state machine.
- Deduplicated the fail-closed and phase-commit mechanisms behind shared
  helpers (`FailRemoteRoute`, `TryCommitRemoteMoonPhase`), merged the twin
  Starmap button builders and plugin-record walks, extracted raw
  StarMapMenu/GalaxyState memory access into `src/Engine/GalaxyState.*`, and
  split the shared utility floor into `NavShared.inl`/`HudCruiseInput.inl` so
  each fragment matches its charter; behavior unchanged.
- Removed the never-run `iGalaxyDiagnosticsMode` capture modes and the deep
  galaxy-focus diagnostics sweeps; failure paths keep the selection proof and
  root member list.

### Removed

- Mod-added cockpit target-status text.
- Custom positional HUD marker and destination label.
- Configurable interaction modes and the obsolete `sMode` compatibility path.
- Ship-POI and generic non-station actions; unsupported markers remain
  vanilla-owned.

### Validation

- Build, active MO2, and release-package artifacts are hash-verified.
- A restarted no-mouse remote-moon and arrival-clear smoke remains required on
  the final release artifact.

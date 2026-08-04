# Validation matrix

Build status is recorded separately from runtime proof. A compiled path is not
treated as a proven gameplay behavior.

## Automated/build checks

- [x] Clean releasedbg configure and build
- [x] Exact runtime gate for Starfield 1.16.244.0
- [x] DLL, INI, example override, and PDB deploy layout
- [x] No serialization registration
- [x] No runtime output/cache file path
- [x] No ESP, Papyrus, SWF, or public plugin API
- [x] Destination contains no retained raw form pointer
- [x] Invalid selection paths do not splice/disable input

## OSF RE proof capture

- [x] Planet: system-view dossier identifies a live PNDT and GNAM tuple;
  tree/marker equality premise is disproven
- [x] Moon: system-view dossier identifies Kurtz PNDT `0005E312`, type 3,
  GNAM `{system=00011720,parent=3,planet=8}`; tree/marker equality premise
  is disproven
- [x] Landable body: Gagarin PNDT `0005E311` is distinguished from the
  system/star tree id and current cockpit body field
- [x] Keyboard hold preserves physical device/id but does **not** generate a
  fresh Cruise down-edge after the map closes
- [ ] Controller hold naturally changes from `SetRouteDestination` to Cruise input
- [x] Rebound Cruise control preserves physical device/id, but the cockpit
  event is a continued hold (`first=false`) and does not activate Cruise
- [x] Direct HUD course event locks, clears, and retargets planets and a moon,
  with low-feed/readback confirmation
- [x] Current-system PNDT/GNAM vote stayed unanimous across the tested Alpha
  Centauri -> Sol transition; system zero is valid
- [x] Native `SpaceCruiseArrival` evaluated: it also fires on deliberate Cruise
  exit, so the independent arrival fallback must remain

The 2026-08-04 OSF RE run disproved the production selection provider join:
in system view the tree feed describes the system/star, the focused marker can
be zero, and the dossier describes the selected body. The dossier also emits
multiple bodies while navigating, so a stable focus discriminator is still
required before it can be accepted. The exact evidence and implementation
handoff are in OSF RE
`Investigations/Responses/2026-08-04-cruise-from-starmap-bridge.md`.

Controller testing was blocked because no controller was available. Safe
synthetic replay is also incomplete: the engine queue layout was recovered,
but no public ButtonEvent enqueue wrapper or proven queue/pool/thread ownership
route was found. Injection remains prohibited.

## Gameplay acceptance

- [ ] Flight tap marks without Cruise
- [ ] Flight hold enters Cruise and locks the correct body
- [ ] Already-cruising tap changes course immediately in `HoldToCruise`
- [ ] Off-screen/behind body receives the correct marker and course
- [ ] Same body clears; another body replaces
- [ ] Manual Cruise exit preserves the mark
- [ ] Encounter interruption preserves the mark
- [ ] Confirmed arrival clears the mark
- [ ] Landing/docking/system change clears the mark
- [ ] Canceling the map changes nothing
- [ ] Galaxy/surface/inspect, another system, POI/station, and on-foot maps stay vanilla
- [ ] HUD movie rebuild, focus loss, load, and repeated map cycles are safe
- [ ] `MarkOnly` suppresses the carried key until release
- [ ] Cruise Navigation Panel coexists; both input hooks chain and markers remain independent
- [ ] Save made during use loads after uninstall

Remaining runtime work for this matrix is Lane A because a person must focus
specific Starmap bodies and exercise controller bindings and interruption
cases. Use the matching OSF RE log and `CruiseFromStarmap.log` as evidence; do
not mark a row from visual impression alone when a feed readback exists.

## 2026-08-04 build record

- `xmake f -m releasedbg -y` and `xmake -y`: passed; the only diagnostics are
  inherited CommonLibSF C++23 deprecation/conversion warnings.
- `xmake install -o release/Data -y`: passed. The package contains the DLL,
  PDB, default INI, and custom-INI example under `Data/SFSE/Plugins`.
- The companion OSF RE probe also builds and deploys successfully.
- A later OSF RE-only runtime session on Starfield 1.16.244 captured the proof
  above. CruiseFromStarmap was disabled during that final session, so no
  gameplay-acceptance row is claimed from it. The profile was not changed at
  handoff.

## 2026-08-04 production remediation

These are source/build results, not gameplay acceptance:

- [x] Removed every production call to the ID-zero `IsSpaceshipLanded()` and
  `IsSpaceshipDocked()` helpers. Active-flight and destination maintenance now
  use Address Library ID 63482 with `IsInSpace(false)` only after the exact
  1.16.244 runtime gate, Starfield image-range check, and a 16-byte prologue
  fingerprint at RVA `0xB58D50` pass. Initialization aborts before hooks if the
  binding fails.
- [x] Removed tree/marker/dossier equality from `ResolveMapSelection()`. The
  function preserves generation, system-view, live-PNDT, parsed-GNAM, and
  captured-current-system checks against the dossier candidate, then rejects it
  with an explicit stable-focus-discriminator blocker. It never consumes a
  selection using last-dossier-wins.
- [x] Changed the safe default to `MarkOnly`. Any accepted map-key hold is
  suppressed through physical release in both modes. Non-cruising
  `HoldToCruise` no longer treats the disproven continued keyboard hold as a
  fresh Cruise press, and no synthetic event path was added.
- [x] Retained the proven `Reticle_OnCruiseLockCourse` CustomEvent and HUD-low
  confirmation without modification.
- [x] No `SpaceCruiseArrival` hook or clearing path exists in production. The
  existing arrival audit still requires a prior confirmed course-lock
  transition plus close distance; failed evidence preserves the mark.

Production runtime acceptance remains blocked by the stable system-view focus
discriminator. Controller behavior, encounter interruption, manual-exit mark
preservation, and the fail-closed startup guard also require production-enabled
runtime captures before their gameplay rows can be marked.

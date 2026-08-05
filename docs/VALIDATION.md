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
- [x] Selection requires exactly one highlight-radius marker and exact dossier id/type agreement

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
- [x] System-view focus discriminator: exactly one
  `StarMapMenuMarkersData.aMarkersData[]` row with
  `bIsInHighlightRadius=true`, joined by id/type to the dossier PNDT. Proven for
  Gagarin, Kurtz, Jemison, revisit, rapid switch, invalid views, station,
  empty space, cancel, and movie reopen.

The first 2026-08-04 OSF RE run disproved the original selection provider join:
in system view the tree feed describes the system/star, the focused marker can
be zero, and the dossier describes the selected body. The dossier also emits
multiple bodies while navigating, so tree equality, `bIsFocused`, and
last-dossier-wins remain invalid. The follow-up run proved
`bIsInHighlightRadius` as the exact discriminator. The evidence and production
handoff are in OSF RE
`Investigations/Responses/2026-08-04-cruise-from-starmap-focus-discriminator.md`.

Controller testing was blocked because no controller was available. Safe
synthetic replay is also incomplete: the engine queue layout was recovered,
but no public ButtonEvent enqueue wrapper or proven queue/pool/thread ownership
route was found. Injection remains prohibited.

## Gameplay acceptance

- [x] Flight tap marks without Cruise
- [x] `SelectThenCruise`: release after selection, press Cruise normally, and
  lock the correct selected body
- [ ] Flight hold enters Cruise and locks the correct body
- [ ] Already-cruising tap changes course immediately in `HoldToCruise`
- [ ] Off-screen/behind body receives the correct marker and course
- [x] Same body clears; another body replaces
- [ ] Manual Cruise exit preserves the mark
- [ ] Encounter interruption preserves the mark
- [ ] Confirmed arrival clears the mark
- [ ] Landing/docking/system change clears the mark
- [x] Canceling the map changes nothing
- [ ] Galaxy/surface/inspect, another system, POI/station, and on-foot maps stay vanilla
- [ ] HUD movie rebuild, focus loss, load, and repeated map cycles are safe
- [x] `MarkOnly` suppresses the carried key until release
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

- [x] Removed every production call to the `IsSpaceshipLanded()` and
  `IsSpaceshipDocked()` helpers, whose bindings were ID zero in the dependency
  snapshot used for the crash capture. Active-flight and destination
  maintenance use Address Library ID 63482 with `IsInSpace(false)` only after
  the exact 1.16.244 runtime gate, Starfield image-range check, and a 16-byte
  prologue fingerprint at RVA `0xB58D50` pass. Initialization aborts before
  hooks if the binding fails.
- [x] Replaced tree/marker/dossier equality in `ResolveMapSelection()` with the
  proven exact-one `bIsInHighlightRadius` marker plus dossier id/type join. The
  function preserves session, generation, system-view, live-PNDT, parsed-GNAM,
  and captured-current-system checks. It never consumes a selection using
  last-dossier-wins.
- [x] Changed the safe default to `MarkOnly`. Any accepted map-key hold is
  suppressed through physical release in both modes. Non-cruising
  `HoldToCruise` no longer treats the disproven continued keyboard hold as a
  fresh Cruise press, and no synthetic event path was added.
- [x] Retained the proven `Reticle_OnCruiseLockCourse` CustomEvent and HUD-low
  confirmation without modification.
- [x] No `SpaceCruiseArrival` hook or clearing path exists in production. The
  existing arrival audit still requires a prior confirmed course-lock
  transition plus close distance; failed evidence preserves the mark.

The focus discriminator no longer blocks production. Controller behavior,
encounter interruption, manual-exit mark preservation, the fail-closed startup
guard, and all end-to-end marker/course behavior still require
production-enabled runtime captures before their gameplay rows can be marked.

## 2026-08-04 focus discriminator implementation

These are source/build results, not production gameplay acceptance:

- [x] `ResolveMapSelection()` accepts only one highlight-radius planet/moon
  marker whose id/type exactly matches the dossier PNDT, then applies the
  existing session, generation, active-flight, live-PNDT, GNAM, and captured
  current-system gates.
- [x] `xmake f -m releasedbg -y` followed by `xmake -y` passed against the
  current shared CommonLibSF mirror. Diagnostics are inherited C++23
  deprecation/conversion warnings.
- [x] `xmake install -o release/Data -y` produced only the DLL, PDB, default INI,
  and custom-INI example under `Data/SFSE/Plugins`.
- [x] Build, release package, and MO2 deployment DLL SHA-256 hashes match.
- [x] Added the OSF RE-proven visible-map-open gate before entering the Starmap
  AS3 data manager. The first production smoke attempt crashed in
  `TrySubscribe()`/`manager.Invoke("Subscribe", ...)` because production relied
  on `UI::IsMenuOpen` while the background movie was still incomplete; no
  selection had been accepted.
- [x] Production-enabled restart and initial MarkOnly gameplay capture.

## 2026-08-04 production MarkOnly smoke

- [x] Guarded restart passed the runtime/prologue gate, indexed 1,783 PNDT
  records, resolved Alpha Centauri, subscribed both HUD feeds, and opened ten
  fresh Starmap generations without repeating the AS3 subscription crash.
- [x] Gagarin marked and displayed the blue runtime marker; Kurtz and Jemison
  replaced the destination; Jemison displayed the marker; selecting Jemison
  again cleared it visibly and returned navigation state to idle.
- [x] The Eye was rejected as marker `0001285A`, type 4, and remained
  vanilla-owned. Inspect view 2 and galaxy view 0 were also logged as
  vanilla-owned; neither was consumed or closed by the plugin.
- [x] Several normal Starmap closes with `accepted=false` preserved the current
  mark, proving cancel/no-selection behavior in the exercised session.
- [ ] Kurtz selection resolved and stored PNDT `0005E312` with the correct moon
  GNAM, but no blue marker was visible. A subsequent Jemison control rendered
  correctly, isolating the remaining issue to moon availability/bearing in the
  HUD target feeds or the production row join. Do not claim the off-screen/moon
  marker row until that path is instrumented and proven.

## 2026-08-04 SelectThenCruise implementation

These include source/build evidence and the first production gameplay capture:

- [x] Added `SelectThenCruise` without changing the existing `MarkOnly` or
  `HoldToCruise` names.
- [x] The mode queues the selected PNDT only on a vanilla Cruise
  inactive-to-active HUD transition. When the map was opened during active
  Cruise, it uses the existing immediate retarget path.
- [x] The carried map control remains suppressed until release; no synthetic
  input or engine Cruise-start call was added.
- [x] Changed the shipped default to `SelectThenCruise` and disabled the custom
  blue HUD marker by default. The marker remains opt-in through the custom INI.
- [x] Fresh build/package/deployment DLL hashes match:
  `69839AE1E644B261C019B739E8AC6908F3AB8527B2E3493A8579CA3255107642`.
- [x] Live Gagarin selection stored PNDT `0005E311` and closed the map without
  dispatching early. A later normal cockpit Cruise activation queued and
  dispatched `Reticle_OnCruiseLockCourse` for `0005E311`; the engine low feed
  confirmed the Gagarin course lock 11 ms later. Cruise visibly started toward
  Gagarin, and the default configuration logged `marker=false`.

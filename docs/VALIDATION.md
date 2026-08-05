# Validation matrix

Build status is recorded separately from runtime proof. A compiled path is not
treated as a proven gameplay behavior.

## Automated/build checks

- [x] Clean releasedbg configure and build
- [x] Exact runtime gate for Starfield 1.16.244.0
- [x] DLL, INI, example override, and PDB deploy layout
- [x] No serialization registration
- [x] No runtime output/cache file path
- [x] No ESP, Papyrus, SWF replacement, or public plugin API
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
- [ ] Controller binding and glyph for the separate Cruise action (current live
  ControlMap projection is keyboard-only)
- [x] Rebound Cruise control preserves physical device/id, but the cockpit
  event is a continued hold (`first=false`) and does not activate Cruise
- [x] Direct `Reticle_OnCruiseActivate` dispatch returns success but does not
  activate Cruise; the full HUD `ProcessUserEvent("Cruise", down/hold/up)` path
  activates after the stock hold threshold and was visually confirmed in-game
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

Controller testing was blocked because no controller was available. Engine
input injection remains prohibited; the production path uses the stock HUD
movie's public user-event method from the post-advance Scaleform pump instead.

## Gameplay acceptance

- [x] Flight tap marks without Cruise
- [ ] Current reliability build: release after selection, press Cruise normally,
  and lock the correct selected body
- [ ] Flight hold enters Cruise and locks the correct body
- [ ] Already-cruising tap changes course immediately in `TapHoldCruise`
- [ ] Off-screen/behind body receives the correct marker and course
- [x] Same body clears; another body replaces
- [ ] Manual Cruise exit preserves the mark
- [ ] Encounter interruption preserves the mark
- [ ] Confirmed arrival clears the mark
- [ ] Landing/docking/system change clears the mark
- [x] Canceling the map changes nothing
- [ ] Galaxy/surface/inspect, another system, and on-foot maps stay vanilla
- [ ] The Eye marker CELL `0001285A` resolves to station reference `00012894`,
  receives native ship-target assignment, and reports `bIsCruiseTargetLock` on
  that reference
- [ ] A generic current-system non-planet marker with exactly one same-ID cockpit
  target row dispatches and locks; an absent or ambiguous marker remains disabled
  without consuming input or reporting a false lock
- [ ] HUD movie rebuild, focus loss, load, and repeated map cycles are safe
- [ ] Disabled eligibility reasons render and never consume input
- [ ] Fixed cockpit target status renders independently of positional bearing
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

## 2026-08-04 TapHoldCruise and SWF rollback

- [x] OSF RE proved that direct `Reticle_OnCruiseActivate` dispatch does not
  start Cruise, while the stock HUD `ProcessUserEvent` press/hold/release path
  does. The operator visually confirmed Cruise after the HUD feed became active.
- [x] Added `TapHoldCruise` as the default and retained `SelectThenCruise` and
  `MarkOnly`; the old `HoldToCruise` value is accepted as an alias.
- [x] Native forwarding uses `Cruise` for keyboard/mouse and the stock
  `SHMonocle` combo for controller. Controller behavior remains static evidence.
- [x] Rejected and removed the FFDec `StarMapButtonHintBar.PopulateButtons`
  replacement after its round trip dropped `NEED_ACTIVATION` plus fourteen
  activation slots used by inline callbacks. The deployed movie crashed during
  AS3 initialization at `Starfield.exe+331B11E`; the native plugin had completed
  initialization and PNDT indexing but no menu callback had run.
- [x] Build, release, and MO2 contain no SWF after rollback. The action label is
  now changed through the stock public `ButtonBaseData.sButtonText` property only
  from a visible Starmap data callback, and only while the exact selection gate
  passes.
- [x] Replacement build/package/MO2 DLL hashes match:
  `364E92B41ACDFB95F5617D6F0850EF3E8CE852242EB457FD254DDDFA77519444`.
- [ ] Clean startup and runtime action-label/tap/hold behavior after rollback.

## 2026-08-04 stacked cruise action hint

These are source/build results, not live visual proof:

- [x] Reused the stock `ReleaseHoldComboButton` and
  `ReleaseHoldComboButtonData` classes from the loaded Starmap movie to render
  `SET CRUISE TARGET` above `HOLD TO CRUISE` without shipping or patching a SWF.
- [x] The native callback leaves the original `SetRouteDestinationButton` and
  its binding unchanged. It adds one separate control per Starmap movie, follows
  the primary live `Cruise` keyboard binding only inside the exact gate, and
  disables/hides that control outside the gate.
- [x] Releasedbg configure/build and the non-deploying install layout pass. The
  only compiler diagnostics are inherited CommonLibSF warnings. The current
  separate-binding build and release-package DLL SHA-256 hashes match:
  `FE5EE2C2CC5D7ECFF2532BCCA3E14CFBD5C4E2B8FB7705504F37C4DD1C544C7B`.
- [x] Build, release package, and MO2 deployment hashes match. The live-Cruise-
  binding build has not yet been claimed as live UI/gameplay validation.
- [x] The first runtime attempt loaded the intended DLL and reached the exact
  Gagarin selection gate, but left `SET COURSE` visible because direct AS3 class
  construction did not attach the imported display symbol. The replacement now
  uses the same stock `ButtonFactory.AddToButtonBar` path as
  `StarMapButtonHintBar.PopulateButtons`, with explicit per-step failure logs.
- [x] The second runtime attempt proved that the factory renders the intended
  stacked control, but native `removeChild` failed after creation. Repeated data
  callbacks therefore leaked multiple controls. The replacement now creates at
  most one control per movie and leaves it Starmap-owned.
- [x] The third revision separates the actions: vanilla `SET COURSE` keeps its
  `SetRouteDestination` binding, while the stacked control follows the live
  `Cruise` keyboard binding (`T` by default) and is only routed/enabled/visible
  inside the exact gate. This is source/build evidence until the revised DLL is
  exercised in-game.
- [ ] Confirm the stacked prompt, tap, hold, controller glyph, large-text mode,
  invalid-selection restoration, and repeated map/movie cycles in Starfield.

## 2026-08-05 reliability and feedback pass

These are source/build results; the current DLL still needs a production-enabled
Starfield pass before gameplay rows can be checked:

- [x] Removed the system/star tree callback's marker/dossier invalidation. The
  proven body join now changes only from marker/dossier data or real
  session/view/movie lifetime boundaries.
- [x] Centralized selection gating in one eligibility result used by both the
  visible action and acceptance callback.
- [x] In active-flight system view, the separate action remains present while
  ineligible and receives a disabled reason label. Input routing remains off
  unless the exact planet/moon gate passes.
- [x] Added a fixed cockpit `CRUISE TARGET` status independent of low/high
  positional-bearing availability. The optional diamond marker remains separate.
- [x] Reduced the exposed interaction to `TapHoldCruise`. Legacy custom mode
  strings warn and use the supported behavior instead of entering unreachable
  selection paths.
- [x] Production PNDT/GNAM indexing now covers full, medium, and light plugin
  tiers using the same runtime FormID encoding as the companion OSF RE probe.
  Compile-time checks cover one ID from each tier.
- [x] `xmake f -m releasedbg -y` and the non-deploying `xmake -y` passed. Build
  and `build/deploy/Data` DLL SHA-256 hashes match:
  `7AA345F1B0887DA2F0FC5589A743ED930F4D438BA9C0352CDE451230DBAC3BC0`.
- [x] Explicit xmake install deployed the same DLL to MO2; build, package,
  release, and MO2 SHA-256 hashes match. No live gameplay claim is made from
  this build yet.

## 2026-08-05 Scaleform thread-affinity crash fix

These checks establish the corrected static boundary. A restarted Starfield
session is still required to prove the runtime result:

- [x] The crash stack faults inside the AS3 VM with an SFSE task-pump frame.
  The final plugin log records low/high HUD subscriptions on different worker
  thread ids (`41244`, then `24496`) immediately before the crash.
- [x] SFSE permanent-task work is now a coalesced producer only. It refuses the
  task queue's inline fallback and sends engine work through
  `RE::BSService::TaskQueue` for main-thread execution.
- [x] `UI_AdvanceActiveMenus` Address Library ID 130455 was byte-checked against
  the installed 1.16.244 executable at RVA `0x2542320`; the five-byte hook
  boundary is `4C 89 44 24 18`. The executable has exactly two direct callers,
  RVAs `0x1890E88` and `0x1890F01`, matching the OSF UI proof.
- [x] CruiseFromStarmap hooks the function entry and runs the original first.
  This composes with OSF UI's two caller hooks regardless of their later
  installation: OSF UI still sees both unmodified calls to the same entry.
- [x] Feed/action callbacks retain no passed GFx values and perform no movie
  lookup, object construction, or arbitrary AS3 invocation. They copy plain C++
  snapshots/actions; the post-advance pump performs subscriptions and mutations
  after the VM unwinds. Stale retained values are also released there.
- [x] `xmake f -m releasedbg -y` followed by `xmake -y` passed with inherited
  CommonLibSF warnings only. Build and deployed MO2 DLL SHA-256 match:
  `72C978600FD269C6885D58F83524D44BA415EEF1594FA6BB6248071D94665E96`.
- [x] Relaunched-game crash regression smoke: repeated/spammed use did not
  reproduce the Scaleform access violation.
- [ ] Confirm both HUD subscriptions log on one owning thread, then complete the
  interaction matrix: cockpit idle, stacked prompt, tap, completed hold, Cruise
  activation, course lock, and HUD/movie rebuild.

## 2026-08-05 active-Cruise tap-only action

These are source/build/deployment results; the new prompt variant still needs a
relaunched-game visual and input check:

- [x] A map session captured while Cruise is active now creates the stock
  `BasicButton`/`ButtonBaseData` variant with only `SET CRUISE TARGET` and one tap
  callback. It does not display or register the combo button's hold action.
- [x] Both variants use the engine-owned `Cruise` user-event name so the stock
  keybox can resolve the player's live binding. Only the current variant is
  enabled and visible, while each is created at most once per Starmap movie; a
  stale hold callback is normalized to tap while the tap-only variant owns the
  session.
- [x] If the tap-only button closes the map on its down edge, the physical Cruise
  key is suppressed until release so the carried cockpit event cannot disturb
  the active Cruise state.
- [x] Shipped `ShipReticle` source sets `_CanActivateCruiseMode` from HUD payload
  `bShowCruiseButton`; its public getter also rejects playback/dialogue, and
  `UpdateCruiseButton` enables the hold event only when that getter is true and
  Cruise is inactive. The map now mirrors those public getters fail-closed.
- [x] When stock Cruise is inactive but unavailable, the map uses the tap-only
  variant. Availability is polled while the map is open so the stacked variant
  can return when a short cooldown expires.
- [x] `xmake f -m releasedbg -y` followed by `xmake -j1 -y` passed with inherited
  CommonLibSF warnings only. Built and deployed MO2 DLL SHA-256 match:
  `3866CB20CDFBBFBDCA553858E85233A990C5E3965F3057003B9DA9ACC041CF21`.
- [ ] While already cruising, open the system map and confirm only the single-line
  `SET CRUISE TARGET` prompt appears; tap another body and confirm course lock
  changes without stopping or restarting Cruise.
- [ ] Exit Cruise, immediately open the system map during its cooldown, and
  confirm only the tap action appears. Wait for availability to return and
  confirm the stacked `HOLD TO CRUISE` action appears and can engage normally.

## 2026-08-05 station and non-planet destination support

The first runtime attempt disproved the assumption that a station's Starmap ID
is already present in the cockpit target feed. The Eye is exposed by the map as
CELL `0001285A`, while its persistent ship reference is `00012894`:

- [x] Preserved the exact-one `bIsInHighlightRadius` discriminator and nonzero-ID
  requirement for every system-view destination.
- [x] Left the planet/moon path unchanged: exact dossier id/type agreement, live
  PNDT, parsed GNAM, and captured-current-system equality remain required.
- [x] Static 1.16.244 plugin inspection established these map-cell to persistent
  reference mappings: The Eye `0001285A -> 00012894`, Nova Galactic Staryard
  `00219520 -> 00216F51`, and Deimos Staryard `00219DFF -> 003120D6`.
- [x] Station ship bases are discovered across the active full/medium/light load
  order by the vanilla `IsStarstation` keyword (`003402A3`). Only persistent CELL
  children are indexed; deleted overrides and ambiguous results fail closed.
- [x] A direct dynamic marker is accepted only when it is a live reference whose
  live base is in that station-base set.
- [x] A non-station marker remains eligible when its map ID matches exactly one
  current cockpit target-feed row; that row supplies the course-addressable ID
  without native station-target reassignment.
- [x] Destination values keep the map ID/type separate from the resolved target
  ID used by the HUD and course-lock readback.
- [x] On map close, station selections call the byte-verified 1.16.244 native ship
  target setter (Address Library ID `97892`) and require readback through the
  current-target global (ID `883585`) before Cruise input or course dispatch.
- [x] `Reticle_OnCruiseLockCourse` still dispatches only the resolved target ID;
  `AutopilotLocked` still requires the low feed to report
  `bIsCruiseTargetLock` on that same target ID.
- [x] `xmake -j1 -y` passed with inherited CommonLibSF warnings only. Built and
  deployed MO2 DLL SHA-256 match:
  `CFAB64184FEDBBC1FFF4E75B1E0541F26FF9CC1E912AC27011FD71B4252CF9E5`.
- [ ] Relaunch Starfield, select The Eye, and verify an enabled action plus log
  evidence for map `0001285A`, target `00012894`, native assignment, dispatch,
  and matching `bIsCruiseTargetLock` readback.
- [ ] Repeat with Nova Galactic and Deimos Staryards.
- [ ] Verify a generic non-planet marker with one exact current HUD row dispatches
  and receives matching lock readback.
- [ ] Confirm missing and ambiguous non-station markers remain disabled as
  `TARGET IS NOT AVAILABLE TO CRUISE` or `TARGET IS AMBIGUOUS`.

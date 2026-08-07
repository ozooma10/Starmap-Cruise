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
- [x] Controller binding, native glyph, tap callback, and completed hold callback
  for the separate Cruise action were confirmed in game on 2026-08-06
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
CELL `0001285A`, while its placed ship reference is `00012894`:

- [x] Preserved the exact-one `bIsInHighlightRadius` discriminator and nonzero-ID
  requirement for every system-view destination.
- [x] Left the planet/moon path unchanged: exact dossier id/type agreement, live
  PNDT, parsed GNAM, and captured-current-system equality remain required.
- [x] Static 1.16.244 plugin inspection established these map-cell to placed
  reference mappings: The Eye `0001285A -> 00012894`, Nova Galactic Staryard
  `00219520 -> 00216F51`, and Deimos Staryard `00219DFF -> 003120D6`.
- [x] Station ship bases are discovered across the active full/medium/light load
  order by the vanilla `IsStarstation` keyword (`003402A3`). Placed references in both persistent and temporary CELL
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
- [ ] Confirm Ship POIs and generic non-station markers expose no plugin action.

## 2026-08-05 HUD getter crash hardening

The Trainwreck 1.4.0 report for Starfield 1.16.244 faults in
`ReconcileHudUi` at the first dotted
`root1.Menu_mc.Reticle_mc.CruiseModeHUDActive` lookup. The engine is resolving
that path with a null internal object; OSF UI is only the preceding hook frame.

- [x] HUD reconciliation waits 1.5 seconds after the current HUD
  movie-created timestamp and confirms that generation before entering
  Scaleform.
- [x] Dirty HUD work is preserved while the movie settles. Generation and the
  menu's root are rechecked immediately after resolving the reticle.
- [x] `Reticle_mc` is resolved and type-checked once for the three Cruise
  getters, which use `GetMember` on that validated object instead of separate
  dotted `GetVariable` traversals.
- [x] An unreadable `CruiseModeHUDActive` preserves the last active state rather
  than manufacturing an exit; hold availability still fails closed.
- [x] Map-open fallback polling reaches Scaleform only after the HUD movie
  guard passes.
- [x] `xmake f -m releasedbg -y` followed by `xmake -j1 -y` passed with
  inherited CommonLibSF warnings only. The releasedbg and local deploy-package
  DLL SHA-256 hashes match:
  `03492C0A9AEB9634B95B2054EA291EDE74B311B776E43B28809449E9A71DBF3D`.
  External MO2 deployment was deliberately disabled for this build.
- [ ] Relaunch Starfield and verify startup, landed-to-takeoff HUD replacement,
  docking/undocking, loading, and repeated Starmap opens do not enter HUD
  Scaleform until 1.5 seconds after the matching movie-created log.
- [ ] Exit Cruise, open the Starmap during cooldown, and verify tap-only changes
  to stacked hold after cooldown without a crash or close/reopen.
- [ ] With OSF UI enabled, repeat active-Cruise targeting, completed hold,
  course-lock readback, and rapid map open/close cycles.

## 2026-08-06 remote pending-jump campaign and implementation

- [x] Six remote planet/moon selections retained exact-one highlighted-marker
  joins to nonzero dossier PNDT id/type. Parsed GNAM identified Alpha Centauri,
  Sol, and Narion while `uSystemLocationID` continued to identify the player's
  current system rather than the browsed system.
- [x] With the seamless-jump mod disabled, a vanilla single hop and both legs of
  a vanilla two-hop route produced player-ship GravJump states `0`, `1`, and `2`.
  Resolver convergence followed `LoadingMenu` close by about 4.1-4.5 ms; the
  stock Cruise action became available about 0.71-0.88 seconds later. The
  intermediate system remained stable for about 44.3 seconds before the second
  leg, proving that arrival cannot mean the first system change.
- [x] Quickload, pause-menu load, and main-menu load each produced exactly one
  `TESLoadGameEvent`. Neither vanilla grav-jump leg produced one. The guarded
  binding is Address Library ID `64149`, source static `838425`, source vtable
  `413741`, with verified 1.16.244 prologue
  `48 83 EC 28 65 48 8B 04 25 58 00 00 00 BA B8 00`.
- [x] Production adds a planet/moon-only `PendingJump` state. It preserves a
  remote mark across intermediate systems and `LoadingMenu`, never changes the
  vanilla route, and waits for the target system, a settled world, and exactly
  one matching cockpit row before using the existing stock HUD Cruise/course
  path. Remote stations and non-planets remain unavailable.
- [x] The load callback publishes only an atomic clear request; the verified
  main-thread frame owns destination and input mutation. If any load-event
  fingerprint/source identity check fails, remote selection fails closed while
  current-system targeting stays enabled.
- [x] Local releasedbg `xmake -r` completed with inherited CommonLibSF warnings
  only. The built and local deploy-package DLL SHA-256 hashes match:
  `9E51DB5B2FCA71E03A74B555EB49D98E82960ED635D41A82706F825C44B1BF72`.
- [x] After Starfield exited, that exact package was copied to the active MO2
  mod and the deployed DLL hash matched. The `Default` profile enables
  CruiseFromStarmap and disables both OSF RE and True Seamless Grav Jumps for
  the production smoke test. Deployment is not in-game proof.
- [x] With the production plugin enabled and the OSF RE sandbox probe disabled,
  a remote tap armed Bolivar I `0005E547` from Volii. The pending mark survived
  the vanilla map reopen/close and `LoadingMenu`; after resolver arrival in
  system `0001D022` and the 2.5-second settle gate, the plugin forwarded the
  stock HUD Cruise press, dispatched the course, and received matching engine
  lock readback. The operator visibly confirmed automatic Cruise activation and
  targeting on 2026-08-06.
- [x] A manually executed three-leg vanilla route from Bolivar to Gagarin kept
  the original pending target through Volii `0000FCD0` and Olympus `00011CFD`
  without engaging Cruise. At final Alpha Centauri `00011720` arrival, the same
  target passed the settle/unique-row gates, activated stock Cruise, dispatched
  `0005E311`, and received exact Gagarin lock readback.
- [x] A production quickload with remote Jupiter `0005DEBA` pending emitted the
  guarded `TESLoadGameEvent`; its callback queued only the atomic signal, and
  the next main-thread frame cleared Jupiter before the replacement HUD movie
  subscribed or the current-system resolver recovered. No Cruise engagement or
  course dispatch occurred after load. Pause-menu and main-menu load paths share
  this same event and were separately proven by the OSF RE campaign.
- [x] Current-system planetary regression: a completed Jemison hold latched the
  stock HUD press, activated Cruise, dispatched `0003F5A1`, and received exact
  engine lock readback after the remote-pending changes.
- [ ] Recheck the current-system station flow; controller binding/glyph
  validation remains pending. Generic non-station targets are now hidden.

## 2026-08-06 remote mark plus vanilla ExecuteRoute handoff

- [x] Decompilation of the shipped 1.16.244 `galaxystarmapmenu.swf` identifies
  the vanilla-owned seam. `JumpDataPanel.SetPlotPointData()` copies
  `bCanExecuteRoute` into the public Execute hint's `Visible` property, and
  public `SendExecuteEvent()` checks that property again before dispatching
  `StarMapMenu_ExecuteRoute` through `BSUIDataManager`.
- [x] The first `BCB4631...B715` implementation required an already executable
  route while the remote body was highlighted. Live vanilla testing disproved
  that UX: **Set Course** immediately returns to galaxy view, and the destination
  system cannot be re-entered until the route is cleared. The implementation was
  therefore unreachable despite loading and passing every startup guard in PID
  `36092`; it is rejected rather than counted as gameplay proof.
- [x] Shipped `StarMapButtonHintBar.as` provides the missing vanilla-owned first
  stage. Its **Set Course** callback dispatches
  `StarMapMenu_OnHintButtonClicked` as a `CustomEvent` with
  `{buttonAction:"SetRouteDestination"}`. The revised remote tap dispatches that
  exact event only while the stock button data is enabled and visible.
- [x] The corrected implementation captures the browsed system name and marked
  PNDT, lets vanilla build the route and change to galaxy view, then waits up to
  five seconds in the verified post-advance window. It requires route-end text
  to match the captured system and `bCanExecuteRoute` to expose Execute before
  invoking `JumpDataPanel.SendExecuteEvent()`. Session/movie changes, early map
  close, mismatch, timeout, or invocation failure clear only the Cruise mark;
  vanilla route/warning state is preserved.
- [x] Releasedbg `0A1083AE...97A9` was deployed and loaded in PID `19716`.
  At `09:13:46`, one Jupiter tap dispatched stock Set Course and Execute Route;
  arrival resolved Sol and automatic Cruise locked Jupiter `0005DEBA`. The user
  confirmed vanilla had already delivered the ship directly to Jupiter, so the
  Cruise leg was redundant. A second remote Chawla run showed the same direct-body
  boundary. This proves the one-tap route handoff but rejects body-scoped Set
  Course as the product behavior.
- [x] Further shipped-SWF inspection identifies the stock system-to-galaxy seam:
  `GalaxyStarMapMenu.OnCancelEvent()` emits `StarMapMenu_OnCancel`. Back from
  system view leaves the same system focused in galaxy view, where the unchanged
  Set Course callback can create a system-scoped route.
- [x] The replacement implementation keeps the highlighted PNDT only as the
  Cruise target. It requires the system/star tree form to be a live STDT whose
  parsed DNAM system ID matches the target PNDT's parsed GNAM system ID, emits
  stock Back, verifies galaxy view republishes the exact STDT/DNAM root, and
  only then emits stock Set Course. Execute requires the displayed route system
  to match; vanilla may select a body within that system as the jump entry point.
- [x] With live MO2 deployment disabled, clean releasedbg `xmake -r -y` compiled,
  linked, and installed with inherited CommonLibSF warnings only. Build and local
  deploy-package DLL hashes match:
  `0B78083F829B724BB9564B9BF92DE235FC044467877334BEF404132B78DB8BD2`.
- [ ] After Starfield exits, deploy exact `0B78083F...B8BD2`, restart, and confirm
  the Back -> system Set Course -> Execute chain loads.
- [ ] From a remote system-view body, confirm one `JUMP THEN CRUISE` tap returns
  to the matching galaxy-system node, accepts vanilla's in-system entry body, and
  visibly begins the Grav Jump. At settled system arrival, require a meaningful
  automatic Cruise leg plus exact marked-body course-lock readback.
- [ ] Confirm an unexplored/out-of-range route remains on vanilla's galaxy-view
  warning after five seconds and does not retain a Cruise mark or begin travel.
- [ ] Re-run one current-system completed-hold regression to prove the local
  map-close/HUD-Cruise path remains unchanged.

### 2026-08-06 STDT system-root corrections

- [x] Live production log first captured remote Sol selection failing closed
  with tree root `0005E5CB` and expected system `0`. Read-only `Starfield.esm`
  inspection identifies that form as STDT `SolStar` with `DNAM=0`; the prior
  PNDT/GNAM lookup could never resolve a star form.
- [x] The first correction incorrectly assumed CommonLibSF's
  `BGSStar::uniqueID` exposed STDT `DNAM`. Its build/package/MO2 DLLs matched at
  `8B35AC11379CA0C6E023854E1654B49A79140C935EA155BBCA834FDBB18DF4EA`.
- [x] A restarted run exposed a separate callback-order bug: every Sol gate
  reported root `00000000`. The cache now clears only on entry to galaxy view,
  survives galaxy-to-system entry, and accepts updates only from live STDT
  forms. That build/package/MO2 DLL matched at
  `FB601F77C5CF3EB98BF0E95459BA4CC618B38A424FC8D376E44D219B41790626`.
- [x] The next restarted run proved the root cache: it retained Sol STDT
  `0005E5CB`. It also disproved the runtime-member assumption:
  `BGSStar::uniqueID` returned decimal `386507` (`0005E5CB`, the form ID), while
  the selected PNDT correctly expected galaxy system `0`.
- [x] The load-order-aware background index now parses STDT `DNAM` alongside
  PNDT `GNAM`, validates each indexed root against a live STDT form, and compares
  both records in the same galaxy-system ID domain. Direct base-data checks
  confirm `SolStar DNAM=0` and `AlphaCentauriStar DNAM=71456`.
- [x] The STDT/DNAM releasedbg build and package install passed with inherited
  CommonLibSF warnings only. The build, `release/Data`, and active MO2 DLL
  hashes match:
  `EFEF8B38CB0A5E954670F592865FC3853753FA7149F6C0C6614066CD468487DA`.
- [x] The STDT/DNAM build loaded with 124 indexed roots, resolved Sol root
  `0005E5CB` to system `0`, enabled `JUMP THEN CRUISE`, accepted Neptune, and
  dispatched stock Back. This proves the complete pre-Back identity path.
- [x] The post-Back driver then cleared the request after 750 ms because
  `SystemInfo_mc` exposed the placeholder text `' '` rather than `Sol`. About
  1.4 seconds after Back, the tree feed republished exact Sol STDT `0005E5CB`.
- [x] Post-Back focus now uses that exact STDT/DNAM form+system identity instead
  of animated/localized text and waits the full five-second safety window for
  the feed/button state to settle. Route-end display text remains independently
  checked after vanilla Set Course builds the route.
- [x] The post-Back gate releasedbg build and package install passed with
  inherited CommonLibSF warnings only. The build, `release/Data`, and active MO2
  DLL hashes match:
  `F189C6A1DB8F101953303BE2AB8E10800EAF5C424CF6A50D2C01064CE857B2D6`.
- [x] The post-Back build reached the exact Sol root, dispatched system-level Set
  Course, and produced a visible executable Sol route. Vanilla selected Mars as
  the system entry body while the Cruise mark remained Neptune. The plugin's
  extra body-name rule rejected that valid route after 750 ms, leaving the map's
  JUMP panel visible without executing it.
- [x] Removed only that redundant body-name rejection. Exact pre/post-Back
  STDT/DNAM identity, matching route-system text, and the public Execute gate
  remain mandatory before `JumpDataPanel.SendExecuteEvent()`.
- [x] The valid-entry-body releasedbg build and package install passed with
  inherited CommonLibSF warnings only. The build, `release/Data`, and active MO2
  DLL hashes match:
  `827ED5C9C3E0604E551C02BE3000F9E269B1ADBE199AC81887EDFAA8327E122E`.
- [x] The next live test completed the remote Sol `JUMP THEN CRUISE` flow
  through stock Back, system-level Set Course, Execute Route, arrival, and
  Neptune Cruise lock.

### 2026-08-06 temporary-child station placement correction

- [x] The next live test exposed Deimos Staryard marker CELL `00219DFF/4` as
  unavailable because it had neither an indexed station reference nor a current
  HUD target row.
- [x] Read-only base-ESM inspection confirmed `00219DFF -> 003120D6`, with the
  REFR's `NAME=000090B3` station base. The Eye and Nova Galactic Staryard
  references are in type-8 persistent CELL children; Deimos is in a type-9
  temporary CELL child that the indexer previously skipped.
- [x] The load-order parser now indexes placed REFRs from both type-8 and type-9
  CELL children. Selection still requires the indexed form to be a currently
  live `TESObjectREFR`, revalidates its live base against `IsStarstation`,
  and rejects zero or multiple live results.
- [x] A read-only simulation of the revised base-game parser found 23 station
  references in 23 distinct cells with zero ambiguous cells, including
  `00219DFF -> 003120D6`.
- [x] Releasedbg configure/build/install passed with inherited CommonLibSF
  warnings only. The build, `release/Data`, and active MO2 DLL hashes match:
  `C89F9DC26F6DF4781047E3BD7B1E996215D2CFA8F51BCB3FDF6854A3B51E8ECC`.
- [x] The next restarted live test confirmed Deimos Staryard is enabled and its
  Cruise action works with the temporary-child index correction.

### 2026-08-06 dynamic Ship POI CELL resolution

- [x] The next live test exposed highlighted Ship marker CELL `FF018EB6/4` as
  unavailable because the former generic path incorrectly required a cockpit
  target-feed row with that same CELL ID.
- [x] Read-only base-ESM inspection confirmed the structural analogue
  `0021C1B2` is also a CELL, not a ship REFR. Its placed children include GBFM
  ship references, proving the map ID and course-addressable ship ID occupy
  different identity domains.
- [x] The local CommonLibSF mirror's 1.16.244 `TESObjectCELL` layout has a
  byte-derived locked loaded-reference walker at `references+0x80` and
  `lock+0x118`. The resolver uses that walker only on the main UI thread.
- [x] This experimental resolver was later removed from selection policy. Ship
  CELL identity is not reliable enough for the production exactness boundary;
  all non-station markers are now hidden rather than exposed as unavailable or
  eligible.
- [x] Releasedbg compile/link/install passed with inherited CommonLibSF warnings
  only. The build, `release/Data`, and active MO2 DLL hashes match:
  `1ABB3F1652F309FF5CCE444570CE96A79CC0213E7919460555F9655FC8CE8B96`.
- [ ] Restart Starfield, highlight a Ship POI, and confirm the plugin action is
  absent rather than disabled as `TARGET IS NOT AVAILABLE TO CRUISE`.

## 2026-08-06 controller input and glyph repair

- [x] Shipped `ShipReticle` source confirms separate stock input data:
  `Cruise` for keyboard/mouse and `SHMonocle` for gamepad. The Starmap now
  mirrors that design for both its `ReleaseHoldComboButton` and tap-only
  `BasicButton`, swapping the installed data object when the active device
  changes so `ButtonKeyHelper` can resolve the native controller glyph.
- [x] The guarded startup `ControlMap` scan now reads keyboard, mouse, and
  gamepad device arrays. Controller routing follows the live `SHMonocle`
  physical id and retains the real gamepad device on the hold, so the existing
  HUD handoff forwards `SHMonocle` rather than the MKB `Cruise` event.
- [x] A first edge that changes input device is routed to the data object still
  installed for that frame; the next safe post-advance pass swaps the object.
  This prevents the first controller press from being lost during an MKB-to-pad
  transition.
- [x] Releasedbg configure/build/install passes with inherited CommonLibSF
  warnings only.
- [ ] Relaunch Starfield with a controller and confirm the native glyph, tap,
  completed fill/hold, HUD Cruise activation, course lock, and an in-map
  keyboard/controller swap.

## 2026-08-06 nested Data Menu close repair

- [x] Shipped `StarMapButtonHintBar.onCloseSubMenuToGame` confirms the stock
  return-to-game order: `DataMenu_SetMenuForQuickEntry`, then
  `GlobalFunc_CloseAllMenus`.
- [x] Accepted current-system tap/hold now dispatches that exact pair from the
  verified post-advance window. The former single `GalaxyStarMapMenu` hide is
  retained only as a failure fallback.
- [x] Remote planet/moon routing remains unchanged: it still dispatches stock
  `StarMapMenu_OnCancel` to return from system view to galaxy view before
  guarded Set Course and Execute processing.
- [x] Releasedbg configure, compile, and link pass with inherited CommonLibSF
  warnings only. Built DLL SHA-256:
  `84850B3B36B74120A62B12FE260CBDDFE9607C20F9E83977DFBF0BA26A50CEE9`.
- [x] After Starfield exited, the new DLL was copied to the active MO2 mod. The
  built and deployed SHA-256 hashes match:
  `84850B3B36B74120A62B12FE260CBDDFE9607C20F9E83977DFBF0BA26A50CEE9`.
- [x] In-game confirmation on 2026-08-06: Start -> character menu -> Starmap ->
  accepted Cruise action closed every menu layer and returned directly to
  gameplay instead of revealing the character menu.

## 2026-08-06 remote ExecuteRoute settle hardening

- [x] The latest Gagarin runtime trace reached the exact STDT/DNAM galaxy root,
  dispatched system-scope Set Course, observed an executable Alpha Centauri
  route with Jemison as vanilla's entry body, and invoked stock Execute Route.
  The map then closed without an observable player-jump acknowledgement, so a
  successful ActionScript invocation alone is not counted as travel proof.
- [x] Removed the 750 ms early-final route mismatch. Every non-ready route state
  now receives the full five-second construction window, covering stale route
  identity during the native transition.
- [x] A matching route must remain continuously executable for 500 ms before
  `JumpDataPanel.SendExecuteEvent()` is invoked. Any missing/mismatched frame
  resets that dwell, preventing a first-frame `bCanExecuteRoute` race from
  closing the map before native route state has settled.
- [x] Added a read-only, player-filtered `Spaceship::GravJumpEvent` sink for
  travel acknowledgement. Address Library ID `93876` is guarded by the verified
  1.16.244 global-event getter prologue and source vtable `445846`; ambient NPC
  ship events are ignored.
- [x] Releasedbg configure/build/install passes with inherited CommonLibSF
  warnings only. The built and MO2-deployed DLL SHA-256 hashes match:
  `B9D2B90D809348173D8B970EFDC7BA9ABCA83B14F24373426A08944F4B3CC995`.
- [ ] Restart Starfield and repeat several remote `JUMP THEN CRUISE` attempts.
  Require the continuous-ready log, followed by player grav-jump states `0`,
  `1`, and `2`; also confirm an unavailable route remains on the galaxy map for
  the full timeout without retaining the Cruise mark.
## 2026-08-06 remote route focus-reset correction

- [x] The restarted `B9D2` predecessor trace accepted Gagarin from Sol, reached
  Alpha Centauri's exact STDT/DNAM root after 3.510 seconds, and dispatched stock
  system-scope Set Course. No executable-route or Execute log followed.
- [x] At 2.212 seconds into route construction, application focus loss called
  the generic physical-hold reset. That reset incorrectly demoted
  `MapSelection` to `Marked`; the next main-thread reconciliation then cleared
  Gagarin as a normal system change. A prior successful route required about
  2.844 seconds to become executable, proving this attempt was cancelled before
  its normal route-ready window.
- [x] Physical-hold/focus cleanup now demotes only `AwaitingCruise`, never an
  accepted `MapSelection`. Main-thread system-change cleanup also treats an
  active guarded remote-route request as authoritative, independently of the
  mutable navigation display state.
- [x] Releasedbg configure/build/install passes with inherited CommonLibSF
  warnings only. The built and MO2-deployed DLL SHA-256 hashes match:
  `EFB3A55A0B33C10885E4970492F3F022D29B5347EEEE662B86FFA040F4DFFF81`.
- [ ] Restart Starfield and repeat the Gagarin remote tap. Require matching-root,
  continuous-ready, Execute Route, and player grav-jump state logs in order.
## 2026-08-06 remote dynamic-POI action suppression

- [x] Runtime evidence identifies the unavailable Ship selection as CELL
  `0021C1B2/4` while the cockpit system was Sol `0` and the browsed live STDT
  system was Alpha Centauri `71456`. Its loaded-reference walk correctly found
  no ship because the remote encounter system was not loaded.
- [x] After exact station resolution, a remaining non-planet marker is now hidden
  whenever its browsed STDT/DNAM system differs from the captured cockpit system.
  This is structural and localization-independent; it does not compare the
  marker text with the English word `Ship`.
- [x] Current-system Ship CELLs still use the exact-one live, in-space,
  non-station GBFM resolver. Remote planets/moons retain `JUMP THEN CRUISE`.
- [x] Releasedbg configure/build/install passes with inherited CommonLibSF
  warnings only. The built and MO2-deployed DLL SHA-256 hashes match:
  `77CBC11D78A15FD1C30A45E23503E4D013DAFF2373C1FC2B810468406C4A690F`.
- [ ] Restart Starfield and confirm the remote Ship marker has no Cruise action,
  while a current-system exact-one Ship marker still shows `SET CRUISE TARGET`.
## 2026-08-06 foreground-aware remote route timeout

- [x] The restarted `77CB` trace accepted remote Olivas at `16:58:13.874` and
  dispatched stock Back. Starfield lost foreground at `16:58:16.659`, before the
  galaxy STDT feed republished the focused root. The old wall-clock timeout then
  expired at `16:58:18.875` with `focused galaxy STDT/DNAM identity is
  unavailable`; neither Set Course nor Execute was reached.
- [x] The remote-route driver now returns without advancing or expiring while
  Starfield is not foreground. On focus return, an active request receives a new
  full phase timeout and its partial Execute-readiness dwell is reset.
- [x] Releasedbg compile/link passed with inherited CommonLibSF warnings only.
  Initial install correctly failed while PID `20192` held the DLL open. After
  Starfield exited, `xmake install -y` succeeded and the built/MO2 DLL SHA-256
  hashes match:
  `FBD7188E604C65F23EAD5984A71234BC0189E6E7C046C3B90ABF6A570E369617`.
- [ ] Restart Starfield and retry one remote planet/moon. Keep the game focused
  through the initial transition, or deliberately switch away and return;
  require matching-root, continuous-ready, Execute Route, and player grav-jump
  state logs in order.
## 2026-08-06 carried-root and remote-input quarantine

- [x] The restarted `FBD7` trace explains the origin-system Cruise report. Voss
  was highlighted at `17:46:31.558`, but the marker changed to remote station
  CELL `0003DBEC` 92 ms later and before the physical press. Because station
  resolution preceded the remote-system hide gate, the UI exposed a local
  `SET CRUISE TARGET` hold; its completed one-second hold then correctly engaged
  Cruise in Sol for reference `000013B8`.
- [x] All remote non-planet markers are now hidden before station, Ship, or generic
  target resolution. Highlight jitter therefore cannot turn a remote
  planet/moon tap into an origin-system station/Ship Cruise hold.
- [x] The same trace accepted remote Gagarin at `17:47:24.101`, reached galaxy
  view 661 ms later, then received a map close at `17:47:26.875` before the
  optional star feed republished the exact root. No Set Course, Execute,
  LoadingMenu, or player grav-jump event occurred.
- [x] The already-proven captured STDT/DNAM root now survives stock Back and is
  pinned against transient valid-star rows until system-scope Set Course. This
  removes the optional republish delay; exact displayed route-system identity
  and continuous `bCanExecuteRoute` readiness remain mandatory before Execute.
- [x] Repeat presses of the Cruise-bound keyboard, mouse, or controller control
  are removed from the input list while a guarded remote route is active. This
  prevents an impatient second press from invoking a different galaxy/cockpit
  context during the asynchronous handoff.
- [x] Releasedbg configure/build/install passes with inherited CommonLibSF
  warnings only. The built and MO2-deployed DLL SHA-256 hashes match:
  `8C57D315586A2F45C67F0A83FE0EF539C783BB1C92E7BF2E24A2F18B5E434490`.
- [ ] Restart Starfield and test one remote planet/moon with a single tap. Require
  immediate carried-root Set Course after galaxy view, continuous-ready Execute,
  then player grav-jump states `0`, `1`, and `2`. Also confirm every remote
  station/Ship marker has no Cruise action.

## 2026-08-06 cursor-independent galaxy selection

- [x] The next live Sol attempt remained in galaxy view until the mouse moved
  over the captured system; only then did the driver dispatch Set Course. This
  isolates the remaining delay to vanilla galaxy-marker selection rather than
  STDT identity, route construction, or Execute.
- [x] Shipped 1.16.244 ActionScript confirms
  `QuickSystemSelect.OnSelectionChange` emits
  `StarMapMenu_QuickSelectChange {bodyID: entry.uBodyID}` before its
  Open-for-Plot item emits `SetRouteDestination`. The separate
  `StarMapMenu_Galaxy_FocusSystem` event is parameterless and would enter system
  view, so it is not the correct route-plot seam.
- [x] On the first guarded galaxy-view advance, the driver now emits that exact
  Quick Select change once with the already-proven captured STDT root. It then
  waits for native enabled/visible Set Course data and retains every existing
  route-system, executable, dwell, session, and arrival gate.
- [x] Releasedbg compilation and xmake installation pass with inherited
  CommonLibSF warnings only. Built and MO2-deployed DLL SHA-256 hashes match:
  `215B3C07734F19452C00CC28DDD5E4CDED39F9EAC6169B6F08CBFE54250F58A3`.
- [ ] Restart after deployment and trigger one remote planet/moon without moving
  the mouse. Require `primed stock Quick Select`, system-level Set Course,
  continuous-ready Execute, and player grav-jump states `0`, `1`, and `2` in
  order.

## 2026-08-06 cursor-independent galaxy marker context

- [x] The `215B3C07...F58A3` run isolated the remaining failure exactly. Remote
  Gagarin `0005E311` was accepted with system root `0005E60A` (Alpha Centauri),
  the log confirmed `primed stock Quick Select system bodyID=0005E60A ...
  without cursor input`, and the request then failed closed at five seconds with
  `vanilla Set Course is disabled or hidden (enabled=false visible=true)`.
  Moving the physical mouse over the destination system had previously allowed
  the same flow to continue. `StarMapMenu_QuickSelectChange` alone therefore
  does not establish the galaxy marker context native requires.
- [x] Reaching galaxy view with the captured STDT root and establishing the
  galaxy marker context are now separate phases (`kAwaitGalaxy` ->
  `kEstablishSelection` -> `kAwaitRoute`), each with its own five-second window.
  A slow Back transition can no longer consume the selection budget, and the
  failure log now names which of the two stages ran out.
- [x] The marker-context phase runs a fixed ladder, one rung per post-advance
  pass and only while native still reports no selection:
  rung 1 emits the shipped `StarMapMenu_QuickSelectChange {bodyID}` payload;
  rung 2 invokes the shipped public `SetHoveredSystem` galaxy setter with the
  same root, located by a bounded exact-name search of the menu root plus any
  galaxy-named container. Rung 2 failing is not fatal and mutates nothing.
- [x] `StarMapMenuQuickSelectData` is now subscribed read-only through the same
  proven `BSUIDataManager.Subscribe` path. It publishes
  `uCursorSelectionIndex` and the selected entry's `uBodyID`; the payload's
  member names are logged once per session so the shape stays evidence-pinned
  rather than assumed. Nothing is written back to the feed.
- [x] Stock Set Course is dispatched only after native itself names the captured
  system, through one of three native-published authorities: the vanilla Set
  Course button reporting enabled and visible, the Quick Select cursor resting
  on the captured root, or exactly one galaxy highlight marker carrying it. The
  two weaker authorities additionally require the vanilla button to be present
  and visible. The plugin never writes, forces, or infers that button's state,
  and the authority that unblocked the dispatch is named in the log.
- [x] When the ladder is exhausted with no native selection, one bounded
  read-only diagnostic pass logs the menu-root and hint-bar member names plus
  every hint button's `bEnabled`/`bVisible`/text/action. This is intended to
  identify the true vanilla seam from a single failing run instead of another
  guessed event name.
- [x] Route-end system identity, the public `bCanExecuteRoute` gate, the 500 ms
  continuous readiness dwell, map session/movie generation, the foreground pause,
  and the player-filtered grav-jump acknowledgement are all unchanged. Every
  failure path still clears only the Cruise mark, leaves the map open, and
  preserves vanilla route/warning state.
- [x] Log phrasing now distinguishes the stages explicitly: `marker context
  established by <authority>`, `Set Course enabled`, `Set Course dispatched`,
  `route identity confirmed`, and `Execute dispatched`.
- [x] The first native Windows `releasedbg` compile/link and MO2 deployment were
  performed on 2026-08-07 against checked-out CommonLibSF commit `856774a`.
  They passed with inherited CommonLibSF warnings only. The following no-mouse
  run failed as recorded in the next section, so build/deploy proof was not
  mistaken for gameplay proof.
- [ ] Restart Starfield and trigger one remote planet/moon `JUMP THEN CRUISE`
  **without moving the mouse after the press**. Require, in order: `marker
  context established by <authority>`, `Set Course dispatched`, `route identity
  confirmed`, `Execute dispatched`, then player grav-jump states `0`, `1`, `2`,
  and finally the arrival Cruise lock on the marked body.
- [ ] If it still fails closed, capture the `galaxy diagnostics` lines and the
  `StarMapMenuQuickSelectData members` line: together they name the real
  vanilla galaxy-selection seam without another speculative event.

## 2026-08-07 native single-system galaxy focus

- [x] The first Windows compile/deploy of the previous two-rung change passed,
  and the one requested no-mouse Neptune test failed closed without dispatching
  Set Course. The live payload was exactly
  `StarMapMenuQuickSelectData members: bShowMenu:bool, bOpenForPlot:bool`;
  it contains no entry array, selected index, or selected body ID.
- [x] The same run proved both speculative rungs ineffective: the direct
  `StarMapMenu_QuickSelectChange` event produced no native selection readback,
  and no public `SetHoveredSystem` method was reachable. At timeout native still
  reported `SetRouteDestination` resolved/visible but disabled, zero Quick
  Select entries, and zero highlighted galaxy markers. Vanilla route state was
  preserved and only the Cruise mark was cleared.
- [x] Shipped `QuickSystemSelect.as` confirms the list emits
  `StarMapMenu_QuickSelectChange` only from an actual selected entry and sends
  `SetRouteDestination` from its Open-for-Plot item press. Native
  `GalaxyState` analysis confirms the list entries are delivered separately by
  a direct `SetMarkers(Array)` call; the two-bool feed is complete.
- [x] Static 1.16.244 native analysis identifies why opening Quick Select is not
  itself the fix: its candidate builder measures galaxy marker positions
  against cursor coordinates and opens the list only for multiple candidates.
  The one-candidate path instead calls `0x1416A1880`, Address Library ID
  `94315`, with `(GalaxyState, StarMapMenu+0x1B8, systemBodyID, false)`.
- [x] The first follow-up driver invoked that exact vanilla single-system focus operation
  once after stock Back proves the captured STDT root in galaxy view. It is
  guarded by the exact 16-byte 1.16.244 prologue, StarMapMenu primary vtable ID
  `446845`, GalaxyState primary vtable ID `446425`, the active menu/movie
  generation, and a nonzero captured root. Any mismatch fails closed before the
  call. No Quick Select array, button state, route, or synthetic input is
  written.
- [x] The downstream authority gates are unchanged: system-scope Set Course is
  dispatched only after native readback names the captured system, and Execute
  still requires matching route-end system identity plus continuous public
  readiness for 500 ms.
- [x] Releasedbg build and MO2 deployment for the ID `94315` change pass with
  inherited CommonLibSF warnings only. Built/deployed DLL hashes match at
  `EAC3CA0907BAC5E6CE2FBB14CF6F87B836615DA88D66738FD4BE248C530B265E`;
  built/deployed PDB hashes match at
  `776279214B55D6F493008AF97C0D169558DE3A6593FCD41EC20C4047E9E176FE`.
  Starfield was closed for deployment, no custom INI was present, and the
  winning default INI had `bVerboseLog=true`.
- [x] The no-mouse Chawla follow-up proved ID `94315` is not a plot-selection
  seam. It made the vanilla Set Course button enabled within 17 ms and the
  plugin dispatched Set Course, but native then entered Alpha Centauri system
  view and produced no route. The full five-second route phase failed closed
  with `system-level Set Course left galaxy view before producing a route`.
- [x] Static follow-up identifies the non-entering operation as GalaxyState
  primary-vtable slot `+0x48`, `0x14169DEB0`, Address Library ID `94292`. It
  writes/publishes the native selected-system field at `+0x880` without queuing
  a view transition. `SetRouteDestination` reads that selected ID only while
  the Quick Select ownership byte at `+0x8F8` is active, then calls stock close
  `0x1416A0DA0`, Address Library ID `94308`, which clears it synchronously.
- [x] The driver now uses ID `94292`, verifies exact selected-system readback,
  and arms `+0x8F8` only immediately around the stock Set Course event. If the
  event does not synchronously consume the byte, byte-verified ID `94308`
  restores the state and the request fails closed. The physical/fake cursor,
  button enabled state, marker arrays, and route data are never written.
- [x] Releasedbg build and MO2 deployment pass with inherited CommonLibSF
  warnings only. Built/deployed DLL hashes match at
  `075F58AA57961A7F208C23DB07028A07972334C815603E595556B564CF06E930`;
  built/deployed PDB hashes match at
  `907F36EA5BD5B9694C688AAD269D8BFF9EB12A0444610F5D5DA9D8809F77A7EB`.
  Starfield remained closed throughout deployment.
- [ ] Restart Starfield and run one remote planet/moon `JUMP THEN CRUISE` tap
  without moving the mouse for the full ten seconds. Require `focus rung 1:
  invoked stock native galaxy selected-system setter`, `Quick Select route
  selection armed`, Set Course without returning to system view, matching-route
  Execute, player grav-jump states `0`, `1`, `2`, and final Cruise lock on the
  marked body.

## 2026-08-07 remote-moon parent continuation

- [x] The live failure is isolated downstream of the vanilla system route.
  Chawla `0005E315` arrived in Alpha Centauri at Jemison but was absent from
  `TargetLowFrequencyProvider`; Aranae IV-c `0005E290` accepted
  `Reticle_OnCruiseLockCourse` after a manual same-system activation but never
  produced matching `bIsCruiseTargetLock`. Removing the exact-one-row gate would
  therefore activate Cruise without a lockable destination.
- [x] The same PNDT/GNAM parser used by the production body index resolves each
  failing moon to exactly one parent without a guessed FormID: Chawla
  `(system=71456,parent=4,planet=10)` resolves to Olivas `0005E313`
  `(71456,0,4)`; Aranae IV-c `(72955,4,12)` resolves to Aranae IV `0005E28C`
  `(72955,0,4)`. The runtime Neptune result separately proves that a planet
  exposed in the cockpit feed can be exact-locked even when vanilla entered the
  system at a different planet.
- [x] Production now retains the moon as the sole public destination and keeps
  the resolved parent in a private continuation. It requires one parent planet
  HUD row, stock Cruise activation, dispatched parent ID, exact parent lock
  readback, lock loss with Cruise inactive in the same settled system, a newer
  low-feed revision with one final-moon row, a second stock Cruise activation, and exact final-moon
  lock readback. Dispatch success alone advances neither lock gate.
- [x] Missing/ambiguous parent identity or HUD rows, bounded feed/activation/lock
  and arrival timeouts, manual interruption, map reopen, load, HUD replacement,
  landing/docking, or system mismatch fail closed. The existing system-route,
  GalaxyState, Quick Select ownership, Execute dwell, grav-jump, and direct
  planet/final-row paths are unchanged.
- [x] With Starfield PID `16308` closed first, `xmake f -m releasedbg -y`
  followed by `xmake -y` compiled, linked, installed, and deployed successfully
  with inherited CommonLibSF warnings only. Built/deployed DLL hashes match at
  `40D54D514C30FC46E49DADA7495AE5EA1A15EB03E43B8CE74F1FA47889935AA7`;
  built/deployed PDB hashes match at
  `59DBEF2ECDDE4A113AA4D20927BE6F78884C5BA09D86B0A8F31822631E8518DB`.
  No custom INI exists and the deployed winning default has
  `bVerboseLog=true`.
- [x] The first no-mouse Chawla trace proved the entire first leg: vanilla entered
  Alpha Centauri at Jemison, the private parent resolved uniquely to Olivas
  `0005E313`, stock Cruise activated, the Olivas course dispatched, and exact
  Olivas `bIsCruiseTargetLock` readback arrived 12 ms later. After 94 seconds the
  lock ended at Olivas, but the added `0.05` light-second audit did not pass and
  failed closed after two seconds. Stock Cruise did not become available again
  until five seconds after lock loss. The operator observed Olivas as the
  terminal result; Chawla was never dispatched on the second leg.
- [x] That trace disproves the generic close-distance threshold for an internal
  parent planet. The continuation now uses a stricter identity/topology proof:
  after an exact parent lock ends, Cruise must be inactive, the same system must
  remain settled, and a strictly newer cockpit feed must contain exactly one row
  for the retained moon. A manual exit away from the parent cannot advance while
  that moon remains absent. The ordinary final-destination arrival audit retains
  its existing close-distance rule.
- [x] With Starfield closed, the corrected gate passed a fresh
  `xmake f -m releasedbg -y` and `xmake -y` build/deploy with inherited
  CommonLibSF warnings only. Built/deployed DLL hashes match at
  `1DA15F20BD3DACEB747755DDF736E5594D034C905137A93A70BCBDB4552C4BAD`;
  built/deployed PDB hashes match at
  `E6593F937F7EC17BB2C53CDFF0B55B417373588A23103E2DB6B1C6341EF248F4`.
  The winning default remains `bVerboseLog=true` with no custom override.
- [x] A no-mouse Triton `0005DECE` trace completed the required sequence. Vanilla
  built and executed a Sol route ending at Mars, player grav-jump states 0, 1,
  and 2 were observed, and the settled Sol feed lacked Triton. Live GNAM resolved
  the unique private parent Neptune `0005DECD`; its course was dispatched and
  received exact engine lock readback 11 ms later. After that lock ended, stock
  Cruise became available and newer feed revision 56 uniquely exposed Triton,
  independently confirming the parent arrival/feed refresh. The plugin then
  reactivated stock Cruise, dispatched Triton, and received exact final-moon
  lock readback 14 ms later. The retained public destination remained Triton
  while the orange engine marker showed the private Neptune first leg.
- [x] A same-system Ariel `0005DEC9` regression preserved the pre-existing direct
  path. After stock Cruise activation, the plugin dispatched Ariel once at
  `10:51:49`; no `bIsCruiseTargetLock` readback arrived within 1.5 seconds, so
  the mark was preserved. The operator observed stock Cruise approach Uranus,
  and the cockpit feed reported exact Ariel lock at `10:56:21`. No remote-moon
  continuation or plugin-dispatched Uranus course occurred, so this proves that
  the guarded remote parent workflow did not replace Starfield's deferred
  same-system behavior.
- [x] The Ariel trace revises the earlier interpretation of the 1.5-second
  timeout: an absent immediate `bIsCruiseTargetLock` does not prove that the
  final-moon event was discarded. Cruise remained active for roughly 4.5 minutes
  after the single Ariel dispatch and later published exact Ariel lock without a
  second event. Production now logs every low-feed engine course-ID transition
  and applies that stock behavior to remote moons without trusting dispatch
  success or the orange visual marker.
- [x] With Starfield PID `42376` closed, `xmake f -m releasedbg -y` followed by
  `xmake -y` compiled, linked, installed, and deployed the one-activation variant
  with inherited CommonLibSF warnings only. Built/deployed DLL hashes match at
  `1DAEA647DA798000D2AB99FD649E1B6B580FAF97CF91AE625878E54A3140BFF7`;
  built/deployed PDB hashes match at
  `CEA709DD97CBE5C3489B48303E37E3264D10980B9567459811DD9C7B9CCA63A9`.
  No custom INI exists in the mod, MO2 overwrite, or Documents SFSE paths, and
  the deployed default has `bVerboseLog=true`.
- [x] The first no-mouse one-activation Chawla trace proved the stock latent
  behavior. Vanilla entered Alpha Centauri at Jemison, the plugin activated
  Cruise once and dispatched only Chawla at `11:20:13`, and neither Olivas nor
  Chawla published an exact lock during the initial five-second gate. The plugin
  cleared its public mark at `11:20:18`, but stock Cruise retained the request:
  exact Chawla lock appeared at `11:21:32`, ended at `11:21:57`, and Cruise
  became inactive at `11:22:02`; the operator confirmed arrival at Chawla. No
  Olivas exact-lock transition or second activation/dispatch occurred.
- [x] That trace proves the orange parent approach is not necessarily
  `bIsCruiseTargetLock` and that missing immediate readback is not course
  rejection. An initial bounded latent-resolution design was rejected because
  both Ariel and Chawla prove this phase is engine-owned travel whose duration
  depends on distance, not a command handshake. The driver now keeps the final
  mark pending without an arbitrary travel timeout while Cruise/system/world
  gates remain valid. Dispatch is still never success; only a unique final-moon
  exact lock completes the continuation. An optional published parent exact
  lock retains the stronger lock-end/newer-feed audit. Opening the map pauses
  the driver and an accepted replacement target resets it normally.
- [x] With Starfield closed, `xmake f -m releasedbg -y` and `xmake -y` built and
  deployed the corrected latent-retention gate together with remote-station
  support. Built/deployed DLL SHA-256 matches at
  `16BBC10F5FAFCC458C885A854EB57E55D9763F76108D2DD3E7875289F85F96FA`;
  built/deployed PDB SHA-256 matches at
  `BCF32F7D13400E617C2B4DB85D8EDEC6974B41EC27F90CAC9B817647FDA03541`.
  The winning default has `bVerboseLog=true`, with no custom override in the mod,
  MO2 overwrite, or Documents SFSE paths.
- [ ] Repeat one no-mouse remote moon. Do not mark the corrected gate validated
  until the public final mark survives the latent interval and matching exact
  final-moon lock appears without another activation or dispatch.

## 2026-08-07 remote station continuation

- [x] The prior remote non-planet exclusion was broader than necessary. Ships
  and generic POIs still have no stable unloaded target identity, but the
  active-load-order index already preserves station CELL, placed REFR, and
  `IsStarstation` base identity independently of whether that system is loaded.
- [x] The existing parser simulation found 23 placed station references in 23
  distinct CELLs with zero ambiguous CELLs. Known exact mappings include The Eye
  `0001285A -> 00012894`, Nova Galactic Staryard
  `00219520 -> 00216F51`, and Deimos Staryard
  `00219DFF -> 003120D6`.
- [x] A remote non-planet marker is now eligible only when its CELL has exactly
  one indexed station REFR/base tuple, exactly one CELL-owned XMRK REFR, and the
  guarded load-game sink is ready. The destination value retains the map CELL,
  exact physical REFR/base, exact course XMRK, and exact STDT/DNAM system
  identity. Remote ships and generic POIs remain hidden.
- [x] The remote station uses the unchanged vanilla system-level route handoff:
  stock Back, guarded GalaxyState selected-system setter/readback, narrowly
  scoped Quick Select ownership byte, stock Set Course, exact route-system
  identity, executable-route dwell, stock Execute Route, player grav-jump
  acknowledgement, and settled target-system agreement.
- [x] No native ship-target assignment occurs in the origin system. After
  settled arrival, one bounded window allows the retained station REFR to become
  live. Its live base must exactly match the retained indexed base and still
  carry `IsStarstation`; a different, missing, or ambiguous identity fails
  closed. The guarded native target setter and exact target-global readback must
  then succeed. The exact CELL-owned XMRK is independently revalidated. One exact
  XMRK HUD row takes the direct path. If that row is absent, the exact
  CELL/DNAM/GNAM orbital continuation documented below may advance only through
  one exact visible ancestor row.
- [x] Both station paths dispatch only the retained CELL-owned XMRK, while the
  public/native identity remains the physical station REFR/base. Dispatch is not
  success: the XMRK course must appear on exactly one low-frequency row with
  matching `bIsCruiseTargetLock`. Activation, dispatch, bounded handshake/feed
  timeouts, unrelated courses, load, landing/docking, system mismatch, or
  replacement fail closed; active Cruise travel itself is unbounded.
- [x] The releasedbg build/deploy and matching DLL/PDB hashes are recorded in the
  remote-moon section above; the same binary contains remote-station support.
- [x] The corrected no-mouse RE-939 trace proved the complete remote-station
  chain: vanilla system jump, exact retained physical REFR/base live resolution,
  native target assignment/readback, exact retained CELL/XMRK revalidation,
  stock Cruise activation, exact XMRK dispatch, and matching one-row
  `bIsCruiseTargetLock`.

### 2026-08-07 first remote-station live trace

- [x] A no-mouse remote Alpha Centauri station selection proved the new front
  half. CELL `0003DBEC/4` resolved statically to REFR/base
  `000013B8/000013B6`; the exact Alpha Centauri route ended at Jemison, Execute
  closed the map, player grav-jump states `0`, `1`, and `2` appeared, and the
  settled system resolved to `71456`.
- [x] After arrival the same exact REFR/base resolved live, native ship-target
  assignment committed, and target-global readback agreed.
- [x] The station did not appear in `TargetLowFrequencyProvider` during its fresh
  ten-second HUD-row window, so the continuation cleared the mark without
  activating Cruise or dispatching a course. This is a safe failure, not remote
  station validation. Do not remove the exact-one row gate.

### 2026-08-07 exact orbital staging for missing remote-station rows

- [x] Base-game PNDT `002D5F53` has `DNAM=scLC175StationRE939` and GNAM
  `{system=71456,parent=11,planet=28}`. That `DNAM` exactly matches CELL
  `0003DBEC`'s EDID, proving the selected station's orbital identity without a
  guessed FormID.
- [x] The same live-load-order GNAM index resolves the orbital's unique parent
  to Voss `0005E316` (`planet=11,parent=4`) and Voss's unique parent to Olivas
  `0005E313` (`planet=4,parent=0`). The design can therefore select an exact
  visible ancestor even when the immediate parent is itself a distant moon,
  then accept only ordered inward locks such as Olivas to Voss before the station.
- [x] The index now parses PNDT space-cell `DNAM`, active CELL EDID, station
  CELL/PNDT links, and unique same-system GNAM parents. A missing station row
  may enter the existing one-activation latent orbital continuation only after
  exact live REFR/base assignment and one exact ancestor row. The public mark
  remains the station; active travel has no arbitrary timeout; dispatch is not
  success. This version incorrectly treated the physical station REFR as the
  Cruise course identity.
- [x] With Starfield closed, releasedbg configure/build/deploy completed. Built
  and active-MO2 DLL SHA-256 matches at
  `A5848DCDC173B1F1EB45B77D5431C86BABFC704C81F1A8300E968374E5730D4F`;
  built and active-MO2 PDB SHA-256 matches at
  `DEBFDBF5C74F5BAD219CDF88D9BEDC3095E2B0C5C66F0E25178E10A41D15CC34`.
  The winning default has `bVerboseLog=true` and no custom override was found.
- [x] The later dual-identity trace below supersedes this physical-REFR course
  model. It proved the same vanilla jump and exact physical assignment, retained
  Olivas as the unique visible ancestor, and then received the final XMRK lock
  directly. Stock did not publish an intermediate ancestor lock, so no
  ancestor-lock arrival/feed transition was required.

### 2026-08-07 RE-939 live result and dual-identity correction

- [x] The restarted no-mouse Sol-to-Alpha-Centauri attempt physically reached
  Starstation RE-939. Vanilla ended the exact system route at Jemison, emitted
  player grav-jump states `0`, `1`, and `2`, and settled in Alpha Centauri. The
  plugin then revalidated and natively assigned physical station REFR/base
  `000013B8/000013B6`, joined CELL `0003DBEC` to orbital PNDT `002D5F53`, and
  retained the exact Voss `0005E316` to Olivas `0005E313` ancestry.
- [x] The old model dispatched physical station REFR `000013B8`, but the engine
  published exact `bIsCruiseTargetLock` on `0003DBEE`. The plugin therefore
  rejected `0003DBEE` as unrelated and cleared its public mark even though stock
  Cruise continued and arrived at RE-939. The travel path worked; the plugin's
  station course identity was incomplete.
- [x] Raw active-master inspection identifies `0003DBEE` as the sole XMRK REFR
  in CELL `0003DBEC`'s complete placed-child hierarchy. The active-load-order
  index now derives this relationship rather than hard-coding either FormID.
  Remote station eligibility requires exactly one station REFR/base tuple and
  exactly one CELL-owned XMRK REFR. Native assignment and public status use the
  physical station; HUD-row selection, event dispatch, and exact lock readback
  use the XMRK. Missing, ambiguous, deleted, overridden, or live-mismatched
  identities fail closed.
- [x] With Starfield closed, the exact releasedbg configure/build completed.
  xmake's trailing default-INI install copy reported a permission error after
  linking, so the new DLL/PDB were copied explicitly to the exact active MO2
  plugin directory. Built/deployed SHA-256 matches at
  `FA8EBA2432F394639A2AC705DA0FC00AF32BB39A019F660A84D7937445019CCE`
  for the DLL and
  `F7873CAF5C64600DFCE690B042D552785CAA28B0A87F11AF8626FA679BE26321`
  for the PDB. The deployed default already exactly matches the source default,
  has `bVerboseLog=true`, and no winning `CruiseFromStarmapCustom.ini` override
  was found in the active mod, MO2 overwrite, or Documents paths.
- [x] The restarted no-mouse trace on the deployed correction indexed 25 exact
  CELL/XMRK course links and selected RE-939 CELL `0003DBEC` as physical
  REFR/base `000013B8/000013B6` with course XMRK `0003DBEE`. Vanilla entered
  Alpha Centauri at Jemison after player grav-jump states `0`, `1`, and `2`.
  Settled arrival revalidated and assigned the physical station, exact ancestry
  retained Olivas `0005E313` as one HUD row, and one stock Cruise activation
  dispatched only `0003DBEE`. The engine published matching exact lock 10 ms
  later on low-feed revision 11; no unrelated-course or fail-closed clear
  occurred. The operator confirmed physical arrival at Starstation RE-939.

### 2026-08-07 marker-independent arrival sampling

- [x] The validated RE-939 course lock ended after 102.9 seconds, but the
  two-second arrival audit logged no evidence and preserved public mark
  `000013B8` despite operator-confirmed arrival. The winning default has
  `bShowMarker=false`.
- [x] `UpdateMarker` returned before joining the exact low-frequency target row
  to its high-frequency bearing whenever marker rendering was disabled, so
  `g_markedDistance` remained `-1`. This was a rendering-policy dependency, not
  failed travel or failed exact course identity.
- [x] Distance sampling now occurs for every valid exact retained-course bearing
  before the visual setting is checked. `bShowMarker=false` still prevents all
  plugin marker creation/rendering, while the last valid distance remains
  available across the lock-loss transition. Destination replacement/clear
  still resets the sample to `-1`.
- [x] The first default-config retest exact-locked Olivas `0005E313`, then began
  the two-second audit when that lock ended after 29.7 seconds, but again logged
  no arrival evidence and preserved the mark. The prior live probe proves
  `TargetHighFrequencyProvider.distance` uses meters: its deliberate-exit
  control retained a value near 38.3 million. Production was comparing that raw
  meter value directly to `0.05` as though it were already light-seconds.
- [x] The guard now converts `0.05` light-seconds to `14,989,622.9` meters before
  comparison. Lock-loss and audit logs include the sampled meter value and
  threshold so the next test distinguishes valid close arrival from manual exit
  without inference.
- [x] The marker-independent-only build passed exact releasedbg configure/build/deploy and
  `xmake install -o release/Data -y` passed with inherited CommonLibSF warnings
  only. Build, active MO2, and release-package hashes all match:
  DLL `AA3149654D97347034AFA32444427B31319E67BC8F6EFBA7745DC532C6A09ABC`,
  PDB `793D4D1C327CD35B6033C55EFB276F9AED859E572CB4B27163ED83C781EEF40B`,
  and default INI
  `6873CBEE7241BF901146B61B01543FB3094EE54C1A384291CBD969BA37A46649`.
  The active default has `bShowMarker=false` and `bVerboseLog=true`; no winning
  custom override was found. These hashes predate the meter-unit correction.
- [x] With Starfield closed, the meter-unit correction passed exact releasedbg
  configure/build/deploy and package refresh with inherited CommonLibSF warnings
  only. Build, active MO2, and release-package hashes all match:
  DLL `DC4666919DD2A1619C560CF6FD2C0C3DA51D9358EED032CDBBDFA782CB139D5C`,
  PDB `F329B38428801619483B6A2D91BC5C09F0B24F84A79EA78A00D961BF62CEF615`,
  and default INI
  `6873CBEE7241BF901146B61B01543FB3094EE54C1A384291CBD969BA37A46649`.
- [ ] Run one default-config arrival trace. Require exact lock loss
  followed by `confirmed arrival (course transition plus close distance)` and
  public destination clear.

## 2026-08-07 remote route while Cruise is active

- [x] The failing Chawla attempt began with exact Phobos Cruise lock
  `0005DEB7` active. Chawla replaced the public destination, stock Back and the
  guarded GalaxyState/Quick Select handoff built the correct Alpha Centauri route
  ending at Jemison, and route readiness remained exact for 506 ms.
- [x] `JumpDataPanel.SendExecuteEvent()` returned invocation success while Cruise
  remained active, but no Starmap close or player grav-jump state followed. The
  operator was left in the Starmap. Therefore ActionScript dispatch is not
  Execute acknowledgement when stock Cruise is active.
- [x] The first guarded fix deferred Execute and tried the normal stock HUD
  Cruise control. A live Alpha-Centauri-to-Sol Callisto run proved that route is
  not accepted while the Starmap owns the UI: both press and release logged
  `invoked=true handled=false`, `CruiseModeHUDActive` never became false, and the
  route failed closed before Execute. The stock Back step explains the visible
  return to galaxy view.
- [x] A second Triton control reproduced the same result without focus loss:
  exact Sol route ending at Mars, then `handled=false`, no inactive readback,
  four-second safety release, and no Execute. ActionScript invocation is not an
  input acknowledgement.
- [x] Production now disables remote planet/moon/station selection while Cruise
  is active and labels the control `EXIT CRUISE FIRST`. Acceptance rechecks the
  active flag before any destination or map mutation, and the route driver checks
  again immediately before Execute. Current-system Cruise retargeting is unchanged.
- [x] With Starfield closed, releasedbg configure/build/install passed with only
  inherited CommonLibSF warnings. Built and active-MO2 DLL SHA-256 matches at
  `9974639BB5103C5C1A93C673943798C284F09C822B776F9AD101128886A0A711`;
  built and active-MO2 PDB SHA-256 matches at
  `3B7503567C53BE94CC276FB1F35163583F545969FE84EB8334DFE74670D5BA5A`.
  The deployed default has `bVerboseLog=true`; no custom override exists in the
  mod, MO2 overwrite, Documents, or direct game paths checked.
- [x] Execute now has its own bounded acknowledgement phase. Only the stock
  Starmap close consumes that phase and preserves `PendingJump`; a dispatched
  event without map close clears the remote mark instead of leaving a false
  pending jump.
- [x] With Starfield closed, releasedbg configure/build/install passed with only
  inherited CommonLibSF warnings. Built/deployed DLL SHA-256 matches at
  `1BF2C4C9EC2130C84ACB3F7FCDF53BF66F607B666AECDA8B3D85A4F5A0354DF3`;
  built/deployed PDB SHA-256 matches at
  `436C2F8CBAEE4BAFEDD6FC32292E69AE261D398EB2C45ECDE9A6125B6B461F76`.
  The winning default has `bVerboseLog=true` and no custom override exists in
  the mod, MO2 overwrite, or Documents SFSE paths.
- [ ] With Cruise active, highlight one remote target and verify the disabled
  `EXIT CRUISE FIRST` control performs no Back transition and preserves the
  current mark/course. Exit Cruise in the cockpit, reopen the map, and run the
  ordinary no-mouse remote flow separately.

## 2026-08-07 fast Starmap-open system recovery

- [x] The failing live trace opened Starmap session 1 with
  `currentSystem=unresolved` at `13:41:59.821`. The load-order index completed
  and the same HUD rows uniquely resolved system `0` at `13:42:01.233`, but the
  immutable open-time snapshot continued reporting `CURRENT SYSTEM UNAVAILABLE`.
  Closing and reopening immediately captured system `0` and exposed the Cruise
  actions, proving this was a late authoritative-system race rather than missing
  marker or dossier data.
- [x] An unresolved open session now adopts that first unique cockpit-system
  resolution only while its exact map session and movie generation remain live.
  An already captured system is never changed, and every existing marker,
  dossier, STDT/GNAM, active-flight, and remote-route gate remains unchanged.
- [x] A releasedbg `xmake -j1 -y` build passed with deployment explicitly
  disabled and only inherited CommonLibSF warnings. The local build/deploy DLL
  SHA-256 is `A5848DCDC173B1F1EB45B77D5431C86BABFC704C81F1A8300E968374E5730D4F`.
- [x] With Starfield closed, the releasedbg DLL/PDB were deployed to active MO2;
  their built/deployed hashes match the exact values recorded in the station
  continuation section above.
- [ ] Restart Starfield, then open the Starmap immediately after loading. Confirm
  the log reports recovered current system for the same open session and the
  Cruise action appears without closing and reopening the map.

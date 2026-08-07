# CruiseFromStarmap developer runbook

Game target: Starfield 1.16.244.0 only. Source repository, not the MO2 deploy
folder.

## Build

```powershell
xmake f -m releasedbg -y
xmake -y
```

The xmake target deploys to `MO2/mods/CruiseFromStarmap` through
`XSE_SF_MODS_PATH`. Do not edit that payload by hand.

## Invariants

- Never retain a raw form or Scaleform pointer across menu/movie transitions.
- A selection is invalid unless exactly one `StarMapMenuMarkersData` row has
  `bIsInHighlightRadius=true` and a nonzero id. Planet/moon rows must also
  exactly match the dossier PNDT and its GNAM/current-system identity.
  A current-system station-backed row may be a live station reference or a CELL
  that resolves through placed children to exactly one currently live reference
  whose base carries `IsStarstation`. A remote station CELL may be accepted only
  when the active-load-order index contains exactly one station REFR/base tuple
  and exactly one CELL-owned XMRK REFR. Retain all identities for exact live
  revalidation after arrival. Any non-planet marker
  other than an exact indexed station is hidden; Ship POIs and generic markers
  are intentionally unsupported. Keep the map id/type and resolved
  target reference separate. Unmatched generic POIs, tree focus, `bIsFocused`,
  timing heuristics, and last-dossier-wins are invalid.
- A remote planet/moon/station handoff must prove the captured STDT/DNAM system root
  after stock Back, then allow every non-ready route state the full five-second
  build window. Route-end system identity and public `bCanExecuteRoute` must
  remain continuously ready for 500 ms before invoking stock Execute Route.
  Physical-hold/focus cleanup must not demote `MapSelection`, and an active
  remote-route request is authoritative against ordinary system-change cleanup.
  While Starfield is not foreground, do not advance or expire the remote-route
  driver; restart the current phase timeout and readiness dwell on focus return.
  Carry the already-proven STDT root through stock Back, pin it against transient
  star-feed rows until system-scope Set Course, and consume repeat presses of the
  Cruise-bound control while the guarded handoff remains active.
  Reaching galaxy view with the captured root is a separate phase from
  establishing the galaxy marker context, and each owns a full timeout. Marker
  context is attempted once through vanilla's non-entering GalaxyState
  selected-system setter, Address Library ID 94292, after exact 1.16.244
  function bytes plus the live StarMapMenu and GalaxyState primary vtables pass.
  Native then receives a fixed number of completed AS3 advances to publish the
  selected ID. Immediately around Set Course, arm only the exact Quick Select
  route-ownership byte that makes vanilla consume that selected ID; require the
  stock handler to clear it synchronously, and use byte-verified ID 94308 to
  restore it if not consumed.
  Set Course may be dispatched
  only after native itself names the captured system through the vanilla Set
  Course button, the native Quick Select cursor, or a unique galaxy highlight
  marker; the two weaker authorities additionally require the vanilla Set Course
  button to be present and visible. Never write, force, or infer that button's
  enabled state, and never synthesize cursor input.
  An ActionScript invocation is not proof that travel began; only the guarded,
  player-filtered `GravJumpEvent` stream provides jump acknowledgement.
  Do not accept a remote route while Cruise is active: expose disabled
  `EXIT CRUISE FIRST`, preserve the current destination, and never dispatch Back,
  Set Course, or Execute. Recheck inactivity before Execute in case state changes
  after acceptance. Keep a bounded post-Execute phase and
  accept only stock Starmap close as the UI acknowledgement; invocation success
  alone is not Execute success.
- After a remote system jump, keep the direct final-body path when exactly one
  matching HUD row exists. If a remote moon has no row, resolve its parent only
  from the live load-order PNDT/GNAM index: require one candidate in the same
  system with `parent == 0` and `planet == moon.parent`, then require exactly one
  planet HUD row. Keep the final moon in `g_destination`; the parent is a private
  identity and must never appear in public destination/status state. Activate
  stock Cruise once and dispatch only the retained final moon. Starfield may
  retain that request without publishing an exact parent lock; keep it pending
  without an arbitrary travel timeout while Cruise remains continuously active
  and system/world identity stays valid. Opening the Starmap pauses this driver;
  an accepted new destination replaces it through `SetDestination`. Never treat dispatch success as
  course success: require the unique final moon's exact `bIsCruiseTargetLock`.
  If Starfield publishes the exact parent lock, require that lock to end and a
  newer unique final-moon feed before accepting the final exact lock. Fail
  closed on ambiguity, missing rows/handshake timeouts, manual interruption, load, HUD
  replacement, landing/docking, or system mismatch. Do not clear the final mark
  merely because the parent course completed.
- After a remote station route settles in the retained system, allow a bounded
  window for its exact indexed physical REFR/base tuple to become live, then
  require native target assignment/readback. Independently revalidate the exact
  CELL-owned XMRK REFR and use it—not the physical station REFR—as the Cruise
  HUD-row, dispatch, and lock-readback identity. If the XMRK has one HUD row,
  keep the direct path. Otherwise join the CELL EDID to exactly one PNDT `DNAM`,
  walk only unique live same-system `GNAM` parents, and require an exact ancestor
  with exactly one HUD row as the first private waypoint. Retain the proven
  inward ancestry segment so only ordered exact intermediate locks are accepted.
  Keep the public destination as the physical station, activate stock Cruise
  once, dispatch only the retained XMRK, and allow the engine-owned travel phase
  to remain unbounded while Cruise/system/world identity stays valid. Require
  the XMRK's exact `bIsCruiseTargetLock`; ambiguity, identity drift, bounded
  handshake/feed timeout, unrelated course, interruption, load,
  landing/docking, or system mismatch clears it fail-closed.
- Ship POIs and generic non-planet POIs are hidden and remain vanilla-owned.
- `bShowMarker` gates only plugin marker rendering. Exact retained-target
  high-frequency distance sampling must continue while it is false because the
  independent lock-loss/close-distance arrival audit depends on that sample.
  `TargetHighFrequencyProvider.distance` is meters; convert the guarded `0.05`
  light-second threshold to meters before comparing it.
- SFSE permanent tasks are worker-thread producers only. Marshal ordinary
  engine work through `RE::BSService::TaskQueue`; enter Scaleform only from the
  byte-verified post-`UI_AdvanceActiveMenus` pump, when the owning main thread's
  AS3 advance has returned.
- Feed callbacks may read their passed GFx values and copy plain C++ state only.
  They must not fetch a movie root, construct/drive HUD objects, subscribe,
  dispatch course events, or otherwise re-enter AS3. The post-advance pump owns
  those mutations. Subscriptions still require movie/world settle gates; the
  Starmap additionally requires its visible `MenuOpenCloseEvent`, because
  `UI::IsMenuOpen` can expose its incomplete background movie.
- Track physical input by `(deviceType,idCode)`. Reset pending holds on release,
  focus loss, load, or Starmap movie replacement.
- Suppress a carried map key until physical release. Keyboard testing proved
  that its cockpit event is a continued hold (`first=false`), not a new press.
- A tap only marks. Queue its target id after the HUD reports a later vanilla
  inactive-to-active Cruise transition, or immediately when the map was opened
  while Cruise was already active. A completed map hold may drive the proven HUD
  down/held/up route; confirm every course lock from the low feed.
- Do not synthesize Cruise input until the OSF RE probe proves a complete
  engine-owned down/held/up injection route.
- Do not use the landed/docked CommonLibSF helpers for the active-flight gate.
  Active-flight state uses Address Library ID 63482 `IsInSpace(false)` only
  after the exact runtime and 1.16.244 prologue fingerprint both pass.
- No serialization, save forms, runtime caches/files, ESP, SWF, or V1 API.

## Runtime evidence

The companion investigation and probe live in:

- `../OSF RE/Investigations/Requests/2026-08-04-cruise-from-starmap-bridge.md`
- `../OSF RE/src/Probe/StarmapCruiseProbe.cpp`
- `../OSF RE/Investigations/Responses/2026-08-04-cruise-from-starmap-focus-discriminator.md`

Do not relax a gate from an unverified log inference. Record runtime results in
`docs/VALIDATION.md` and the owning OSF RE context module when proven.

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
  A station-backed non-planet row may be a live station reference or a CELL that
  resolves through placed children to exactly one currently live reference
  whose base carries `IsStarstation`. A Ship POI row may resolve only when its
  CELL contains exactly one live, in-space, non-station GBFM reference after
  excluding the player ship. Any non-planet marker in a browsed system other
  than the cockpit system is hidden before station/ship resolution rather than
  advertised as an unavailable Cruise target. Another non-planet row must match exactly one current
  cockpit target-feed row by the same ID. Keep the map id/type and resolved
  target reference separate. Unmatched generic POIs, tree focus, `bIsFocused`,
  timing heuristics, and last-dossier-wins are invalid.
- A remote planet/moon handoff must prove the captured STDT/DNAM system root
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
  context is attempted with cursor-independent stock seams only, one rung at a
  time with a fixed number of completed AS3 advances between rungs: the shipped
  Quick Select `bodyID` change event, then the shipped public `SetHoveredSystem`
  galaxy setter. Set Course may be dispatched
  only after native itself names the captured system through the vanilla Set
  Course button, the native Quick Select cursor, or a unique galaxy highlight
  marker; the two weaker authorities additionally require the vanilla Set Course
  button to be present and visible. Never write, force, or infer that button's
  enabled state, and never synthesize cursor input.
  An ActionScript invocation is not proof that travel began; only the guarded,
  player-filtered `GravJumpEvent` stream provides jump acknowledgement.
- Anything outside the currently loaded system's system view is vanilla-owned.
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

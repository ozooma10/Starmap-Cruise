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
  `bIsInHighlightRadius=true`, that row's id/type exactly matches the dossier
  planet/moon PNDT, and its GNAM/current-system identities agree. Tree focus,
  `bIsFocused`, timing heuristics, and last-dossier-wins are all invalid.
- Anything outside current-system planet/moon system view is vanilla-owned.
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
- A tap only marks. Queue its PNDT id after the HUD reports a later vanilla
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

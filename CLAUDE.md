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
- A selection is invalid unless a proven system-view focus discriminator selects
  exactly one dossier PNDT and its GNAM/current-system identities agree. The
  current build must fail closed because that discriminator is not yet proven;
  tree/marker/dossier equality and last-dossier-wins are both disproven.
- Anything outside current-system planet/moon system view is vanilla-owned.
- Construct/drive HUD objects and dispatch course events only from HUD feed
  callbacks. Per-frame work may subscribe only after movie/world settle gates.
- Track physical input by `(deviceType,idCode)`. Reset pending holds on release,
  focus loss, load, or Starmap movie replacement.
- Suppress a carried map key until physical release. Keyboard testing proved
  that its cockpit event is a continued hold (`first=false`), not a new press.
- Do not synthesize Cruise input until the OSF RE probe proves a complete
  engine-owned down/held/up injection route.
- Never call the ID-zero landed/docked CommonLibSF helpers. Active-flight state
  uses Address Library ID 63482 `IsInSpace(false)` only after the exact runtime
  and 1.16.244 prologue fingerprint both pass.
- No serialization, save forms, runtime caches/files, ESP, SWF, or V1 API.

## Runtime evidence

The companion investigation and probe live in:

- `../OSF RE/Investigations/Requests/2026-08-04-cruise-from-starmap-bridge.md`
- `../OSF RE/src/Probe/StarmapCruiseProbe.cpp`

Do not relax a gate from an unverified log inference. Record runtime results in
`docs/VALIDATION.md` and the owning OSF RE context module when proven.

# Release validation

Build and deployment evidence is recorded separately from live gameplay proof.
A successful compile, matching hash, or DLL load never validates a gameplay
flow.

The chronological investigation record is preserved in
[`history/VALIDATION-2026-08.md`](history/VALIDATION-2026-08.md). Raw production
captures remain under [`history/captures/`](history/captures/).

## Supported release scope

- [x] Starfield `1.16.244.0` runtime and fingerprint gate
- [x] Current-system planet and moon selection
- [x] Vanilla system-level `JUMP THEN CRUISE` for remote planets and moons
- [x] Parent-assisted remote-moon continuation with exact final lock readback
- [x] Indexed current-system and remote stations with separate physical and
  XMRK course identities
- [x] Keyboard/mouse and controller use the live stock Cruise binding and glyph
- [x] Ship POIs and generic non-station markers expose no plugin action
- [x] No ESP, Papyrus, serialization, save forms, SWF replacement, runtime
  cache, cockpit overlay, or public plugin API

## Current automated and static checks

- [x] Clean releasedbg configure and build
- [x] `git diff --check`
- [x] Release payload allowlist: DLL, default INI, custom-INI example, and PDB
- [x] Built and release-package DLL/PDB/INI hashes match
- [ ] Active MO2 payload matches the latest build (not deployed by release packaging)
- [x] Default and example INIs expose only `bVerboseLog`
- [x] Built DLL contains no retired cockpit-status, custom-marker, or `sMode`
  identifiers
- [x] Destination state retains no raw form or Scaleform pointer
- [x] Exact marker/dossier/body, STDT/DNAM system, route dwell, player grav-jump,
  settled-world, HUD-row, and course-lock readback gates remain present
- [x] Exact retained-target distance sampling remains independent of presentation
- [x] Arrival evidence, exact lock loss, and the consuming audit must share the
  current HUD movie generation; stale pre-replacement distance cannot clear a mark
- [x] Fatal post-advance exception handling clears guarded navigation, route,
  continuation, pending action, hold, and HUD-input state before latching off AS3
- [x] Conditional `AwaitingCruise` demotions use compare-exchange and cannot
  overwrite a newer `MapSelection` or `AutopilotLocked` publication
- [x] Destination replacement and clear share one dependent-state reset while
  only clear cancels the authoritative active remote-route request
- [x] Off-target `RecordReaderTests` fixtures cover `XXXX` extended subrecords,
  zlib-compressed bodies, full/medium/small master remapping, strict PNDT DNAM
  termination, live-form predicate rejection, and exact-versus-ambiguous
  CELL/XMRK station links
- [x] Release retired-string verification uses a built-in byte-preserving,
  identifier-boundary scan and cannot silently disappear when `rg` is
  unavailable; release archive naming derives from xmake's built DLL version
  resource instead of a second version literal

Latest verified artifacts after the repository cleanup:

- DLL: `178DC69FF330A01C2E51D4B603349D0D97EBD54A96FAFC84B8ED9E054B2DD616`
- PDB: `4F290F1ADC46A6981118253BA7F13ACF6DCB09C841EF9E65BB1954A13CE2B01B`
- INI: `86D9199F8472EF0282E65C0531502FDF77B30AC1B62DC83CC8585D498707AF1B`
- Main ZIP:
  `9BC36327CCFB0BC04D99FA6055EF93F72F85CFC9A0C21EFE6B496A3E7E309281`
- Symbols ZIP:
  `E59DA1AD1049B97B5CB2FA404214CC905E8AC668467F2ACB85AC3F731D7C7FFB`

The active MO2 DLL remains the prior build
`0278D1C4D8C4B723F7D0141A9F7EA45882898E28750F5CEBB2F4756108DAFA6C`.
Release packaging deliberately did not deploy the new artifact, and no current
gameplay claim is derived from the hashes above.

No winning `CruiseFromStarmapCustom.ini` override was found in the active mod,
MO2 overwrite, or Documents paths. The active default has `bVerboseLog=true`.

## Live evidence already established

- [x] Current-system tap, replacement, same-target clear, hold-to-Cruise, and
  exact planet/moon course readback
- [x] Cursor-independent vanilla system route construction and execution with
  player grav-jump states `0`, `1`, and `2`
- [x] Remote Neptune direct continuation after a vanilla Sol entry at Mars
- [x] Remote Triton parent-assisted continuation: vanilla system jump, Neptune
  exact lock, parent arrival/feed refresh, Triton exact lock
- [x] Same-system Ariel retained event: Starfield approached Uranus and later
  published the exact Ariel lock without a second dispatch
- [x] Remote Chawla latent continuation reached Chawla without requiring a
  published parent lock
- [x] Remote Starstation RE-939: vanilla jump, exact live physical assignment,
  XMRK course dispatch/readback, and physical arrival
- [x] Remote actions are disabled with `EXIT CRUISE FIRST` while Cruise is active
- [x] A late authoritative current-system resolution updates the same open map
  session without requiring close/reopen

## Simplification-campaign validation matrix

One fixed no-mouse matrix, reused at every simplification stage's go/no-go gate.
Run with `bVerboseLog=true` and preserve the matching `CruiseFromStarmap.log`
excerpt under `history/captures/` for each row exercised by a stage.

| # | Scenario | Reference trace |
|---|----------|-----------------|
| A | Remote planet, direct final-body row | Neptune |
| B | Remote moon, engine-latent completion (no staging) | Chawla |
| C | Remote moon, staged parent lock published | Triton |
| D | Remote station, direct XMRK row | — |
| E | Remote station, staged orbital ancestry | RE-939 |
| F | Manual Cruise exit mid-continuation fails closed | — |
| G | Remote selection while Cruise active shows `EXIT CRUISE FIRST` | — |
| H | Alt-tab during route build logs "timeout restarted" and completes | — |

A stage that touches the remote route driver must include rows A and H; a stage
that touches the continuation must include B, C, and F (plus D and E when
station identity paths change). Compile-only stages require a clean releasedbg
build and a startup-log diff against a captured baseline instead.

### Campaign stage status (2026-08-07)

- [x] Stage 1 (compile-only): dead write-only fields removed, six inline
  durations named, `TryInstallGlobalEventSink` factored, fail-pair sites
  unified through `FailActiveContinuationsOrRelease`,
  `RemoteStationContinuationActive` renamed `RemoteStationTargetAssigned`,
  fingerprint failures now log observed vs expected bytes, doc drift fixed.
  Clean releasedbg build; startup-log diff still required.
- [x] Follow-up structural verification: pure BodyIndex record decoding moved to
  `src/BodyIndex/RecordReader.*` behind a live-form predicate and passed its
  synthetic fixture suite; release scanning/version ownership no longer depends
  on the caller environment.
- [x] Stage 2: retired (2026-08-08). The `iGalaxyDiagnosticsMode` capture modes
  were removed without ever being run; the deep galaxy-focus member sweeps went
  with them. The failure-path diagnostic now logs the selection proof plus the
  menu-root member list only, and the QuickSelect feed-shape one-shot pin is
  retained. The old sweeps remain recoverable from git history.
- [x] Stage 3 (code): unreachable Quick Select cursor authority removed
  (feed-shape proof retained via the one-shot member log); one-rung
  `GalaxyFocusRung` ladder collapsed to `focusAttempted`/`focusReadbackPasses`.
  **Smoke required: rows A and G.**
- [x] Stage 4 (code): eight hand-rolled Driver commit blocks unified through
  `TryCommitRemoteRoutePhase` with unchanged verify-set semantics. The
  same-frame LiveGalaxyState pass-through was deliberately NOT taken: the
  mode-1 diagnostic dump can now enter AS3 between proof and arm, which
  invalidates its no-AS3-between premise; per-touch re-proof stays.
  (2026-08-08: mode 1 has since been removed, but per-touch re-proof is
  retained anyway — every raw GalaxyState write keeps a fresh vtable proof
  immediately before it.)
  **Smoke required: rows A and H.**
- [x] Stage 5a (code): `kAwaitingParentLock` and `kAwaitingLatentFinalLock`
  merged into unbounded `kTraveling` with a bounded `dispatchConfirmed`
  registration window; identical evidence gates, floors, and fail-closed
  reasons preserved (log strings reworded). **Smoke required: rows B, C, F
  before Stage 5b is attempted.**
- [ ] Stage 5b: fold `kParentLocked` into a `waypointLocked` flag and merge the
  two post-waypoint windows. Blocked on the Stage 5a smoke.
- [ ] Stage 6: record smoke results here and in the history file; re-run
  `tools/package-release.ps1` hash verification on the final build.

## Required release smoke

- [ ] Restart on the newly built DLL and run one no-mouse remote-moon flow from
  another system while Cruise is inactive
- [ ] Require the log to prove vanilla system jump, exact final or parent-assisted
  course transition, exact final-moon lock, and no fail-closed clear
- [ ] Continue to physical arrival and require exact prior lock plus converted
  same-HUD-generation close-distance evidence to clear the public destination
- [ ] Confirm that only vanilla cockpit target presentation appears

Do not mark this smoke complete from visual impression alone. Preserve the
matching `CruiseFromStarmap.log` excerpt or add a capture under
`history/captures/`.

## Additional compatibility matrix

- [ ] Current-system indexed station such as The Eye
- [ ] Controller tap and completed hold on the final release build
- [ ] Focus loss, loading, landing, docking, and pilot-seat exit fail closed
- [ ] Focus loss during an active remote-route build preserves `MapSelection`,
  restarts the route timeout on return, and does not trigger system-change cleanup
- [ ] HUD replacement during an armed ordinary arrival audit never consumes the
  prior movie's distance sample; the public mark remains available
- [ ] Encounter interruption preserves an ordinary mark
- [ ] Cruise Navigation Panel and other input-hook chaining
- [ ] Save made during use loads after uninstall

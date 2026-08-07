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
- [x] Built, active MO2, and release-package DLL/PDB/INI hashes match
- [x] Default and example INIs expose only `bVerboseLog`
- [x] Built DLL contains no retired cockpit-status, custom-marker, or `sMode`
  identifiers
- [x] Destination state retains no raw form or Scaleform pointer
- [x] Exact marker/dossier/body, STDT/DNAM system, route dwell, player grav-jump,
  settled-world, HUD-row, and course-lock readback gates remain present
- [x] Exact retained-target distance sampling remains independent of presentation

Latest verified artifacts after the repository cleanup:

- DLL: `C5CB36B12AA8FDF2F63C5485871DCA90D0E07B1A633FA2AED7E8AB99CCD9A3DB`
- PDB: `816B570C7D66CAA15F823653038EF1C461CBCC1C0D8A44E8AAE25F4AB6C81EDF`
- INI: `EC6DEC97ACA8723DE975C1EB987C8B1943BEADC7A3B534BFF7C92BB2404852BC`
- Main ZIP:
  `0DD5CEB3C02D0AE8CF194E2E17E27F37303BD909D4AFAD6BF5EC174C45A9E42A`
- Symbols ZIP:
  `28D65C188088BBF1756C32084A65EA32A4A0418DE1ED49D4903C30B246D4D41B`

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

## Required release smoke

- [ ] Restart on the newly built DLL and run one no-mouse remote-moon flow from
  another system while Cruise is inactive
- [ ] Require the log to prove vanilla system jump, exact final or parent-assisted
  course transition, exact final-moon lock, and no fail-closed clear
- [ ] Continue to physical arrival and require exact prior lock plus converted
  close-distance evidence to clear the public destination
- [ ] Confirm that only vanilla cockpit target presentation appears

Do not mark this smoke complete from visual impression alone. Preserve the
matching `CruiseFromStarmap.log` excerpt or add a capture under
`history/captures/`.

## Additional compatibility matrix

- [ ] Current-system indexed station such as The Eye
- [ ] Controller tap and completed hold on the final release build
- [ ] Focus loss, loading, landing, docking, and pilot-seat exit fail closed
- [ ] Encounter interruption preserves an ordinary mark
- [ ] Cruise Navigation Panel and other input-hook chaining
- [ ] Save made during use loads after uninstall

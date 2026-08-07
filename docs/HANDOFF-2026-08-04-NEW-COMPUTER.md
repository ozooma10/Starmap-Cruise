# Cruise From Starmap — new-computer handoff

Prepared: 2026-08-04  
Game/runtime target: Starfield 1.16.244.0 (Steam)  
Plugin version: 0.1.0  
Immediate objective: finish the OSF RE system-view focus-discriminator investigation, then
implement only what the new runtime evidence proves.

## Read this first

The project is buildable and crash-remediated, but it is **not gameplay-complete**. The
production plugin deliberately fails closed because no stable field has yet been proven to
identify the one planet or moon selected in the system Starmap. Do not replace this with
tree/marker/dossier equality, callback recency, or "last dossier wins"; all three premises are
disproven or insufficient.

The next computer needs two sibling repositories:

```text
<workspace>/
  CruiseFromStarmap/
  OSF RE/
    lib/commonlibsf/
```

`CruiseFromStarmap/xmake.lua` intentionally includes
`../OSF RE/lib/commonlibsf`, so that relative layout matters.

## Source snapshot

These revisions reproduce the handoff state:

| Repository | Branch | Revision | Remote | State at handoff |
|---|---|---|---|---|
| CruiseFromStarmap | `main` | `3522b5364f42514329769ef682b2984e8ed9ed24` | `git@github.com:ozooma10/cruise-from-starmap.git` | Clean and equal to `origin/main` before this handoff file was added |
| OSF RE | `master` | `07fec5bd31219a76d3b8aa9b21ebdf4c8bc8305e` | `https://github.com/ozooma10/osf-re.git` | Cruise investigation is tracked and pushed; unrelated NPC-appearance files are dirty locally |
| CommonLibSF submodule | gitlink / local branch `forge` | `12b5d4e620dd3a83455b38818491948c8725fcdb` | `git@github.com:ozooma10/commonlibsf.git` | Clean; OSF RE gitlink pins this revision |

The Cruise-specific OSF RE request, probe, completed bridge response, and context module entered
OSF RE in commit `12fad9b` and are all contained by the OSF RE revision above. A clean clone does
not lose them.

At the time this file was written, the dirty OSF RE files were for the unrelated
`NpcAppearanceLoader` investigation:

```text
M  Investigations/INDEX.md
?? Investigations/HANDOFF-2026-08-04-npc-appearance-loader.md
?? Investigations/Responses/2026-08-04-npc-appearance-loader.md
?? tools/ghidra/context_repo/modules/gameplay.npc_appearance_loader.json
```

They are not required by Cruise From Starmap. Preserve them separately if that other work must
move too; do not discard or overwrite them while preparing this transfer.

This handoff document itself is a new production-repository change. Commit and push it, or copy it
separately, before retiring the old computer.

## What is source and what is not

Source of truth:

- the two Git repositories and the pinned CommonLibSF submodule;
- the Markdown evidence and validation files listed below;
- the shipped default/example INIs in `CruiseFromStarmap`.

Regenerable state—do not rely on it as the only copy:

- either repository's `build/` directory;
- `MO2/mods/CruiseFromStarmap` and `MO2/mods/OSF RE` deploy payloads;
- xmake's package/build cache;
- generated DLLs and PDBs.

Machine/user state worth backing up separately if needed:

- MO2 instance configuration, profiles, `modlist.txt`, and downloaded mod archives;
- Starfield saves needed to reproduce an actively-flying ship state;
- a personal `CruiseFromStarmapCustom.ini`, if one is later created;
- SFSE logs and crash dumps that have not yet been summarized into evidence;
- Git/GitHub credentials or a replacement authentication setup;
- Ghidra projects and other non-Git RE databases, if future static work depends on them.

There was no live `CruiseFromStarmapCustom.ini` in the deploy folder at handoff. The plugin never
creates one. The example remains in Git.

## New-machine prerequisites

Install or restore:

1. Windows 11 and Steam Starfield **1.16.244.0**.
2. The matching SFSE build.
3. Address Library v21 containing `versionlib-1-16-244-0.bin`. The enclosing mod folder may still
   carry `1.16.242.0` in its name; verify the actual versionlib filename/content.
4. Mod Organizer 2 configured to launch `Starfield (SFSE)`.
5. Git, including GitHub authentication if changes will be pushed.
6. xmake 3.0.0 or newer.
7. A Visual Studio/MSVC x64 C++ toolchain with C++23 support.
8. Internet access for xmake to acquire zlib on the first configure, unless its package cache is
   transferred.

Do not copy `Starfield.exe` between computers as the versioning strategy. Install through Steam,
verify the executable version, and let the plugin's exact runtime/fingerprint guards reject any
mismatch.

## Clone and restore the repositories

With GitHub SSH configured:

```powershell
New-Item -ItemType Directory -Path 'C:\Modding\Starfield' -Force
Set-Location 'C:\Modding\Starfield'

git clone git@github.com:ozooma10/cruise-from-starmap.git CruiseFromStarmap
git clone --recurse-submodules https://github.com/ozooma10/osf-re.git 'OSF RE'
```

Because OSF RE's submodule URL uses SSH, an HTTPS-only setup can initialize it explicitly:

```powershell
Set-Location 'C:\Modding\Starfield\OSF RE'
git -c submodule.lib/commonlibsf.url=https://github.com/ozooma10/commonlibsf.git `
    submodule update --init --recursive
```

Verify the revisions:

```powershell
git -C 'C:\Modding\Starfield\CruiseFromStarmap' rev-parse HEAD
git -C 'C:\Modding\Starfield\OSF RE' rev-parse HEAD
git -C 'C:\Modding\Starfield\OSF RE\lib\commonlibsf' rev-parse HEAD
```

If the remotes have advanced, normally continue from the newer reviewed commits. To reproduce the
exact handoff, use the revisions in the source table. Do not move the CommonLibSF submodule to a
newer branch tip merely because one exists; the pinned gitlink is the validated dependency.

## Environment and directory setup

The paths may change, but preserve the sibling repository layout and set the new MO2 mods path:

```powershell
setx XSE_SF_MODS_PATH 'C:\Modding\Starfield\MO2\mods'
```

Open a new shell after `setx`. Leave `XSE_SF_GAME_PATH` unset so builds deploy through MO2 rather
than directly into the game's `Data` directory. `XSE_SF_MO2_PATH` is optional when MO2 is already
the sibling `../MO2`; otherwise set it or pass `-MO2Path` to the OSF RE launcher.

Never hardcode the Documents directory. Resolve logs and the command channel this way:

```powershell
$docs = [Environment]::GetFolderPath('MyDocuments')
$sfseLogs = Join-Path $docs 'My Games\Starfield\SFSE\Logs'
$sandbox = Join-Path $docs 'My Games\Starfield\SFSE\Sandbox'
```

## Fresh-machine preflight

Run the OSF RE preflight before trusting any runtime result:

```powershell
Set-Location 'C:\Modding\Starfield\OSF RE'
pwsh -File .\tools\dev\Check-REEnv.ps1
```

`OSF RE/BOOTSTRAP.md` is the full fresh-machine setup authority if this check finds missing Python,
Ghidra, Address Library, game, or path components.

## Build and deploy

### Production plugin

```powershell
Set-Location 'C:\Modding\Starfield\CruiseFromStarmap'
xmake f -m releasedbg -y
xmake -y
```

Expected deploy directory:

```text
<XSE_SF_MODS_PATH>/CruiseFromStarmap/SFSE/Plugins/
  CruiseFromStarmap.dll
  CruiseFromStarmap.pdb
  CruiseFromStarmap.ini
  CruiseFromStarmapCustom.ini.example
```

Optional package-layout check:

```powershell
xmake install -o release/Data -y
```

The old-machine releasedbg DLL was 684,544 bytes with SHA-256
`3224209CEE9324DD90A91CA1426A54B692672AFCED3BF0C26EF9710218B6F863`.
This is only a reference; a different compiler/toolchain may produce a different binary from the
same source.

### OSF RE probe

```powershell
Set-Location 'C:\Modding\Starfield\OSF RE'
xmake f -m releasedbg -y
xmake build -y
```

Expected deploy directory:

```text
<XSE_SF_MODS_PATH>/OSF RE/SFSE/Plugins/
  OSF RE.dll
  OSF RE.pdb
```

The Cruise focus trace is opt-in and quiet until `stcruise focus start` is issued.

## Historical production behavior

This section records the 2026-08-04 build and is superseded by `README.md` and
`docs/ARCHITECTURE.md`. Its retired mode and custom HUD-marker configuration are
intentionally omitted.

The 0.1.0 plugin is standalone: no ESP, Papyrus, serialization, save forms, SWF replacement,
runtime-created files, Cruise Navigation Panel dependency, or public inter-plugin API.

The intended flow is current-system planets/moons only. Stations, POIs, ships, other systems,
surface/galaxy/inspect views, grav-jump routes, and native ship target assignment remain deferred.

The source compiles all later marker/course machinery, but map input remains vanilla because
`ResolveMapSelection()` rejects the unresolved focus discriminator. Therefore a successful build
does not mean a tap marks a body yet.

## Proven findings already implemented

1. **Crash blocker removed.** Production never calls CommonLibSF's ID-zero
   `IsSpaceshipLanded()` or `IsSpaceshipDocked()`. It uses Address Library ID 63482
   `IsInSpace(false)` only after the exact 1.16.244 runtime, Starfield image range, RVA, and 16-byte
   prologue fingerprint pass. Initialization aborts before hooks on failure.
2. **Selection join corrected.** In system view, the tree identifies the system/star, the focused
   marker may be zero, and the dossier carries real PNDT bodies. Tree/marker/dossier equality was
   removed. The dossier still emits multiple bodies, so it is validated but rejected until a
   stable focus discriminator exists.
3. **Current-system resolver retained.** The cockpit HUD PNDT/GNAM unique-maximum resolver was
   proven across Alpha Centauri to Sol. GNAM system `00000000` is valid, so presence is represented
   independently from the numeric system value.
4. **Cruise course event proven.** `Reticle_OnCruiseLockCourse {uBodyID}` plus HUD-low
   `bIsCruiseTargetLock` readback locked, cleared, and retargeted planets and a moon.
5. **Arrival signal constrained.** `SpaceCruiseArrival` also fires on deliberate Cruise exit and
   is not arrival proof. Production has no direct clearing hook for it; an arrival audit requires
   prior lock transition plus close-distance evidence.
6. **Keyboard hold behavior proven.** The physical device/id survives map close, but the cockpit
   event is a continued hold (`first=false`) and vanilla Cruise does not start. Carried input is
   suppressed until release. No synthetic replay exists.
7. **Controller remains untested.** No controller was available during the completed capture.

## Remaining blockers

The immediate blocker is a stable, generation-safe system-view value/path that uniquely selects
one dossier PNDT at the first `SetRouteDestination` down-edge.

Also unresolved or unaccepted:

- safe synthetic Cruise input replay (pool, binding, queue, and threading ownership unproven);
- controller behavior;
- every production gameplay-acceptance row in `docs/VALIDATION.md`;
- manual-exit and encounter-interruption mark preservation in a production-enabled capture;
- production fail-closed startup guard capture on the new machine;
- final arrival behavior.

Do not implement around these blockers by guessing.

## Continue the focus-discriminator investigation

Primary request:

```text
OSF RE/Investigations/Requests/2026-08-04-cruise-from-starmap-focus-discriminator.md
```

Use **Lane A** because a person must identify the visibly selected body. Disable
`CruiseFromStarmap` for this probe-only session so production input handling cannot contaminate the
capture. Do not change an MO2 profile silently; record which mods are enabled for the session.

Build/deploy OSF RE, then launch through MO2:

```powershell
Set-Location 'C:\Modding\Starfield\OSF RE'
pwsh -ExecutionPolicy Bypass -File .\tools\dev\Start-StarfieldDebug.ps1 `
    -ForceRestart -SkipAttach -Mode releasedbg
```

This flag intentionally stops a running Starfield process. Announce that before using it if a
session may be active.

The SFSE log is:

```powershell
$logPath = Join-Path ([Environment]::GetFolderPath('MyDocuments')) `
    'My Games\Starfield\SFSE\Logs\starfield-re-sandbox.log'
```

Start the trace through the CommandFile channel:

```text
stcruise focus start
```

Before every positive or negative control, set an operator-ground-truth label:

```text
stcruise focus label gagarin-planet
```

For positive controls, dwell on the named visible body for two seconds and press the ordinary Set
Route/Cruise action once. Capture at least:

1. Gagarin or another planet;
2. Kurtz, Phobos, or another moon;
3. a second landable body with differing known fields;
4. the same body after moving away and back;
5. a rapid A-to-B cursor move followed immediately by Set Route on B.

Capture labelled negative controls for the system/star, empty space if actionable, a POI/station if
available, inspect view, galaxy view, and cancel without Set Route. Reopen/rebuild the Starmap and
repeat one positive to test generation isolation.

Useful commands:

```text
stcruise focus capture
stcruise focus status
stcruise focus stop
```

The trace records every marker row, provider sequences and ages, physical device/id, the exact
input edge, GNAM/current-system evidence, and bounded AS3 focus-related state. It does not suppress
input or mutate the course.

The game pauses when unfocused, although the CommandFile poll thread remains available. Re-focus
when a frame-driven probe stalls:

```powershell
(New-Object -ComObject WScript.Shell).AppActivate((Get-Process Starfield).Id)
```

## Evidence acceptance and writeback

A focus candidate is acceptable only if it:

- equals the operator-labelled PNDT for every planet, moon, repeat, and rapid A-to-B positive;
- rejects every invalid-view/negative control;
- does not depend only on the newest callback or dossier emission;
- joins to a live PNDT with parsed GNAM in the captured current system;
- does not cross menu session or movie generation boundaries;
- has a named source/field/path, type, update timing, ownership, and lifetime.

After a qualifying runtime capture, create:

```text
OSF RE/Investigations/Responses/2026-08-04-cruise-from-starmap-focus-discriminator.md
```

Then update:

- `OSF RE/Investigations/INDEX.md`;
- `OSF RE/tools/ghidra/context_repo/modules/ui.starmap_cruise.json`, but only for proven facts;
- `CruiseFromStarmap/docs/VALIDATION.md`;
- production `ResolveMapSelection()` with the smallest proven discriminator and all existing gates.

Do not edit the completed bridge evidence:

```text
OSF RE/Investigations/Responses/2026-08-04-cruise-from-starmap-bridge.md
```

If no candidate passes, write a **BLOCKED** response and leave production fail-closed.

## Important files

Production:

- `CLAUDE.md` — developer invariants and build commands.
- `README.md` — user-facing scope and current fail-closed behavior.
- `docs/ARCHITECTURE.md` — data path, state machine, threading, and held-key fallback.
- `docs/VALIDATION.md` — authoritative build/proof/gameplay matrix.
- `src/Bridge.cpp` — runtime guard, feeds, selection gate, input state, marker, and course logic.
- `src/BodyIndex.cpp` — active-load-order PNDT/GNAM parser.
- `src/Types.h` — pointer-free destination value types.
- `CruiseFromStarmap.ini` and `CruiseFromStarmapCustom.ini.example` — configuration.

OSF RE:

- `CLAUDE.md`, `BOOTSTRAP.md`, and `Investigations/RE_PLAYBOOK.md` — operating rules.
- `Investigations/Requests/2026-08-04-cruise-from-starmap-bridge.md` — original bridge request.
- `Investigations/Responses/2026-08-04-cruise-from-starmap-bridge.md` — completed evidence.
- `Investigations/Requests/2026-08-04-cruise-from-starmap-focus-discriminator.md` — current request.
- `src/Probe/StarmapCruiseProbe.cpp` — opt-in focus trace and prior bridge probe.
- `tools/ghidra/context_repo/modules/ui.starmap_cruise.json` — canonical proven RE context.

## Production safety invariants

- Never retain raw `TESForm*` or `GFx::Value` state across menu/movie transitions.
- Never call a `REL::ID(0)` helper.
- Never consume invalid or ambiguous Starmap input; vanilla must receive it untouched.
- Treat tree, marker, and dossier as different data roles.
- Track current-system presence separately from system value zero.
- Construct/drive HUD objects and dispatch UI events only from owning HUD callbacks.
- Track held input by physical `(deviceType,idCode)` and clear it on release, focus loss, load, or
  movie replacement.
- Do not synthesize Cruise input without a fully proven engine-owned route.
- Do not use `SpaceCruiseArrival` alone to clear a destination.
- Add no serialization, save form, runtime cache, ESP, SWF replacement, or V1 public API.

## Old-computer departure checklist

- [ ] Commit and push this handoff document, or copy it outside the machine.
- [ ] Confirm `CruiseFromStarmap` source is pushed and record its new HEAD if this file is committed.
- [ ] Confirm the Cruise-specific OSF RE files remain present on `origin/master`.
- [ ] Separately commit/copy/discard-by-explicit-choice the unrelated dirty NPC-appearance work.
- [ ] Back up any needed MO2 profiles, downloads, save games, custom INI, unsummarized logs, and
      crash dumps.
- [ ] Export or recreate GitHub authentication on the new machine.
- [ ] Do not use MO2 deploy folders or `build/` as the only backup.

## New-computer completion checklist

- [ ] Exact repository/submodule revisions verified.
- [ ] Starfield, SFSE, and Address Library versions verified.
- [ ] `Check-REEnv.ps1` passes the required checks.
- [ ] Production clean releasedbg build and package layout pass.
- [ ] OSF RE releasedbg probe build/deploy passes.
- [ ] Production startup log proves the ID 63482 fingerprint guard passes—or fails closed safely.
- [ ] A fresh Lane A focus-discriminator capture is collected with labelled positive and negative
      controls.
- [ ] No gameplay row is marked without production runtime logs/readback.


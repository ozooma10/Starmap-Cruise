# Cruise From Starmap

Cruise From Starmap adds a separate Cruise action to Starfield's in-flight
system map. Select a supported planet, moon, or station and use the same key or
controller binding as the stock cockpit Cruise action.

The plugin uses vanilla Cruise and vanilla grav-jump routing. It does not add a
cockpit overlay, replace a SWF, construct routes, or write route/cursor state.

## Features

- Tap a current-system target to remember it and return to the cockpit.
- Complete the Starmap hold to enter stock Cruise and lock the selected target.
- Retarget a current-system course while Cruise is already active.
- Use `JUMP THEN CRUISE` for an eligible target in another system. Vanilla owns
  route construction, fuel/range checks, every grav-jump leg, and execution.
- Continue safely to remote moons that Starfield initially exposes only through
  their parent planet.
- Target indexed current-system and remote stations through their exact physical
  and Cruise-course identities.
- Preserve the final public destination while an internal planet or orbital
  waypoint is used.
- Require matching cockpit `bIsCruiseTargetLock` readback before reporting a
  course as successful.
- Clear a completed destination only after exact prior-lock and close-distance
  arrival evidence agree.

## Requirements

- Starfield `1.16.244.0` for Steam
- Matching SFSE
- Address Library v21 with `versionlib-1-16-244-0.bin`

This release is exact-version guarded and fails closed on a different runtime or
unexpected native bytes/vtables.

## Installation

Install through a mod manager or extract the archive's `Data` folder into the
game data directory. The active payload should contain:

```text
Data/
  SFSE/
    Plugins/
      CruiseFromStarmap.dll
      CruiseFromStarmap.ini
      CruiseFromStarmapCustom.ini.example
```

The development-symbol PDB may be distributed separately and is not required to
play.

## Usage

1. Be seated in the pilot seat, flying in space, with the system view open.
2. Highlight a supported destination.
3. Use the control carrying the player's live Cruise binding and native glyph.

For a current-system target:

- Tap/release `SET CRUISE TARGET` to mark it and return to the cockpit.
- Complete `HOLD TO CRUISE` to mark it and start stock Cruise immediately.
- Selecting the same marked target again clears it.

For a remote target:

- Tap `JUMP THEN CRUISE` once.
- Vanilla returns to galaxy view, builds and executes the matching system route,
  and performs the grav jump.
- After settled arrival, the plugin activates stock Cruise and exact-locks the
  retained final destination.

Remote actions are disabled with `EXIT CRUISE FIRST` while Cruise is active.
Exit Cruise in the cockpit, reopen the map, and start the remote action normally.

Remote moons and stations may visibly approach a parent planet or orbital body
before Starfield publishes the final exact course. This is engine-owned travel;
there is no arbitrary travel-duration timeout while Cruise, system, and world
identity remain valid.

## Supported targets and limitations

Supported:

- Planets and moons in the current or another system
- Indexed stations in the current or another system

Intentionally unsupported:

- Ship POIs
- Generic non-station markers
- Surface, inspect, galaxy, on-foot, landed, and docked selections
- Remote routing while Cruise is already active

The plugin accepts only one exact highlighted map row joined to live body or
station identity. Missing, ambiguous, mismatched, unavailable, or invalid-view
targets expose no active plugin action and remain vanilla-owned.

Destination state is process-local. Loading a save, landing, docking, leaving
the pilot seat, changing system outside the guarded route, or replacing the
destination clears or replaces it fail-closed. The plugin creates no save data.

## Configuration

The shipped default contains only diagnostic logging:

```ini
[General]
bVerboseLog=true
```

For personal changes, copy `CruiseFromStarmapCustom.ini.example` to
`CruiseFromStarmapCustom.ini` beside the default and include only overrides. The
custom file wins and is not replaced by updates.

Logs are written by SFSE to `CruiseFromStarmap.log`.

## Upgrade and uninstall

To upgrade, replace the DLL and shipped default/example INIs while preserving
your optional `CruiseFromStarmapCustom.ini`.

To uninstall, remove the CruiseFromStarmap DLL, PDB if present, default INI,
custom example, and personal custom INI from `Data/SFSE/Plugins`. No save cleanup
is required because the plugin has no ESP, scripts, serialization, or save forms.

## Compatibility

Cruise From Starmap has no Cruise Navigation Panel dependency and replaces no
SWF. Plugins that hook the same input or Scaleform paths still require an
in-game compatibility test; the current release matrix does not claim every
hook-chain combination.

## Building

The workspace uses xmake, C++23, zlib, and CommonLibSF. Set
`COMMONLIBSF_PATH` to a CommonLibSF checkout, or use the local development
fallback at `../OSF RE/lib/commonlibsf`:

```powershell
xmake f -m releasedbg -y
xmake -y
```

With `XSE_SF_MODS_PATH` set, the target deploys to
`<mods>/CruiseFromStarmap/SFSE/Plugins`. Create and verify a release archive with:

```powershell
pwsh -NoProfile -File .\tools\package-release.ps1
```

## Documentation

- [Architecture and safety invariants](docs/ARCHITECTURE.md)
- [Current release validation](docs/VALIDATION.md)
- [Historical investigation record](docs/history/VALIDATION-2026-08.md)
- [Changelog](CHANGELOG.md)

## License

Cruise From Starmap is licensed under GPL-3.0-or-later. See [LICENSE](LICENSE).

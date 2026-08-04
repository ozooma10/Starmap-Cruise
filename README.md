# Cruise From Starmap

The intended feature is to highlight a current-system planet or moon, press the
existing Set Route / Cruise control, and return to the cockpit with that body
marked. The current 0.1.0 safety build deliberately leaves that input to vanilla:
live 1.16.244 evidence proved the dossier PNDT source, but not the stable focus
discriminator needed to distinguish the selected dossier emission from other
bodies emitted while browsing.

This is a standalone native SFSE plugin. It has no ESP, scripts, save data,
serialization, SWF replacement, Cruise Navigation Panel dependency, or public
inter-plugin API.

## Requirements and scope

- Starfield 1.16.244.0 (Steam)
- Matching SFSE
- Address Library v21 (`versionlib-1-16-244-0.bin`)
- Planets and moons in the currently loaded system only

Stations, POIs, ships, another system, surface/galaxy views, grav-jump route
integration, and native ship target assignment are intentionally rejected in
0.1.0. Rejected input is left completely untouched for vanilla.

## Modes

- `MarkOnly` (default): tap to mark only once selection focus is proven. The
  carried physical key is suppressed until
  release after the map closes, so it cannot accidentally start/toggle Cruise.
- `HoldToCruise`: retained for the already-cruising retarget path, but it cannot
  start Cruise from one carried keyboard hold. The remapped cockpit event is a
  continued hold (`first=false`), so it is suppressed until release. No
  synthetic input is generated.

Selecting the marked body again clears the mark. It only toggles an existing
autopilot lock off if the cockpit feed confirms that the same PNDT id is the
active `bIsCruiseTargetLock` body.

## Safety gates

A Starmap press may be consumed only when all of these gates pass:

1. The map was opened while the player was actively flying (not landed or
   docked).
2. `StarMapMenuData.iCurrentMenuView` is the system view.
3. A proven system-view focus discriminator selects exactly one dossier
   `BT_PLANET` or `BT_MOON`. The tree identifies the system/star and the marker
   may be zero, so neither participates in body identity.
4. The dossier id resolves to a live PNDT form and to a GNAM tuple parsed from the
   active load order.
5. GNAM's system id equals the current system derived from the cockpit target
   feed before the map opened.

Gate 3 is not yet proven, so this build fails closed after validating the
dossier candidate and logs the blocker without consuming the input. It never
uses the last dossier emission as the selection.

No raw `TESForm*` survives a menu transition. Destination state is a value
containing only kind, runtime form id, GNAM identity, localized name, and menu
generation.

## Configuration

Edit `Data/SFSE/Plugins/CruiseFromStarmapCustom.ini`, not the shipped default:

```ini
[General]
sMode=MarkOnly
bShowMarker=true
bShowDestinationName=true
bVerboseLog=true
```

The custom file overrides the default and is never created or written by the
plugin.

## Build

This workspace build uses the clean shared CommonLibSF mirror under
`../OSF RE/lib/commonlibsf` and zlib through xmake:

```powershell
xmake f -m releasedbg -y
xmake -y
```

With `XSE_SF_MODS_PATH` set, the build deploys the DLL, PDB, default INI, and
custom-INI example to
`<mods>/CruiseFromStarmap/SFSE/Plugins`. It never deploys an ESP or writes a
body cache.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the state and threading
model and [docs/VALIDATION.md](docs/VALIDATION.md) for the acceptance matrix.

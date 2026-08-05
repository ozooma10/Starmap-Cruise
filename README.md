# Cruise From Starmap

Highlight a current-system planet or moon and use the separate **Set Cruise
Target** action. Tap/release to remember the body and return to the cockpit, or
complete its Starmap hold fill to enter vanilla Cruise immediately and lock that
body. The key may be released once the fill completes. In flight system view,
the action remains visible but disabled with a short reason when the highlighted
marker cannot be accepted. A fixed cockpit label confirms the marked body even
when its optional positional marker is unavailable. Live 1.16.244 evidence
identifies the selection as the one
`StarMapMenuMarkersData` row whose `bIsInHighlightRadius` value is true, joined
exactly to the dossier PNDT identity. Zero, multiple, mismatched, or invalid-view
candidates remain entirely vanilla-owned.

This is a standalone native SFSE plugin. It has no ESP, scripts, save data,
serialization, SWF replacement, Cruise Navigation Panel dependency, or public
inter-plugin API.

## Requirements and scope

- Starfield 1.16.244.0 (Steam)
- Matching SFSE
- Address Library v21 (`versionlib-1-16-244-0.bin`)
- Planets and moons in the currently loaded system only
- Keyboard/mouse for the separate Starmap action; controller UI remains pending

Stations, POIs, ships, another system, surface/galaxy views, grav-jump route
integration, and native ship target assignment are intentionally rejected in
0.1.0. Rejected input is left completely untouched for vanilla.

## Interaction

- Tap/release marks the body and returns to the cockpit. Starting Cruise normally
  afterward locks that marked body.
- Completing the Starmap hold fill marks the body, closes the map, and latches a
  held state through the stock `SpaceshipHudMenu.ProcessUserEvent` path. Vanilla
  owns the cockpit threshold; the latch releases when Cruise becomes active or
  after a four-second safety limit.
- The cockpit shows `CRUISE TARGET: <name>` while a mark exists. It changes to
  `LOCKING CRUISE TARGET` and `CRUISE LOCK` as engine readback advances.

`TapHoldCruise` is the only exposed flow. Older `SelectThenCruise`, `MarkOnly`,
and `HoldToCruise` custom values are read for compatibility, warn at startup,
and use `TapHoldCruise` behavior. If Cruise is already active when the map opens,
an accepted selection retargets it immediately. The custom blue marker remains
optional and is not the ship target or Cruise itself.

Selecting the marked body again clears the mark. It only toggles an existing
autopilot lock off if the cockpit feed confirms that the same PNDT id is the
active `bIsCruiseTargetLock` body.

## Safety gates

A Starmap press may be consumed only when all of these gates pass:

1. The map was opened while the player was actively flying (not landed or
   docked).
2. `StarMapMenuData.iCurrentMenuView` is the system view.
3. Exactly one marker row has `bIsInHighlightRadius=true`, is a planet or moon,
   and its id/type exactly matches the dossier PNDT candidate. The tree still
   identifies the system/star and does not participate in body identity.
4. The dossier id resolves to a live PNDT form and to a GNAM tuple parsed from the
   full, medium, or light tier of the active load order.
5. GNAM's system id equals the current system derived from the cockpit target
   feed before the map opened.

Gate 3 was proven with planet, moon, revisit, rapid-switch, invalid-view,
station, empty-space, and movie-reopen controls. The build still fails closed
without exact marker/dossier agreement and never uses the last dossier emission
as the selection.

No raw `TESForm*` survives a menu transition. Destination state is a value
containing only kind, runtime form id, GNAM identity, localized name, and menu
generation.

## Configuration

Edit `Data/SFSE/Plugins/CruiseFromStarmapCustom.ini`, not the shipped default:

```ini
[General]
sMode=TapHoldCruise
bShowMarker=false
bShowDestinationName=true
bShowTargetStatus=true
bVerboseLog=true
```

The custom file overrides the default and is never created or written by the
plugin. The separate Starmap action follows the primary keyboard binding for the
game's `Cruise` action (`T` by default). The binding is cached at startup;
restart Starfield after changing it in the Controls menu.

## Build

This workspace build uses the clean shared CommonLibSF mirror under
`../OSF RE/lib/commonlibsf` and zlib through xmake:

```powershell
xmake f -m releasedbg -y
xmake -y
```

With `XSE_SF_MODS_PATH` set, the build deploys the DLL, PDB, default INI, and
custom-INI example to `<mods>/CruiseFromStarmap/SFSE/Plugins`. It never deploys
an ESP, replaces a SWF, or writes a body cache. While an active-flight system
view is open, the plugin adds the Starmap's stock
`ReleaseHoldComboButton`: `SET CRUISE TARGET` on top and `HOLD TO CRUISE`
below. It is enabled only while the exact planet/moon gate passes; otherwise its
disabled label explains the rejection, such as `STATIONS ARE NOT SUPPORTED` or
`HIGHLIGHT A PLANET OR MOON`. This separate action uses the player's `Cruise` keyboard binding;
the vanilla `SET COURSE` action and its binding remain unchanged. The added
button is created once per Starmap movie and hidden outside flight system view. The binding
is cached at plugin startup to keep `ControlMap` scanning off the map-open frame;
restart Starfield after changing the Cruise key.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the state and threading
model and [docs/VALIDATION.md](docs/VALIDATION.md) for the acceptance matrix.

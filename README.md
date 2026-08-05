# Cruise From Starmap

Highlight a current-system destination—including a planet, moon, station, or
other addressable non-planet marker—and use the separate **Set Cruise Target** action.
Tap/release to remember the target and return to the cockpit, or complete its
Starmap hold fill to enter vanilla Cruise immediately and lock that target. The
key may be released once the fill completes. In flight system view, the action
remains visible but disabled with a short reason when the highlighted marker
cannot be accepted. A fixed cockpit label confirms the marked target even when
its optional positional marker is unavailable. Live 1.16.244 evidence identifies
the selection as the one `StarMapMenuMarkersData` row whose
`bIsInHighlightRadius` value is true. Planet and moon rows are joined exactly to
the dossier PNDT identity. A station marker may identify either its live
reference or its map cell; the active-plugin index resolves the latter to one
persistent reference whose ship base carries `IsStarstation`. Other non-planet
markers must match exactly one row in the current cockpit target feed. Zero,
multiple, mismatched, or invalid-view candidates remain entirely vanilla-owned.

This is a standalone native SFSE plugin. It has no ESP, scripts, save data,
serialization, SWF replacement, Cruise Navigation Panel dependency, or public
inter-plugin API.

## Requirements and scope

- Starfield 1.16.244.0 (Steam)
- Matching SFSE
- Address Library v21 (`versionlib-1-16-244-0.bin`)
- Highlighted planets, moons, stations, and addressable non-planet markers in the
  currently loaded system
- Keyboard/mouse for the separate Starmap action; controller UI remains pending

Another system, surface/galaxy views, and grav-jump route integration remain
outside this plugin's scope. A station marker must be a live station reference
or a cell that resolves to exactly one persistent live station reference. On map
close, that reference becomes the native ship target before Cruise is requested.
Another non-planet marker must already expose exactly one matching current HUD
target row. Targets the HUD/Cruise system itself cannot lock remain marked rather
than being reported as confirmed.

## Interaction

- Tap/release marks the target and returns to the cockpit. Starting Cruise
  normally afterward locks that marked target.
- When the cockpit's stock Cruise action is currently available, completing the
  Starmap hold fill marks the target, closes the map, and latches a held state
  through the stock `SpaceshipHudMenu.ProcessUserEvent` path. Vanilla owns the
  cockpit threshold; the latch releases when Cruise becomes active or after a
  four-second safety limit.
- If Cruise was already active when the map opened, the Starmap instead shows one
  `SET CRUISE TARGET` action. A tap closes the map and queues the selected target
  as the new course; there is no hold action or `HOLD TO CRUISE` affordance.
- If Cruise is inactive but the stock cockpit action is temporarily unavailable,
  such as its short post-exit cooldown, the same tap-only action marks the target
  without advertising or attempting a hold-to-engage action.
- The cockpit shows `CRUISE TARGET: <name>` while a mark exists. It changes to
  `LOCKING CRUISE TARGET` and `CRUISE LOCK` as engine readback advances.

`TapHoldCruise` is the only exposed flow. Older `SelectThenCruise`, `MarkOnly`,
and `HoldToCruise` custom values are read for compatibility, warn at startup,
and use `TapHoldCruise` behavior. If Cruise is already active when the map opens,
an accepted selection retargets it immediately. The custom blue marker remains
optional and is not the ship target or Cruise itself.

Selecting the marked target again clears the mark. It only toggles an existing
autopilot lock off if the cockpit feed confirms that the same target id is the
active `bIsCruiseTargetLock` target.

## Safety gates

A Starmap press may be consumed only when all of these gates pass:

1. The map was opened while the player was actively flying (not landed or
   docked).
2. `StarMapMenuData.iCurrentMenuView` is the system view.
3. Exactly one marker row has `bIsInHighlightRadius=true` and exposes a nonzero
   target ID. The tree still identifies the system/star and does not participate
   in destination identity.
4. A planet/moon marker must match the dossier id/type; that dossier id must be a
   live PNDT with a parsed GNAM tuple in the captured cockpit system.
5. A station marker must be a live reference whose base carries `IsStarstation`,
   or a cell that resolves to exactly one persistent reference with such a base.
   Other non-planet markers must match exactly one current cockpit HUD row by ID.
   The map id/type and resolved target ID are retained separately.

The highlight-radius discriminator was proven with planet, moon, revisit,
rapid-switch, invalid-view, station, empty-space, and movie-reopen controls.
Planet/moon course dispatch and readback are runtime-proven. Static 1.16.244
analysis verifies the station resolution and native ship-target assignment path.
Station and generic non-planet gameplay confirmation remains pending.

No raw `TESForm*` survives a menu transition. Destination state is a value
containing only kind, map id/type, resolved target id, current-system identity,
localized name, and menu generation.

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
view is open, the plugin adds a stock Starmap control. Before Cruise starts it is
a `ReleaseHoldComboButton`: `SET CRUISE TARGET` on top and `HOLD TO CRUISE`
below. If Cruise is already active or its stock cockpit hold action is currently
unavailable, it is a tap-only `BasicButton` labeled `SET CRUISE TARGET`. The
active variant is enabled only while the destination gate passes; otherwise its
disabled label explains the rejection, such as `HIGHLIGHT A DESTINATION`,
`TARGET HAS NO CRUISE ID`, or `TARGET IS NOT AVAILABLE TO CRUISE`. This separate
action uses the player's `Cruise`
keyboard binding; the vanilla `SET COURSE` action and its binding remain
unchanged. Each needed variant is created at most once per Starmap movie and the
inactive one is disabled and hidden. The binding is cached at plugin startup to
keep `ControlMap` scanning off the map-open frame; restart Starfield after
changing the Cruise key.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the state and threading
model and [docs/VALIDATION.md](docs/VALIDATION.md) for the acceptance matrix.

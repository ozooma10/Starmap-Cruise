# Cruise From Starmap

Highlight a destination—including a planet, moon, current-system station, or
other addressable current-system marker—and use the separate Cruise action. For
a current-system target, tap/release remembers it and returns to the cockpit, or
a completed Starmap hold enters vanilla Cruise immediately and locks it. For a
remote planet or moon with an executable matching vanilla route, the tap starts
a system-level route and remembers which body Cruise should lock after arrival. The key
may be released once a local hold completes. In flight system view, the action
remains visible but disabled with a short reason when the highlighted marker
cannot be accepted. A fixed cockpit label confirms the marked target even when
its optional positional marker is unavailable. Live 1.16.244 evidence identifies
the selection as the one `StarMapMenuMarkersData` row whose
`bIsInHighlightRadius` value is true. Planet and moon rows are joined exactly to
the dossier PNDT identity. A station marker may identify either its live
reference or its map cell; the active-plugin index resolves the latter to one
indexed, currently live reference whose ship base carries `IsStarstation`.
Other non-planet markers must match exactly one row in the current cockpit target
feed. Zero,
multiple, mismatched, or invalid-view candidates remain entirely vanilla-owned.

This is a standalone native SFSE plugin. It has no ESP, scripts, save data,
serialization, SWF replacement, Cruise Navigation Panel dependency, or public
inter-plugin API.

## Requirements and scope

- Starfield 1.16.244.0 (Steam)
- Matching SFSE
- Address Library v21 (`versionlib-1-16-244-0.bin`)
- Highlighted planets and moons in the current or another system
- Highlighted stations, resolvable Ship POIs, and addressable non-planet markers
  in the currently loaded system
- Keyboard/mouse and controller for the separate Starmap action, using the
  player's live `Cruise`/`SHMonocle` bindings and native button glyphs

Native grav-jump route construction or modification remains outside this
plugin's scope. For a remote planet or moon, one tap arms the process-local body
mark and emits vanilla Back from system view. Once galaxy view is active, the
plugin emits vanilla Quick Select's system-selection event with the captured
STDT root, verifies the resulting **Set Course** state, and dispatches the exact
custom event emitted by vanilla **Set Course** there. The plugin then waits for
the visible vanilla travel panel, verifies that it remains an executable route
to the captured system for 500 ms, and invokes the same public method as the
vanilla Execute button. Vanilla still owns fuel, range,
exploration, travel, and every route leg. The mark survives intermediate jumps and is
reconciled only after arrival in the target system. A remote station or other
non-planet marker remains unavailable. A station marker must be a live station reference
or a cell that resolves to exactly one indexed, currently live station reference.
On map close, that reference becomes the native ship target before Cruise is
requested. A Ship POI CELL must resolve through its loaded-reference list to
exactly one live, in-space, non-station ship, excluding the player ship; that
reference receives the same guarded native assignment. A remote non-planet
marker exposes no Cruise action until its system is the loaded cockpit system.
Another non-planet marker must already expose exactly one
matching current HUD target row. Targets the HUD/Cruise system itself cannot lock remain marked rather
than being reported as confirmed.

## Interaction

- Tap/release marks the target, closes the complete Starmap/Data Menu stack,
  and returns to the cockpit. Starting Cruise normally afterward locks that
  marked target.
- A remote planet or moon uses a tap-only `JUMP THEN CRUISE` action. It is
  enabled only when the system/star root is exact and vanilla **Set Course** is
  available. The tap records the body as the Cruise target, dispatches stock
  `StarMapMenu_OnCancel`, and waits for galaxy view. It then establishes the
  galaxy marker context without the cursor: it emits the stock
  `StarMapMenu_QuickSelectChange` payload with the captured STDT root and, if
  native still reports no selection a fixed number of UI advances later,
  invokes the shipped public galaxy hover setter with that same root. Stock
  `SetRouteDestination` is dispatched at system scope only after native itself
  names the captured system — through the
  vanilla **Set Course** button, the native Quick Select cursor, or a unique
  galaxy highlight marker. The plugin never writes or forces that button. After
  vanilla builds the route, the plugin requires matching route-end system text
  plus the public `bCanExecuteRoute` gate continuously for 500 ms, then calls
  `JumpDataPanel.SendExecuteEvent()`. That method rechecks the Execute gate and
  dispatches stock `StarMapMenu_ExecuteRoute`. Every transient mismatch gets the
  full five-second route-build window; a route that is still missing, mismatched,
  or non-executable then fails closed: the Cruise mark is cleared, the map stays open, and
  vanilla's route/warning remains untouched. On settled arrival in the target
  system, one exact cockpit target-feed row is required before the plugin
  requests stock Cruise and queues the marked course.
- When the cockpit's stock Cruise action is currently available, completing the
  Starmap hold fill marks the target, closes the complete Starmap/Data Menu
  stack, and latches a held state through the stock
  `SpaceshipHudMenu.ProcessUserEvent` path. Vanilla owns the cockpit threshold;
  the latch releases when Cruise becomes active or after a four-second safety
  limit.
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

Selecting a marked current-system target again clears the mark. It only toggles
an existing autopilot lock off if the cockpit feed confirms that the same target
id is the active `bIsCruiseTargetLock` target. A remote `JUMP THEN CRUISE` tap
always represents travel intent rather than this local toggle.

## Safety gates

A Starmap press may be consumed only when all of these gates pass:

1. The map was opened while the player was actively flying (not landed or
   docked).
2. `StarMapMenuData.iCurrentMenuView` is the system view.
3. Exactly one marker row has `bIsInHighlightRadius=true` and exposes a nonzero
   target ID. The tree still identifies the system/star and does not participate
   in destination identity.
4. A planet/moon marker must match the dossier id/type; that dossier id must be a
   live PNDT with a parsed GNAM tuple. Remote targets additionally require the
   byte/source/vtable-guarded `TESLoadGameEvent` sink so a save load cannot retain
   a stale process-local mark. At acceptance they require the tree focus to be
   a live STDT star whose parsed DNAM system ID matches the destination's
   parsed GNAM system ID, vanilla **Set Course** to be
   available, and the browsed-system header to resolve. After stock Back the
   captured STDT/DNAM root is carried into galaxy view and sent through
   vanilla Quick Select's `bodyID` selection event without requiring mouse
   movement. After
   system-scope route
   creation they require the public Execute hint to remain visible
   (`bCanExecuteRoute=true`) and its destination-system text to match the system
   name captured with the mark continuously for 500 ms. Vanilla may choose a body within that system as
   its grav-jump entry point; Cruise still owns the marked final body.
5. A station marker must be a live reference whose base carries `IsStarstation`,
   or a cell that resolves to exactly one indexed, currently live reference with
   such a base. A Ship POI must be a CELL containing exactly one live, in-space,
   non-station GBFM reference after excluding the player ship. Other non-planet
   markers must match exactly one current cockpit HUD row by ID.
   The map id/type and resolved target ID are retained separately.

The highlight-radius discriminator was proven with planet, moon, revisit,
rapid-switch, invalid-view, station, empty-space, and movie-reopen controls.
Planet/moon course dispatch and readback are runtime-proven. Static 1.16.244
analysis verifies the station resolution and native target-assignment path.
Station gameplay is runtime-proven; Ship POI and generic non-planet confirmation
remain pending.

No raw `TESForm*` survives a menu transition. Destination state is a value
containing only kind, map id/type, resolved target id, target-system identity,
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

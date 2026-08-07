# Cruise From Starmap

Highlight a destination—including a planet, moon, or indexed station—and use
the separate Cruise action. For
a current-system target, tap/release remembers it and returns to the cockpit, or
a completed Starmap hold enters vanilla Cruise immediately and locks it. For a
remote planet, moon, or exact indexed station with an executable matching vanilla route, the tap starts
a system-level route and remembers which target Cruise should lock after arrival. The key
may be released once a local hold completes. In flight system view, the action
remains visible but disabled with a short reason when the highlighted marker
cannot be accepted. Live 1.16.244 evidence identifies the selection as the one
`StarMapMenuMarkersData` row whose
`bIsInHighlightRadius` value is true. Planet and moon rows are joined exactly to
the dossier PNDT identity. A station marker may identify either its live
reference or its map cell; the active-plugin index resolves the latter to one
indexed station reference whose ship base carries `IsStarstation`. Remote
stations additionally require one exact XMRK course reference owned by that
same CELL; the physical station becomes live only after system arrival.
Ship POIs and every other non-station marker are intentionally hidden; zero,
multiple, mismatched, or invalid-view candidates remain entirely vanilla-owned.

This is a standalone native SFSE plugin. It has no ESP, scripts, save data,
serialization, SWF replacement, Cruise Navigation Panel dependency, or public
inter-plugin API.

## Requirements and scope

- Starfield 1.16.244.0 (Steam)
- Matching SFSE
- Address Library v21 (`versionlib-1-16-244-0.bin`)
- Highlighted planets and moons in the current or another system
- Highlighted indexed stations in the current or another system
- Keyboard/mouse and controller for the separate Starmap action, using the
  player's live `Cruise`/`SHMonocle` bindings and native button glyphs

Native grav-jump route construction or modification remains outside this
plugin's scope. For a remote planet, moon, or indexed station, one tap arms the process-local target
mark and emits vanilla Back from system view. Once galaxy view is active, the
plugin invokes vanilla's non-entering selected-system setter with the captured
STDT root, verifies the native readback, and dispatches the exact custom event
emitted by vanilla **Set Course** through its Quick Select route-selection path.
The selection/cleanup bindings, StarMap menu, and GalaxyState object are all
exact-version byte/vtable guarded. The plugin then waits for
the visible vanilla travel panel, verifies that it remains an executable route
to the captured system for 500 ms, and invokes the same public method as the
vanilla Execute button. Vanilla still owns fuel, range,
exploration, travel, and every route leg. The mark survives intermediate jumps and is
reconciled only after arrival in the target system. A final body already exposed
as exactly one cockpit HUD row uses the direct Cruise lock. If a retained remote
moon is absent, the plugin resolves its parent planet from the active-load-order
PNDT/GNAM hierarchy and requires exactly one parent HUD row, but dispatches only
the retained moon after one stock Cruise activation. Starfield may resolve that
request latently through the parent without publishing the parent as
`bIsCruiseTargetLock`. Dispatch success is never course success: the continuation
stays pending only while Cruise remains active in the same settled system and
has no arbitrary travel-duration limit. It completes solely when a unique
retained-moon row reports the exact lock. A
published exact parent lock, when present, receives the stronger parent/feed
transition audit. The public destination remains the moon throughout.
A remote station marker must be a CELL with exactly one active-load-order indexed
station REFR/base tuple and exactly one CELL-owned XMRK REFR. After vanilla
arrives in the retained system and the world settles, the station REFR must
become live with that exact `IsStarstation` base. Only then does the plugin
assign the physical station as the native ship target and require exact setter
readback. The separately retained XMRK is the Cruise course identity. An XMRK
already exposed as exactly one HUD row takes the direct path. Otherwise the
index joins the station CELL EDID to PNDT `DNAM`, walks its unique same-system
`GNAM` ancestry, and requires an exact ancestor with exactly one HUD row as the
first private waypoint. One stock Cruise activation and one exact XMRK dispatch
may then resolve latently through that waypoint and only ordered inward
ancestors; only the XMRK's exact `bIsCruiseTargetLock` completes the course. The
public destination remains the physical station, and the travel phase has no
arbitrary duration limit. Current-system stations continue to resolve and
assign on map close. Ship POIs and generic non-planet markers expose no plugin
action.

## Interaction

- Tap/release marks the target, closes the complete Starmap/Data Menu stack,
  and returns to the cockpit. Starting Cruise normally afterward locks that
  marked target.
- A remote planet, moon, or exact indexed station uses a tap-only `JUMP THEN CRUISE` action. It is
  enabled only when the system/star root is exact and vanilla **Set Course** is
  available. The tap records the body as the Cruise target, dispatches stock
  `StarMapMenu_OnCancel`, and waits for galaxy view. It then establishes the
  galaxy marker context without consulting the physical cursor: it invokes the
  native non-entering selected-system setter (Address Library ID `94292`) with
  the captured STDT root, then leaves completed UI advances for native to
  publish the result. Immediately around Set Course it arms the same native
  selected-ID branch used by an open Quick Select list; vanilla must consume and
  clear that ownership synchronously or the stock close path restores it. Stock
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
  vanilla's route/warning remains untouched. A remote selection is disabled
  while Cruise is active because the stock HUD Cruise control is not handled
  while the Starmap owns the UI; exit Cruise in the cockpit first. Execute itself
  is accepted only when stock closes the Starmap; a dispatched event without that
  acknowledgement fails closed. On settled arrival in the target
  system, one exact final-body cockpit target-feed row is required for the
  direct path. A missing remote moon may continue only through one exact
  PNDT/GNAM parent planet that itself has one HUD row. After one stock Cruise
  activation, the plugin dispatches the retained moon—not the parent—and
  remains pending through the engine-owned latent travel while Cruise is active.
  Only a unique retained-moon row with exact `bIsCruiseTargetLock` readback
  completes the course. If Starfield publishes the exact parent lock on the way,
  that lock must end while Cruise remains active and the target system remains
  settled; a newer cockpit feed must then uniquely expose the retained moon.
  For a station, settled arrival instead requires the retained static CELL,
  physical REFR/base, and CELL-owned XMRK course REFR to resolve to the same
  exact indexed identities. Native physical-target assignment and readback must
  succeed. A missing XMRK row can continue only when the CELL maps uniquely
  through PNDT `DNAM` and every traversed `GNAM` parent is unique, live,
  in-system, and exact; an ancestor with one HUD row becomes the first private
  waypoint, and later exact intermediate locks must move inward along the
  retained ancestry chain. Cruise remains pending through unbounded active
  travel, and only the retained XMRK's exact course-lock readback succeeds.
  Identity, HUD-row, activation, dispatch, and feed-transition timeouts fail
  closed.
- When the cockpit's stock Cruise action is currently available, completing the
  Starmap hold fill marks the target, closes the complete Starmap/Data Menu
  stack, and latches a held state through the stock
  `SpaceshipHudMenu.ProcessUserEvent` path. Vanilla owns the cockpit threshold;
  the latch releases when Cruise becomes active or after a four-second safety
  limit.
- If Cruise was already active when the map opened, a current-system target uses
  one `SET CRUISE TARGET` action: a tap closes the map and queues the new course.
  A remote target is disabled with `EXIT CRUISE FIRST`; after Cruise is exited,
  reopening the map exposes the normal `JUMP THEN CRUISE` action. No automatic
  exit is claimed because the proven HUD input path is rejected while the
  Starmap is open.
- If Cruise is inactive but the stock cockpit action is temporarily unavailable,
  such as its short post-exit cooldown, the same tap-only action marks the target
  without advertising or attempting a hold-to-engage action.

The tap/hold interaction is fixed rather than configurable. If Cruise is already
active when the map opens, an accepted current-system selection retargets it
immediately.

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
   captured STDT/DNAM root is carried into galaxy view and passed to vanilla's
   byte- and vtable-guarded non-entering selected-system/Quick Select route path
   without requiring mouse movement. After
   system-scope route
   creation they require the public Execute hint to remain visible
   (`bCanExecuteRoute=true`) and its destination-system text to match the system
   name captured with the mark continuously for 500 ms. Vanilla may choose a body within that system as
   its grav-jump entry point; Cruise still owns the marked final body. Remote
   route acceptance requires Cruise to be inactive and rechecks that condition
   immediately before Execute.
   Stock Starmap close is required as Execute acknowledgement. At
   settled system arrival, a missing remote moon can resolve only through the
   unique live PNDT whose GNAM system matches and whose planet index equals the
   moon's GNAM parent index. One stock Cruise activation and one retained-moon
   course event are sent. Dispatch success is not acceptance: the request may
   remain latent for as long as Cruise is continuously active, and only the
   final moon's exact lock completes it. If an exact parent lock is
   published, its end and a later unique final-moon feed are additionally
   required. Missing/ambiguous identities or rows, unrelated exact courses,
   handshake timeouts, manual interruption, load, landing/docking, or system
   mismatch stop the continuation fail-closed. Opening the Starmap pauses the
   private driver; accepting another destination replaces the retained moon and
   resets its continuation through the normal `SetDestination` path.
5. A current-system station marker must be a live reference whose base carries
   `IsStarstation`, or a cell that resolves to exactly one indexed, currently live
   reference with such a base. A remote station must be a CELL with exactly one
   indexed station REFR/base tuple, exactly one CELL-owned XMRK REFR, and the
   guarded load-event sink. It uses the same exact STDT/DNAM system route as a
   remote body; after settled arrival, the retained physical REFR/base and XMRK
   must resolve live and native physical-target setter/readback must agree. A
   missing XMRK row requires one exact CELL-EDID/PNDT-DNAM orbital identity, a
   unique live GNAM ancestry chain, and one exact visible ancestor row before
   Cruise. Only the retained XMRK's exact `bIsCruiseTargetLock` succeeds. Ship
   POIs and other non-station markers are hidden. The map id/type, public/native
   station target, and Cruise course target are retained separately.

The highlight-radius discriminator was proven with planet, moon, revisit,
rapid-switch, invalid-view, station, empty-space, and movie-reopen controls.
Planet/moon direct course dispatch and readback are runtime-proven. Current-system
station gameplay is runtime-proven. A remote RE-939 trace proved the vanilla
system jump, physical station assignment, engine-owned travel, and arrival, and
validated exact dispatch/readback through its CELL-owned XMRK course identity.
Ship POIs and generic
non-planet markers are intentionally unsupported and hidden. The two-activation parent-staged remote-moon flow is
runtime-proven by a no-mouse Triton trace. A one-activation Chawla trace proved
that Starfield can retain the final-moon event without publishing a parent lock
and later exact-lock and reach Chawla; the corrected plugin-side latent-retention
gate remains pending a fresh trace.

No raw `TESForm*` survives a menu transition. Destination state is a value
containing only kind, map id/type, resolved public target/base/course ids,
target-system identity, localized name, and menu generation.

## Configuration

Edit `Data/SFSE/Plugins/CruiseFromStarmapCustom.ini`, not the shipped default:

```ini
[General]
bVerboseLog=true
```

The plugin does not draw a cockpit target overlay. It still samples the exact
retained course row's high-frequency distance so the independent
lock-loss/close-distance arrival audit can clear a completed destination safely.
The provider reports meters; the guarded `0.05` light-second threshold is
converted to meters before comparison.

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
  unavailable, it is a tap-only `BasicButton`. Current-system targets retain
  `SET CRUISE TARGET`; remote targets show disabled `EXIT CRUISE FIRST` while
  Cruise is active. The active variant is enabled only while the destination gate passes; otherwise its
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

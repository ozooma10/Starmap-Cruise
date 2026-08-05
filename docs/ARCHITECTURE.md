# Architecture

## Data path

```text
GalaxyStarMapMenu movie
  StarMapMenuData -------------------- view + system/body location ids
  StarMapMenuSystemBodyInfoData ------ system/star identity (not selected body)
  StarMapMenuMarkersData ------------- unique bIsInHighlightRadius marker row
  StarmapSystemBodyInfoProvider ------ dossier PNDT candidates while browsing
                    |
                    | planets/moons: marker/dossier id+type agreement
                    |   + live PNDT + parsed GNAM + current system
                    | stations: marker live reference or CELL
                    |   + active-load-order IsStarstation base
                    |   + exactly one persistent live reference
                    | other non-planets: exact current HUD target ID
                    v
              BodyDestination value (map id/type + target id)
                    |
                    +---- station map close: native ship-target assignment
                    |
                    v
SpaceshipHudMenu movie
  TargetLowFrequencyProvider -------- uniqueID/name/course-lock/current system
  TargetHighFrequencyProvider ------- index-aligned bearing + distance
                    |
                    +---- fixed target-status label (independent of bearing)
                    +---- optional positional marker and localized label
                    |
                    +---- ProcessUserEvent("Cruise", down/up)
                    |
                    +---- Reticle_OnCruiseLockCourse {uBodyID}
```

The active load order is parsed in memory on a background thread. Planet records
retain their PNDT/GNAM identity. Station bases are identified by the vanilla
`IsStarstation` keyword; CELL persistent-child references are then indexed by
cell and validated again as live references when selected. Full, medium, and
light master mappings are respected. No cache is read or written.

## State machine

```text
Idle -> MapSelection -> Marked -> AwaitingCruise -> AutopilotLocked
          |               ^             |                 |
          | invalid       | release /    | timeout         | manual exit or
          +-> vanilla     | no Cruise    +-----------------+ interruption
```

- `MapSelection` begins only after the selection gate passes: one nonzero
  highlight-radius marker, captured current system, active flight, and current
  session/movie generation. Planet/moon markers additionally require matching
  dossier id/type, a live PNDT, and parsed GNAM in that system. A station marker
  must be a live station reference or a CELL resolving to exactly one persistent
  live reference whose base carries `IsStarstation`. Another non-planet marker
  must match exactly one row in the current cockpit target feed.
- For a resolved station CELL/reference, map close assigns the reference as the
  native ship target before any Cruise input or course event is issued. Exact
  current-feed non-planets already have a course-addressable target ID.
- `Marked` owns the process-local destination but not autopilot.
- A quick release leaves the destination
  `Marked`. Completing the Starmap button's fill latches a stock HUD Cruise down
  edge after map close, independent of physical release. The HUD callback sends
  the up edge when Cruise becomes active, with a four-second safety release if
  activation never arrives. A later vanilla inactive-to-active Cruise transition moves
  the destination to `AwaitingCruise` and queues its target id.
- If Cruise was already active when the map opened, the stacked control is
  replaced by a stock tap-only `BasicButton`. Its accepted tap queues the course
  request immediately; no hold action is exposed or accepted.
- The same tap-only control is used while the stock cockpit
  `ShipReticle.CanActivateCruiseMode` getter is false, including the short
  post-exit cooldown. The tap still marks the destination but does not attempt
  to synthesize a currently unavailable Cruise hold.
- The carried physical event is still suppressed until release, so its
  remapped continued edge cannot double-trigger the cockpit.
- `AwaitingCruise` means a HUD course request is queued or awaiting low-feed
  confirmation.
- `AutopilotLocked` is entered only when the low feed reports
  `bIsCruiseTargetLock` on the same target id.

The mark survives manual Cruise exits and interruptions. A lost Cruise lock
starts a two-second arrival audit; the mark clears only when the prior lock and
close-distance evidence agree. Landing/docking, leaving the pilot seat, a
system change, a loading transition, explicit toggle, or replacement also
clears it.

## Threading and movie lifetime

- Movie creation increments a generation and invalidates every GFx handle from
  that movie. HUD reconciliation waits 1.5 seconds after the HUD movie-created
  timestamp and confirms that generation is still current before entering
  Scaleform. It rechecks the menu's root immediately after resolving
  `Reticle_mc`; dirty HUD work remains queued while the replacement settles.
- SFSE permanent tasks run on rotating render-graph workers. They only coalesce
  and post ordinary per-frame work through the engine's `BSService::TaskQueue`,
  which drains on the game main thread; they never touch UI or Scaleform.
- Scaleform work is driven by a byte-verified 1.16.244 entry hook around
  `UI_AdvanceActiveMenus` (Address Library ID 130455). The original function
  runs first; subscriptions and mutations run after it returns, on the owning
  main thread with that AS3 advance complete. Hooking the function entry
  coexists with OSF UI, whose stricter pause-menu pump hooks the two callers.
- Feed subscriptions occur one per safe post-advance pass after a settle delay
  and re-check the live root/generation immediately before entering AS3.
  Starmap subscriptions also require the visible map-open event;
  `UI::IsMenuOpen` alone can expose its incomplete background movie after load.
- During active-flight system view, the native callback shows a separate stock
  control using the primary live `ShipHUD/Cruise` keyboard binding. A
  `ReleaseHoldComboButton` owns tap/hold before Cruise starts; a `BasicButton`
  owns only tap when the map session began during Cruise or the stock cockpit
  hold is unavailable. Both variants expose the engine-owned `Cruise` event so
  the button keybox resolves the player's live binding; only the current one is
  enabled and visible. Native callback handling also normalizes any stale hold
  signal to tap while the tap-only variant owns the session.
  The control is interactive only while the selection gate passes;
  otherwise its disabled label exposes the current rejection reason. The binding
  is read from the version-gated engine `ControlMap` once at startup. This keeps
  the validated mapping-array scan off the map-open frame; an in-session control
  remap takes effect after restarting Starfield. The input hook temporarily
  presents that physical key as `Cruise`, then restores the engine event string
  after the UI call. The vanilla `SetRouteDestination`
  button is never changed. Each variant is created at most once per movie and
  hidden when inactive; no SWF bytecode is replaced.
- Hold availability mirrors the shipped `ShipReticle.UpdateCruiseButton` rule by
  resolving and type-checking `Reticle_mc` once after the HUD movie guard, then
  reading its public `CanActivateCruiseMode`, `MonocleModeActive`, and
  `CruiseModeHUDActive` members. Getter failure is fail-closed to tap-only and
  does not synthesize a Cruise exit. While the Starmap is open, fallback polls
  reach Scaleform only after the HUD movie has settled.
- The system/star tree provider is diagnostic-only. It never invalidates or
  participates in the marker/dossier body join.
- Feed callbacks copy passed GFx payloads into plain C++ snapshots or queue a
  value action, then return without fetching another root or invoking AS3.
  HUD object construction, forwarded Cruise edges, fixed target status, marker
  movement, course dispatch, and map-button updates run afterward from the
  post-advance pump. Stale GFx handles are released there as well.
- Input interception only reads/copies value state, edits its own queue links
  for the duration of the UI call, and restores them in reverse order.
- A small Win32 foreground watcher resets only the plugin's pending hold on
  application focus loss; it never calls game or Scaleform APIs.

## Held-key handoff

Keyboard testing proved that the physical device/id survives, but the remapped
cockpit event is a continued hold with `first=false`; feeding that event back to
the engine does not activate Cruise. OSF RE then proved the complete stock UI
route: invoking `SpaceshipHudMenu.ProcessUserEvent("Cruise", true)` in the HUD
movie's post-advance safe window, preserving the hold, and later invoking it
with `false` activates Cruise through its normal hold logic. Dispatching
`Reticle_OnCruiseActivate` alone does not.

The forwarded event name follows the captured physical device: `Cruise` for
keyboard/mouse and `SHMonocle` for controller, matching the two button-data
variants installed by stock `ShipReticle`.

The plugin uses only that public movie method and never allocates, injects, or
enqueues an engine `ButtonEvent`. Completion of the visible Starmap fill is the
intent boundary: the HUD press stays latched until Cruise activation or the
four-second safety limit. Any carried physical event remains suppressed through
release to prevent a second remapped edge.

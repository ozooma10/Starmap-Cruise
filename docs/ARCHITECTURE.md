# Architecture

## Data path

```text
GalaxyStarMapMenu movie
  StarMapMenuData -------------------- view + system/body location ids
  StarMapMenuSystemBodyInfoData ------ system/star identity (not selected body)
  StarMapMenuMarkersData ------------- unique bIsInHighlightRadius marker row
  StarmapSystemBodyInfoProvider ------ dossier PNDT candidates while browsing
                    |
                    | exact marker/dossier id+type agreement
                    | + live PNDT + parsed GNAM + current system
                    v
              BodyDestination value
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

The GNAM index is parsed in memory on a background thread from the full, medium,
and light tiers of the game's active compiled-file collection. Tier-specific
runtime form ids are validated as PNDT before entries are retained. No cache is
read or written.

## State machine

```text
Idle -> MapSelection -> Marked -> AwaitingCruise -> AutopilotLocked
          |               ^             |                 |
          | invalid       | release /    | timeout         | manual exit or
          +-> vanilla     | no Cruise    +-----------------+ interruption
```

- `MapSelection` begins only after the exact selection gate passes: one
  highlight-radius planet/moon marker, matching dossier id/type, live PNDT,
  parsed GNAM, captured current system, active flight, and current session/movie
  generation.
- `Marked` owns the process-local destination but not autopilot.
- A quick release leaves the destination
  `Marked`. Completing the Starmap button's fill latches a stock HUD Cruise down
  edge after map close, independent of physical release. The HUD callback sends
  the up edge when Cruise becomes active, with a four-second safety release if
  activation never arrives. A later vanilla inactive-to-active Cruise transition moves
  the destination to `AwaitingCruise` and queues its PNDT id.
- The same request is queued immediately if Cruise was already active when the
  accepted map selection began.
- The carried physical event is still suppressed until release, so its
  remapped continued edge cannot double-trigger the cockpit.
- `AwaitingCruise` means a HUD course request is queued or awaiting low-feed
  confirmation.
- `AutopilotLocked` is entered only when the low feed reports
  `bIsCruiseTargetLock` on the same PNDT id.

The mark survives manual Cruise exits and interruptions. A lost Cruise lock
starts a two-second arrival audit; the mark clears only when the prior lock and
close-distance evidence agree. Landing/docking, leaving the pilot seat, a
system change, a loading transition, explicit toggle, or replacement also
clears it.

## Threading and movie lifetime

- Movie creation increments a generation and invalidates every GFx handle from
  that movie.
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
- During active-flight system view, the native callback shows a separate
  `ReleaseHoldComboButton` using the primary live `ShipHUD/Cruise` keyboard
  binding. The button is interactive only while the exact selection gate passes;
  otherwise its disabled label exposes the current rejection reason. The binding is read from
  the version-gated engine `ControlMap` once at startup. This keeps the validated
  mapping-array scan off the map-open frame; an in-session control remap takes
  effect after restarting Starfield.
  The input hook temporarily presents that physical key to the Starmap button
  manager as `Cruise`, then restores the engine event string after the UI call.
  The vanilla `SetRouteDestination` button is never changed. The added button is
  created once per movie, then hidden only outside flight system view; no
  SWF bytecode is replaced.
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

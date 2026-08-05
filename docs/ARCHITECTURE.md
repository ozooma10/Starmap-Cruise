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
                    +---- optional runtime marker and localized label
                    |
                    +---- Reticle_OnCruiseLockCourse {uBodyID}
```

The GNAM index is parsed in memory on a background thread from every plugin in
the game's active compiled-file collection. Runtime form ids are validated as
PNDT before entries are retained. No cache is read or written.

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
- In the default `SelectThenCruise` mode, a vanilla inactive-to-active Cruise
  transition moves `Marked` to `AwaitingCruise` and queues the marked PNDT id.
  The same request is queued immediately if Cruise was already active when the
  accepted map selection began.
- A carried map hold is suppressed until release and cannot enter Cruise by
  itself. `MarkOnly` never moves a mark into `AwaitingCruise`; `HoldToCruise`
  retains only the already-active retarget path.
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
- Feed subscriptions occur one per frame after a settle delay and re-check the
  live root/generation immediately before entering AS3. Starmap subscriptions
  also require the visible map-open event; `UI::IsMenuOpen` alone can expose its
  incomplete background movie after a load.
- HUD object construction, marker movement, and course dispatch run from the
  HUD feed callback—the same UI thread that owns the movie.
- Input interception only reads/copies value state, edits its own queue links
  for the duration of the UI call, and restores them in reverse order.
- A small Win32 foreground watcher resets only the plugin's pending hold on
  application focus loss; it never calls game or Scaleform APIs.

## Held-key fallback

Keyboard testing proved that the physical device/id survives, but the cockpit
event is a continued hold with `first=false`; vanilla does not activate Cruise.
The plugin therefore suppresses the carried event until release in every mode.
`SelectThenCruise` responds only to a later vanilla Cruise activation observed
in the HUD, so the game continues to own input and Cruise startup.
Synthetic replay remains prohibited because pool ownership, reverse binding,
thread ownership, and a safe upstream enqueue API are unproven.

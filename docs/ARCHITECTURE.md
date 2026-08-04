# Architecture

## Data path

```text
GalaxyStarMapMenu movie
  StarMapMenuData -------------------- view + system/body location ids
  StarMapMenuSystemBodyInfoData ------ system/star identity (not selected body)
  StarMapMenuMarkersData ------------- may have no focused marker in system view
  StarmapSystemBodyInfoProvider ------ dossier PNDT candidates while browsing
                    |
                    | stable focus discriminator (NOT YET PROVEN)
                    | + live PNDT + parsed GNAM + current system
                    v
              BodyDestination value
                    |
                    v
SpaceshipHudMenu movie
  TargetLowFrequencyProvider -------- uniqueID/name/course-lock/current system
  TargetHighFrequencyProvider ------- index-aligned bearing + distance
                    |
                    +---- runtime marker and localized label
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

- `MapSelection` begins only after the exact selection gate passes. The current
  build cannot enter it because the system-view focus discriminator is still
  unproven; validated dossier candidates are logged and rejected.
- `Marked` owns the process-local destination but not autopilot.
- `AwaitingCruise` is reserved for a course request when Cruise was already
  active. A carried map hold is suppressed until release and cannot enter
  Cruise by itself.
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
  live root/generation immediately before entering AS3.
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
Synthetic replay remains prohibited because pool ownership, reverse binding,
thread ownership, and a safe upstream enqueue API are unproven.

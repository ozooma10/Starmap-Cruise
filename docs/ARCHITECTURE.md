# Architecture

## Data path

```text
GalaxyStarMapMenu movie
  StarMapMenuData -------------------- view + system/body location ids
  StarMapMenuSystemBodyInfoData ------ system/star identity (not selected body)
  StarMapMenuMarkersData ------------- unique bIsInHighlightRadius marker row
  StarmapSystemBodyInfoProvider ------ dossier PNDT candidates while browsing
  StarMapMenuQuickSelectData --------- native galaxy selection readback (read only)
                    |
                    | planets/moons: marker/dossier id+type agreement
                    |   + live PNDT + parsed GNAM
                    |   + guarded load sink when system is remote
                    | stations: marker live reference or CELL
                    |   + active-load-order IsStarstation base
                    |   + exactly one indexed, currently live reference
                    | ship POIs: CELL loaded-reference walk
                    |   + exactly one live in-space non-station GBFM reference
                    |   + player ship excluded
                    | other non-planets: exact current HUD target ID
                    v
              BodyDestination value (map id/type + target id)
                    |
                    +---- remote planet/moon: stock Back to galaxy
                    |       + verify same focused system/root
                    |       + stock system-level SetRouteDestination
                    |       + vanilla builds route
                    |       + matching visible vanilla Execute gate
                    |       + JumpDataPanel.SendExecuteEvent()
                    |       + PendingJump until target system
                    |
                    +---- station/ship map close: native ship-target assignment
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
retain their PNDT/GNAM identity and star records retain their STDT/DNAM system
identity. Station bases are identified by the vanilla
`IsStarstation` keyword; CELL placed references are then indexed by
cell and validated again as live references when selected. Full, medium, and
light master mappings are respected. No cache is read or written.

## State machine

```text
Idle -> MapSelection -> Marked ---------> AwaitingCruise -> AutopilotLocked
          |       |                          |                 |
          |       +-> stock Back -> system Set Course           |
          |                 | system route verified             |
          |                 +-> stock Execute -> PendingJump ---+
          | invalid                 | intermediate jumps       | manual exit or
          +-> vanilla               | and LoadingMenu           +-> interruption
```

- `MapSelection` begins only after the selection gate passes: one nonzero
  highlight-radius marker, captured current system, active flight, and current
  session/movie generation. Planet/moon markers additionally require matching
  dossier id/type, a live PNDT, and parsed GNAM. A remote planet/moon also
  requires the guarded load-event sink. A station marker
  must be a live station reference or a CELL resolving to exactly one indexed,
  currently live reference whose base carries `IsStarstation`. A Ship POI must
  be a CELL resolving through the verified loaded-reference walker to exactly one
  live, in-space, non-station GBFM reference after excluding the player ship.
  A non-planet marker whose live STDT system differs from the captured cockpit
  system is hidden before station/ship resolution: its target is not safe until
  that system is loaded. Another non-planet marker must
  match exactly one row in the current cockpit target feed.
- For a resolved station or ship CELL/reference, map close assigns the reference
  as the native ship target before any Cruise input or course event is issued. Exact
  current-feed non-planets already have a course-addressable target ID.
- `Marked` owns the process-local destination but not autopilot.
- `PendingJump` is used only for a remote planet or moon. It preserves the mark
  across intermediate system changes and `LoadingMenu`, without constructing or
  altering the vanilla route itself. A remote tap first captures the body as the
  Cruise target plus the browsed system name/root, then emits the exact
  `StarMapMenu_OnCancel` event used by vanilla Back. Reaching galaxy view with
  the captured root and establishing the galaxy marker context are two separate
  guarded phases, each with its own five-second window. In the marker-context
  phase the driver invokes vanilla's non-entering GalaxyState selected-system
  setter once with the captured STDT root, then waits a fixed number of
  completed AS3 advances for native to publish its result. Static 1.16.244
  analysis identifies this as Address Library ID `94292`, primary vtable slot
  `+0x48`. The call is allowed only after its 16-byte function fingerprint, the
  live StarMapMenu primary vtable, and the owned GalaxyState primary vtable all
  match the exact runtime. Immediately around Set Course the driver sets the
  one-byte Quick Select ownership state so vanilla reads the selected-system ID
  rather than cursor hover, then requires the stock handler to consume and clear
  it synchronously. If it does not, byte-verified stock close ID `94308` restores
  the state and the route fails closed. Set Course is dispatched as
  `StarMapMenu_OnHintButtonClicked {buttonAction: "SetRouteDestination"}` only
  once native itself names the captured system: the vanilla Set Course button
  reporting enabled and visible, the native `StarMapMenuQuickSelectData` cursor
  resting on the captured root, or exactly one galaxy highlight marker carrying
  it. The latter two additionally require the vanilla button to be present and
  visible; none of them writes to it. When the ladder is exhausted with no
  native selection, one bounded read-only diagnostic pass logs the menu-root and
  hint-bar member names plus every hint button's enabled/visible/text/action
  before the phase fails closed.
  During guarded five-second stages, the route-end system text must match the
  captured name and the public Execute hint must remain visible continuously for
  500 ms. Transient mismatch states receive the full five-second route-build
  window. Vanilla may choose any body within that matching system as its grav-jump
  entry point. The plugin then enters `PendingJump` and invokes public
  `JumpDataPanel.SendExecuteEvent()`, which rechecks the same Execute visibility
  before dispatching `StarMapMenu_ExecuteRoute`.
  Physical-hold and application-focus cleanup does not demote the accepted
  `MapSelection` state. While either that state or the guarded remote-route
  request remains active, ordinary current-system reconciliation cannot clear
  the destination before Execute. The route driver is paused while Starfield is
  not foreground; focus return restarts the current phase timeout and clears any
  partial Execute-readiness dwell. The exact STDT root proven at acceptance is
  carried through stock Back and pinned against transient star-feed rows until
  system-scope Set Course. The marker-context ladder establishes the native
  galaxy selection without mouse hover, and Set Course waits for a native
  selection authority rather than for elapsed time. Repeat presses of the
  Cruise-bound control are
  consumed while this handoff is active. The later route-system text gate remains
  authoritative before Execute. If the map closes early, movie/session
  identity changes, the route mismatches, or it never becomes executable, only
  the Cruise mark is cleared; vanilla's route and warning remain untouched.
  After the resolver reports the target system, the map is closed, the world has
  settled, and exactly one matching cockpit target row exists, it requests
  Cruise through the existing stock HUD press and course path. If Cruise is
  already active, it queues the course directly.
- A quick release leaves the destination `Marked`. Accepted current-system taps
  and holds mirror shipped `StarMapButtonHintBar.onCloseSubMenuToGame`: they
  dispatch `DataMenu_SetMenuForQuickEntry` followed by
  `GlobalFunc_CloseAllMenus` from the verified post-advance window. This closes
  both Starmap and any parent Data Menu instead of revealing the character menu.
  Completing the Starmap button's fill also latches a stock HUD Cruise down edge
  after menu close, independent of physical release. The HUD callback sends the
  up edge when Cruise becomes active, with a four-second safety release if
  activation never arrives. A later vanilla inactive-to-active Cruise transition
  moves the destination to `AwaitingCruise` and queues its target id.
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
clears a normal mark. `PendingJump` instead survives expected travel transitions
but clears on a guarded `TESLoadGameEvent`, settled non-space state, or
replacement.

## Threading and movie lifetime

- Movie creation increments a generation and invalidates every GFx handle from
  that movie. HUD reconciliation waits 1.5 seconds after the HUD movie-created
  timestamp and confirms that generation is still current before entering
  Scaleform. It rechecks the menu's root immediately after resolving
  `Reticle_mc`; dirty HUD work remains queued while the replacement settles.
- SFSE permanent tasks run on rotating render-graph workers. They only coalesce
  and post ordinary per-frame work through the engine's `BSService::TaskQueue`,
  which drains on the game main thread; they never touch UI or Scaleform.
- `TESLoadGameEvent` registration is enabled only when the 1.16.244 getter
  prologue, source address, and source vtable all match. Its callback publishes
  one atomic clear signal; destination and input state are reset by the next
  main-thread service frame. If any identity check fails, remote targets fail
  closed while current-system behavior remains available.
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
  control using the live `ShipHUD/Cruise` keyboard/mouse binding or the stock
  `SHMonocle` controller binding. A
  `ReleaseHoldComboButton` owns tap/hold before Cruise starts; a `BasicButton`
  owns only tap when the map session began during Cruise or the stock cockpit
  hold is unavailable. Each button has separate MKB `Cruise` and gamepad
  `SHMonocle` data objects, mirroring stock `ShipReticle`; the active input
  device's object is installed so the keybox resolves the player's live glyph.
  Only the current tap/hold variant is enabled and visible. Native callback
  handling also normalizes any stale hold signal to tap while the tap-only
  variant owns the session.
  The control is interactive only while the selection gate passes;
  otherwise its disabled label exposes the current rejection reason. The binding
  is read from all three device arrays in the version-gated engine `ControlMap`
  once at startup. This keeps the validated mapping-array scan off the map-open
  frame; an in-session control remap takes effect after restarting Starfield.
  The input hook tracks first-down device changes and temporarily presents the
  matching physical control as the event currently installed on the button,
  then restores the engine event string after the UI call. The vanilla
  `SetRouteDestination`
  button is never changed. Each variant is created at most once per movie and
  hidden when inactive; no SWF bytecode is replaced.
- For a remote planet/moon, the tap-only control is additionally gated by an
  exact live STDT star whose parsed DNAM system ID matches the selected PNDT's
  GNAM system ID, plus the live public
  `SetRouteDestinationButtonData` enabled/visible state. Acceptance captures
  `SystemNameHeader_mc` for later route-display comparison, emits stock Back,
  carries the exact captured STDT/DNAM root into galaxy view, and then invokes
  the guarded non-entering selected-system setter against that root. Once a native
  selection authority names the captured system, it emits the same custom event
  as stock Set Course. The post-advance driver watches
  `JumpData_mc`: `ExecuteButton_mc.ExecuteButtonHint_mc.Visible` is the shipped
  `bCanExecuteRoute` result, and the displayed route-end system must exactly
  match the captured name continuously for 500 ms before
  `JumpDataPanel.SendExecuteEvent()` is invoked. Any non-ready observation resets
  that dwell; route construction still has a five-second timeout. A displayed
  body is accepted as vanilla's entry point within that system.
  The plugin does not dispatch a native far-travel event, construct a route,
  change an exploration flag, or hide the map itself on this path. Vanilla owns
  SetRouteDestination, route construction, ExecuteRoute, and normal menu/travel
  transitions. A 250 ms post-advance poll also keeps button eligibility current
  when route/button state changes without publishing a subscribed data feed;
  the eligibility signature prevents unchanged button mutations.
- Hold availability mirrors the shipped `ShipReticle.UpdateCruiseButton` rule by
  resolving and type-checking `Reticle_mc` once after the HUD movie guard, then
  reading its public `CanActivateCruiseMode`, `MonocleModeActive`, and
  `CruiseModeHUDActive` members. Getter failure is fail-closed to tap-only and
  does not synthesize a Cruise exit. While the Starmap is open, fallback polls
  reach Scaleform only after the HUD movie has settled.
- The system/star tree provider never participates in the marker/dossier body
  join. For a remote action only, its live STDT star identity separately proves
  which system node vanilla must retain when returning to galaxy view. Only live
  STDT rows update the cached root; zero/transient rows cannot erase it. The root
  is cleared on entry to galaxy view but retained across galaxy-to-system entry
  because the star feed may publish before the view feed.
- `StarMapMenuQuickSelectData` is subscribed read-only. Live 1.16.244 payload
  enumeration proves it contains only `bShowMenu` and `bOpenForPlot`; Quick
  Select entries arrive through native's direct `SetMarkers(Array)` movie call,
  not this feed. The handler therefore cannot claim an entry/cursor authority
  from absent fields. Member names remain logged once per session so future
  payload changes stay evidence-pinned.
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

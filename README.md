# Starmap Cruise

Starmap Cruise adds a Cruise action to Starfield's system map.

## Usage

While aboard your ship in space, open the system map from the pilot seat, a navigation console, or your personal starmap and highlight a supported destination. The added action uses the same keyboard or gamepad control that you have bound to Cruise:

- **Tap** to close the map and mark the highlighted destination as your Cruise target. If Cruise is already active, the new course is requested immediately.
- **Hold** while Cruise is inactive to close the map, engage Cruise, and turn on Autopilot for the highlighted destination.
- For a planet or moon in another system, use **JUMP THEN CRUISE** from the pilot seat, a navigation console, or the personal shipboard starmap. The mod follows Starfield's vanilla route and travel flow, then engages Cruise and Autopilot after the same player ship reaches the destination system and flight has settled. If the jump places the ship directly at the selected body, the trip completes without engaging Cruise.

When roaming your ship, same-system planets, moons, and stations retain the same tap and hold behavior. Free-roam activation is rejected while the ship is landed, docked, loading, grav jumping, or still settling after travel.

The hold option is shown only when Cruise can be engaged. During a cooldown, while Cruise state is unavailable, or while Cruise is already active, the safe tap action may be the only option. A remote route started while roaming uses vanilla's menu travel/loading flow; it does not add a walkable grav-jump duration.

## Supported targets and limitations

Supported:

- Planets, moons and stations in the current system
- Planets and moons in another system

Currently unsupported:

- Stations in another system
- Generic non-station markers

Unsupported, ambiguous, or unresolved selections do not show an actionable Starmap Cruise prompt. 

## Build and test

Configure and build the plugin:

```powershell
xmake f -m releasedbg -y
xmake build
```
Generate `compile_commands.json`:

```powershell
xmake project -k compile_commands
```

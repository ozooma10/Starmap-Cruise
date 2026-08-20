# Starmap Cruise

Starmap Cruise adds a Cruise action to Starfield's system map.

## Usage

While piloting your ship in space, open the system map and highlight a supported destination. The added action uses the same keyboard or gamepad control that you have bound to Cruise:

- **Tap** to close the map and mark the highlighted destination as your Cruise target. If Cruise is already active, the new course is requested immediately.
- **Hold** while Cruise is inactive to close the map, engage Cruise, and lock the highlighted destination as the course.
- For a planet or moon in another system, use **JUMP THEN CRUISE**. The mod follows Starfield's vanilla route and jump flow, then engages Cruise after the ship reaches the destination system and flight has settled. If the jump places the ship directly at the selected body, the trip completes without engaging Cruise.

The hold option is shown only when Cruise can be engaged. During a cooldown, while Cruise state is unavailable, or while Cruise is already active, the safe tap action may be the only option.

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

# Starmap Cruise

Starmap Cruise adds a Cruise action to Starfield's system map. Select a planet, moon, or station and set your cruise target straight from the system map.

## Usage

While piloting your ship in space, open the system map and highlight one supported destination. The added action uses the same keyboard, mouse, or gamepad control that you have bound to Cruise:

- **Tap** to close the map and mark the highlighted destination as your Cruise target. If Cruise is already active, the new course is requested immediately.
- **Hold** while Cruise is inactive to close the map, engage Cruise, and lock the highlighted destination as the course.
- For a planet or moon in another system, use **JUMP THEN CRUISE**. The mod follows Starfield's vanilla route and jump flow, then engages Cruise after the ship reaches the destination system and flight has settled.

The hold option is shown only when Cruise can be engaged. During a cooldown, while Cruise state is unavailable, or while Cruise is already active, the safe tap action may be the only option.

## Requirements

- The Steam release of Starfield; Microsoft Store/Game Pass versions are not supported
- SFSE 0.2.21 recommended
- Address Library for SFSE Plugins
- Starfield 1.16.244

## Installation

Install through a mod manager or extract the archive's `Data` folder into the game data directory. 

The active payload should contain:

```text
Data/
  SFSE/
    Plugins/
      Starmap Cruise.dll
```

When updating from a pre-1.0 release, remove
`Data\SFSE\Plugins\CruiseFromStarmap.dll` before installing v1.0.0. The plugin
file was renamed to `Starmap Cruise.dll`; leaving both files installed would
load two copies of the plugin.


## Supported targets and limitations

Supported:

- Planets and moons in the current system
- Planets and moons in another system when Starfield can build a vanilla route
- Indexed stations in the current system
- Remote planet and moon jumps made through FTL - A Grav Lanes Successor

Currently unsupported:

- Stations in another system
- Generic non-station markers

Unsupported, ambiguous, or unresolved selections do not show an actionable Starmap Cruise prompt. This is expected: the mod does not guess at a destination or fall back to a nearby target, and Starfield's normal map controls remain available.

## FTL compatibility

Compatibility with **FTL - A Grav Lanes Successor** is automatic when `FTL.esm` is loaded. For remote planets and moons, Starmap Cruise recognizes FTL's scripted replacement for vanilla grav-jump completion and continues with Cruise after arrival. No patch or configuration is required.

FTL compatibility does not add support for remote stations or generic markers. Other mods that replace the grav-jump completion flow are not covered by this compatibility path.

## Troubleshooting

If the Cruise action does not appear or a route does not complete:

1. Confirm that you are using the Steam version of Starfield 1.16.244, with SFSE and the matching Address Library installed. SFSE 0.2.21 is recommended.
2. Confirm that Cruise has a binding for the active input device. The map action intentionally stays hidden when no keyboard/mouse or gamepad Cruise binding can be resolved.
3. Test while piloting in space with exactly one supported planet, moon, or same-system station highlighted.
4. Check `Documents\My Games\Starfield\SFSE\Logs\Starmap Cruise.log` for initialization, selection, routing, and timeout messages. If Windows redirects the Documents folder through OneDrive, use the redirected Documents location.

## Build and test

Configure and build the plugin:

```powershell
xmake f -m releasedbg -y
xmake build
```

Build and run the core test suite:

```powershell
xmake build CruiseFromStarmapTests
xmake test -v
```

The native suite covers domain invariants, selection and action policies, map
sessions, local and remote navigation state machines, command-dispatch recovery,
protocol deadlines and correlation, presentation caching, input claiming, and
the map/HUD/travel inbox boundaries and concurrent producers.

SFSE relocations, live Scaleform objects, vanilla route dispatch, station
reference discovery, and the final HUD/gameplay result still require the
in-game release matrix because those interfaces only exist inside Starfield.


Generate `compile_commands.json`:

```powershell
xmake project -k compile_commands
```
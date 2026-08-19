# Starmap Cruise

Starmap Cruise adds a Cruise action to Starfield's system map. Select a planet, moon, or station and set your cruise target straight from the system map.

## Requirements

- SFSE
- Address Library for SFSE Plugins
- Starfield 1.16.244

## Installation

Install through a mod manager or extract the archive's `Data` folder into the game data directory. 

The active payload should contain:

```text
Data/
  SFSE/
    Plugins/
      CruiseFromStarmap.dll
```


## Supported targets and limitations

Supported:

- Planets and moons in the current system
- Planets and moons in another system when Starfield can build a vanilla route
- Indexed stations in the current system

Currently unsupported:

- Stations in another system
- Generic non-station markers


## Build and test

Configure and build the plugin:

```powershell
xmake f -m releasedbg -y
xmake build
```

Build and run the core test suite:

```powershell
xmake build CruiseFromStarmapTests
xmake run CruiseFromStarmapTests
```

Generate `compile_commands.json`:

```powershell
xmake project -k compile_commands
```
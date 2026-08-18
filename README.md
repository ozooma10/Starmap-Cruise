# Starmap Cruise

Starmap Cruise adds a Cruise action to Starfield's system map. Select a planet, moon, or station and set your cruise target straight from the system map.

## Requirements

- SFSE
- Address Library for SFSE Plugins

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

- Planets and moons in the current or another system
- Indexed stations in the current or another system

Currently Unsupported:

- Remote Systems
- Generic non-station markers


### Build
To build the project, run the following command:
```bat
xmake build
```
Generate compile_commands.json
```bat
xmake project -k compile_commands
```
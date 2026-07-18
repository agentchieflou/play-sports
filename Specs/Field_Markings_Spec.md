# Specification: Field Markings and Materials

This document defines the exact positions and layout of field markings (yard lines, hash marks, end zones, and sidelines) to be rendered on the playing field. Level designers should implement these markings using materials, textures, or decals.

## Coordinates for Key Lines (centered at 50-yard line)

Coordinates use standard UE cm unit scaling (1 yard = 91.44 cm).

### 1. Goal Lines and End Zones
* **Home End Zone**: X = -5,486.4 cm to X = -4,572.0 cm.
* **Home Goal Line**: X = -4,572.0 cm.
* **Away Goal Line**: X = +4,572.0 cm.
* **Away End Zone**: X = +4,572.0 cm to X = +5,486.4 cm.
* **Endlines (Back of End Zones)**: X = -5,486.4 cm and X = +5,486.4 cm.

### 2. Sidelines
* **Left Sideline**: Y = -2,438.4 cm (-26.6667 yards).
* **Right Sideline**: Y = +2,438.4 cm (+26.6667 yards).

### 3. Yard Lines
Major lines are spaced every 5 yards (457.2 cm) along the X-axis:

| Game Yard Line | X Coordinate (cm) | Description |
| --- | --- | --- |
| **Home Goal Line** | -4,572.0 | X = -50 yards |
| **Home 10** | -3,657.6 | X = -40 yards |
| **Home 20** | -2,743.2 | X = -30 yards |
| **Home 30** | -1,828.8 | X = -20 yards |
| **Home 40** | -914.4 | X = -10 yards |
| **50 Yard Line** | 0.0 | X = 0 yards |
| **Away 40** | +914.4 | X = +10 yards |
| **Away 30** | +1,828.8 | X = +20 yards |
| **Away 20** | +2,743.2 | X = +30 yards |
| **Away 10** | +3,657.6 | X = +40 yards |
| **Away Goal Line** | +4,572.0 | X = +50 yards |

### 4. Hash Marks (NFL Specification)
NFL hash marks are **18 feet 6 inches** (563.88 cm / 6.1667 yards) wide, centered laterally:
* **Left Hash Mark Line**: Y = -281.94 cm.
* **Right Hash Mark Line**: Y = +281.94 cm.
* They occur at every 1-yard interval (91.44 cm) along the X-axis between the goal lines.

## Placement Guidelines

1. **Goal Lines and Sidelines**: Should be solid white lines, 4 inches (10.16 cm) wide.
2. **Yard Lines**: Should run from sideline to sideline, solid white, 4 inches (10.16 cm) wide, spaced every 5 yards.
3. **End Zone Styling**: Branded with team logos or contrasting colors (e.g. green field, blue/red end zones).
4. **Decals/Materials**: You can apply a tiled field material with grass textures and overlay these markings as a composite material or screen-space decals aligned with the coordinates.

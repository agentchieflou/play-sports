# Specification: Field Geometry and Scaling

This document defines the exact dimensions and scaling conventions for the American football field level in `play-sports`. Level designers and programmers must follow these specifications.

## Scaling Convention

Unreal Engine uses **centimeters (cm)** as its default unit (1 Unreal Unit = 1 cm).
All measurements in this project are based on the standard conversion:

* **1 Yard = 3 Feet = 36 Inches = 91.44 cm**
* **1 Foot = 30.48 cm**

## Field Dimensions

The level must represent a regulation football field with the following dimensions:

| Component | Yards | Unreal Units (cm) | Description |
| --- | --- | --- | --- |
| **Total Field Length** | 120.0 | 10,972.8 | Includes playing field and both end zones |
| **Playing Field Length** | 100.0 | 9,144.0 | Between the two Goal Lines |
| **End Zone Length (each)** | 10.0 | 914.4 | Beyond the Goal Lines |
| **Field Width** | 53.3333 | 4,876.8 | Width between the Sidelines (160 feet) |

## Landmark Coordinates

Placing a field coordinate helper or reference grid at the center of the playing field (origin at the 50-yard line, center of width) yields the following boundary coordinates:

### X-Axis (Field Length)
* **Center Field (50-yard line)**: X = 0.0
* **Goal Line A (Home)**: X = -4,572.0 cm (-50 yards)
* **Goal Line B (Away)**: X = +4,572.0 cm (+50 yards)
* **Back of End Zone A**: X = -5,486.4 cm (-60 yards)
* **Back of End Zone B**: X = +5,486.4 cm (+60 yards)

### Y-Axis (Field Width / Lateral)
* **Center Line (Midfield)**: Y = 0.0
* **Sideline A (Left)**: Y = -2,438.4 cm (-26.6667 yards)
* **Sideline B (Right)**: Y = +2,438.4 cm (+26.6667 yards)

## Level Design Execution Guidelines

1. **Base Mesh / Plane**: Create a plane or static mesh for the grass surface centered at (0, 0, 0) with dimensions `10,972.8 cm` (X) by `4,876.8 cm` (Y).
2. **Coordinate Helper**: Place the `APSFieldGrid` actor at (0, 0, 0) to ensure the coordinate systems align perfectly.

# Specification: Boundary and End-Zone Trigger Volumes

This document defines the dimensions, locations, and tagging requirements for placing `TriggerVolume` actors in the level editor to support collision checks for scoring and out-of-bounds events.

## End-Zone Trigger Volumes

Place two `TriggerVolume` actors covering the Home and Away end zones. These are used to detect touchdowns.

### 1. Home End Zone (End Zone A)
* **Location (Center)**: X = -5,029.2 cm, Y = 0.0 cm, Z = 250.0 cm (assuming field center is at Z=0)
* **Dimensions (Brush Extent)**:
  * Length (X): 914.4 cm (10.0 yards)
  * Width (Y): 4,876.8 cm (53.3333 yards)
  * Height (Z): 500.0 cm (ensuring player jumps are captured)
* **Actor Tag**: Add `EndZoneA` to the Actor's Tags array.

### 2. Away End Zone (End Zone B)
* **Location (Center)**: X = 5,029.2 cm, Y = 0.0 cm, Z = 250.0 cm
* **Dimensions (Brush Extent)**:
  * Length (X): 914.4 cm (10.0 yards)
  * Width (Y): 4,876.8 cm (53.3333 yards)
  * Height (Z): 500.0 cm
* **Actor Tag**: Add `EndZoneB` to the Actor's Tags array.

---

## Out-of-Bounds Trigger Volumes

Place volumes surrounding the playing surface to detect when the ball carrier or ball goes out of bounds. While `APSFieldGrid` provides mathematical checks, placing physical trigger volumes allows utilizing Unreal Engine's collision/overlap delegates directly.

### 1. Left Sideline Boundary
* **Location (Center)**: X = 0.0 cm, Y = -3,719.2 cm, Z = 250.0 cm
* **Dimensions (Brush Extent)**:
  * Length (X): 12,000.0 cm (encompasses entire field length + buffer)
  * Width (Y): 2,561.6 cm (extends far left away from the sideline)
  * Height (Z): 500.0 cm
* **Actor Tag**: Add `OutOfBounds` to the Actor's Tags array.

### 2. Right Sideline Boundary
* **Location (Center)**: X = 0.0 cm, Y = +3,719.2 cm, Z = 250.0 cm
* **Dimensions (Brush Extent)**:
  * Length (X): 12,000.0 cm
  * Width (Y): 2,561.6 cm (extends far right away from the sideline)
  * Height (Z): 500.0 cm
* **Actor Tag**: Add `OutOfBounds` to the Actor's Tags array.

### 3. Back Endline A Boundary (Home Side)
* **Location (Center)**: X = -7,986.4 cm, Y = 0.0 cm, Z = 250.0 cm
* **Dimensions (Brush Extent)**:
  * Length (X): 5,000.0 cm (extends back away from the endline)
  * Width (Y): 10,000.0 cm
  * Height (Z): 500.0 cm
* **Actor Tag**: Add `OutOfBounds` to the Actor's Tags array.

### 4. Back Endline B Boundary (Away Side)
* **Location (Center)**: X = +7,986.4 cm, Y = 0.0 cm, Z = 250.0 cm
* **Dimensions (Brush Extent)**:
  * Length (X): 5,000.0 cm
  * Width (Y): 10,000.0 cm
  * Height (Z): 500.0 cm
* **Actor Tag**: Add `OutOfBounds` to the Actor's Tags array.

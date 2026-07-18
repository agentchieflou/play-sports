# Specification: End-to-End PIE Test

This document outlines the visual and log-based verification steps for the end-to-end play simulation flow in the Unreal Editor (PIE).

## Setup Instructions

1. **Open Project**: Open `play-sports.uproject` in Unreal Editor 5.3.
2. **Create/Verify Map**:
   - Ensure you are on a test level (e.g. `GameMap` or a basic default map).
3. **Configure GameMode**:
   - Create a Blueprint subclass of `PSGameMode`, naming it `BP_PSGameMode`.
   - Open `BP_PSGameMode` and verify default settings:
     - `RosterJsonPath` should point to `Data/sample_players.json` (relative to the project directory).
     - `PlayerRosterTable` can be left blank (will dynamically create a transient table at runtime).
   - In World Settings (or Project Settings -> Maps & Modes), set the **Default GameMode** to `BP_PSGameMode`.
4. **Run Game**: Click **Play** (PIE).

## Expected Outputs and Verification

### 1. Output Log Messages
Open the **Output Log** window (`Window -> Output Log`) and verify the following sequence of logs from `LogTemp`:

* **Roster Ingestion Start**:
  ```
  LogTemp: Display: PSGameMode: No PlayerRosterTable configured. Created a transient table.
  LogTemp: Display: PSGameMode: Attempting to load roster from <YourProjectPath>/Data/sample_players.json
  ```

* **Roster Ingestion Success**:
  ```
  LogTemp: Display: PSGameMode: Successfully ingested roster. Loaded 22 players.
  ```

* **Play Simulation Initialization**:
  ```
  LogTemp: Display: PSGameMode: Initialized PlaySimulation with 11 Offense and 11 Defense players.
  ```

* **Tick Progression and Phase Transitions**:
  Verify that the simulation ticks and automatically cycles through phases with transition logs:
  ```
  LogTemp: Display: PSGameMode: Play Phase Transitioned to Snap at simulation time 0.000000
  LogTemp: Display: PSGameMode: Play Phase Transitioned to PassRush at simulation time 1.000000
  LogTemp: Display: PSGameMode: Play Phase Transitioned to BallCarrierMovement at simulation time 2.000000
  LogTemp: Display: PSGameMode: Play Phase Transitioned to Scoring at simulation time 3.000000
  ```

* **Play Resolution**:
  At the `Scoring` phase transition, the play result is resolved from player attributes. You should see logging of the result, for example:
  ```
  PSGameMode: Play Result: Completion status resolved. Yards Gained: X, Result: [Incomplete / Tackle / Touchdown]
  ```

### 2. On-Screen Debug Print (Optional)
If screen logging is enabled or desired, ensure the phase changes and play results print to the viewport using `GEngine->AddOnScreenDebugMessage`.

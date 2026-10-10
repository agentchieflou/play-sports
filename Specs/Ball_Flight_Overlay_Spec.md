# Specification: The ball-flight indicators' look (editor handoff)

**Epic:** 32 (`roadmap/broadcast-overlays.md`, Track A). **Status:** code done; the visual assets
and a look check in PIE are editor work.

## What exists in code

- `UPSOverlayBallFlightSubsystem` follows the ball in flight. It works out what is drawn:
  - **A pass** starts with its `Throw` event on the bus. The arc comes from the ball's physics
    state at that instant: its position, its projectile velocity and its gravity. It is the
    flight the ball will take until something touches it.
    - The landing spot is where the ball comes down to the throw's catch height.
    - Its ring is the receiver's catch radius: his capsule plus the ball.
    - The lead ring is where the receiver will be when the ball arrives, at his current
      velocity. It is green when he gets there in time.
  - **A kick** starts when the ball leaves the kicker during a `Kickoff`, `Punt` or `FieldGoal`
    phase. It comes down on the ground. It is judged at the first goal posts ahead of it:
    `GOOD`, `WIDE LEFT`, `WIDE RIGHT` or `SHORT`.
- `APSOverlayBallFlight` draws all of it, in world space:
  - the arc, as a spline with a dot at each point;
  - the landing and lead rings, flat on the ground;
  - the kick readout, a text above the crossbar facing back down the field.
- **Tiers** (`OverlayDetail`):
  - `Full`: the dots the ball has passed go away.
  - `Simplified`: the arc stays whole.
  - `Minimal`: only the landing spot and the kick readout.
- **The look is data:** `Data/ball_flight_overlay.json` (schema in `Data/README.md`). Until this
  handoff is done, it points at engine basic shapes:
  - `/Engine/BasicShapes/Sphere` for the dots;
  - `/Engine/BasicShapes/Cylinder`, squashed, for the rings;
  - `BasicShapeMaterial`, through its `Color` parameter;
  - the engine's default font for the readout.

`PlaySports.Overlay.BallFlightStyleAndMath`, `PassArcFollowsTheThrow` and `KickArcReadsTheUprights`
cover the logic headlessly:
- the arc against the engine's own aim;
- the catch radius;
- the lead;
- the per-tier drawing;
- ending a flight;
- kick verdicts at both ends.

They can't show how any of it looks.

## What the editor session does

1. **Arc.** Replace the dots with a ribbon, if it reads better. Either:
   - a spline-mesh strip along `ArcSpline`, which already holds the arc; or
   - a Niagara ribbon fed the same points.

   Swapping the dots for a ribbon is a change to `APSOverlayBallFlight`. Leave a note here when
   it's done. Thin, bright and unlit, like a broadcast pass-trajectory graphic.
2. **Rings.** Author a flat ring mesh and an unlit material. The same ring can serve both landing
   and lead.
   - Pivot at the center, lying in XY, 100 cm across at scale 1 (or set `MeshDiameter`).
   - A vector parameter named `Color`.
   - Depth-tested, so players occlude it as they do the reticle (`Specs/Overlay_Reticle_Spec.md`).
3. **Readout.** Pick a broadcast font material for the `ReadoutText` text render component. If it
   reads better, use a backing plate.
4. **Data.** Point `DotMeshPath`, `RingMeshPath` and `MaterialPath` at the new assets. Run
   `python tools/validate_data.py`.
5. **Check in PIE**, from the broadcast camera, by day and at night:
   - **A pass.** The ball follows the arc. It reaches the landing ring at the receiver's chest.
     The lead ring turns green as he gets under it. Everything clears shortly after the catch.
   - **The posts.** The default `GoalPostX` is -1000 and 11000, the end lines in the game mode's
     frame. Confirm they match where the level's posts stand. Field geometry is still open:
     Epic 2.1 and 2.2, and `APSFieldGrid` uses another frame. If they don't match, fix the data,
     not the code.

## Not covered until the simulation flies kicks

`UPSPlaySimulation` resolves field goals, punts and kickoffs with a roll. No ball goes in the
air, so in a game today the kick readout has nothing to follow.
- It follows any kick that launches the ball. `APSPlayerPawn::ExecuteKick` does, but nothing
  calls it during a game yet.
- When the kicking game launches the ball, the readout works with no change here.

## Done when

- In PIE, a thrown pass shows its arc, landing spot and lead.
- They read well at broadcast distance and clear after the play.
- The data points at the authored assets, and `tools/validate_data.py` passes.

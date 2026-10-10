# Specification: The selected-player reticle's look (editor handoff)

**Epic:** 30 (`roadmap/broadcast-overlays.md`, Track A). **Status:** code done; the visual asset
is editor work.

## What exists in code

- `APSOverlayReticle` draws the reticle. It is a flat static mesh attached under the
  controlled pawn's feet, so it follows him with no per-frame work.
- `UPSOverlayReticleComponent` (on `APSPlayerController`) decides who the reticle marks and how
  it looks:
  - `PreSnap`, `InPlay`, `BallCarrier` (the emphasised look), or `Hidden` while no player is
    controlled.
  - The color is the human's team color (from team select), or a per-side color.
  - The ball carrier's ring pulses, but only on a tier whose `OverlayDetail` is `Full`.
- **The look is data:** `Data/overlay_reticle.json` (schema in `Data/README.md`).
  - The mesh and material come from `MeshPath` and `MaterialPath`. The color goes into the
    material's vector parameter named `ColorParameter`.
  - Until this handoff is done, these point at the engine's `/Engine/BasicShapes/Cylinder` and
    `BasicShapeMaterial` (its `Color` parameter). The result is a thin disc, not the broadcast
    hexagon.

The logic is covered headlessly by `PlaySports.Overlay.SelectedPlayerReticle`: who the reticle
marks, its state, color, radius, the pulse, and hiding it. The test can't show how the reticle
looks.

## What the editor session does

1. **Mesh.** Author a flat hexagon ring, `SM_PSReticleHex`, under `/Game/UI/Overlays/`.
   - Pivot at the center, lying in XY.
   - 100 cm across at scale 1, or set `MeshDiameter` to its true width.
   - Keep it a few cm thick; `Thickness` scales it down to 2 cm.
   - Disable collision and shadow casting on the asset as well.
2. **Material.** Author `M_PSReticle` and an instance, `MI_PSReticle`.
   - Unlit and translucent (or additive), with a soft emissive edge and an inner glow.
   - A vector parameter named `Color`. The code multiplies the per-state `Brightness` into it, so
     values above 1 should read as glow.
   - Depth-test it, so players occlude it as broadcast ground graphics do (Epic 34 does the same
     for the first-down line).
3. **Data.** In `Data/overlay_reticle.json`, point `MeshPath` and `MaterialPath` at the new
   assets (`/Game/UI/Overlays/SM_PSReticleHex.SM_PSReticleHex`, ...). Run
   `python tools/validate_data.py`.
4. **Check in PIE**, from the broadcast camera, on grass, by day and at night:
   - The ring sits on the ground under the controlled player and doesn't z-fight. If it does,
     raise `GroundClearance`.
   - The three states are distinct.
   - The pulse is subtle.
   - Team colors stay readable on green.
   Tune `Radius`, `Brightness`, `PulseHz` and `PulseAmount` in the JSON, not in the asset.
5. **Optional decal variant.** If a true ground-projected decal reads better on uneven turf,
   it replaces the mesh path, using a deferred-decal material with the same `Color` parameter. The
   component's logic doesn't change; only `APSOverlayReticle`'s draw component does. That is a
   code change, so leave a note in this spec.

## Done when

- The reticle reads as the broadcast hexagon at broadcast camera distance in PIE.
- `Data/overlay_reticle.json` points at the authored assets, and `tools/validate_data.py` passes.

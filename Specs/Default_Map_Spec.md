# Specification: Default Map Asset Creation

This document outlines the required editor verification steps for the default map configuration implemented in `Config/DefaultEngine.ini`.

## Editor Asset Verification

Because Unreal Engine level assets are binary `.umap` files, they are not stored directly in this source repository. A human editor session must create the asset:

1. **Open Unreal Editor**: Open `play-sports.uproject` in Unreal Editor 5.3.
2. **Create Map Folder**: In the Content Browser, create a folder named `Maps` under the root `Content` directory (resulting in `/Game/Maps`).
3. **Create Map**:
   - Select `File -> New Level...`
   - Select a blank or basic level template.
   - Save the level in the newly created folder as `GameMap` (resulting in `/Game/Maps/GameMap`).
4. **Verify Settings**:
   - Go to `Edit -> Project Settings...`
   - Navigate to `Project -> Maps & Modes`.
   - Under `Default Maps`, verify that both **Editor Startup Map** and **Game Default Map** are set to `GameMap`.
5. **Level Setup**:
   - Setup the field geometry, markings, and trigger volumes in this `GameMap` as outlined in `Field_Geometry_Spec.md`, `Field_Markings_Spec.md`, and `Trigger_Volumes_Spec.md`.

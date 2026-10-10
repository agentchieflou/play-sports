# Specification: Pre-snap route ribbons (editor handoff)

**Epic:** 27 (`roadmap/broadcast-overlays.md`, Track A). **Status:** the code is done. The
ribbon, ring and glow look, and checking it in PIE, are editor work.

## What exists in code

- **One resolution.** `PSPlayResolution::ResolvePlay` turns a play call into each player's job
  where he stands: his slot, his pre-snap changes (hot route, kept in, released), and his
  route's waypoints in world space. `UPSPlayOrchestrator` hands that result to the AI at the
  snap. The art draws the same result before the snap, so what is drawn is what is run.
- **The line before the snap.** The `GameState` bus event now carries `LineOfScrimmage`. It is
  the same spot the `Snap` event uses (`PSGameStateEvents::LineOfScrimmageFor`).
- **`UPSOverlayPlayArtSubsystem`** (world subsystem) is the model. `GetRouteArt()` returns
  primitives (`FPSPlayArtPrimitive`, `PSPlayArtTypes.h`); a renderer only draws them.
  - **Ribbon:** a polyline on the turf, from the receiver's feet through his waypoints.
    - `BreakIndices` are its cuts: the corners where it turns by the route-running model's
      break angle or more, plus an option route's read. Keep those corners sharp.
    - `FakeIndices` are a double move's fake breaks. Mark them differently, for example with a
      notch or a short dash.
    - `Size` is its width; the primary read is wider.
  - **Ring:** a flat ring at the end of each route, `Size` its radius.
  - **Option routes:** the ribbon stops at the read. Each branch is its own ribbon from the read,
    with its own ring. Branches carry `bBranch` and a lower `Opacity`.
  - **Color:** by the play's `ReadOrder`: the primary read, then the reads after it, then
    unranked routes (`ReadColors`, `UnrankedColor` in `Data/play_art.json`).
  - **Who has art:** only routes from the route library. Blockers and "go to your spot" jobs
    (the QB's drop, a back's mesh point) have none, and neither do kicks or clock plays
    (`NoRouteArtCategories`).
- **When.** From the offense's call to the snap. The art is rebuilt after each `PlayCall`,
  `PreSnap` and `GameState` event, and `PlayArtRefreshHz` times a second (per platform tier) to
  follow motion.
  - At the snap: `IsFading()` is true and `GetOpacity()` runs from 1 down to 0 over
    `SnapFadeSeconds` on a `Full` tier.
  - On a `Simplified` tier the art goes at once at the snap.
  - A `Minimal` tier draws none.
  - Every new down starts empty.
- **Who sees it.** Use `IsVisibleTo(Viewer)` for each view:
  - The `RouteArt` setting (Gameplay) turns it off.
  - In a head-to-head game, the versus rules decide (`RouteArtAudience`, shared or split
    screen).
  - Otherwise a player on defense doesn't see the offense's art; a spectator does.
- **Drawing today.** In development builds the subsystem draws each primitive with debug
  shapes (`PSPlayArt::DrawDebug`) for the first local player:
  - lines for ribbons;
  - small circles at cuts and fakes;
  - flat circles for rings.
  Shipping builds draw nothing.
- **Headless coverage (`PlaySports.PlayArt.*`).**
  - `OneResolution`: the resolver's routes, mirroring, spots, blockers and zones; the
    orchestrator handing out exactly the resolved waypoints; the GameState event's line.
  - `CompileRouteArt`: ribbons from the feet, cuts, fakes, rings, option branches, colors and
    widths by read.
  - `DrawnIsRun`: the art of a real call against the announced line, redrawn by a hot route;
    every ribbon equal to the route the AI runs after the snap; the fade; the next down.
  - `ShowHidePolicy`: tiers, the setting, kicks, a defending viewer, the head-to-head rules.
  - `StyleValidates`: `Data/play_art.json`.

What CI cannot show is how the art looks.

## What the editor session does

1. **Ribbon.** Build a renderer actor or component that reads `GetRouteArt()` each frame. Use
   a spline mesh along each ribbon's `Points`, or a chain of decals projected on the turf.
   - Use a flat ribbon mesh, `Size` wide, with linear spline points so cuts stay sharp. Don't
     smooth the curve: `BreakIndices` are where the receiver cuts.
   - Tint the ribbon with `Color`, at `Opacity` times `GetOpacity()`.
   - Pool the components. The subsystem already owns what is drawn; don't keep a second copy
     of it (rule 6).
2. **Ring.** A flat ring decal or mesh at each `Ring` primitive, `Size` its radius, in its
   color.
3. **Glow material.** A translucent, unlit material with a soft emissive edge so ribbons read on
   grass from the broadcast camera. Give it a parameter for the tint and one for the opacity.
   Check it in day and night lighting (Epic 144).
4. **Views.** Draw only in views where `IsVisibleTo` is true. On a split screen that is one
   player's view only, so use per-view visibility (owner-only primitives or a scene capture),
   not world visibility.
5. **Debug draw off.** Once the renderer reads well, set `bDrawDebug` to false in
   `Data/play_art.json`.
6. **Check in PIE.**
   - Call Slant-Flat as the offense. Both slants (primary color, wider) and the back's flat
     (second color) should show, with rings at their ends.
   - Hot-route a receiver: his ribbon should change.
   - Send the slot in motion: his ribbon should follow him.
   - Snap: the art fades, and the receivers run the drawn routes.
   - Call Twins Option: the option route's two branches should show, lighter.
   - Turn `RouteArt` off in the settings: the art goes.
   - Play defense against the CPU: you see no route art.
7. **Colors and sizes.** If a color reads badly on the turf, change it in `Data/play_art.json`,
   not in code.

## Done when

- The ribbons, rings and glow read clearly from the broadcast and all-22 cameras.
- The debug draw is off.
- Epic 27's renderer story and its editor-pass story are ticked.

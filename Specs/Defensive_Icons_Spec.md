# Specification: Defensive assignment icons (editor handoff)

**Epic:** 31 (`roadmap/broadcast-overlays.md`, Track A). **Status:** the code is done. The look of
the stars, lines and arrows, and checking them in PIE, are editor work. This is the defense's half
of the pre-snap play art; the offense's route ribbons are `Specs/Route_Ribbons_Spec.md`. Both use the
same primitives and the same renderer.

## What exists in code

- **One resolution.** `PSPlayResolution::ResolvePlay` resolves the defense's call, as it will run
  (`UPSPlayCallSubsystem::GetDefensivePlayToRun`: its adjustment applied), for where each defender
  stands. It returns each defender's job, his zone landmark on his own side of the field, and any
  shadow matchup. `UPSPlayOrchestrator` hands the same result to the AI at the snap.
- **Man matchups.** `PSPlayResolution::ResolveManMatchups` takes each man defender's receiver
  the way `UPSDefenderAIComponent` takes him at the snap, the defenders in field order:
  1. the receiver the play names (a shadow);
  2. else the receiver he lined up to press (`UPSCoverageMatchupSubsystem`, Epic 69);
  3. else the nearest open receiver.

  The nearest-open-receiver rule is one function (`PSPlayResolution::NearestOpenReceiver`), and
  the defender AI picks with it.
- **`UPSOverlayPlayArtSubsystem`** keeps the defense's icons beside the route art: `GetDefenseArt()`
  returns them, as `FPSPlayArtPrimitive`s.
  - **Star** at each zone defender's landmark: the call's `ZoneOffset` from the line, or his own
    spot for a zone with no offset. A man defender with nobody left to cover plays his spot, so
    he gets a star there too. `Size` is the star's radius.
  - **Connector** from each man defender to his receiver. `Target` is the receiver. `Source` says
    how the defender got him: `Shadow`, `Press` or `Man`.
  - **Arrow** from each rusher's spot through the line, `RushArrowDepth` behind it. `Source` is
    `Blitz` for the call's blitzers and `Rush` for the linemen.
  - Run fits draw nothing.
  - Colors and sizes come from `Data/play_art.json`.
  - The kicking game's calls draw nothing (`NoDefenseArtCategories`).
- **When.** From the defense's call to the snap. The icons are rebuilt after each `PlayCall`,
  `DefensivePreSnap` and `GameState` event, and `PlayArtRefreshHz` times a second (per platform
  tier) as the defense lines up and disguises. They fade at the snap with the route art (a `Full`
  tier), go at once on `Simplified`, and are not drawn on `Minimal`.
- **Who sees them.** Use `IsDefenseArtVisibleTo(Viewer)` for each view:
  - The `DefenseIcons` setting (Gameplay) turns them off.
  - In a head-to-head game only the versus rules decide (`DefensiveIconsAudience`, shared or
    split screen). Study mode doesn't reach past them.
  - Otherwise the defending player and spectators see them. The offense sees them only with the
    `StudyMode` setting on, a learning aid for reading coverages.
- **Drawing today.** In development builds the subsystem draws the icons with debug shapes
  (`PSPlayArt::DrawDebug`) for the first local player:
  - a star outline for zones;
  - a line for man matchups;
  - a directional arrow for rushers.
  Shipping builds draw nothing.
- **Headless coverage (`PlaySports.PlayArt.*`).**
  - `DefenseIcons`: stars, mirrored landmarks, a zone with no offset, matchups (named, then
    nearest open, in order), a man defender with nobody left, blitz and rush arrows, run fits.
  - `DefenseDrawnIsRun`: the defense's real call with a shadow and any press plan. After the
    snap, every man defender covers the receiver he was drawn to and every rusher rushes.
  - `DefenseIconPolicy`: the settings, the tiers, a hands team, the defending and offensive
    viewers, study mode, and the head-to-head rules.

What CI cannot show is how the icons look.

## What the editor session does

1. **Use the route ribbons' renderer** (`Specs/Route_Ribbons_Spec.md`). Extend it to read
   `GetDefenseArt()` as well; there is one renderer for all play art.
2. **Star.** A flat five-pointed star decal or mesh on the turf, `Size` its radius, white by
   default. The reference frame's zone stars are the model. A star for a deep zone may read
   better a little larger; if so, add that to the data, not to code.
3. **Connector.** A thin line on the turf from the defender to his receiver (`Points[0]` to
   `Points[1]`). It can be dashed so it doesn't read as a route. `Press` matchups may be drawn
   heavier.
4. **Arrow.** A flat arrow on the turf with its head at `Points[1]`. Blitzers in the blitz
   color, linemen in the rush color.
5. **Views.** Draw the icons only in views where `IsDefenseArtVisibleTo` is true. On a split
   screen, use per-view visibility, as for the ribbons.
6. **Check in PIE.**
   - Play defense and call Cover 2: stars at the deep halves.
   - Call Nickel Man-Free: a line from each defender to his man. Shadow the best receiver: the
     line moves to him.
   - Call Double-A Blitz: blitz arrows through the A gaps.
   - Snap: the icons fade, and the defenders play what was drawn.
   - Play offense: no icons until `StudyMode` is on.
   - Head to head on one screen: nobody sees them.
7. **Colors and sizes** go in `Data/play_art.json`. Then set `bDrawDebug` to false once the
   ribbons and icons both read well.

## Done when

- Stars, lines and arrows read clearly from the broadcast and all-22 cameras.
- The debug draw is off.

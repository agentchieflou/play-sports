# Specification: Run-gap integrity overlay (editor handoff)

**Epic:** 81, story 4 (`roadmap/ai-depth.md`, Track F). **Status:** the code is done. Drawing the
markers properly, and checking the overlay in PIE, are editor work.

## What exists in code

- **`UPSDefenderGapSubsystem`** is the one authority on who owns which run gap.
  - `GetIntegrity()` gives each gap's owner, whether he fills it, and whether he is engaged with
    a blocker.
  - A `GapIntegrity` bus event fires whenever the open gaps change, or when an exchange
    happens.
- **`UPSDefenderGapOverlaySubsystem`** (world subsystem) shows that integrity while a play is
  live. It keeps one marker per gap, D left to D right (`GetMarkers()`, `FPSGapMarker`).
  - **Where.** Each marker sits on its gap's spot on the line, raised `MarkerHeight`.
  - **What.** Each marker has a state:

    | State | Meaning | Default color |
    |---|---|---|
    | `Filled` | The owner is in the gap. | green `#3FB950` |
    | `Blocked` | The owner is in the gap but engaged with a blocker. | amber `#D29922` |
    | `Open` | The owner is somewhere else. | red `#F85149` |
    | `Unowned` | Nobody owns the gap: the call took its defender out of the fit. | purple `#A371F7` |

  - **When.** Markers are rebuilt on each `GapIntegrity` event, and every `RefreshSeconds`
    so they follow the line. The snap and the whistle clear them.
  - **Emphasis.** The owner of an open gap is emphasized through Epic 36's
    `UPSOverlayEmphasisSubsystem`: the `Mismatch` look, source `GapIntegrity`. The overlay takes
    the emphasis back when he fills the gap, or when the play ends.
  - **Turning it on.** It is off by default. Use `ps.Overlay.GapIntegrity 1` at the console,
    `SetEnabled(true)`, or `bEnabledByDefault` in `Data/gap_overlay.json`.
  - **Drawing today.** Development builds draw each marker as a debug ring on the turf
    (`bDrawDebug`), with a line from an open gap to its owner. Shipping builds draw nothing.
- **Headless coverage.** `PlaySports.AI.RunFit.IntegrityOverlay` checks:
  - the style loads and validates;
  - the overlay is hidden by default;
  - the markers' states, colors and spots;
  - the blocked state on the next refresh;
  - the emphasis on and off;
  - the markers following the line;
  - the console toggle;
  - the whistle clearing everything.

What CI cannot show is how the overlay looks.

## What the editor session does

1. **Marker.** Make a flat world-space marker that `GetMarkers()` drives, one per gap. Use a
   decal or a translucent unlit disc on the turf, tinted by the marker's `Color`. `MarkerRadius`
   is its size.
   - Spawn or pool them from a small actor or component that reads `GetMarkers()` each frame.
   - The subsystem already owns which markers exist and where. Don't duplicate that state
     (rule 6).
   - Once the marker reads well, set `bDrawDebug` to false in `Data/gap_overlay.json`.
2. **Open-gap link.** Draw a thin line or arrow from an open gap's marker to its owner, as the
   debug draw does. Players should see who is out of position.
3. **Emphasis.** Check that the `Mismatch` look from Epic 36's emphasis material reads on the
   owner. That material is the editor work in `Specs/Player_Emphasis_Spec.md`.
4. **Check in PIE.** Run a run play against a 4-3 with `ps.Overlay.GapIntegrity 1`. Confirm:
   - the eight markers sit between the linemen, and follow the line as it moves;
   - a linebacker dropped into coverage leaves his gap purple;
   - a fitter washed out of his gap turns it red and lights him up;
   - the scrape exchange swaps the two gaps' owners;
   - everything clears at the whistle.
5. **Colors.** If a color reads badly on the turf, in day or night lighting (Epic 144), change it
   in `Data/gap_overlay.json`, not in code.

## Done when

- The markers and the open-gap links read clearly from the broadcast and all-22 cameras.
- The debug draw is off.
- Epic 81's story 4 is ticked.

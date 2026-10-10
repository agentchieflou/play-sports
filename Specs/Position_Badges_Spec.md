# Specification: The position badges' look (editor handoff)

**Epic:** 28 (`roadmap/broadcast-overlays.md`, Track A). **Status:** code done; the look and a check
in PIE are editor work.

## What exists in code

- `UPSOverlayBadgeComponent` is on `APSPlayerController`. It works out each badge:
  - **Who wears one.** Every player but the one the human controls, in five groups: receivers,
    backs, the quarterback, the line and the defense.
  - **What it says.** While the human's QB can throw, his receiver slots wear their pass
    buttons. The slots run left to right across the formation. The button is the glyph of the
    slot's `PassTarget` action on the device in use: `X`, `Y`, `B`, `RB`, `A` on a gamepad, the
    number keys on a keyboard. Everyone else wears his role's label (`QB`, `TE`, `DB`, ...).
  - **When it shows.** Each group's rules are data: before the snap; during the play hidden,
    while the QB can throw, or always.
  - **Where.** Above the player's head, scaled by distance from the camera. It is nudged up clear
    of the other badges and of the ball; one with no room isn't drawn.
- `UPSOverlayBadgeWidget` draws the badges as colored plates with a text label. `APSHUD` shows it
  under the score bug.
  - Each frame the widget asks the component to lay out for the camera as it is then, so the
    badges never trail the camera.
  - A Widget Blueprint can redraw them from `OnBadgesChanged`. Each badge carries a `GlyphId` for
    an imported button icon.
- **Tiers** (`OverlayDetail`):
  - `Full` fades badges in.
  - `Simplified` shows them at once.
  - `Minimal` keeps the pass buttons only.
- **The look is data:** `Data/overlay_badges.json` (schema in `Data/README.md`).

`PlaySports.Overlay.BadgeStyleAndLayout` and `PlaySports.Overlay.BadgesNamePassTargets` cover the
logic headlessly:
- the projection, the scaling and the overlap rules;
- who wears which label on either device;
- the group colors;
- the timing before and after the snap;
- the tiers.

They lay out for a camera given by the test. They can't show the widget on screen.

## What the editor session does

1. **Plates.** Replace the flat plate with a broadcast badge, either way:
   - as a reskin: a Widget Blueprint subclass of `UPSOverlayBadgeWidget` assigned to `APSHUD`'s
     `BadgeWidgetClass`;
   - in code: a rounded brush on the plate.

   Keep the group colors from the data.
2. **Glyph icons.** Import the controller button icons. Draw the icon named by each badge's
   `GlyphId` in place of the text label, falling back to the label when there is no icon. The
   input glyph table (`Data/input_glyphs.json`) already names the IDs (`Xbox_X`, ...).
3. **Check in PIE**, from the broadcast camera:
   - **Placement.** Each badge sits just above its player's head and follows him without lag or
     jitter. If the badges sit off their players, compare `UPSOverlayBadgeComponent`'s
     projection with the engine's. It assumes the default horizontal field of view.
   - **Buttons.** The four or five receivers show the buttons that throw to them, and the
     buttons change when the gamepad or keyboard is picked up.
   - **Timing.** The defense's labels go at the snap, and the buttons go when the ball is thrown.
   - **Clearance.** Badges never cover each other or the ball.

   Tune `HeadClearance`, sizes, `ReferenceDistance` and the scale range in the JSON.

## Done when

- The badges read as the broadcast frame's letters at broadcast distance.
- They track their players in PIE.
- The data passes `python tools/validate_data.py`.

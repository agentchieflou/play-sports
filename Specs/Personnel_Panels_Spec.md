# Specification: The personnel panels' look (editor handoff)

**Epic:** 29 (`roadmap/broadcast-overlays.md`, Track A). **Status:** code done; the look, the team
logos and a check in PIE are editor work.

## What exists in code

- **`UPSOverlayPersonnelSubsystem`** works out the two panels.
  - **Counts.** It counts each side's roles from the players on the field: "RB 1 | TE 3 | WR 1"
    against "DL 3 | LB 4 | DB 4".
  - **Package names.** A package the personnel catalog (Epic 19.5) lists with those exact counts
    goes by the catalog's name. Any other is named by rule ("13 Personnel"; "Nickel" by
    defensive backs).
  - **Teams.** The side with the ball is the offense, with its team label and color as the score
    bug shows them.
  - **Timing.** The panels show before the snap only. A substitution flashes its side's panel
    and the counts it changed (Full tier).
- **`UPSOverlayPersonnelWidget`** draws them.
  - It is built in code: offense bottom-left, defense bottom-right, above the score bug.
  - Each panel has a team color bar, a logo slot (`SetTeamLogo`), the package name and the
    counts.
  - `APSHUD` shows it, except on a Minimal tier. A Widget Blueprint can redraw it from
    `OnPanelsChanged`.
- **The rules and colors are data:** `Data/personnel_panel.json` (schema in `Data/README.md`).

`PlaySports.Overlay.PersonnelStyleAndNaming` and `PlaySports.Overlay.PersonnelPanelsFollowTheField`
cover the model headlessly:
- the counts and names, against the broadcast frame's own example;
- teams by possession;
- the substitution flash;
- the tiers.

They can't show the widget.

## What the editor session does

1. **Look.** Restyle the panels to the broadcast package, either way:
   - as a reskin: a Widget Blueprint subclass of `UPSOverlayPersonnelWidget` assigned to
     `APSHUD`'s `PersonnelWidgetClass`;
   - by adjusting the code-built brushes.

   Keep colors and type sizes in the JSON.
2. **Logos.** Import team logos. Call `SetTeamLogo(bOffense, Texture)` when teams are known (team
   select, or the `team=`/`away=` options), or bind the slot in the Blueprint.
3. **Check in PIE:**
   - The panels sit clear of the score bug and the badges.
   - They read at broadcast distance, show before the snap and go at it.
   - A package change (call a 12-personnel play after an 11) flashes the right counts.

## Done when

- The panels match the broadcast package in PIE, with logos.
- The data passes `python tools/validate_data.py`.

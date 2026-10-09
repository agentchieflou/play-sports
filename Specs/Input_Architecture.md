# Specification: Input Architecture (the contract Track M and Track I build on)

`roadmap/ux-ui.md` and `roadmap/PARALLEL.md` already cite this file; it did not exist. This first
version (2026-10-08) seeds it with the one input scheme this project has that a person has actually
walked a 3D scene with: the browser world's keyboard, mouse and standard-mapping gamepad controls
from `agentchieflou/this-next-please` (`RawAssets/world/reference/browser/WORLD.md` §Moving). Track M
(Epics 126–128) owns the Enhanced Input implementation; Track I (101–104) builds feel and menus on it.

## 1. Rules

1. **Input never reaches a pawn directly.** `APSPlayerController` applies mapping contexts on
   possession; `APSPlayerPawn` stays input-free (Epic 126, rule 1). Actions are declared once in a
   code-defined `UPSInputConfig` data asset; no magic bindings in gameplay code (rule 4).
2. **One action catalog, several contexts.** A context is a *situation* (on-field, play-call, menu,
   lobby/world, conversation) with a priority; the same physical button means different things per
   context, never per code path.
3. **A controller must be able to do everything.** The browser world's rule: anything a person can
   do with a mouse and keyboard they can do with a pad, including answering a dialogue by moving
   between its buttons (D-pad/stick + A) — the only exception was free text, for which there was no
   on-screen keyboard yet. In play-sports the on-screen keyboard is Epic 104's.
4. **Device changes are events** on `UPSTelemetryBus` (Epic 127); glyphs follow the active device
   (Epic 128).
5. **Reach, not range.** The world let a person act on an agent only within 3.2 m and facing it; the
   football equivalent is "the possessed pawn only", enforced by the possession authority (rule 6),
   never by the input layer guessing.

## 2. The baseline mapping (lobby / world / sideline walking; the on-field context is Epic 127's)

| Action | Keyboard and mouse | Gamepad (standard mapping) |
|---|---|---|
| Move | W A S D, or the arrows | left stick |
| Look | the mouse, after a click on the scene (pointer lock); Q and ← → turn | right stick |
| Sprint | Shift | RT, or press the left stick |
| Interact / talk to what you face | E or Enter | A |
| In a dialogue: move between buttons / press | Tab / Enter | D-pad or left stick / A |
| Back / cancel | Esc | B |
| Take over (secondary interact) | T | X |
| Toggle third ↔ first person | V | Y |
| Character / look picker | C (Esc closes) | Start (B or Start closes; D-pad moves, A picks) |
| Debug: frame figures | F3 or \` | — |

Epic 126's catalog (Move, Sprint, Confirm, Cancel, SwitchPlayer) is a subset of this; the rows above
add Look, Interact, Secondary, ViewToggle and Picker, which Epic 143 (the look picker) and Epic 101
(the front-end shell) need. Keep the physical assignments: A confirms, B cancels, Y toggles the
camera, Start opens the character sheet, on every context.

## 3. Feel (from the world's walking, for Epic 104)

- Walk blends into a jog and a run with stick deflection, not a toggle; Shift/RT is an override.
- Turning on the spot steps the feet round (no sliding); legs swing with the distance walked so the
  feet never slide; the camera swings to the side when you face someone to talk.
- Dead zones and response curves are Enhanced Input modifiers with values from a tuning DataTable row
  (`FInputTuningRow`, Epic 127), never constants.
- Reduced-motion preference: the walk snaps instead of easing, cameras cut instead of swinging
  (Epic 103 reads the OS/engine accessibility setting).

## 4. Acceptance for this spec's own stories (Epic 142, story 4)

- `Data/input_actions.json` (validated by `tools/validate_data.py`) lists the catalog above with the
  context each action lives in; `UPSInputConfig` reads it through `UPSDataIngestion`
  (rule 4: no ad-hoc parser).
- A headless automation test asserts every catalog action has a keyboard and a gamepad binding in
  every context it is declared for.

Status (2026-10-09): implemented with Epic 126. The catalog declares two contexts, `World` (the
section 2 table) and `OnField` (Epic 126's Move, Sprint, Confirm, Cancel, SwitchPlayer, with the
same physical buttons). The debug frame-figures key and the dialogue-navigation rows are not in it
yet: the first has no gamepad binding and the second needs a conversation context.
`PlaySports.Input.CatalogCoversKeyboardAndGamepad` is the section 4 test.

Status (2026-10-09, Epic 127): the on-field gamepad layout is the `OnField` rows of the catalog —
left stick moves, A confirms, B cancels, X or LB switches player, RT or L3 sprints. Every gamepad
stick binding gets a radial dead zone and an exponential response curve from `FInputTuningRow`
(`Data/input_tuning.json`; section 3), and the engine's own stick `AxisConfig` dead zones in
`Config/DefaultInput.ini` are zeroed so the two don't stack. Rule 4 is live:
`UPSInputDeviceComponent` publishes `InputDeviceChange` on the bus from a last-input heuristic
(analog input counts only past `DeviceSwitchAnalogThreshold`) and gamepad connect/disconnect.

# Specification: Input Architecture (the contract Track M and Track I build on)

This is the written contract for human input: what exists, who owns each piece, and where the
next epics plug in. Track M (Epics 126–128) built the substrate. Epic 104 (feel, move
vocabulary, buffering), Epic 103 (settings and remapping), Epic 107 (two players on one
machine) and Epic 130 (touch) build on it. The data files are the authority on bindings and
tuning, and the code on behaviour; this file explains both and must change in the same PR as
either.

The first version (2026-10-08) seeded it with the browser world's controls from
`agentchieflou/this-next-please` (`RawAssets/world/reference/browser/WORLD.md` §Moving), the one
input scheme this project has that a person has actually walked a 3D scene with. Section 4
keeps that mapping.

## 1. Rules

1. **Input never reaches a pawn directly.** `APSPlayerController` applies mapping contexts on
   possession; `APSPlayerPawn` stays input-free (Epic 126, rule 1). Actions are declared once in
   the input catalog, never as magic bindings in gameplay code (rule 4).
2. **One action catalog, several contexts.** A context is a *situation* (on-field, play-call,
   menu, lobby/world, conversation) with a priority. The same physical button means different
   things per context, never per code path.
3. **A controller must be able to do everything.** The browser world's rule: anything a person
   can do with a mouse and keyboard they can do with a pad, including answering a dialogue by
   moving between its buttons (D-pad/stick + A). Free text was the only exception, because there
   was no on-screen keyboard yet. In play-sports the on-screen keyboard is Epic 104's. The
   validators enforce the rule: every action has a keyboard/mouse key and a gamepad key in every
   context it lives in.
4. **Device changes are events** on `UPSTelemetryBus` (`InputDeviceChange`, Epic 127). Glyphs and
   rumble follow that event; nothing asks the controller which device is active.
5. **Reach, not range.** The world let a person act on an agent only within 3.2 m and facing it.
   The football equivalent is "the possessed pawn only", enforced by the possession authority
   (rule 6), never by the input layer guessing.

## 2. The pieces

| Piece | Where | Owns |
|---|---|---|
| Action catalog | `Data/input_actions.json` → `UPSInputConfig` (read through `UPSDataIngestion`) | Actions, contexts, priorities and key bindings. Builds one `UInputAction` per action and one `UInputMappingContext` per context at runtime, so nothing in `Content/` is needed. |
| Stick tuning | `Data/input_tuning.json` → `FInputTuningRow` | Radial dead zone, response curve and the device-switch threshold, applied as Enhanced Input modifiers on every gamepad stick binding. |
| Player controller | `APSPlayerController` | The context stack, the Move/Sprint/SwitchPlayer/Pause handlers, and human↔AI possession (Epic 127). |
| Device tracking | `UPSInputDeviceComponent` (on the controller) | The active device (`EPSInputDevice`), from a Slate input pre-processor and gamepad connect/disconnect, published as `InputDeviceChange`. |
| Glyphs | `Data/input_glyphs.json` → `UPSInputGlyphs` (owned by `UPSInputConfig`) | Which button picture stands for a key, per glyph set (Xbox, keyboard and mouse). |
| Rumble | `Data/force_feedback.json` → `UPSForceFeedbackComponent` (on the controller) | Which gameplay events shake the gamepad, and how. |
| Menus | `UPSMenuComponent` (on the controller, Epic 101) | Screen stack, UI input mode, Back keys read from the catalog's `Menu` context. |
| Play calling | `UPSPlayCallComponent` (on the controller, Epic 102) | Opens the play-call screens for the player's side; Confirm on the field hikes. |

## 3. The context stack

Contexts and their priorities are catalog data. A higher priority wins when two active contexts
bind the same key.

| Context | Priority | Pushed by | Purpose |
|---|---|---|---|
| `World` | 0 | nothing yet (lobby and sideline walking, Epic 143) | The browser world's baseline (section 4). |
| `OnField` | 1 | `APSPlayerController::OnPossess` of an `APSPlayerPawn`; popped on unpossess | The possessed pawn during play. |
| `Menu` | 2 | not pushed on Enhanced Input | Names the keys menus treat as Confirm (Enter, A) and Back (Escape, B). While a screen is open the player is in UI input mode and Slate moves focus (D-pad, stick, arrows, Tab); `UPSMenuComponent` reads its Back keys from this context. |

`APSPlayerController::ActiveInputContexts` is the stack. The controller mirrors it into the local
player's `UEnhancedInputLocalPlayerSubsystem` when one exists. Headless test worlds have no local
player, so tests observe the stack itself. The controller is the only code that talks to the
Enhanced Input subsystem.

## 4. The action catalog (as of 2026-10-10)

`Data/input_actions.json` is the authority; this table is a snapshot. Gamepad buttons are named
as the Xbox glyph set labels them.

| Action | Type | Contexts | Keyboard/mouse | Gamepad | Handled by |
|---|---|---|---|---|---|
| Move | Axis2D | World, OnField | W A S D, arrows | LS | `APSPlayerController::HandleMove` (relative to the control yaw) |
| Look | Axis2D | World | Mouse | RS | nobody yet (Epic 143's camera) |
| Sprint | Boolean | World, OnField | Shift | RT, L3 | the controller (pawn burst while held) |
| Confirm | Boolean | OnField, Menu | Enter | A | `UPSPlayCallComponent` via `OnCatalogActionStarted`: hikes, or reopens the play-call screen (Epic 102); menus via Slate |
| Cancel | Boolean | World, OnField, Menu | Esc | B | `OnCatalogActionStarted`; menu Back |
| Pause | Boolean | OnField | P | Menu (Start) | the controller → `UPSMenuComponent::TogglePause` |
| SwitchPlayer | Boolean | OnField | T | X, LB | the controller → `SwitchToBestPawn` |
| Interact | Boolean | World | E, Enter | A | `OnCatalogActionStarted` |
| Secondary | Boolean | World | T | X | `OnCatalogActionStarted` |
| ViewToggle | Boolean | World | V | Y | `OnCatalogActionStarted` |
| Picker | Boolean | World | C | Menu (Start) | `OnCatalogActionStarted` (Epic 143) |

Physical meaning is kept across contexts: A confirms, B cancels and Y toggles the camera in
every context. Start opens the character sheet off the field and pauses on it (Epic 101). The
look picker is an off-field screen, so the two never meet in one context. On the keyboard the
pause key is P, because Escape is already Cancel on the field and PIE uses Escape to stop.

Not in the catalog yet, from the browser world's mapping: the debug "frame figures" key (F3 or
\`, which has no gamepad binding) and dialogue navigation (Tab/Enter, D-pad/A), which needs a
conversation context.

**Consuming an action.** Move, Sprint, SwitchPlayer and Pause have handlers on the controller.
Every other Boolean action is broadcast as `APSPlayerController::OnCatalogActionStarted(ActionId)`
for its consumer to subscribe to by ID. Consumers never cast to the controller to read input.

**Adding an action:**

1. Add it to `Data/input_actions.json` with a keyboard/mouse key and a `Gamepad_*` key in every
   context it lives in. `tools/validate_data.py` and `UPSInputConfig::Validate()` refuse it
   otherwise, and also refuse a key bound twice in one context.
2. Make sure the Xbox glyph set can draw its gamepad key (the validators check this too).
3. Consume it through `OnCatalogActionStarted`. If it drives the pawn, use a new component
   rather than growing the controller (rule 1).

## 5. Devices, glyphs and rumble

**Active device.** `UPSInputDeviceComponent` watches every key, button and analog move through an
observe-only Slate pre-processor. Analog moves count only past `DeviceSwitchAnalogThreshold`, so
stick drift can't flip the device. Gamepad connect and disconnect come from
`IPlatformInputDeviceMapper`; a disconnect while the gamepad is active falls back to
keyboard/mouse. Each change is published as `InputDeviceChange`.

**Glyphs (Epic 128).** `Data/input_glyphs.json` maps physical keys to glyphs per glyph set. Each
glyph is a `GlyphId` that an imported icon texture is registered under, plus a text `Label`
drawn until that icon exists. Only the glyph sets live in this file: which key an action uses
comes from the catalog, so the catalog stays the one authority on bindings (rule 6) and a remap
shows its new button with no glyph edit.

- Ask for an action's glyph with
  `UPSInputConfig::GetGlyphForAction(ActionId, ContextId, Device, OutGlyph)`. Get `Device` from
  the bus.
- The answer is the set's action-wide glyph if it has one (Move on keyboard shows `WASD`).
  Otherwise it is the glyph of the first key of that device the catalog binds to the action in
  that context.
- Keyboard sets fall back to a keycap labelled with the key's own name, so a remapped key still
  shows. Gamepad sets list every button and never guess.
- Each device has exactly one default set: `Xbox` for gamepads, `KeyboardMouse` otherwise.
  Picking another set (a PlayStation pad, for instance) is an extension: add the set, then
  choose it from the connected hardware.

**Rumble (Epic 128).** `UPSForceFeedbackComponent` subscribes to the bus and turns events into
cues, each with its pattern in `Data/force_feedback.json`:

| Bus event | Cue | Who feels it (as authored) |
|---|---|---|
| `Damage` | Hit | The pawn hit |
| `Tackle` | Tackle, or Sack when `bIsSack` | The tackler and the carrier |
| `Catch` | Catch, or Interception when `bIsInterception` | The receiver; everyone, for an interception |
| `Fumble` | Fumble | The fumbler and the recoverer |
| `Score` | Score | Everyone |

- The controlled pawn comes from `ControlChange` and the device from `InputDeviceChange`, both
  read from the bus.
- A cue with `bOnlyWhenInvolved` plays only when the event names the controlled pawn.
- Patterns reach `APlayerController::PlayDynamicForceFeedback` only while a gamepad is the
  active device.
- `bEnabled` is the player's vibration setting, for Epic 103's settings screen.
- Whether a tackle is a sack is the publisher's call (`FPSTelemetryTackleEvent::bIsSack`). The
  rumble layer never works it out itself.

**Known gap:** no gameplay code publishes `Tackle` or `Score` on the bus yet.
`UPSBallActionComponent::ResolveTackle` calls `UPSPlaySimulation::RecordTackle` directly, and
end-zone touchdowns call `RecordTouchdown`. So the Tackle, Sack and Score cues fire once those
outcomes are published, and Hit (every landed tackle), Catch, Interception and Fumble fire today.

## 6. Extension points

### Epic 104: feel, move vocabulary, buffering

- **Situation contexts.** Add `PreSnap`, `BallCarrier`, `Passing` and `Defense` to the catalog
  with priorities above `OnField`. Their pushes and pops follow the bus: `PhaseChange` for the
  play phase, `ControlChange`, and the possession component for "the human holds the ball".
  `PushInputContext`/`PopInputContext` are private to the controller today. Expose them, or add a
  context-director component that asks the controller to push and pop, so the controller stays
  the only code touching the Enhanced Input subsystem.
- **Moves** (juke, spin, truck, stiff-arm, hurdle, slide) are catalog actions in the
  ball-carrier context. Tap, hold and double-tap need Enhanced Input triggers. Give
  `FPSInputActionDef` (or `FPSInputKeyBinding`) a trigger field, and have
  `UPSInputConfig::BuildRuntimeObjects` attach the `UInputTrigger` objects the way it attaches
  stick modifiers today. Hold times are tuning, not constants.
- **Buffering** belongs in a component on the controller (rule 1). It subscribes to
  `OnCatalogActionStarted`, timestamps presses, and releases them when the pawn's animation
  commitment window opens (Track D). Buffer windows are tuning rows.
- **Feel.** Walk, jog and run blend from the stick magnitude `HandleMove` already receives,
  after the tuned dead zone and curve. Sprint stays an override (section 7).

### Epic 103: settings and remapping

- **Remap** by editing the bindings in `UPSInputConfig::Catalog` and calling
  `BuildRuntimeObjects()`. Then re-apply the active contexts: the rebuild makes new
  `UInputMappingContext` objects, so the subsystem's copies go stale.
  - Store the player's overrides with the save system. Never rewrite `Data/input_actions.json`.
  - Run `UPSInputConfig::Validate()` before accepting a remap, so a player can't unbind an
    action from a device or bind one key twice.
  - Glyphs follow on their own.
- **Vibration** is `UPSForceFeedbackComponent::bEnabled`. Use the engine's per-controller
  `ForceFeedbackScale` for a strength slider rather than editing the authored patterns.
- **Stick dead zone and curve sliders** set `FInputTuningRow` and rebuild.

### Epic 107: two players on one machine

Each local player gets an `APSPlayerController` with its own device, rumble and menu components.
Before that works, three gaps need closing:

1. **Device tracking is not per player.** `UPSInputDeviceComponent`'s pre-processor sees every
   device's input. Filter by the event's `FInputDeviceId` and platform user against the
   controller's own.
2. **`InputDeviceChange` and `ControlChange` carry no player identity.** Add one (the local
   player index or `FPlatformUserId`) so each controller's rumble and glyph consumers keep only
   their own events. Until then every `UPSForceFeedbackComponent` follows every `ControlChange`.
3. **`SwitchToBestPawn` can steal the other human's pawn.** It must skip pawns another human
   controls (`APSPlayerPawn::IsUserControlled()`).

Rumble itself is already per controller: `PlayDynamicForceFeedback` plays on that controller's
own gamepad.

### Epic 130: touch

- Touch drives the same catalog actions through Enhanced Input; a virtual stick feeds Move.
- `EPSInputDevice` gains `Touch`. `UPSInputGlyphs::Validate` then requires a default touch glyph
  set by itself, because it checks one default set per device value.
- Whether rumble cues map to phone haptics is Epic 130's decision.

## 7. Feel (from the world's walking, for Epic 104)

- Walking blends into a jog and a run with stick deflection, not a toggle; Shift/RT is an
  override.
- Turning on the spot steps the feet round with no sliding, and the legs swing with the distance
  walked so the feet never slide. The camera swings to the side when you face someone to talk.
- Dead zones and response curves are Enhanced Input modifiers with values from a tuning
  DataTable row (`FInputTuningRow`, Epic 127), never constants.
- Reduced-motion preference: the walk snaps instead of easing, and cameras cut instead of
  swinging. Epic 103 reads the OS/engine accessibility setting.

## 8. Verification

These automation tests run in CI's headless pass:

| Test | Checks |
|---|---|
| `PlaySports.Input.CatalogCoversKeyboardAndGamepad` | Every action has keyboard and gamepad keys in each of its contexts (Epic 142, story 4). |
| `PlaySports.Input.ValidateReportsCatalogMistakes` | `UPSInputConfig::Validate()` reports each kind of catalog mistake (Epic 126). |
| `PlaySports.Input.PossessionAppliesGameplayContext` | Possession pushes the gameplay context (Epic 126). |
| `PlaySports.Input.InjectedMoveReachesPawn` | A Move value reaches the pawn (Epic 126). |
| `PlaySports.Input.GamepadStickTuningApplied` | Stick dead zone and curve come from the tuning (Epic 127). |
| `PlaySports.Input.DeviceChangeRoundTripsOnBus` | Device changes are published on the bus (Epic 127). |
| `PlaySports.Input.HumanAIPossessionHandoff` | Human↔AI handoff in both directions (Epic 127). |
| `PlaySports.Input.SwitchPlayerFollowsBallAndCarrier` | SwitchPlayer goes to the carrier or the nearest teammate (Epic 127). |
| `PlaySports.Input.ForceFeedbackTuningValidates` | The rumble patterns load and validate (Epic 128). |
| `PlaySports.Input.TelemetryEventsDriveForceFeedback` | Bus events become rumble dispatches (Epic 128). |
| `PlaySports.Input.GlyphTableCoversCatalog` | The glyph table loads, validates and draws every bound key (Epic 128). |

What CI cannot show is how the input feels in a player's hands: real rumble strength on a pad,
glyph icons (none are imported yet; the labels stand in), and the menu flow on a gamepad. Those
need a person with a controller in PIE or a packaged build.

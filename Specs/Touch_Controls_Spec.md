# Specification: Touch Controls (Epic 130)

**Epic:** 130 (`roadmap/platform-ports.md`, Track N). **Reference device:** iPhone 17 Pro, in
landscape. **Related:**

- `Specs/Input_Architecture.md`: the action catalog and context stack that touch drives;
- `Specs/Platform_Audit.md`: the phone's budgets;
- `Specs/ADR_iOS_Build.md`: getting a build onto the phone.

Touch is a third way into the **same** action layer as the keyboard and the gamepad. It adds no
actions and no gameplay of its own. A touch-only gameplay path is a review-rejection
(Input_Architecture section 1). This file covers:

- what the code does;
- where every control sits and why;
- what each control means in each context;
- the visual half that an editor session still has to build.

## 1. How touch reaches the game (code, built)

```
finger ──> Slate touch event ──> UPSTouchInputComponent ──> catalog action + value
                                 (hit-test the layout,        │
                                  pick the winning context,   │ through the action's gamepad mapping
                                  read the stick/press/swipe) ▼
                       APSPlayerController::InjectCatalogInput ──> Enhanced Input ──> the usual handlers
```

1. **Fingers arrive** through an observe-only Slate input pre-processor, in game-viewport pixels.
   It never consumes a touch, so widgets and the viewport still receive it.
2. **Hit-testing.** `UPSTouchInputComponent` matches the finger to a control in
   `Data/touch_controls.json`, in this order:
   - the nearest **button** under it that an active context binds;
   - otherwise the **stick**, if the finger landed in the stick zone (the stick then centres
     where the finger landed);
   - otherwise a possible **swipe**, if it landed in the gesture zone.
3. **The control's action** comes from the highest-priority active context that binds the
   control (the later one on a tie). That is the rule Enhanced Input applies to a key two active
   contexts bind, so the touch button sets stack exactly like the mapping contexts do.
4. **The value.** The stick gives its deflection (X right, Y forward, full at the throw radius,
   clamped to the unit circle). A held button gives `true` every frame. A swipe gives `true` for
   one frame.

   A finger is **latched** to the action its control drove when it landed. If a context change
   gives the control another meaning while it is held, the finger stops driving the old action
   and stays silent until it lifts. A pad key behaves the same: Enhanced Input ignores a key held
   across a mapping change until it is released. So the hike button held into `Passing` does
   not throw to receiver 5 (the rule Epic 104.4's input buffer relies on).
5. **Delivery.** The value goes to `APSPlayerController::InjectCatalogInput` together with the
   modifiers and triggers of **the gamepad mapping the catalog gives that action in that
   context**. Enhanced Input (`InjectInputForAction`) then treats it exactly as it would treat
   that gamepad key:
   - the virtual stick gets the left stick's tuned dead zone and response curve
     (`Data/input_tuning.json`);
   - Move reaches `HandleMove`;
   - every Boolean action reaches `OnCatalogActionStarted` and `OnCatalogActionCompleted`.

   The passing, carrier-move, pre-snap and play-call components, and the input buffer in front of
   them, therefore need no touch code. A hold is a hold, so the passing component tells a touch
   pass (tap) from a bullet (hold) with the same timing as on a pad.
6. **Active device.** A touch switches `UPSInputDeviceComponent` to `EPSInputDevice::Touch`. The
   change is published as `InputDeviceChange`, so prompts switch to the `Touch` glyph set on
   their own. On a phone, a run starts on Touch, and a Bluetooth pad that disconnects falls back
   to Touch.
7. **Menus.** While a menu screen is open, the layer stands down: no controls, no actions. Menus
   take taps through their own widgets (Slate buttons). The same goes while the telestrator's
   analysis mode is on (Epic 44): a finger draws on `UPSTelestratorWidget`, whose toolbar has
   the tools, undo, clear, save and done. Touch gets into analysis with the `Telestrator`
   action: the on-screen D-pad Up in a replay.
8. **No engine joysticks.** `Config/DefaultInput.ini` turns off the engine's default virtual
   joysticks (`DefaultTouchInterface=None`). They would draw a second layer of gamepad-key sticks
   over these controls.
9. **No rumble on touch.** Rumble stays gamepad-only: cues play only while a gamepad is the
   active device. Phone haptics would be a separate decision with its own setting.

## 2. Coordinates and safe zones

All positions in `Data/touch_controls.json` are in the **HUD-safe area**: 0 to 1 across its
width and its height, from the top left. All sizes (button radius, stick throw, swipe length)
are fractions of the safe area's **height**, so a button stays round on any screen.

The safe area is the viewport minus `SafeZone`. On an iPhone 17 Pro in landscape the screen is
about 874 × 402 points; confirm on the device.

| Margin | Value | On the reference phone | Keeps clear of |
|---|---|---|---|
| Left, Right | 7% | about 61 pt each | The Dynamic Island (on whichever side it ends up) and the rounded corners. iOS reserves about 60 pt there in landscape. |
| Top | 4% | about 16 pt | The status-bar strip and the top corners |
| Bottom | 6% | about 24 pt | The home indicator, which iOS reserves about 21 pt for |

That leaves a safe area of about 752 × 362 pt, an aspect of 2.08. The layout is checked at
`LayoutAspect` 2.0: buttons must fit inside the safe area and must not overlap. The validators
enforce both.

**To check on the device:** iOS's own safe-area insets on the phone. If they exceed these
margins, widen `SafeZone`; the controls follow.

## 3. The on-screen layout

Landscape, safe area. The left thumb steers; the right thumb has the buttons and swipes,
arranged like the pad's face buttons and bumpers, so a player who knows the pad finds every
action in the same place.

```
 +--------------------------------------------------------------------------+
 |     [DUp]                (View) (Pause)                     (R3)         |
 | [DLeft]  [DRight]              [TrigLeft] [UpperLeft]       [UpperRight] |
 |     [DDown]                                                              |
 |                                                                          |
 |                                         .  .  . gesture zone .  .  .     |
 |                                                       [Top]              |
 | : : : : stick zone : : : : :                                             |
 | :                          :                 [Left]            [Right]   |
 | :      ( Stick )           :                                             |
 | :   floats where           :                         [Bottom]            |
 | :   the thumb lands        :          [Sprint]                           |
 +--------------------------------------------------------------------------+
```

| Control | Kind | Where (safe-area x, y) | Size | Pad twin |
|---|---|---|---|---|
| `Stick` | Virtual stick, floating in x 0-0.42, y 0.36-1 | rests at 0.16, 0.72 | full push at 0.16 heights (58 pt) | Left stick |
| `ButtonBottom` | Button | 0.86, 0.80 | radius 0.08 (58 pt across) | A |
| `ButtonRight` | Button | 0.95, 0.60 | 0.08 | B |
| `ButtonLeft` | Button | 0.77, 0.60 | 0.08 | X |
| `ButtonTop` | Button | 0.86, 0.40 | 0.08 | Y |
| `ButtonUpperLeft` | Button | 0.70, 0.18 | 0.07 (51 pt) | LB |
| `ButtonUpperRight` | Button | 0.95, 0.18 | 0.07 | RB |
| `Sprint` | Button | 0.68, 0.86 | 0.09 (65 pt) | RT |
| `Pause` | Button | 0.50, 0.07 | 0.065 (47 pt) | Menu (Start) |
| `ButtonView` | Button | 0.40, 0.07 | 0.065 (47 pt) | View (Back) |
| `ButtonRightStick` | Button | 0.82, 0.07 | 0.065 | R3 (right stick press) |
| `TriggerLeft` | Button | 0.58, 0.18 | 0.07 | LT |
| `DPadUp`, `DPadDown`, `DPadLeft`, `DPadRight` | Buttons, a cross at the top left, clear of the stick zone | 0.20, 0.07 / 0.20, 0.28 / 0.13, 0.175 / 0.27, 0.175 | 0.065 (47 pt) | D-pad |
| `SwipeUp`, `SwipeDown`, `SwipeLeft`, `SwipeRight` | Swipe from anywhere in the gesture zone (x 0.45-1) that is not a button | — | at least 0.12 heights (43 pt), within 0.35 s | — |

Every button is at least 44 pt across, Apple's minimum touch target.

Only the controls the active contexts bind are drawn:

- The D-pad and the LT and View twins appear only before the offense's snap. That is the busiest
  moment, with fifteen buttons, but the play isn't live yet.
- With the ball there are nine.
- On defense before the snap there are nine, and during the play eight.

The stick floats, because a phone has no physical stick to find by feel: the thumb lands anywhere
in the left zone and steers from there.

## 4. The button set per context

Contexts and priorities come from the catalog (Input_Architecture section 3). `OnField` is
always on during play; at most one depth context sits above it and takes over the controls it
binds. Touch mirrors the pad exactly: each button means what its pad twin means in the same
context. The automation test checks this for every button in every gameplay context.

**Offense and the ball.**

| Control | `OnField` (always on during play, and off the ball) | `PreSnap` (offense, before the snap) | `Passing` (QB with the ball behind the line) | `BallCarrier` (anyone else with the ball) |
|---|---|---|---|---|
| Stick | Move | Move | Move (also places the pass) | Move (also picks the juke's side) |
| Sprint | Sprint | Sprint | Sprint | Sprint |
| ButtonBottom | Confirm (hike, play call) | Confirm (hike) | Throw to receiver 5 | Truck |
| ButtonRight | Cancel | Cancel | Throw to receiver 3 | Spin |
| ButtonLeft | Switch player | Switch player | Throw to receiver 1 | Juke |
| ButtonTop | — | Tempo (huddle, no-huddle, hurry-up) | Throw to receiver 2 | Hurdle |
| ButtonView | — | Timeout | — | — |
| ButtonUpperLeft | Switch player | Switch player | Pump fake | Slide |
| ButtonUpperRight | — | Select receiver | Throw to receiver 4 | Stiff-arm |
| TriggerLeft | — | Slide protection | — | — |
| D-pad up / right / left / down | — | Audible / Hot route / Motion / Block or release | — | — |
| Pause | Pause | Pause | Pause | Pause |
| ButtonRightStick | Film view | Film view | Film view | Film view |
| Swipe up / down | — | — | — | Hurdle / Slide |
| Swipe left / right | — | Pick the player to the left / right | — | Juke |

**Defense and kicking.** Only the controls these contexts change are listed. Every other control
does what it does on `OnField` (first table).

| Control | `DefensePreSnap` (defense, before the snap) | `Defense` (defense, during the play) | `Kicking` (the human's side kicks) |
|---|---|---|---|
| ButtonBottom | Confirm | Confirm | Kick (the meter, `Data/kick_meter.json`) |
| ButtonTop | Disguise the shell | — | — |
| ButtonUpperRight | Select a receiver to shadow | Strip | — |
| TriggerLeft | Jump the snap | — | — |
| ButtonView | Timeout | — | — |
| D-pad up / right / left / down | Audible / Shadow / Show blitz / Creep (Epic 67) | — | — |
| Swipe left / right | Pick the player to the left / right | — | — |

- **Hidden controls.** A control no active context binds (a dash above) is not drawn and doesn't
  respond. A finger there can still swipe.
- **Passes.** Tap a receiver button for a touch pass; hold it for a bullet. The stick places the
  ball (`Data/passing_input.json`).
- **Juke side.** A swiped juke cuts to the side **the stick** points, as on the pad. The swipe's
  own direction does not pick the side, because Juke carries no direction value. Making it do so
  is a catalog change for the input-depth owner (Epic 104).
- **Picking a player.** The pad flicks the right stick left or right
  (`Gamepad_RightStick_Left`/`Right`). Touch swipes left or right in the gesture zone: a flick of
  the thumb on the right side of the screen, where the right stick would be.
- **Duplicates.** The pad binds Switch player to both X and LB, so touch shows it on both too.
  A UX pass may drop a duplicate. If it does, update the test's twin table
  (`PSTouchInputTests.cpp`), which encodes "each button means what its pad twin means".

**Adding an action, or a context,** means adding its touch control (or its touch button set) and
the action's `Touch` glyph in the same change. `tools/validate_data.py` and
`PSTouchControls::ValidateLayout` refuse:

- a covered context that touch can't fully reach;
- a catalog context that is neither in `TouchContexts` nor deliberately listed in
  `ContextsWithoutTouch`.

**Known gap: a press just before its context comes on.** The input buffer (Epic 104.4) replays a
pad button pressed in the instant before its context came on, if that button meant nothing where
it went down. It finds such presses through key state. Touch has no key state: a touch button
pressed where no active context binds it isn't claimed at all, so that press is lost on touch.
The buffer offers `KeyStateQuery` for exactly this. Wiring it means claiming unbound buttons as
dormant presses and answering for their pad twins. It is small, but it touches the buffer, so it
waits for its own change. Every other buffer behaviour (a press waiting for a busy carrier or
passer) applies to touch already: those presses arrive as catalog actions like any other.

Not covered by touch: these two, and only these, are in `ContextsWithoutTouch`.

- `World`: pushed by nothing yet (Epic 143). When it is, move it to `TouchContexts` with its button
  set.
- `Menu`: menus are Slate widgets, tapped directly.

## 5. Glyphs

`Data/input_glyphs.json` has a default `Touch` set with one action glyph per touch-driven action:
`Touch_Stick` "Stick", `Touch_Confirm` "Go", `Touch_Juke` "Juke", and so on. Touch binds no keys,
so on Touch, `UPSInputConfig::GetGlyphForAction` answers with the action's glyph. A prompt that
reads "Press [A] to hike" on a pad reads "Tap [Go] to hike" on touch, and switches the moment the
device changes. The labels stand in until icons are imported, as for Xbox.

## 6. The editor/visual half (handoff, not built)

The code hit-tests from data; nothing draws the controls yet. An editor session adds a touch HUD
widget (UMG), shown while the active device is `Touch` (listen for `InputDeviceChange` on the
bus):

1. **Draw the controls** from `UPSTouchInputComponent::GetActiveControls()`: each entry pairs a
   control with the action it drives right now. Place each with `GetControlPlacement` (centre
   and radius in viewport pixels), and label it with the action's Touch glyph. Redraw when the
   context changes; poll each frame or on `ControlChange` and the play phase.
2. **Make every drawn element hit-test invisible**, so taps pass through to the layer, which
   does the hit-testing. A visible-and-clickable UMG button would also fire a click.
3. **The stick.** Draw the base at its rest position, faded. While the stick finger is down, draw
   it at the touch-down point with a knob showing the deflection.
4. **Feedback.** A pressed button darkens. A recognised swipe flashes its direction arrow briefly.
5. **Look.** Semi-transparent, so the field stays readable (about 40% opacity at rest, 70%
   pressed). Respect the reduced-motion setting (Input_Architecture section 7) for the feedback.
6. **Check in PIE** with Project Settings → Input → "Use Mouse for Touch" on. The engine should
   then turn mouse clicks into touch events, so the layer and the device switch can be tried on
   the desktop. This is unverified: if the layer doesn't react, the clicks aren't arriving
   flagged as touch, and only the phone will tell.

## 7. Verification

| What | How it's verified | Still needed |
|---|---|---|
| The layout and the touch glyphs are sound: context coverage, value types, fit, no overlap | `tools/validate_data.py`, `PlaySports.Input.TouchLayoutValidates` | — |
| Each gesture gives the pad's action and value: the stick (dead zone, curve, clamp), every button in every gameplay context, swipes | `PlaySports.Input.TouchGesturesMatchGamepad` | — |
| The device switches to Touch on a finger, and prompts follow | `PlaySports.Input.TouchLayoutValidates` | — |
| Slate delivers touches to the layer with the right viewport coordinates | Nothing: headless tests have no Slate input | PIE with "Use Mouse for Touch", then the phone |
| The injected values drive play on a device (`InjectInputForAction`) | Nothing: headless worlds have no local player | PIE, then the phone |
| The engine joysticks are gone, the layout fits the real safe area, thumbs reach everything | Nothing | The phone (an iOS build: `Specs/ADR_iOS_Build.md`) |
| How it feels: stick throw, swipe threshold, button size | Nothing | Play on the phone; tune the numbers in `Data/touch_controls.json` |

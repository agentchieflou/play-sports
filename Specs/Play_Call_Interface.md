# Specification: Play-Call Interface (Epic 102)

How a play gets called and run: who owns the call, the screens a person calls from, how the CPU
calls, and what makes the ball snap. This is also the editor handoff for the screens' look.

## 1. What runs today (code, part 1)

- **One authority per call (rule 6).** `UPSPlayCallSubsystem` (a world subsystem) holds each
  side's call for the coming snap.
  - The playbook comes from `Data/sample_playbook.json`, with routes in
    `Data/sample_routes.json`. Both load through `UPSPlaybookIngestion`.
  - Every call, the human's or the CPU's, is announced on the bus as `PlayCall`.
- **The call window.** `APSGameMode` opens a window at every scrimmage down: at `StartPlay` and
  whenever the play returns to `PreSnap`. While the window is open, the game mode asks
  `PollReadyToSnap` every tick.
- **Who calls each side:**
  - **No human on the side:** `UPSCoachingAI` calls it (Epic 18's situation weighting) on the
    first poll.
  - **A human on the side:** the side waits for that person's call. Which pawns humans control
    comes from `ControlChange` on the bus.
  - **A human arrives after the CPU has called:** the CPU's call is dropped and the human is asked
    for theirs.
- **The snap:**
  - **CPU offense:** snaps `CpuSnapDelaySeconds` (`Data/play_call.json`) after both calls are in.
  - **Human offense:** snaps when its player hikes (Confirm on the field: Enter or A).
  - At the `Snap` event the subsystem hands both calls to `UPSPlayOrchestrator` (Epic 17). Every
    AI pawn gets its route or defensive assignment, measured from the snap's `LineOfScrimmage`.
  - A snap without an open window (the functional gym, scripts) still runs CPU calls for both
    sides.
- **The screens** use Epic 101's screen stack (`Data/ui_menus.json`):
  - `PlayCallScreen` (`PlayCall`) lists the player's formations. Back can't leave it, so a call
    is required.
  - Choosing a formation opens `PlayCallPlays` with that formation's plays.
  - Each play shows its name and category (offense), or its front and coverage (defense).
  - Below each play is a text line of what everyone does, e.g. `WR Slant · RB Flat · TE pass
    block`. This stands in for play art until Track A's art pipeline (Epic 35) exists.
  - Choosing a play calls it and closes the screens.
- **The human side.** `UPSPlayCallComponent` on `APSPlayerController` opens the screens when its
  player's side is asked for a call.
  - The player's side is the side of the pawn they control, otherwise the controller's
    `HumanSide`.
  - On the field, Confirm hikes once the offense has called, or reopens the screens if no call is
    in yet (for example after pausing).

## 2. Not yet (102's remaining stories and other epics)

- **Suggestions with reasoning (102.2).** The coaching AI's category weights for the situation,
  shown on the screens.
- **Recent and favourite plays, and a tendency readout (102.3).**
- **Play clock (102.5).** The play clock runs while the human decides, and when it expires the
  existing delay-of-game penalty applies. The quick-call fallback (auto-call the suggestion
  shortly before expiry) is still to come.
- **Defensive adjustments (102.4).** Pre-snap shifts and changing the coverage once the offense
  lines up. Today the defensive call is its front and coverage.
- **Play art (102.1).** Drawn route diagrams need Epic 35.
- **Passing.** A human QB can't throw yet; the passing input model is Epic 104. On a pass play the
  human QB scrambles while the AI runs the routes.
- **Competitive integrity (Epic 107).** The `PlayCall` event carries both sides' calls. With two
  humans, each HUD must show only its own side's call.
- **Tendency profiles.** The CPU calls with a neutral profile (aggression 0.5, no category bias).
  Per-team tendencies (`FPSTendencyProfile`) aren't loaded from data yet.

## 3. Editor session (when one is available)

1. **Try it in PIE.** Start the game map. The human takes the QB on the tick after BeginPlay and
   the play-call screen opens:
   - choose a formation, then a play;
   - press A (or Enter) to hike;
   - the AI runs the called routes and coverages.
   With no human pawn (for example `bTakeDefaultControlOnBeginPlay` off), CPU plays CPU, snapping
   every down on its own.
2. **Restyle.** The play-call screens use the same `UPSMenuScreenWidget` as every menu, so a
   `WBP_MenuScreen` subclass restyles them too. Each option's `Detail` is the play's text line.
   A dedicated play-call widget (formation art, a play grid) can replace the screen by checking
   `GetScreen().Content`.

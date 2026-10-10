# Specification: Play-Call Interface (Epic 102)

How a play gets called and run: who owns the call, the screens a person calls from, how the CPU
calls, and what makes the ball snap. This is also the editor handoff for the screens' look.

## 1. What runs today (code)

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
  - Beside each play is its diagram (102.1, below), and under its name a text line of what
    everyone does, e.g. `WR Slant · RB Flat · TE pass block`, which screen narration reads.
  - Choosing a play calls it and closes the screens.
- **Play-art previews (102.1).** Every option that calls a play (a formation's plays, the
  suggestion, recent and favourite plays) shows the play drawn as a diagram.
  - The diagram is the field's own play art (Epics 27, 31 and 35), not a second drawing:
    `UPSOverlayPlayArtSubsystem::BuildPlayDiagram` resolves the play for the players where they
    stand with `PSPlayResolution` (the snap's resolution, man matchups included) and compiles it
    with `PSPlayArt::CompilePlayArt`, then `PSPlayDiagram::BuildDiagram` lays it flat.
  - What it shows: the line of scrimmage; each route as a line ending in an arrowhead, in its
    read's color and width (an option route splits at its read); a T for each blocker (stem up
    for a run block, back for a pass block); the quarterback's drop and a back's path to his
    spot; on defense a star at each zone landmark with the defender's drop to it, a line from
    each man defender to his receiver, and each rusher's arrow. The side's players are rings
    (offense) or Xs (defense); the other side is drawn faintly for reference.
  - `UPSPlayDiagramWidget` paints it with Slate lines, fitted to whatever size it is given,
    upfield up. The code-built screen puts it in a box of `PreviewWidth` x `PreviewHeight` Slate
    units, which scale with the display, so it is the same share of a phone's screen as a
    monitor's. Lines never go thinner than `MinStrokeWidth`.
  - The look is `Data/play_art.json`'s `Diagram` block (`FPSPlayDiagramStyle`).
  - It is drawn whatever the route-art settings and the platform tier say: the play is the
    player's own choice. With nobody on the field (no lineup to resolve against) a play shows
    no preview, only its text line.
  - Limit: the preview resolves against the players on the field now. A formation whose
    personnel package differs from the one lined up shows its slots on the current players
    until the call brings the package on (Epic 19.5).
- **Suggestions (102.2).** The call screen's first option is the coaching AI's top-ranked play
  for the situation. `UPSCoachingAI::RankPlays` is the same weighting the CPU rolls on, but
  without the roll, so the suggestion is always the top-weighted play.
  - The option's detail line gives the reasons, e.g.
    `Short yardage: run it (+1.5) · 3rd down: the percentage play (+0.8)`.
  - The screen's body states the situation, e.g. `3rd & 7 at own 35`.
- **Recent plays and your tendencies (102.3, in part).**
  - Every play a human calls and runs is kept for the game (`GetCallHistory`).
  - Once there is history, the call screen offers **Recent plays**: the last
    `RecentPlaysShown` distinct calls for the side, each calling it again.
  - The body adds a readout of what you've been calling, e.g.
    `Your calls: Run 67% · Short pass 33%`, which is what an opponent would key on (ties to
    Epic 78).
  - Quick-calls and CPU calls don't count.
- **Favourite plays (102.3).**
  - On any play list, X or F (the `Favorite` action in the input catalog's `Menu` context)
    stars or unstars the play under focus.
  - Starred plays show a `*` wherever they are listed, and the call screen offers
    **Favorite plays** for the side once there are any.
  - Favourites live in the player's profile save (`UPSProfileSaveGame`, `Profile` slot through
    `UPSSaveSubsystem`), so they outlast the game. A world without a game instance (headless
    tests) keeps them in memory.
- **Defensive adjustments (102.4).**
  - After a human defense calls its front and coverage, an **Adjust** screen shows the
    offense's formation (visible at the line; the play itself isn't) and offers the adjustments
    in `Data/defensive_adjustments.json`, or no adjustment.
  - An adjustment turns every defender of one role to one assignment on top of the call, e.g.
    "Send the linebacker" makes the LBs blitz. It is applied when the calls go out at the snap
    (`GetDefensivePlayToRun`) and cleared each new down.
  - Back keeps the call as it is. The CPU defense doesn't adjust yet.
- **Play clock (102.5).**
  - The play-call screens show the live play clock. The game mode passes it to the subsystem
    every pre-snap tick (`SetPlayClock`).
  - When it reaches `QuickCallPlayClockSeconds` with a human's side still uncalled, the top
    suggestion is called for them (caller `QuickCall`) and the screens close.
  - A quick-called offense snaps like a CPU one, after `CpuSnapDelaySeconds`, so the down
    starts before the clock expires.
  - A human who called but doesn't hike still takes the delay-of-game penalty.
- **The human side.** `UPSPlayCallComponent` on `APSPlayerController` opens the screens when its
  player's side is asked for a call.
  - The player's side is the side of the pawn they control, otherwise the controller's
    `HumanSide`.
  - On the field, Confirm hikes once the offense has called, or reopens the screens if no call is
    in yet (for example after pausing).

## 2. Not yet (102's remaining stories and other epics)

- **Adjustment timing.** A CPU offense snaps `CpuSnapDelaySeconds` after both calls are in.
  A human defense's call comes last, so that delay is all the time the Adjust screen gets. A
  longer delay against a human defense may be wanted after playtesting.
- **Play-art previews, the look (102.1).** The diagrams are built and tested headless; nobody has
  seen one on a screen yet (section 3, step 3).
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
2. **Restyle (optional; no Widget Blueprint is needed, Epic 146.4).** The play-call screens use the same `UPSMenuScreenWidget` as every menu, so a
   `WBP_MenuScreen` subclass restyles them too. Each option's `Detail` is the play's text line.
   A dedicated play-call widget (formation art, a play grid) can replace the screen by checking
   `GetScreen().Content`; it places a `UPSPlayDiagramWidget` per play and calls `ShowPlay` with
   the option's `Payload` (the play's ID).
3. **Check the previews (102.1)** in PIE, on a monitor and on an iPhone (or the editor's mobile
   preview at a phone's DPI scale):
   - every play list shows a diagram beside each play, upfield up, the routes' colors matching
     the field's ribbons after the call, the defense faint behind an offensive play;
   - the lines read at the thumbnail size: raise `MinStrokeWidth`, `PlayerRadius` or the
     preview size in `play_art.json`'s `Diagram` block if not, and check the list still fits a
     phone's screen in landscape;
   - the backdrop (`BackgroundColor`, `BackgroundOpacity`) sits well on the button styles, and
     the diagram fades in with the screen.
